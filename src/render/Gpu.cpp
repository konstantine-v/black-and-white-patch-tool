#include "render/Gpu.h"

#include "core/Log.h"
#include "render/Pixels.h"
#include "render/Translate.h"

#include <d3d9.h>
#include <wrl/client.h>

#include <algorithm>
#include <cstring>
#include <new>
#include <array>
#include <utility>
#include <vector>

namespace bw::render::gpu {

namespace {

using Microsoft::WRL::ComPtr;

constexpr std::uint32_t kStages = 8;

struct Device {
    ComPtr<IDirect3D9Ex> direct3D;
    ComPtr<IDirect3DDevice9Ex> device;
    HWND window = nullptr;
    Options options;

    /// Presentation: a swap chain the size of the window's client area, recreated when that changes. Separate from the
    /// device's implicit swap chain so that resizing never resets the device (which would reset all states).
    ComPtr<IDirect3DSwapChain9> swapChain;
    std::uint32_t swapWidth = 0;
    std::uint32_t swapHeight = 0;
    HWND swapWindow = nullptr;

    std::array<Texture*, kStages> bound{};
    bool lighting = true;
    bool depthBound = false;

    /// CPU -> render target staging: A8R8G8B8 in system memory, uploaded to a default-pool surface.
    ComPtr<IDirect3DSurface9> stagingSystem;
    ComPtr<IDirect3DSurface9> stagingGpu;
    std::uint32_t stagingWidth = 0;
    std::uint32_t stagingHeight = 0;
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
    for (Texture* texture : g_device->bound) {
        if (texture != nullptr) {
            texture->Upload();
        }
    }
}

}  // namespace

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
    IDirect3DTexture9* system = nullptr;
    IDirect3DTexture9* gpu = nullptr;
    if (FAILED(BW_GPU_CHECK(D()->CreateTexture(width, height, levels, 0, native, D3DPOOL_SYSTEMMEM, &system, nullptr)))) {
        log::Warning("Direct3D 9: cannot create a {}x{} {} texture", width, height, Name(format));
        return nullptr;
    }
    if (FAILED(BW_GPU_CHECK(D()->CreateTexture(width, height, levels, 0, native, D3DPOOL_DEFAULT, &gpu, nullptr)))) {
        system->Release();
        return nullptr;
    }
    std::unique_ptr<Texture> texture(new (std::nothrow) Texture(system, gpu, system->GetLevelCount()));
    if (!texture) {
        system->Release();
        gpu->Release();
    }
    return texture;
}

Texture::~Texture()
{
    if (g_device != nullptr) {
        for (Texture*& bound : g_device->bound) {
            if (bound == this) {
                bound = nullptr;
                if (D() != nullptr) {
                    D()->SetTexture(static_cast<DWORD>(&bound - g_device->bound.data()), nullptr);
                }
            }
        }
    }
    static_cast<IDirect3DTexture9*>(m_system)->Release();
    static_cast<IDirect3DTexture9*>(m_gpu)->Release();
}

