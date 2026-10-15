#include "render/Gpu.h"

#include "core/Log.h"
#include "render/Pixels.h"
#include "render/TexturePack.h"
#include "render/Translate.h"

#include <d3d9.h>
#include <wrl/client.h>

#include <algorithm>
#include <cstring>
#include <new>
#include <array>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace bw::render::gpu {

namespace {


constexpr std::uint32_t kStages = 8;
constexpr std::uint64_t kAnimatedMilliseconds = 250;

/// One ProbeDepth slot: an occlusion query and its latest answer.
struct Probe {
    ComPtr<IDirect3DQuery9> query;
    bool pending = false;
    bool answer = false;
};

struct Device {
    ComPtr<IDirect3D9Ex> direct3D;
    ComPtr<IDirect3DDevice9Ex> device;
    HWND window = nullptr;
    Options options;
    D3DMULTISAMPLE_TYPE multisample = D3DMULTISAMPLE_NONE;

    /// Presentation: a swap chain the size of the window's client area, recreated when that changes. Separate from the
    /// device's implicit swap chain so that resizing never resets the device (which would reset all states).
    ComPtr<IDirect3DSwapChain9> swapChain;
    std::uint32_t swapWidth = 0;
    std::uint32_t swapHeight = 0;
    HWND swapWindow = nullptr;

    std::array<Texture*, kStages> bound{};
    std::array<IDirect3DTexture9*, kStages> boundNative{};  // what each stage was set to (not owned)
    RenderTarget* target = nullptr;
    DepthBuffer* depth = nullptr;
    bool lighting = true;
    bool inScene = false;

    /// CPU -> render target staging: A8R8G8B8 in system memory, uploaded to a default-pool surface.
    ComPtr<IDirect3DSurface9> stagingSystem;
    ComPtr<IDirect3DSurface9> stagingGpu;
    std::uint32_t stagingWidth = 0;
    std::uint32_t stagingHeight = 0;

    std::array<Probe, kDepthProbes> probes;
    bool queriesUnavailable = false;