bool Texture::Lock(std::uint32_t level, const RECT* rect, bool readOnly, LockedRect& out) noexcept
{
    D3DLOCKED_RECT locked{};
    const DWORD flags = readOnly ? D3DLOCK_READONLY : 0;
    if (FAILED(BW_GPU_CHECK(static_cast<IDirect3DTexture9*>(m_system)->LockRect(level, &locked, rect, flags)))) {
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
    static_cast<IDirect3DTexture9*>(m_system)->UnlockRect(level);
    if ((m_readOnlyLocks & (1u << level)) == 0) {
        m_dirty = true;
    }
    m_readOnlyLocks &= ~(1u << level);
}

void Texture::Upload() noexcept
{
    if (m_dirty && Ready()) {
        ++g_statistics.textureUploads;
        BW_GPU_CHECK(D()->UpdateTexture(static_cast<IDirect3DTexture9*>(m_system),
                                        static_cast<IDirect3DTexture9*>(m_gpu)));
        m_dirty = false;
    }
}

// ----------------------------------------------------------------------------------------------------- RenderTarget

std::unique_ptr<RenderTarget> RenderTarget::Create(std::uint32_t width, std::uint32_t height, Format format) noexcept
{
    if (!Ready()) {
        return nullptr;
    }
    IDirect3DSurface9* surface = nullptr;
    if (FAILED(BW_GPU_CHECK(D()->CreateRenderTarget(width, height, ToD3D(format), D3DMULTISAMPLE_NONE, 0, FALSE,
                                                    &surface, nullptr)))) {
        return nullptr;
    }
    log::Info("Direct3D 9: render target {}x{} {}", width, height, Name(format));
    return std::unique_ptr<RenderTarget>(new (std::nothrow) RenderTarget(surface, width, height, format));
}

RenderTarget::~RenderTarget()
{
    static_cast<IDirect3DSurface9*>(m_surface)->Release();
}

bool RenderTarget::Read(void* bits, std::int32_t pitch) noexcept
{
    ++g_statistics.targetReads;
    ComPtr<IDirect3DSurface9> system;
    if (FAILED(BW_GPU_CHECK(D()->CreateOffscreenPlainSurface(m_width, m_height, ToD3D(m_format), D3DPOOL_SYSTEMMEM,
                                                              &system, nullptr))) ||
        FAILED(BW_GPU_CHECK(D()->GetRenderTargetData(static_cast<IDirect3DSurface9*>(m_surface), system.Get())))) {
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
    BW_GPU_CHECK(D()->ColorFill(static_cast<IDirect3DSurface9*>(m_surface), rect, argb));
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
    BW_GPU_CHECK(d.device->StretchRect(d.stagingGpu.Get(), &stagingRect, static_cast<IDirect3DSurface9*>(m_surface),
                                       &destination, SameSize(stagingRect, destination) ? D3DTEXF_POINT : D3DTEXF_LINEAR));
}

void RenderTarget::CopyFrom(RenderTarget& source, const RECT& sourceRect, const RECT& destination) noexcept
{
    BW_GPU_CHECK(D()->StretchRect(static_cast<IDirect3DSurface9*>(source.m_surface), &sourceRect,
                                  static_cast<IDirect3DSurface9*>(m_surface), &destination,
                                  SameSize(sourceRect, destination) ? D3DTEXF_POINT : D3DTEXF_LINEAR));
}

// ------------------------------------------------------------------------------------------------------ DepthBuffer

std::unique_ptr<DepthBuffer> DepthBuffer::Create(std::uint32_t width, std::uint32_t height) noexcept
{
    if (!Ready()) {
        return nullptr;
    }
    IDirect3DSurface9* surface = nullptr;
    if (SUCCEEDED(D()->CreateDepthStencilSurface(width, height, D3DFMT_D32F_LOCKABLE, D3DMULTISAMPLE_NONE, 0, FALSE,
                                                 &surface, nullptr))) {
        return std::unique_ptr<DepthBuffer>(new (std::nothrow) DepthBuffer(surface, true));
    }
    log::Warning("Direct3D 9: no lockable depth buffer; the game cannot read depth (sun visibility)");
    if (FAILED(BW_GPU_CHECK(D()->CreateDepthStencilSurface(width, height, D3DFMT_D24S8, D3DMULTISAMPLE_NONE, 0, FALSE,
                                                           &surface, nullptr)))) {
        return nullptr;
    }
    return std::unique_ptr<DepthBuffer>(new (std::nothrow) DepthBuffer(surface, false));
}

bool DepthBuffer::Read(const RECT& rect, float* values) noexcept
{
    if (!m_readable) {
        return false;
    }
    ++g_statistics.depthReads;
    D3DLOCKED_RECT locked{};
    if (FAILED(BW_GPU_CHECK(static_cast<IDirect3DSurface9*>(m_surface)->LockRect(&locked, &rect, D3DLOCK_READONLY)))) {
        return false;
    }
    const auto width = static_cast<std::size_t>(rect.right - rect.left);
    for (LONG y = 0; y < rect.bottom - rect.top; ++y) {
        std::memcpy(values + std::size_t(y) * width, static_cast<const std::uint8_t*>(locked.pBits) + std::size_t(y) * locked.Pitch,
                    width * sizeof(float));
    }
    static_cast<IDirect3DSurface9*>(m_surface)->UnlockRect();
    return true;
}

DepthBuffer::~DepthBuffer()
{
    static_cast<IDirect3DSurface9*>(m_surface)->Release();
}

// ------------------------------------------------------------------------------------------------------------ State

void SetRenderTarget(RenderTarget* target, DepthBuffer* depth) noexcept
{
    if (!Ready() || target == nullptr) {
        return;
    }
    BW_GPU_CHECK(D()->SetRenderTarget(0, static_cast<IDirect3DSurface9*>(target->Native())));
    BW_GPU_CHECK(D()->SetDepthStencilSurface(depth != nullptr ? static_cast<IDirect3DSurface9*>(depth->Native()) : nullptr));
    g_device->depthBound = depth != nullptr;
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
    BW_GPU_CHECK(D()->SetTexture(stage, texture != nullptr ? static_cast<IDirect3DTexture9*>(texture->m_gpu) : nullptr));
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
    if (!g_device->depthBound) {
        flags &= ~static_cast<std::uint32_t>(D3DCLEAR_ZBUFFER | D3DCLEAR_STENCIL);
        if (flags == 0) {
            return;
        }
    }
    BW_GPU_CHECK(D()->Clear(count, static_cast<const D3DRECT*>(rects), flags, color, z, stencil));
}

void BeginScene() noexcept
{
    if (Ready()) {
        BW_GPU_CHECK(D()->BeginScene());
    }
}

void EndScene() noexcept
{
    if (Ready()) {
        BW_GPU_CHECK(D()->EndScene());
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
        BW_GPU_CHECK(d.device->StretchRect(static_cast<IDirect3DSurface9*>(frame.Native()), &source, back.Get(), &target,
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
        g_device->swapChain.Reset();
        g_device->stagingSystem.Reset();
        g_device->stagingGpu.Reset();
        g_device->device.Reset();
        g_device->direct3D.Reset();
    }
}

}  // namespace bw::render::gpu