    /// Texture-pack images by file, shared by every texture they replace; null when unreadable.
    std::unordered_map<std::wstring, ComPtr<IDirect3DTexture9>> replacements;
};

Device* g_device = nullptr;
Statistics g_statistics;

D3DFORMAT ToD3D(Format format) noexcept
{
    switch (format) {
    case Format::R5G6B5:
        return D3DFMT_R5G6B5;
    case Format::X1R5G5B5:
        return D3DFMT_X1R5G5B5;
    case Format::A1R5G5B5:
        return D3DFMT_A1R5G5B5;
    case Format::A4R4G4B4:
        return D3DFMT_A4R4G4B4;
    case Format::R8G8B8:
        return D3DFMT_R8G8B8;
    case Format::X8R8G8B8:
        return D3DFMT_X8R8G8B8;
    case Format::A8R8G8B8:
        return D3DFMT_A8R8G8B8;
    case Format::L8:
        return D3DFMT_L8;
    case Format::A8:
        return D3DFMT_A8;
    case Format::A8L8:
        return D3DFMT_A8L8;
    case Format::V8U8:
        return D3DFMT_V8U8;
    case Format::X8L8V8U8:
        return D3DFMT_X8L8V8U8;
    case Format::Dxt1:
        return D3DFMT_DXT1;
    case Format::Dxt2:
        return D3DFMT_DXT2;
    case Format::Dxt3:
        return D3DFMT_DXT3;
    case Format::Dxt4:
        return D3DFMT_DXT4;
    case Format::Dxt5:
        return D3DFMT_DXT5;
    case Format::D16:
        return D3DFMT_D16;
    case Format::D24X8:
        return D3DFMT_D24X8;
    case Format::D24S8:
        return D3DFMT_D24S8;
    case Format::D32:
        return D3DFMT_D32;
    default:
        return D3DFMT_UNKNOWN;
    }
}

/// 16-bit colour formats whose textures can be widened to A8R8G8B8 (Texture::SetFullPrecision).
bool IsWidenable(Format format) noexcept
{
    return format == Format::R5G6B5 || format == Format::X1R5G5B5 || format == Format::A1R5G5B5 ||
           format == Format::A4R4G4B4;
}

std::uint32_t LevelSize(std::uint32_t size, std::uint32_t level) noexcept
{
    return std::max<std::uint32_t>(size >> level, 1);
}

IDirect3DDevice9Ex* D() noexcept
{
    return g_device != nullptr ? g_device->device.Get() : nullptr;
}

/// Logs a failed call once per call site.
#define BW_GPU_CHECK(expression)                                                                                    \
    [&] {                                                                                                          \
        const HRESULT result_ = (expression);                                                                      \
        if (FAILED(result_)) {                                                                                     \
            static bool logged_ = false;                                                                           \
            if (!logged_) {                                                                                        \
                logged_ = true;                                                                                    \
                log::Warning("Direct3D 9: {} failed (0x{:08x})", #expression, static_cast<std::uint32_t>(result_)); \
            }                                                                                                      \
        }                                                                                                          \
        return result_;                                                                                            \
    }()

bool EnsureSwapChain(HWND window, std::uint32_t width, std::uint32_t height) noexcept
{
    Device& d = *g_device;
    if (d.swapChain && d.swapWidth == width && d.swapHeight == height && d.swapWindow == window) {
        return true;
    }
    d.swapChain.Reset();
    D3DPRESENT_PARAMETERS parameters{};
    parameters.BackBufferWidth = width;
    parameters.BackBufferHeight = height;
    parameters.BackBufferFormat = D3DFMT_X8R8G8B8;
    parameters.BackBufferCount = 1;
    parameters.SwapEffect = D3DSWAPEFFECT_DISCARD;
    parameters.hDeviceWindow = window;
    parameters.Windowed = TRUE;
    parameters.PresentationInterval = d.options.vsync ? D3DPRESENT_INTERVAL_ONE : D3DPRESENT_INTERVAL_IMMEDIATE;
    if (FAILED(BW_GPU_CHECK(d.device->CreateAdditionalSwapChain(&parameters, &d.swapChain)))) {
        return false;
    }
    d.swapWidth = width;
    d.swapHeight = height;
    d.swapWindow = window;
    log::Info("Direct3D 9: presenting at {}x{}", width, height);
    return true;
}

bool EnsureStaging(std::uint32_t width, std::uint32_t height) noexcept
{
    Device& d = *g_device;
    if (d.stagingSystem && d.stagingWidth >= width && d.stagingHeight >= height) {
        return true;
    }
    width = std::max(width, d.stagingWidth);
    height = std::max(height, d.stagingHeight);
    d.stagingSystem.Reset();
    d.stagingGpu.Reset();
    if (FAILED(BW_GPU_CHECK(d.device->CreateOffscreenPlainSurface(width, height, D3DFMT_A8R8G8B8, D3DPOOL_SYSTEMMEM,
                                                                   &d.stagingSystem, nullptr))) ||
        FAILED(BW_GPU_CHECK(d.device->CreateOffscreenPlainSurface(width, height, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT,
                                                                   &d.stagingGpu, nullptr)))) {
        d.stagingSystem.Reset();
        d.stagingGpu.Reset();
        return false;
    }
    d.stagingWidth = width;
    d.stagingHeight = height;
    return true;
}

bool SameSize(const RECT& a, const RECT& b) noexcept
{
    return a.right - a.left == b.right - b.left && a.bottom - a.top == b.bottom - b.top;
}

void UploadBound() noexcept
{
    Device& d = *g_device;
    for (std::uint32_t stage = 0; stage < kStages; ++stage) {
        Texture* texture = d.bound[stage];
        if (texture == nullptr) {
            continue;
        }
        texture->Upload();
        // A texture that became full precision draws from a different Direct3D texture.
        if (texture->Native() != d.boundNative[stage]) {
            d.boundNative[stage] = texture->Native();
            BW_GPU_CHECK(D()->SetTexture(stage, d.boundNative[stage]));
        }
    }
}

/// Every Direct3D 9 state, restored when the scope ends: for the renderer's own draws in between the game's.
class SavedState {
public:
    SavedState() noexcept { BW_GPU_CHECK(D()->CreateStateBlock(D3DSBT_ALL, &m_block)); }
    ~SavedState()
    {
        if (m_block) {
            m_block->Apply();
        }
    }
    SavedState(const SavedState&) = delete;
    SavedState& operator=(const SavedState&) = delete;
    explicit operator bool() const noexcept { return m_block != nullptr; }

private:
    ComPtr<IDirect3DStateBlock9> m_block;
};

/// Inside BeginScene/EndScene for the scope (the game's scene when one is open).
class SceneScope {
public:
    SceneScope() noexcept : m_begun(!g_device->inScene && SUCCEEDED(BW_GPU_CHECK(D()->BeginScene()))) {}
    ~SceneScope()
    {
        if (m_begun) {
            D()->EndScene();
        }
    }
    SceneScope(const SceneScope&) = delete;
    SceneScope& operator=(const SceneScope&) = delete;

private:
    bool m_begun;
};

/// States for the renderer's own draws that do not depend on anything the game set: no blending, fog, culling,
/// clipping, depth bias or shaders.
void SetNeutralStates() noexcept
{
    IDirect3DDevice9Ex* device = D();
    device->SetVertexShader(nullptr);
    device->SetPixelShader(nullptr);
    device->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
    device->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
    device->SetRenderState(D3DRS_STENCILENABLE, FALSE);
    device->SetRenderState(D3DRS_FOGENABLE, FALSE);
    device->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
    device->SetRenderState(D3DRS_FILLMODE, D3DFILL_SOLID);
    device->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE);
    device->SetRenderState(D3DRS_CLIPPLANEENABLE, 0);
    device->SetRenderState(D3DRS_LIGHTING, FALSE);
    device->SetRenderState(D3DRS_DEPTHBIAS, 0);
    device->SetRenderState(D3DRS_SLOPESCALEDEPTHBIAS, 0);
    device->SetRenderState(D3DRS_SRGBWRITEENABLE, FALSE);
}

/// Draws `texture` over the whole of `target` (same size), pixel for pixel.
void DrawTexture(IDirect3DTexture9* texture, IDirect3DSurface9* target, std::uint32_t width, std::uint32_t height) noexcept
{
    IDirect3DDevice9Ex* device = D();
    const SavedState saved;
    if (!saved) {
        return;
    }
    ComPtr<IDirect3DSurface9> previousTarget;
    ComPtr<IDirect3DSurface9> previousDepth;
    device->GetRenderTarget(0, &previousTarget);
    device->GetDepthStencilSurface(&previousDepth);
    device->SetRenderTarget(0, target);
    device->SetDepthStencilSurface(nullptr);
    SetNeutralStates();
    device->SetRenderState(D3DRS_ZENABLE, D3DZB_FALSE);
    device->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
    device->SetRenderState(D3DRS_COLORWRITEENABLE, 0xf);
    device->SetTexture(0, texture);
    device->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
    device->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
    device->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
    device->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
    device->SetTextureStageState(0, D3DTSS_TEXCOORDINDEX, 0);
    device->SetTextureStageState(0, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);
    device->SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
    device->SetTextureStageState(1, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
    device->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_POINT);
    device->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_POINT);
    device->SetSamplerState(0, D3DSAMP_MIPFILTER, D3DTEXF_NONE);
    device->SetSamplerState(0, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
    device->SetSamplerState(0, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
    device->SetSamplerState(0, D3DSAMP_SRGBTEXTURE, FALSE);
    device->SetFVF(D3DFVF_XYZRHW | D3DFVF_TEX1);
    struct Vertex {
        float x, y, z, rhw, u, v;
    };
    // Pixel centres are at integer coordinates in Direct3D 9: shift by half a pixel to map texels to pixels.
    const float right = static_cast<float>(width) - 0.5f;
    const float bottom = static_cast<float>(height) - 0.5f;
    const Vertex quad[4] = {
        {-0.5f, -0.5f, 0.0f, 1.0f, 0.0f, 0.0f},
        {right, -0.5f, 0.0f, 1.0f, 1.0f, 0.0f},
        {-0.5f, bottom, 0.0f, 1.0f, 0.0f, 1.0f},
        {right, bottom, 0.0f, 1.0f, 1.0f, 1.0f},
    };
    {
        const SceneScope scene;
        BW_GPU_CHECK(device->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, quad, sizeof(Vertex)));
    }
    device->SetRenderTarget(0, previousTarget.Get());
    device->SetDepthStencilSurface(previousDepth.Get());
}

void PrepareTarget() noexcept
{
    if (g_device->target != nullptr) {
        (void)g_device->target->ForDrawing();
    }
}

}  // namespace

bool Available() noexcept
{
    ComPtr<IDirect3D9Ex> direct3D;
    return SUCCEEDED(Direct3DCreate9Ex(D3D_SDK_VERSION, &direct3D));
}

bool Initialise(HWND window, const Options& options) noexcept
{
    if (g_device != nullptr) {
        return g_device->device != nullptr;
    }
    g_device = new (std::nothrow) Device();
    if (g_device == nullptr) {
        return false;
    }
    Device& d = *g_device;
    d.options = options;
    d.window = window;
    if (FAILED(Direct3DCreate9Ex(D3D_SDK_VERSION, &d.direct3D))) {
        log::Error("Direct3D 9: Direct3DCreate9Ex failed; Direct3D 9Ex is not available");
        return false;
    }
    // The implicit swap chain is never presented (see Device::swapChain); keep it minimal.
    D3DPRESENT_PARAMETERS parameters{};
    parameters.BackBufferWidth = 1;
    parameters.BackBufferHeight = 1;
    parameters.BackBufferFormat = D3DFMT_X8R8G8B8;
    parameters.BackBufferCount = 1;
    parameters.SwapEffect = D3DSWAPEFFECT_DISCARD;
    parameters.hDeviceWindow = window;
    parameters.Windowed = TRUE;
    parameters.PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;
    const DWORD flags = D3DCREATE_HARDWARE_VERTEXPROCESSING | D3DCREATE_MULTITHREADED;
    const HRESULT result = d.direct3D->CreateDeviceEx(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, window, flags, &parameters,
                                                      nullptr, &d.device);
    if (FAILED(result)) {
        log::Error("Direct3D 9: CreateDeviceEx failed (0x{:08x})", static_cast<std::uint32_t>(result));
        return false;
    }
    D3DADAPTER_IDENTIFIER9 identifier{};
    d.direct3D->GetAdapterIdentifier(D3DADAPTER_DEFAULT, 0, &identifier);
    log::Info("Direct3D 9: device created on {}", identifier.Description);

    if (options.anisotropy > 1) {
        for (DWORD stage = 0; stage < kStages; ++stage) {
            d.device->SetSamplerState(stage, D3DSAMP_MAXANISOTROPY, options.anisotropy);
        }
    }
    if (options.multisample > 1) {
        // The most samples up to the requested number that both the colour and the depth format support.
        for (DWORD samples = std::min<DWORD>(options.multisample, 16); samples >= 2; --samples) {
            const auto type = static_cast<D3DMULTISAMPLE_TYPE>(samples);
            if (SUCCEEDED(d.direct3D->CheckDeviceMultiSampleType(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, D3DFMT_X8R8G8B8,
                                                                 TRUE, type, nullptr)) &&
                SUCCEEDED(d.direct3D->CheckDeviceMultiSampleType(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, D3DFMT_D24S8,
                                                                 TRUE, type, nullptr))) {
                d.multisample = type;
                break;
            }
        }
        if (d.multisample == D3DMULTISAMPLE_NONE) {
            log::Warning("Direct3D 9: multisampling is not supported; anti-aliasing is off");
        }
        else {
            log::Info("Direct3D 9: anti-aliasing with {} samples per pixel", static_cast<int>(d.multisample));
        }
    }
    return true;
}

bool Ready() noexcept
{
    return D() != nullptr;
}

// ---------------------------------------------------------------------------------------------------------- Texture

std::unique_ptr<Texture> Texture::Create(std::uint32_t width, std::uint32_t height, std::uint32_t levels,
                                         Format format) noexcept
{
    if (!Ready()) {
        return nullptr;
    }
    const D3DFORMAT native = ToD3D(format);
    ComPtr<IDirect3DTexture9> system;
    ComPtr<IDirect3DTexture9> gpu;
    if (FAILED(BW_GPU_CHECK(D()->CreateTexture(width, height, levels, 0, native, D3DPOOL_SYSTEMMEM, &system, nullptr)))) {
        log::Warning("Direct3D 9: cannot create a {}x{} {} texture", width, height, Name(format));
        return nullptr;
    }
    if (FAILED(BW_GPU_CHECK(D()->CreateTexture(width, height, levels, 0, native, D3DPOOL_DEFAULT, &gpu, nullptr)))) {
        return nullptr;
    }
    const std::uint32_t createdLevels = system->GetLevelCount();
    return std::unique_ptr<Texture>(
        new (std::nothrow) Texture(std::move(system), std::move(gpu), width, height, createdLevels, format));
}

Texture::Texture(ComPtr<IDirect3DTexture9> system, ComPtr<IDirect3DTexture9> gpu, std::uint32_t width,
                 std::uint32_t height, std::uint32_t levels, Format format) noexcept
    : m_system(std::move(system)), m_gpu(std::move(gpu)), m_width(width), m_height(height), m_levels(levels),
      m_format(format)
{
}

Texture::~Texture()
{
    if (g_device != nullptr) {
        for (std::uint32_t stage = 0; stage < kStages; ++stage) {
            if (g_device->bound[stage] == this) {
                g_device->bound[stage] = nullptr;
                g_device->boundNative[stage] = nullptr;
                if (D() != nullptr) {
                    D()->SetTexture(stage, nullptr);
                }
            }
        }
    }
}

bool Texture::Lock(std::uint32_t level, const RECT* rect, bool readOnly, LockedRect& out) noexcept
{
    D3DLOCKED_RECT locked{};
    const DWORD flags = readOnly ? D3DLOCK_READONLY : 0;
    if (FAILED(BW_GPU_CHECK(m_system.Get()->LockRect(level, &locked, rect, flags)))) {
        return false;
    }
    out.bits = locked.pBits;
    out.pitch = locked.Pitch;
    if (readOnly) {
        m_readOnlyLocks |= 1u << level;
    }
    return true;
}

void Texture::Unlock(std::uint32_t level) noexcept
{
    m_system.Get()->UnlockRect(level);
    if ((m_readOnlyLocks & (1u << level)) == 0) {
        m_dirtyLevels |= 1u << level;
        m_preciseLevels &= ~(1u << level);
    }
    m_readOnlyLocks &= ~(1u << level);
}

bool Texture::MakeFullPrecision() noexcept
{
    if (m_full != nullptr) {
        return true;
    }
    if (!Ready() || !g_device->options.fullPrecisionTextures || !IsWidenable(m_format)) {
        return false;
    }
    ComPtr<IDirect3DTexture9> system;
    ComPtr<IDirect3DTexture9> gpu;
    if (FAILED(BW_GPU_CHECK(D()->CreateTexture(m_width, m_height, m_levels, 0, D3DFMT_A8R8G8B8, D3DPOOL_SYSTEMMEM,
                                                &system, nullptr))) ||
        FAILED(BW_GPU_CHECK(D()->CreateTexture(m_width, m_height, m_levels, 0, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &gpu,
                                                nullptr)))) {
        return false;
    }
    m_fullSystem = std::move(system);
    m_full = std::move(gpu);
    m_gpu.Reset();  // a stage that still uses it is switched by UploadBound
    m_dirtyLevels = ~0u;                // levels without precise pixels are converted at the next upload
    return true;
}

void Texture::ConvertToFullPrecision(std::uint32_t level) noexcept
{
    D3DLOCKED_RECT from{};
    D3DLOCKED_RECT to{};
    if (FAILED(m_system.Get()->LockRect(level, &from, nullptr, D3DLOCK_READONLY))) {
        return;
    }
    if (SUCCEEDED(m_fullSystem.Get()->LockRect(level, &to, nullptr, 0))) {
        const std::uint32_t width = LevelSize(m_width, level);
        for (std::uint32_t y = 0; y < LevelSize(m_height, level); ++y) {
            RowToArgb(m_format, static_cast<const std::uint8_t*>(from.pBits) + std::size_t{y} * from.Pitch,
                      reinterpret_cast<std::uint32_t*>(static_cast<std::uint8_t*>(to.pBits) + std::size_t{y} * to.Pitch),
                      width);
        }
        m_fullSystem.Get()->UnlockRect(level);
    }
    m_system.Get()->UnlockRect(level);
}

bool Texture::TopLevelHash(std::uint64_t& hash) noexcept
{
    D3DLOCKED_RECT locked{};
    if (!CanConvert(m_format, Format::A8R8G8B8) ||
        FAILED(m_system.Get()->LockRect(0, &locked, nullptr, D3DLOCK_READONLY))) {
        return false;
    }
    hash = textures::Hash(m_format, m_width, m_height, static_cast<const std::uint8_t*>(locked.pBits), locked.Pitch);
    m_system.Get()->UnlockRect(0);
    return true;
}

bool Texture::SetFullPrecision(std::uint32_t level, const std::uint32_t* argb, std::int32_t pitch) noexcept
{
    if (level >= m_levels || !MakeFullPrecision()) {
        return false;
    }
    D3DLOCKED_RECT to{};
    if (FAILED(BW_GPU_CHECK(m_fullSystem.Get()->LockRect(level, &to, nullptr, 0)))) {
        return false;
    }
    const std::size_t rowBytes = std::size_t{LevelSize(m_width, level)} * sizeof(std::uint32_t);
    for (std::uint32_t y = 0; y < LevelSize(m_height, level); ++y) {
        std::memcpy(static_cast<std::uint8_t*>(to.pBits) + std::size_t{y} * to.Pitch,
                    reinterpret_cast<const std::uint8_t*>(argb) + std::size_t{y} * pitch, rowBytes);
    }
    m_fullSystem.Get()->UnlockRect(level);
    m_preciseLevels |= 1u << level;
    m_dirtyLevels |= 1u << level;
    return true;
}

void Texture::CopyFullPrecision(std::uint32_t level, Texture& destination, std::uint32_t destinationLevel) noexcept
{
    if ((m_preciseLevels & (1u << level)) == 0 || m_fullSystem == nullptr ||
        LevelSize(m_width, level) != LevelSize(destination.m_width, destinationLevel) ||
        LevelSize(m_height, level) != LevelSize(destination.m_height, destinationLevel)) {
        return;
    }
    D3DLOCKED_RECT from{};
    if (FAILED(m_fullSystem.Get()->LockRect(level, &from, nullptr, D3DLOCK_READONLY))) {
        return;
    }
    (void)destination.SetFullPrecision(destinationLevel, static_cast<const std::uint32_t*>(from.pBits), from.Pitch);
    m_fullSystem.Get()->UnlockRect(level);
}

void Texture::Upload() noexcept
{
    if (m_dirtyLevels == 0 || !Ready()) {
        return;
    }
    ++g_statistics.textureUploads;
    if ((m_dirtyLevels & 1u) != 0 && textures::Active()) {
        // Textures the game redraws all the time (object shadows, every frame) are not assets: identifying them
        // would hash every frame and dump thousands of images. A texture that changes again within a quarter of a
        // second is left alone until it settles.
        const std::uint64_t now = GetTickCount64();
        if (now - m_lastChange < kAnimatedMilliseconds) {
            m_hash = 0;
            m_replacement.Reset();
        }
        else {
            Identify();
        }
        m_lastChange = now;
    }
    if (m_full != nullptr) {
        for (std::uint32_t level = 0; level < m_levels; ++level) {
            if ((m_dirtyLevels & ~m_preciseLevels & (1u << level)) != 0) {
                ConvertToFullPrecision(level);
            }
        }
        BW_GPU_CHECK(D()->UpdateTexture(m_fullSystem.Get(), m_full.Get()));
    }
    else {
        BW_GPU_CHECK(D()->UpdateTexture(m_system.Get(), m_gpu.Get()));
    }
    m_dirtyLevels = 0;
}

namespace {

/// A texture pack's image as a GPU texture with a full mipmap chain (cached by file), or null. May throw
/// std::bad_alloc; see Replacement.
IDirect3DTexture9* LoadReplacement(const std::filesystem::path& path, bool alpha)
{
    Device& d = *g_device;
    const std::wstring key = path.wstring() + (alpha ? L"" : L"|opaque");
    if (const auto it = d.replacements.find(key); it != d.replacements.end()) {
        return it->second.Get();
    }
    ComPtr<IDirect3DTexture9>& cached = d.replacements[key];  // stays null when the file cannot be used
    textures::Image image;
    if (!textures::LoadImage(path, image)) {
        return nullptr;
    }
    D3DCAPS9 caps{};
    d.device->GetDeviceCaps(&caps);
    if (image.width > caps.MaxTextureWidth || image.height > caps.MaxTextureHeight) {
        log::Warning("Textures: {} is {}x{}; this GPU takes at most {}x{}", log::PathText(path), image.width, image.height,
                     caps.MaxTextureWidth, caps.MaxTextureHeight);
        return nullptr;
    }
    if (!alpha) {
        // The game's texture has no alpha: keep its blending exactly as with the original.
        for (std::uint32_t& texel : image.argb) {
            texel |= 0xff000000u;
        }
    }
    const std::uint32_t width = image.width;
    const std::uint32_t height = image.height;
    UINT levels = 1;
    for (std::uint32_t size = std::max(image.width, image.height); size > 1; size /= 2) {
        ++levels;
    }
    ComPtr<IDirect3DTexture9> system;
    ComPtr<IDirect3DTexture9> gpu;
    if (FAILED(BW_GPU_CHECK(d.device->CreateTexture(image.width, image.height, levels, 0, D3DFMT_A8R8G8B8,
                                                     D3DPOOL_SYSTEMMEM, &system, nullptr))) ||
        FAILED(BW_GPU_CHECK(d.device->CreateTexture(image.width, image.height, levels, 0, D3DFMT_A8R8G8B8,
                                                     D3DPOOL_DEFAULT, &gpu, nullptr)))) {
        return nullptr;
    }
    try {
        for (UINT level = 0; level < levels; ++level) {
            if (level > 0) {
                image = textures::HalveImage(image);
            }
            D3DLOCKED_RECT locked{};
            if (FAILED(system->LockRect(level, &locked, nullptr, 0))) {
                return nullptr;
            }
            for (std::uint32_t y = 0; y < image.height; ++y) {
                std::memcpy(static_cast<std::uint8_t*>(locked.pBits) + std::size_t{y} * locked.Pitch,
                            image.argb.data() + std::size_t{y} * image.width, std::size_t{image.width} * 4);
            }
            system->UnlockRect(level);
        }
    }
    catch (const std::bad_alloc&) {
        return nullptr;
    }
    if (FAILED(BW_GPU_CHECK(d.device->UpdateTexture(system.Get(), gpu.Get())))) {
        return nullptr;
    }
    cached = gpu;
    log::Info("Textures: using {} ({}x{})", log::PathText(path), width, height);
    return cached.Get();
}

IDirect3DTexture9* Replacement(const std::filesystem::path& path, bool alpha) noexcept
{
    try {
        return LoadReplacement(path, alpha);
    }
    catch (const std::bad_alloc&) {
        log::Warning("Textures: out of memory for {}", log::PathText(path));
        return nullptr;
    }
}

}  // namespace

void Texture::Identify() noexcept
{
    if (!CanConvert(m_format, Format::A8R8G8B8)) {
        return;
    }
    D3DLOCKED_RECT locked{};
    if (FAILED(m_system.Get()->LockRect(0, &locked, nullptr, D3DLOCK_READONLY))) {
        return;
    }
    const auto* bits = static_cast<const std::uint8_t*>(locked.pBits);
    const std::uint64_t hash = textures::Hash(m_format, m_width, m_height, bits, locked.Pitch);
    if (hash == m_hash) {
        m_system.Get()->UnlockRect(0);
        return;
    }
    m_hash = hash;
    m_replacement.Reset();

    // A source with more colour than this texture's format (a 24-bit file) is shown at full colour.
    textures::Source source;
    (void)textures::FindSource(hash, source);
    const bool fullColourSource =
        source.image && source.image->width == m_width && source.image->height == m_height;
    if (fullColourSource) {
        (void)SetFullPrecision(0, source.image->argb.data(), static_cast<std::int32_t>(m_width * 4));
    }

    if (textures::WantsDump(hash)) try {
        textures::Image image;
        if (fullColourSource) {
            image = *source.image;
        }
        else {
            // The colours as the game sees them, or the full-precision ones where the game decoded a DXT texture.
            image.width = m_width;
            image.height = m_height;
            image.argb.resize(std::size_t{m_width} * m_height);
            D3DLOCKED_RECT precise{};
            if ((m_preciseLevels & 1u) != 0 &&
                SUCCEEDED(m_fullSystem.Get()->LockRect(0, &precise, nullptr, D3DLOCK_READONLY))) {
                ImageToArgb(Format::A8R8G8B8, static_cast<const std::uint8_t*>(precise.pBits), precise.Pitch, m_width,
                            m_height, image.argb.data());
                m_fullSystem.Get()->UnlockRect(0);
            }
            else {
                ImageToArgb(m_format, bits, locked.Pitch, m_width, m_height, image.argb.data());
            }
        }
        textures::Dump(hash, source.name, m_format, image);
    }
    catch (const std::bad_alloc&) {
        log::Warning("Textures: out of memory for a dump");
    }

    if (const std::filesystem::path path = textures::FindReplacement(hash, source.name); !path.empty()) {
        if (IDirect3DTexture9* replacement = Replacement(path, HasAlpha(m_format))) {
            m_replacement = replacement;  // takes a reference; the cache keeps its own
        }
    }
    m_system.Get()->UnlockRect(0);
}

// ----------------------------------------------------------------------------------------------------- RenderTarget

std::unique_ptr<RenderTarget> RenderTarget::Create(std::uint32_t width, std::uint32_t height, Format format) noexcept
{
    if (!Ready()) {
        return nullptr;
    }
    Device& d = *g_device;
    const D3DFORMAT native = ToD3D(format);
    if (d.multisample != D3DMULTISAMPLE_NONE) {
        // Draws go to a multisampled surface; the single-sampled copy is a texture so that it can be drawn back.
        ComPtr<IDirect3DTexture9> texture;
        ComPtr<IDirect3DSurface9> surface;
        ComPtr<IDirect3DSurface9> multisampled;
        if (SUCCEEDED(D()->CreateTexture(width, height, 1, D3DUSAGE_RENDERTARGET, native, D3DPOOL_DEFAULT, &texture,
                                         nullptr)) &&
            SUCCEEDED(texture->GetSurfaceLevel(0, &surface)) &&
            SUCCEEDED(D()->CreateRenderTarget(width, height, native, d.multisample, 0, FALSE, &multisampled, nullptr))) {
            log::Info("Direct3D 9: render target {}x{} {}, {} samples per pixel", width, height, Name(format),
                      static_cast<int>(d.multisample));
            std::unique_ptr<RenderTarget> target(new (std::nothrow) RenderTarget(
                std::move(surface), std::move(texture), std::move(multisampled), width, height, format));
            if (target) {
                return target;
            }
        }
        log::Warning("Direct3D 9: no multisampled {}x{} {} render target; anti-aliasing is off", width, height,
                     Name(format));
        d.multisample = D3DMULTISAMPLE_NONE;  // depth buffers must match
    }
    ComPtr<IDirect3DSurface9> surface;
    if (FAILED(BW_GPU_CHECK(D()->CreateRenderTarget(width, height, native, D3DMULTISAMPLE_NONE, 0, FALSE, &surface,
                                                    nullptr)))) {
        return nullptr;
    }
    log::Info("Direct3D 9: render target {}x{} {}", width, height, Name(format));
    return std::unique_ptr<RenderTarget>(
        new (std::nothrow) RenderTarget(std::move(surface), nullptr, nullptr, width, height, format));
}

RenderTarget::RenderTarget(ComPtr<IDirect3DSurface9> surface, ComPtr<IDirect3DTexture9> texture,
                           ComPtr<IDirect3DSurface9> multisampled, std::uint32_t width, std::uint32_t height,
                           Format format) noexcept
    : m_surface(std::move(surface)), m_texture(std::move(texture)), m_multisampled(std::move(multisampled)),
      m_width(width), m_height(height), m_format(format)
{
}

RenderTarget::~RenderTarget()
{
    if (g_device != nullptr && g_device->target == this) {
        g_device->target = nullptr;
    }
}

IDirect3DSurface9* RenderTarget::Resolved() noexcept
{
    if (m_newest == Newest::Multisampled) {
        ++g_statistics.resolves;
        BW_GPU_CHECK(D()->StretchRect(m_multisampled.Get(), nullptr, m_surface.Get(), nullptr, D3DTEXF_NONE));
        m_newest = Newest::Both;
    }
    return m_surface.Get();
}

IDirect3DSurface9* RenderTarget::ForWriting() noexcept
{
    IDirect3DSurface9* surface = Resolved();
    if (m_multisampled != nullptr) {
        m_newest = Newest::Single;
    }
    return surface;
}

IDirect3DSurface9* RenderTarget::ForDrawing() noexcept
{
    if (m_multisampled == nullptr) {
        return m_surface.Get();
    }
    if (m_newest == Newest::Single) {
        // Copied or CPU-written pixels: put them under what is drawn next.
        DrawTexture(m_texture.Get(), m_multisampled.Get(), m_width, m_height);
    }
    m_newest = Newest::Multisampled;
    return m_multisampled.Get();
}

bool RenderTarget::Read(void* bits, std::int32_t pitch) noexcept
{
    ++g_statistics.targetReads;
    ComPtr<IDirect3DSurface9> system;
    if (FAILED(BW_GPU_CHECK(D()->CreateOffscreenPlainSurface(m_width, m_height, ToD3D(m_format), D3DPOOL_SYSTEMMEM,
                                                              &system, nullptr))) ||
        FAILED(BW_GPU_CHECK(D()->GetRenderTargetData(Resolved(), system.Get())))) {
        return false;
    }
    D3DLOCKED_RECT locked{};
    if (FAILED(system->LockRect(&locked, nullptr, D3DLOCK_READONLY))) {
        return false;
    }
    const std::uint32_t row = RowBytes(m_format, m_width);
    for (std::uint32_t y = 0; y < m_height; ++y) {
        std::memcpy(static_cast<std::uint8_t*>(bits) + std::size_t{y} * pitch,
                    static_cast<const std::uint8_t*>(locked.pBits) + std::size_t{y} * locked.Pitch, row);
    }
    system->UnlockRect();
    return true;
}

bool RenderTarget::Write(const void* bits, std::int32_t pitch) noexcept
{
    ++g_statistics.targetWrites;
    const RECT all{0, 0, static_cast<LONG>(m_width), static_cast<LONG>(m_height)};
    Draw(bits, pitch, m_format, m_width, m_height, all, all);
    return true;
}

void RenderTarget::Fill(const RECT* rect, std::uint32_t argb) noexcept
{
    BW_GPU_CHECK(D()->ColorFill(ForWriting(), rect, argb));
}

void RenderTarget::Draw(const void* bits, std::int32_t pitch, Format format, std::uint32_t width,
                        std::uint32_t height, const RECT& source, const RECT& destination) noexcept
{
    const auto sourceWidth = static_cast<std::uint32_t>(source.right - source.left);
    const auto sourceHeight = static_cast<std::uint32_t>(source.bottom - source.top);
    if (sourceWidth == 0 || sourceHeight == 0 || source.right > static_cast<LONG>(width) ||
        source.bottom > static_cast<LONG>(height) || !EnsureStaging(sourceWidth, sourceHeight)) {
        return;
    }
    Device& d = *g_device;
    D3DLOCKED_RECT locked{};
    const RECT stagingRect{0, 0, static_cast<LONG>(sourceWidth), static_cast<LONG>(sourceHeight)};
    if (FAILED(BW_GPU_CHECK(d.stagingSystem->LockRect(&locked, &stagingRect, 0)))) {
        return;
    }
    const std::uint32_t step = BytesPerPixel(format);
    for (std::uint32_t y = 0; y < sourceHeight; ++y) {
        const auto* row = static_cast<const std::uint8_t*>(bits) + std::size_t(source.top + y) * pitch +
                          std::size_t(source.left) * step;
        RowToArgb(format, row,
                  reinterpret_cast<std::uint32_t*>(static_cast<std::uint8_t*>(locked.pBits) + std::size_t{y} * locked.Pitch),
                  sourceWidth);
    }
    d.stagingSystem->UnlockRect();
    const POINT origin{0, 0};
    BW_GPU_CHECK(d.device->UpdateSurface(d.stagingSystem.Get(), &stagingRect, d.stagingGpu.Get(), &origin));
    BW_GPU_CHECK(d.device->StretchRect(d.stagingGpu.Get(), &stagingRect, ForWriting(), &destination,
                                       SameSize(stagingRect, destination) ? D3DTEXF_POINT : D3DTEXF_LINEAR));
}

void RenderTarget::CopyFrom(RenderTarget& source, const RECT& sourceRect, const RECT& destination) noexcept
{
    IDirect3DSurface9* from = source.Resolved();
    BW_GPU_CHECK(D()->StretchRect(from, &sourceRect, ForWriting(), &destination,
                                  SameSize(sourceRect, destination) ? D3DTEXF_POINT : D3DTEXF_LINEAR));
}

// ------------------------------------------------------------------------------------------------------ DepthBuffer

std::unique_ptr<DepthBuffer> DepthBuffer::Create(std::uint32_t width, std::uint32_t height) noexcept
{
    if (!Ready()) {
        return nullptr;
    }
    Device& d = *g_device;
    ComPtr<IDirect3DSurface9> surface;
    if (d.multisample != D3DMULTISAMPLE_NONE) {
        // Multisampled depth cannot be locked; nothing needs to read it (the sun test uses ProbeDepth).
        if (SUCCEEDED(BW_GPU_CHECK(D()->CreateDepthStencilSurface(width, height, D3DFMT_D24S8, d.multisample, 0, FALSE,
                                                                   &surface, nullptr)))) {
            return std::unique_ptr<DepthBuffer>(new (std::nothrow) DepthBuffer(std::move(surface), false));
        }
    }
    if (SUCCEEDED(D()->CreateDepthStencilSurface(width, height, D3DFMT_D32F_LOCKABLE, D3DMULTISAMPLE_NONE, 0, FALSE,
                                                 &surface, nullptr))) {
        return std::unique_ptr<DepthBuffer>(new (std::nothrow) DepthBuffer(std::move(surface), true));
    }
    log::Warning("Direct3D 9: no lockable depth buffer; the CPU cannot read depth");
    if (FAILED(BW_GPU_CHECK(D()->CreateDepthStencilSurface(width, height, D3DFMT_D24S8, D3DMULTISAMPLE_NONE, 0, FALSE,
                                                           &surface, nullptr)))) {
        return nullptr;
    }
    return std::unique_ptr<DepthBuffer>(new (std::nothrow) DepthBuffer(std::move(surface), false));
}

DepthBuffer::DepthBuffer(ComPtr<IDirect3DSurface9> surface, bool readable) noexcept
    : m_surface(std::move(surface)), m_readable(readable)
{
}

bool DepthBuffer::Read(const RECT& rect, float* values) noexcept
{
    if (!m_readable) {
        return false;
    }
    ++g_statistics.depthReads;
    D3DLOCKED_RECT locked{};
    if (FAILED(BW_GPU_CHECK(m_surface.Get()->LockRect(&locked, &rect, D3DLOCK_READONLY)))) {
        return false;
    }
    const auto width = static_cast<std::size_t>(rect.right - rect.left);
    for (LONG y = 0; y < rect.bottom - rect.top; ++y) {
        std::memcpy(values + std::size_t(y) * width, static_cast<const std::uint8_t*>(locked.pBits) + std::size_t(y) * locked.Pitch,
                    width * sizeof(float));
    }
    m_surface.Get()->UnlockRect();
    return true;
}

DepthBuffer::~DepthBuffer()
{
    if (g_device != nullptr && g_device->depth == this) {
        g_device->depth = nullptr;
        if (D() != nullptr) {
            D()->SetDepthStencilSurface(nullptr);
        }
    }
}

// ------------------------------------------------------------------------------------------------------------ State

void SetRenderTarget(RenderTarget* target, DepthBuffer* depth) noexcept
{
    if (!Ready() || target == nullptr) {
        return;
    }
    BW_GPU_CHECK(D()->SetRenderTarget(0, target->ForDrawing()));
    BW_GPU_CHECK(D()->SetDepthStencilSurface(depth != nullptr ? depth->Native() : nullptr));
    g_device->target = target;
    g_device->depth = depth;
}

void SetRenderState(std::uint32_t state, std::uint32_t value) noexcept
{
    if (!Ready()) {
        return;
    }
    if (state == d3d9::kRsLighting) {
        g_device->lighting = value != 0;
    }
    BW_GPU_CHECK(D()->SetRenderState(static_cast<D3DRENDERSTATETYPE>(state), value));
}

void SetSamplerState(std::uint32_t stage, std::uint32_t state, std::uint32_t value) noexcept
{
    if (!Ready()) {
        return;
    }
    const std::uint32_t anisotropy = g_device->options.anisotropy;
    if (anisotropy > 1) {
        if (state == d3d9::kSampMinFilter && value == d3d9::kTexfLinear) {
            value = d3d9::kTexfAnisotropic;
        }
        if (state == d3d9::kSampMaxAnisotropy) {
            value = std::max(value, anisotropy);
        }
    }
    BW_GPU_CHECK(D()->SetSamplerState(stage, static_cast<D3DSAMPLERSTATETYPE>(state), value));
}

void SetTextureStageState(std::uint32_t stage, std::uint32_t state, std::uint32_t value) noexcept
{
    if (Ready()) {
        BW_GPU_CHECK(D()->SetTextureStageState(stage, static_cast<D3DTEXTURESTAGESTATETYPE>(state), value));
    }
}

void SetTexture(std::uint32_t stage, Texture* texture) noexcept
{
    if (!Ready() || stage >= kStages) {
        return;
    }
    g_device->bound[stage] = texture;
    g_device->boundNative[stage] = texture != nullptr ? texture->Native() : nullptr;
    BW_GPU_CHECK(D()->SetTexture(stage, g_device->boundNative[stage]));
}

void SetTransform(std::uint32_t state, const float* matrix) noexcept
{
    if (Ready()) {
        BW_GPU_CHECK(D()->SetTransform(static_cast<D3DTRANSFORMSTATETYPE>(state), reinterpret_cast<const D3DMATRIX*>(matrix)));
    }
}

void SetViewport(const void* viewport) noexcept
{
    static_assert(sizeof(D3DVIEWPORT9) == 24);
    if (Ready()) {
        BW_GPU_CHECK(D()->SetViewport(static_cast<const D3DVIEWPORT9*>(viewport)));
    }
}

void SetMaterial(const void* material) noexcept
{
    static_assert(sizeof(D3DMATERIAL9) == 68);
    if (Ready()) {
        BW_GPU_CHECK(D()->SetMaterial(static_cast<const D3DMATERIAL9*>(material)));
    }
}

bool SetLight(std::uint32_t index, const void* light) noexcept
{
    static_assert(sizeof(D3DLIGHT9) == 104);
    return Ready() && SUCCEEDED(BW_GPU_CHECK(D()->SetLight(index, static_cast<const D3DLIGHT9*>(light))));
}

void LightEnable(std::uint32_t index, bool enable) noexcept
{
    if (Ready()) {
        BW_GPU_CHECK(D()->LightEnable(index, enable ? TRUE : FALSE));
    }
}

void Clear(std::uint32_t count, const void* rects, std::uint32_t flags, std::uint32_t color, float z,
           std::uint32_t stencil) noexcept
{
    if (!Ready()) {
        return;
    }
    if (g_device->depth == nullptr) {
        flags &= ~static_cast<std::uint32_t>(D3DCLEAR_ZBUFFER | D3DCLEAR_STENCIL);
        if (flags == 0) {
            return;
        }
    }
    PrepareTarget();
    BW_GPU_CHECK(D()->Clear(count, static_cast<const D3DRECT*>(rects), flags, color, z, stencil));
}

// The game sometimes begins a scene twice and ends it twice (around cutscenes); Direct3D 7 refused the extra calls
// harmlessly, so they are dropped here.
void BeginScene() noexcept
{
    if (Ready() && !g_device->inScene && SUCCEEDED(BW_GPU_CHECK(D()->BeginScene()))) {
        g_device->inScene = true;
    }
}

void EndScene() noexcept
{
    if (Ready() && g_device->inScene) {
        BW_GPU_CHECK(D()->EndScene());
        g_device->inScene = false;
    }
}

namespace {

/// Direct3D 7 does not light vertices without normals; Direct3D 9 would light them to the material colour.
class UnlitScope {
public:
    explicit UnlitScope(std::uint32_t fvf) noexcept : m_active(g_device->lighting && IsUnlitFormat(fvf))
    {
        if (m_active) {
            D()->SetRenderState(D3DRS_LIGHTING, FALSE);
        }
    }
    ~UnlitScope()
    {
        if (m_active) {
            D()->SetRenderState(D3DRS_LIGHTING, TRUE);
        }
    }
    UnlitScope(const UnlitScope&) = delete;
    UnlitScope& operator=(const UnlitScope&) = delete;

private:
    bool m_active;
};

}  // namespace

void Draw(std::uint32_t type, std::uint32_t fvf, const void* vertices, std::uint32_t vertexCount) noexcept
{
    const std::uint32_t primitives = PrimitiveCount(type, vertexCount);
    if (!Ready() || primitives == 0) {
        return;
    }
    PrepareTarget();
    UploadBound();
    ++g_statistics.draws;
    BW_GPU_CHECK(D()->SetFVF(fvf));
    const UnlitScope unlit(fvf);
    BW_GPU_CHECK(D()->DrawPrimitiveUP(static_cast<D3DPRIMITIVETYPE>(type), primitives, vertices, VertexStride(fvf)));
}

void DrawIndexed(std::uint32_t type, std::uint32_t fvf, const void* vertices, std::uint32_t vertexCount,
                 const std::uint16_t* indices, std::uint32_t indexCount) noexcept
{
    const std::uint32_t primitives = PrimitiveCount(type, indexCount);
    if (!Ready() || primitives == 0 || vertexCount == 0) {
        return;
    }
    PrepareTarget();
    UploadBound();
    ++g_statistics.draws;
    BW_GPU_CHECK(D()->SetFVF(fvf));
    const UnlitScope unlit(fvf);
    BW_GPU_CHECK(D()->DrawIndexedPrimitiveUP(static_cast<D3DPRIMITIVETYPE>(type), 0, vertexCount, primitives, indices,
                                             D3DFMT_INDEX16, vertices, VertexStride(fvf)));
}

std::uint32_t ValidateDevice() noexcept
{
    DWORD passes = 0;
    if (!Ready() || FAILED(D()->ValidateDevice(&passes))) {
        return 0;
    }
    return passes;
}

bool ProbeDepth(std::uint32_t index, std::int32_t x, std::int32_t y, float depth) noexcept
{
    if (!Ready() || index >= kDepthProbes || g_device->target == nullptr || g_device->depth == nullptr) {
        return false;
    }
    Device& d = *g_device;
    Probe& probe = d.probes[index];
    if (!probe.query) {
        if (d.queriesUnavailable) {
            return false;
        }
        if (FAILED(d.device->CreateQuery(D3DQUERYTYPE_OCCLUSION, &probe.query))) {
            d.queriesUnavailable = true;
            log::Warning("Direct3D 9: occlusion queries are not supported; the sun is never hidden");
            return false;
        }
    }
    if (probe.pending) {
        DWORD samples = 0;
        const HRESULT result = probe.query->GetData(&samples, sizeof samples, 0);
        if (result == S_FALSE) {
            return probe.answer;  // still running: answer again with the previous result
        }
        probe.pending = false;
        if (result == S_OK) {
            probe.answer = samples != 0;
        }
    }

    // One point at `depth` that passes the depth test where the depth buffer is nearer: drawn samples = hidden.
    ++g_statistics.depthProbes;
    PrepareTarget();
    const SavedState saved;
    if (!saved) {
        return probe.answer;
    }
    IDirect3DDevice9Ex* device = d.device.Get();
    SetNeutralStates();
    device->SetRenderState(D3DRS_ZENABLE, D3DZB_TRUE);
    device->SetRenderState(D3DRS_ZFUNC, D3DCMP_GREATER);
    device->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
    device->SetRenderState(D3DRS_COLORWRITEENABLE, 0);
    device->SetRenderState(D3DRS_POINTSPRITEENABLE, FALSE);
    device->SetRenderState(D3DRS_POINTSCALEENABLE, FALSE);
    const float pointSize = 1.0f;
    DWORD pointSizeBits = 0;
    std::memcpy(&pointSizeBits, &pointSize, sizeof pointSizeBits);
    device->SetRenderState(D3DRS_POINTSIZE, pointSizeBits);
    device->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_DISABLE);
    device->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
    const D3DVIEWPORT9 viewport{0, 0, d.target->Width(), d.target->Height(), 0.0f, 1.0f};
    device->SetViewport(&viewport);
    device->SetFVF(D3DFVF_XYZRHW);
    x = std::clamp<std::int32_t>(x, 0, static_cast<std::int32_t>(d.target->Width()) - 1);
    y = std::clamp<std::int32_t>(y, 0, static_cast<std::int32_t>(d.target->Height()) - 1);
    const float point[4] = {static_cast<float>(x), static_cast<float>(y), depth, 1.0f};
    {
        const SceneScope scene;
        probe.query->Issue(D3DISSUE_BEGIN);
        BW_GPU_CHECK(device->DrawPrimitiveUP(D3DPT_POINTLIST, 1, point, sizeof point));
        probe.query->Issue(D3DISSUE_END);
    }
    probe.pending = true;
    return probe.answer;
}

void Present(RenderTarget& frame, const RECT& source, HWND window, const RECT& destination) noexcept
{
    if (!Ready()) {
        return;
    }
    RECT client{};
    GetClientRect(window, &client);
    const auto width = static_cast<std::uint32_t>(std::max<LONG>(client.right, 1));
    const auto height = static_cast<std::uint32_t>(std::max<LONG>(client.bottom, 1));
    if (!EnsureSwapChain(window, width, height)) {
        return;
    }
    Device& d = *g_device;
    ComPtr<IDirect3DSurface9> back;
    if (FAILED(BW_GPU_CHECK(d.swapChain->GetBackBuffer(0, D3DBACKBUFFER_TYPE_MONO, &back)))) {
        return;
    }
    BW_GPU_CHECK(d.device->ColorFill(back.Get(), nullptr, D3DCOLOR_XRGB(0, 0, 0)));
    RECT target = destination;
    IntersectRect(&target, &target, &client);
    if (!IsRectEmpty(&target)) {
        BW_GPU_CHECK(d.device->StretchRect(frame.Resolved(), &source, back.Get(), &target,
                                           SameSize(source, target) ? D3DTEXF_POINT : D3DTEXF_LINEAR));
    }
    BW_GPU_CHECK(d.swapChain->Present(nullptr, nullptr, window, nullptr, 0));
}

Statistics TakeStatistics() noexcept
{
    return std::exchange(g_statistics, Statistics{});
}

void Shutdown() noexcept
{
    if (g_device != nullptr) {
        for (Probe& probe : g_device->probes) {
            probe.query.Reset();
        }
        g_device->replacements.clear();
        g_device->swapChain.Reset();
        g_device->stagingSystem.Reset();
        g_device->stagingGpu.Reset();
        g_device->device.Reset();
        g_device->direct3D.Reset();
    }
}

}  // namespace bw::render::gpu
