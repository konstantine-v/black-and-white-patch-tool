#include "render/Device.h"

#include "core/Log.h"
#include "diagnostics/Screenshot.h"
#include "render/DirectDraw.h"
#include "render/Pixels.h"
#include "render/Gpu.h"
#include "render/Surface.h"
#include "render/Translate.h"

#include <bit>
#include <format>
#include <filesystem>
#include <fstream>
#include <set>
#include <new>
#include <variant>

namespace bw::render {

namespace {

Device* g_current = nullptr;

constexpr D3DMATRIX kIdentity{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};

/// Direct3D 7 default render states (DirectX 7 SDK); ZENABLE is set from the depth buffer at creation.
constexpr std::pair<DWORD, DWORD> kDefaultRenderStates[] = {
    {D3DRENDERSTATE_TEXTUREPERSPECTIVE, TRUE},
    {D3DRENDERSTATE_FILLMODE, D3DFILL_SOLID},
    {D3DRENDERSTATE_SHADEMODE, D3DSHADE_GOURAUD},
    {D3DRENDERSTATE_ZWRITEENABLE, TRUE},
    {D3DRENDERSTATE_LASTPIXEL, TRUE},
    {D3DRENDERSTATE_SRCBLEND, D3DBLEND_ONE},
    {D3DRENDERSTATE_DESTBLEND, D3DBLEND_ZERO},
    {D3DRENDERSTATE_CULLMODE, D3DCULL_CCW},
    {D3DRENDERSTATE_ZFUNC, D3DCMP_LESSEQUAL},
    {D3DRENDERSTATE_ALPHAFUNC, D3DCMP_ALWAYS},
    {D3DRENDERSTATE_FOGEND, std::bit_cast<DWORD>(1.0f)},
    {D3DRENDERSTATE_FOGDENSITY, std::bit_cast<DWORD>(1.0f)},
    {D3DRENDERSTATE_STENCILFAIL, D3DSTENCILOP_KEEP},
    {D3DRENDERSTATE_STENCILZFAIL, D3DSTENCILOP_KEEP},
    {D3DRENDERSTATE_STENCILPASS, D3DSTENCILOP_KEEP},
    {D3DRENDERSTATE_STENCILFUNC, D3DCMP_ALWAYS},
    {D3DRENDERSTATE_STENCILMASK, 0xffffffff},
    {D3DRENDERSTATE_STENCILWRITEMASK, 0xffffffff},
    {D3DRENDERSTATE_TEXTUREFACTOR, 0xffffffff},
    {D3DRENDERSTATE_CLIPPING, TRUE},
    {D3DRENDERSTATE_LIGHTING, TRUE},
    {D3DRENDERSTATE_COLORVERTEX, TRUE},
    {D3DRENDERSTATE_LOCALVIEWER, TRUE},
    {D3DRENDERSTATE_DIFFUSEMATERIALSOURCE, D3DMCS_COLOR1},
    {D3DRENDERSTATE_SPECULARMATERIALSOURCE, D3DMCS_COLOR2},
    {D3DRENDERSTATE_AMBIENTMATERIALSOURCE, D3DMCS_MATERIAL},
    {D3DRENDERSTATE_EMISSIVEMATERIALSOURCE, D3DMCS_MATERIAL},
};

/// Texture formats offered to the game (Direct3D 9 supports all of them on current hardware).
constexpr Format kTextureFormats[] = {
    Format::X1R5G5B5, Format::A1R5G5B5, Format::A4R4G4B4, Format::R5G6B5, Format::X8R8G8B8, Format::A8R8G8B8,
    Format::L8,       Format::A8,       Format::A8L8,     Format::V8U8,   Format::X8L8V8U8, Format::Dxt1,
    Format::Dxt2,     Format::Dxt3,     Format::Dxt4,     Format::Dxt5,
};

D3DMATRIX Multiply(const D3DMATRIX& a, const D3DMATRIX& b) noexcept
{
    // D3DMATRIX has no m[4][4] without D3D_OVERLOADS; its 16 floats are contiguous, row-major.
    const float* left = &a._11;
    const float* right = &b._11;
    D3DMATRIX result{};
    float* out = &result._11;
    for (int row = 0; row < 4; ++row) {
        for (int column = 0; column < 4; ++column) {
            float sum = 0;
            for (int k = 0; k < 4; ++k) {
                sum += left[row * 4 + k] * right[k * 4 + column];
            }
            out[row * 4 + column] = sum;
        }
    }
    return result;
}

/// Debugging aid (BW_DUMP_TEXTURES with [Log] Debug=1): saves the first textures bound as PNG files.
void DumpTexture(Surface& surface) noexcept
{
    static std::set<const Surface*> dumped;
    if (dumped.size() >= 60 || !dumped.insert(&surface).second || IsBlockCompressed(surface.PixelFormat())) {
        return;
    }
    DDSURFACEDESC2 desc{};
    desc.dwSize = sizeof desc;
    if (const HRESULT result = surface.Lock(nullptr, &desc, DDLOCK_READONLY, nullptr); FAILED(result)) {
        log::Debug("Renderer: cannot dump a texture (0x{:08x})", static_cast<std::uint32_t>(result));
        return;
    }
    const std::uint32_t width = surface.Width();
    const std::uint32_t height = surface.Height();
    std::vector<std::uint8_t> rgb(std::size_t{width} * height * 3);
    const std::uint32_t step = BytesPerPixel(surface.PixelFormat());
    for (std::uint32_t y = 0; y < height; ++y) {
        for (std::uint32_t x = 0; x < width; ++x) {
            const std::uint32_t argb = ReadArgb(
                surface.PixelFormat(), static_cast<const std::uint8_t*>(desc.lpSurface) + std::size_t{y} * desc.lPitch + x * step);
            std::uint8_t* out = &rgb[(std::size_t{y} * width + x) * 3];
            out[0] = static_cast<std::uint8_t>(argb >> 16);
            out[1] = static_cast<std::uint8_t>(argb >> 8);
            out[2] = static_cast<std::uint8_t>(argb);
        }
    }
    surface.Unlock(nullptr);
    const std::vector<std::uint8_t> png = diagnostics::EncodePng(width, height, rgb);
    wchar_t exe[MAX_PATH]{};
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    const std::filesystem::path folder = std::filesystem::path(exe).parent_path() / L"Screenshots";
    std::error_code error;
    std::filesystem::create_directories(folder, error);
    const std::filesystem::path name =
        folder / std::format("texture_{:03}_{}_{:08x}.png", dumped.size(), Name(surface.PixelFormat()),
                             surface.Description().ddsCaps.dwCaps);
    std::ofstream(name, std::ios::binary).write(reinterpret_cast<const char*>(png.data()), static_cast<std::streamsize>(png.size()));
    log::Debug("Renderer: dumped {}", name.string());
}

}  // namespace

Device* CurrentDevice() noexcept
{
    return g_current;
}

// --------------------------------------------------------------------------------------------------- State blocks

struct Device::StateBlock {
    struct RenderState {
        DWORD state;
        DWORD value;
    };
    struct StageState {
        DWORD stage;
        DWORD type;
        DWORD value;
    };
    struct Texture {
        DWORD stage;
        Surface* texture;  // not referenced: a block does not keep textures alive (Unbind clears it)
    };
    struct Transform {
        DWORD type;
        D3DMATRIX matrix;
    };
    struct Viewport {
        D3DVIEWPORT7 viewport;
    };
    struct Material {
        D3DMATERIAL7 material;
    };
    struct Light {
        DWORD index;
        D3DLIGHT7 light;
    };
    struct LightEnable {
        DWORD index;
        BOOL enable;
    };
    using Entry = std::variant<RenderState, StageState, Texture, Transform, Viewport, Material, Light, LightEnable>;
    std::vector<Entry> entries;
};

// -------------------------------------------------------------------------------------------------- Construction

HRESULT Device::Create(DirectDraw& owner, Surface& target, Device** out) noexcept
{
    auto* device = new (std::nothrow) Device(owner, target);
    if (device == nullptr) {
        return DDERR_OUTOFMEMORY;
    }
    *out = device;
    return D3D_OK;
}

Device::Device(DirectDraw& owner, Surface& target) noexcept : m_owner(owner)
{
    static_cast<IDirectDraw7&>(m_owner).AddRef();
    g_current = this;

    for (const auto& [state, value] : kDefaultRenderStates) {
        m_renderStates[state] = value;
    }
    m_renderStates[D3DRENDERSTATE_ZENABLE] = target.AttachedDepth() != nullptr ? D3DZB_TRUE : D3DZB_FALSE;
    for (DWORD stage = 0; stage < kStages; ++stage) {
        auto& states = m_stageStates[stage];
        states[D3DTSS_COLOROP] = stage == 0 ? D3DTOP_MODULATE : D3DTOP_DISABLE;
        states[D3DTSS_COLORARG1] = D3DTA_TEXTURE;
        states[D3DTSS_COLORARG2] = D3DTA_CURRENT;
        states[D3DTSS_ALPHAOP] = stage == 0 ? D3DTOP_SELECTARG1 : D3DTOP_DISABLE;
        states[D3DTSS_ALPHAARG1] = D3DTA_TEXTURE;
        states[D3DTSS_ALPHAARG2] = D3DTA_CURRENT;
        states[D3DTSS_TEXCOORDINDEX] = stage;
        states[D3DTSS_ADDRESS] = D3DTADDRESS_WRAP;
        states[D3DTSS_ADDRESSU] = D3DTADDRESS_WRAP;
        states[D3DTSS_ADDRESSV] = D3DTADDRESS_WRAP;
        states[D3DTSS_MAGFILTER] = D3DTFG_POINT;
        states[D3DTSS_MINFILTER] = D3DTFN_POINT;
        states[D3DTSS_MIPFILTER] = D3DTFP_NONE;
        states[D3DTSS_MAXANISOTROPY] = 1;
    }
    m_material.diffuse = {1, 1, 1, 1};
    m_material.ambient = {1, 1, 1, 1};

    SetTarget(target);
    gpu::SetRenderState(d3d9::kRsZEnable, m_renderStates[D3DRENDERSTATE_ZENABLE]);
    log::Info("Renderer: Direct3D 7 device on Direct3D 9 for a {}x{} target", target.Width(), target.Height());
}

Device::~Device()
{
    for (Surface*& texture : m_textures) {
        if (texture != nullptr) {
            texture->Release();
            texture = nullptr;
        }
    }
    if (m_target != nullptr) {
        m_target->Release();
    }
    if (g_current == this) {
        g_current = nullptr;
    }
    static_cast<IDirectDraw7&>(m_owner).Release();
}

void Device::SetTarget(Surface& target) noexcept
{
    target.AddRef();
    if (m_target != nullptr) {
        m_target->Release();
    }
    m_target = &target;
    Surface* depth = target.AttachedDepth();
    gpu::SetRenderTarget(target.GpuTarget(), depth != nullptr ? depth->GpuDepth() : nullptr);
    m_viewport = D3DVIEWPORT7{0, 0, target.Width(), target.Height(), 0.0f, 1.0f};
    gpu::SetViewport(&m_viewport);
}

void Device::Unbind(const Surface& surface) noexcept
{
    for (DWORD stage = 0; stage < kStages; ++stage) {
        if (m_textures[stage] == &surface) {
            m_textures[stage] = nullptr;
            gpu::SetTexture(stage, nullptr);
        }
    }
    for (auto& block : m_stateBlocks) {
        if (block) {
            for (auto& entry : block->entries) {
                if (auto* texture = std::get_if<StateBlock::Texture>(&entry); texture && texture->texture == &surface) {
                    texture->texture = nullptr;
                }
            }
        }
    }
    if (m_target == &surface) {
        m_target = nullptr;
    }
}

// ---------------------------------------------------------------------------------------------------- IUnknown

HRESULT Device::QueryInterface(REFIID iid, LPVOID* out)
{
    if (out == nullptr) {
        return E_POINTER;
    }
    if (iid == IID_IUnknown || iid == IID_IDirect3DDevice7) {
        *out = static_cast<IDirect3DDevice7*>(this);
        AddRef();
        return S_OK;
    }
    *out = nullptr;
    return E_NOINTERFACE;
}

ULONG Device::AddRef()
{
    return m_references.AddReference();
}

ULONG Device::Release()
{
    const ULONG count = m_references.ReleaseReference();
    if (count == 0) {
        delete this;
    }
    return count;
}

// ------------------------------------------------------------------------------------------------------ Queries

HRESULT Device::GetCaps(LPD3DDEVICEDESC7 desc)
{
    if (desc == nullptr) {
        return DDERR_INVALIDPARAMS;
    }
    *desc = m_owner.DeviceDescription();
    return D3D_OK;
}

HRESULT Device::EnumTextureFormats(LPD3DENUMPIXELFORMATSCALLBACK callback, LPVOID context)
{
    if (callback == nullptr) {
        return DDERR_INVALIDPARAMS;
    }
    for (const Format format : kTextureFormats) {
        DDPIXELFORMAT pixelFormat = ToPixelFormat(format);
        if (callback(&pixelFormat, context) == D3DENUMRET_CANCEL) {
            break;
        }
    }
    return D3D_OK;
}

HRESULT Device::GetDirect3D(LPDIRECT3D7* out)
{
    if (out == nullptr) {
        return DDERR_INVALIDPARAMS;
    }
    *out = static_cast<IDirect3D7*>(&m_owner);
    (*out)->AddRef();
    return D3D_OK;
}

HRESULT Device::GetInfo(DWORD, LPVOID, DWORD)
{
    return S_FALSE;  // no statistics available, as on retail runtimes
}

HRESULT Device::ValidateDevice(LPDWORD passes)
{
    // The game validates its texture-blend set-ups at start-up ("check mode N") with no texture bound, which
    // Direct3D 9 drivers reject although every DirectX 7 blend runs on current hardware. A rejection made the game
    // fall back to its multipass blending, which draws objects black. Report one pass, as DirectX 7 drivers did.
    if (passes != nullptr) {
        *passes = 1;
    }
    if (log::DebugEnabled() && gpu::ValidateDevice() == 0) {
        log::Debug("Renderer: Direct3D 9 ValidateDevice would reject the current states");
    }
    return D3D_OK;
}

// ----------------------------------------------------------------------------------------------- Scene, targets

HRESULT Device::BeginScene()
{
    gpu::BeginScene();
    return D3D_OK;
}

HRESULT Device::EndScene()
{
    gpu::EndScene();
    return D3D_OK;
}

HRESULT Device::SetRenderTarget(LPDIRECTDRAWSURFACE7 target, DWORD /*flags*/)
{
    Surface* surface = Surface::From(target);
    if (surface == nullptr || surface->Role() != SurfaceRole::RenderTarget) {
        static bool logged = false;
        if (!logged) {
            logged = true;
            log::Warning("Renderer: SetRenderTarget with a surface that is not a render target");
        }
        return DDERR_INVALIDPARAMS;
    }
    SetTarget(*surface);
    return D3D_OK;
}

HRESULT Device::GetRenderTarget(LPDIRECTDRAWSURFACE7* out)
{
    if (out == nullptr) {
        return DDERR_INVALIDPARAMS;
    }
    *out = m_target;
    if (m_target != nullptr) {
        m_target->AddRef();
    }
    return D3D_OK;
}

HRESULT Device::Clear(DWORD count, LPD3DRECT rects, DWORD flags, D3DCOLOR color, D3DVALUE z, DWORD stencil)
{
    gpu::Clear(count, rects, flags, color, z, stencil);
    return D3D_OK;
}

// -------------------------------------------------------------------------------------------------------- States

void Device::ApplyRenderState(DWORD state, DWORD value) noexcept
{
    m_renderStates[state] = value;
    const StateWrites writes = TranslateRenderState(state, value);
    if (writes[0].kind == StateWrite::Kind::None && !IsRetiredRenderState(state)) {
        static std::array<bool, kRenderStates> logged{};
        if (!logged[state]) {
            logged[state] = true;
            log::Info("Renderer: render state {} has no Direct3D 9 equivalent", state);
        }
    }
    for (const StateWrite& write : writes) {
        if (write.kind == StateWrite::Kind::RenderState) {
            gpu::SetRenderState(write.state, write.value);
        }
    }
}

HRESULT Device::SetRenderState(D3DRENDERSTATETYPE state, DWORD value)
{
    if (static_cast<std::size_t>(state) >= kRenderStates) {
        return DDERR_INVALIDPARAMS;
    }
    if (m_recording != nullptr) {
        m_recording->entries.emplace_back(StateBlock::RenderState{static_cast<DWORD>(state), value});
        return D3D_OK;
    }
    ApplyRenderState(state, value);
    return D3D_OK;
}

HRESULT Device::GetRenderState(D3DRENDERSTATETYPE state, LPDWORD value)
{
    if (static_cast<std::size_t>(state) >= kRenderStates || value == nullptr) {
        return DDERR_INVALIDPARAMS;
    }
    *value = m_renderStates[state];
    return D3D_OK;
}

void Device::ApplyStageState(DWORD stage, DWORD type, DWORD value) noexcept
{
    m_stageStates[stage][type] = value;
    for (const StateWrite& write : TranslateTextureStageState(type, value)) {
        if (write.kind == StateWrite::Kind::SamplerState) {
            gpu::SetSamplerState(stage, write.state, write.value);
        }
        else if (write.kind == StateWrite::Kind::TextureStageState) {
            gpu::SetTextureStageState(stage, write.state, write.value);
        }
    }
}

HRESULT Device::SetTextureStageState(DWORD stage, D3DTEXTURESTAGESTATETYPE type, DWORD value)
{
    if (stage >= kStages || static_cast<std::size_t>(type) >= kStageStates) {
        return DDERR_INVALIDPARAMS;
    }
    if (m_recording != nullptr) {
        m_recording->entries.emplace_back(StateBlock::StageState{stage, static_cast<DWORD>(type), value});
        return D3D_OK;
    }
    ApplyStageState(stage, type, value);
    return D3D_OK;
}

HRESULT Device::GetTextureStageState(DWORD stage, D3DTEXTURESTAGESTATETYPE type, LPDWORD value)
{
    if (stage >= kStages || static_cast<std::size_t>(type) >= kStageStates || value == nullptr) {
        return DDERR_INVALIDPARAMS;
    }
    *value = m_stageStates[stage][type];
    return D3D_OK;
}

HRESULT Device::SetTexture(DWORD stage, LPDIRECTDRAWSURFACE7 texture)
{
    if (stage >= kStages) {
        return DDERR_INVALIDPARAMS;
    }
    Surface* surface = Surface::From(texture);
    if (m_recording != nullptr) {
        m_recording->entries.emplace_back(StateBlock::Texture{stage, surface});
        return D3D_OK;
    }
    if (surface != nullptr && surface->Role() != SurfaceRole::Texture) {
        static bool logged = false;
        if (!logged) {
            logged = true;
            log::Warning("Renderer: SetTexture with a surface that is not a texture (render-to-texture)");
        }
        surface = nullptr;
    }
    if (surface == m_textures[stage]) {
        return D3D_OK;
    }
    if (surface != nullptr && log::DebugEnabled() && GetEnvironmentVariableW(L"BW_DUMP_TEXTURES", nullptr, 0) != 0) {
        DumpTexture(*surface);
    }
    if (surface != nullptr && log::DebugEnabled()) {
        static std::set<std::pair<DWORD, int>> seen;
        if (seen.insert({surface->Description().ddsCaps.dwCaps, static_cast<int>(surface->PixelFormat())}).second) {
            log::Debug("Renderer: texture bound: {} caps 0x{:08x}", Name(surface->PixelFormat()),
                       surface->Description().ddsCaps.dwCaps);
        }
    }
    if (surface != nullptr) {
        surface->AddRef();
    }
    if (m_textures[stage] != nullptr) {
        m_textures[stage]->Release();
    }
    m_textures[stage] = surface;
    gpu::SetTexture(stage, surface != nullptr ? surface->GpuTexture() : nullptr);
    return D3D_OK;
}

HRESULT Device::GetTexture(DWORD stage, LPDIRECTDRAWSURFACE7* out)
{
    if (stage >= kStages || out == nullptr) {
        return DDERR_INVALIDPARAMS;
    }
    *out = m_textures[stage];
    if (*out != nullptr) {
        (*out)->AddRef();
    }
    return D3D_OK;
}

HRESULT Device::SetTransform(D3DTRANSFORMSTATETYPE type, LPD3DMATRIX matrix)
{
    if (matrix == nullptr) {
        return DDERR_INVALIDPARAMS;
    }
    if (m_recording != nullptr) {
        m_recording->entries.emplace_back(StateBlock::Transform{static_cast<DWORD>(type), *matrix});
        return D3D_OK;
    }
    m_transforms[type] = *matrix;
    if (const std::uint32_t native = TranslateTransform(type); native != 0) {
        gpu::SetTransform(native, &matrix->_11);
    }
    return D3D_OK;
}

HRESULT Device::GetTransform(D3DTRANSFORMSTATETYPE type, LPD3DMATRIX matrix)
{
    if (matrix == nullptr) {
        return DDERR_INVALIDPARAMS;
    }
    const auto found = m_transforms.find(type);
    *matrix = found != m_transforms.end() ? found->second : kIdentity;
    return D3D_OK;
}

HRESULT Device::MultiplyTransform(D3DTRANSFORMSTATETYPE type, LPD3DMATRIX matrix)
{
    if (matrix == nullptr) {
        return DDERR_INVALIDPARAMS;
    }
    D3DMATRIX current{};
    GetTransform(type, &current);
    D3DMATRIX product = Multiply(*matrix, current);
    return SetTransform(type, &product);
}

HRESULT Device::SetViewport(LPD3DVIEWPORT7 viewport)
{
    if (viewport == nullptr) {
        return DDERR_INVALIDPARAMS;
    }
    if (m_recording != nullptr) {
        m_recording->entries.emplace_back(StateBlock::Viewport{*viewport});
        return D3D_OK;
    }
    m_viewport = *viewport;
    gpu::SetViewport(viewport);
    return D3D_OK;
}

HRESULT Device::GetViewport(LPD3DVIEWPORT7 viewport)
{
    if (viewport == nullptr) {
        return DDERR_INVALIDPARAMS;
    }
    *viewport = m_viewport;
    return D3D_OK;
}

HRESULT Device::SetMaterial(LPD3DMATERIAL7 material)
{
    if (material == nullptr) {
        return DDERR_INVALIDPARAMS;
    }
    if (m_recording != nullptr) {
        m_recording->entries.emplace_back(StateBlock::Material{*material});
        return D3D_OK;
    }
    m_material = *material;
    gpu::SetMaterial(material);
    return D3D_OK;
}

HRESULT Device::GetMaterial(LPD3DMATERIAL7 material)
{
    if (material == nullptr) {
        return DDERR_INVALIDPARAMS;
    }
    *material = m_material;
    return D3D_OK;
}

HRESULT Device::SetLight(DWORD index, LPD3DLIGHT7 light)
{
    if (light == nullptr) {
        return DDERR_INVALIDPARAMS;
    }
    if (m_recording != nullptr) {
        m_recording->entries.emplace_back(StateBlock::Light{index, *light});
        return D3D_OK;
    }
    m_lights[index] = *light;
    if (!gpu::SetLight(index, light)) {
        static bool logged = false;
        if (!logged) {
            logged = true;
            log::Info("Renderer: Direct3D 9 rejected light {} (type {}, range {})", index,
                      static_cast<int>(light->dltType), light->dvRange);
        }
    }
    return D3D_OK;
}

HRESULT Device::GetLight(DWORD index, LPD3DLIGHT7 light)
{
    const auto found = m_lights.find(index);
    if (light == nullptr || found == m_lights.end()) {
        return DDERR_INVALIDPARAMS;
    }
    *light = found->second;
    return D3D_OK;
}

HRESULT Device::LightEnable(DWORD index, BOOL enable)
{
    if (m_recording != nullptr) {
        m_recording->entries.emplace_back(StateBlock::LightEnable{index, enable});
        return D3D_OK;
    }
    m_lightEnabled[index] = enable;
    gpu::LightEnable(index, enable != FALSE);
    return D3D_OK;
}

HRESULT Device::GetLightEnable(DWORD index, BOOL* enable)
{
    if (enable == nullptr) {
        return DDERR_INVALIDPARAMS;
    }
    const auto found = m_lightEnabled.find(index);
    *enable = found != m_lightEnabled.end() ? found->second : FALSE;
    return D3D_OK;
}

HRESULT Device::SetClipStatus(LPD3DCLIPSTATUS status)
{
    if (status != nullptr) {
        m_clipStatus = *status;
    }
    return D3D_OK;
}

HRESULT Device::GetClipStatus(LPD3DCLIPSTATUS status)
{
    if (status == nullptr) {
        return DDERR_INVALIDPARAMS;
    }
    *status = m_clipStatus;
    return D3D_OK;
}

HRESULT Device::PreLoad(LPDIRECTDRAWSURFACE7)
{
    return D3D_OK;
}

// --------------------------------------------------------------------------------------------------------- Draws

HRESULT Device::DrawPrimitive(D3DPRIMITIVETYPE type, DWORD fvf, LPVOID vertices, DWORD count, DWORD /*flags*/)
{
    if (vertices == nullptr) {
        return DDERR_INVALIDPARAMS;
    }
    gpu::Draw(type, fvf, vertices, count);
    return D3D_OK;
}

HRESULT Device::DrawIndexedPrimitive(D3DPRIMITIVETYPE type, DWORD fvf, LPVOID vertices, DWORD vertexCount,
                                     LPWORD indices, DWORD indexCount, DWORD /*flags*/)
{
    if (vertices == nullptr || indices == nullptr) {
        return DDERR_INVALIDPARAMS;
    }
    gpu::DrawIndexed(type, fvf, vertices, vertexCount, indices, indexCount);
    return D3D_OK;
}

// -------------------------------------------------------------------------------------------------- State blocks

HRESULT Device::BeginStateBlock()
{
    if (m_recording != nullptr) {
        return D3DERR_INBEGINSTATEBLOCK;
    }
    m_stateBlocks.push_back(std::make_unique<StateBlock>());
    m_recording = m_stateBlocks.back().get();
    return D3D_OK;
}

HRESULT Device::EndStateBlock(LPDWORD handle)
{
    if (m_recording == nullptr || handle == nullptr) {
        return D3DERR_NOTINBEGINSTATEBLOCK;
    }
    m_recording = nullptr;
    *handle = static_cast<DWORD>(m_stateBlocks.size());  // the block pushed by BeginStateBlock
    return D3D_OK;
}

HRESULT Device::ApplyStateBlock(DWORD handle)
{
    if (handle == 0 || handle > m_stateBlocks.size() || !m_stateBlocks[handle - 1] || m_recording != nullptr) {
        return D3DERR_INVALIDSTATEBLOCK;
    }
    for (StateBlock::Entry& entry : m_stateBlocks[handle - 1]->entries) {
        std::visit(
            [this](auto& value) {
                using T = std::decay_t<decltype(value)>;
                if constexpr (std::is_same_v<T, StateBlock::RenderState>) {
                    ApplyRenderState(value.state, value.value);
                }
                else if constexpr (std::is_same_v<T, StateBlock::StageState>) {
                    ApplyStageState(value.stage, value.type, value.value);
                }
                else if constexpr (std::is_same_v<T, StateBlock::Texture>) {
                    SetTexture(value.stage, value.texture);
                }
                else if constexpr (std::is_same_v<T, StateBlock::Transform>) {
                    SetTransform(static_cast<D3DTRANSFORMSTATETYPE>(value.type), &value.matrix);
                }
                else if constexpr (std::is_same_v<T, StateBlock::Viewport>) {
                    SetViewport(&value.viewport);
                }
                else if constexpr (std::is_same_v<T, StateBlock::Material>) {
                    SetMaterial(&value.material);
                }
                else if constexpr (std::is_same_v<T, StateBlock::Light>) {
                    SetLight(value.index, &value.light);
                }
                else {
                    LightEnable(value.index, value.enable);
                }
            },
            entry);
    }
    return D3D_OK;
}

HRESULT Device::CaptureStateBlock(DWORD handle)
{
    if (handle == 0 || handle > m_stateBlocks.size() || !m_stateBlocks[handle - 1]) {
        return D3DERR_INVALIDSTATEBLOCK;
    }
    for (StateBlock::Entry& entry : m_stateBlocks[handle - 1]->entries) {
        std::visit(
            [this](auto& value) {
                using T = std::decay_t<decltype(value)>;
                if constexpr (std::is_same_v<T, StateBlock::RenderState>) {
                    value.value = m_renderStates[value.state];
                }
                else if constexpr (std::is_same_v<T, StateBlock::StageState>) {
                    value.value = m_stageStates[value.stage][value.type];
                }
                else if constexpr (std::is_same_v<T, StateBlock::Texture>) {
                    value.texture = m_textures[value.stage];
                }
                else if constexpr (std::is_same_v<T, StateBlock::Transform>) {
                    GetTransform(static_cast<D3DTRANSFORMSTATETYPE>(value.type), &value.matrix);
                }
                else if constexpr (std::is_same_v<T, StateBlock::Viewport>) {
                    value.viewport = m_viewport;
                }
                else if constexpr (std::is_same_v<T, StateBlock::Material>) {
                    value.material = m_material;
                }
                else if constexpr (std::is_same_v<T, StateBlock::Light>) {
                    GetLight(value.index, &value.light);
                }
                else {
                    GetLightEnable(value.index, &value.enable);
                }
            },
            entry);
    }
    return D3D_OK;
}

HRESULT Device::DeleteStateBlock(DWORD handle)
{
    if (handle == 0 || handle > m_stateBlocks.size() || !m_stateBlocks[handle - 1]) {
        return D3DERR_INVALIDSTATEBLOCK;
    }
    if (m_recording == m_stateBlocks[handle - 1].get()) {
        m_recording = nullptr;
    }
    m_stateBlocks[handle - 1].reset();
    return D3D_OK;
}

HRESULT Device::CreateStateBlock(D3DSTATEBLOCKTYPE type, LPDWORD handle)
{
    if (handle == nullptr) {
        return DDERR_INVALIDPARAMS;
    }
    if (type != D3DSBT_ALL) {
        static bool logged = false;
        if (!logged) {
            logged = true;
            log::Info("Renderer: pixel/vertex state blocks capture all states");
        }
    }
    auto block = std::make_unique<StateBlock>();
    for (DWORD state = 1; state < kRenderStates; ++state) {
        if (!IsRetiredRenderState(state)) {
            block->entries.emplace_back(StateBlock::RenderState{state, m_renderStates[state]});
        }
    }
    for (DWORD stage = 0; stage < kStages; ++stage) {
        for (DWORD stageType = D3DTSS_COLOROP; stageType <= D3DTSS_TEXTURETRANSFORMFLAGS; ++stageType) {
            block->entries.emplace_back(StateBlock::StageState{stage, stageType, m_stageStates[stage][stageType]});
        }
        block->entries.emplace_back(StateBlock::Texture{stage, m_textures[stage]});
    }
    for (const auto& [transform, matrix] : m_transforms) {
        block->entries.emplace_back(StateBlock::Transform{transform, matrix});
    }
    block->entries.emplace_back(StateBlock::Viewport{m_viewport});
    block->entries.emplace_back(StateBlock::Material{m_material});
    for (const auto& [index, light] : m_lights) {
        block->entries.emplace_back(StateBlock::Light{index, light});
    }
    for (const auto& [index, enable] : m_lightEnabled) {
        block->entries.emplace_back(StateBlock::LightEnable{index, enable});
    }
    m_stateBlocks.push_back(std::move(block));
    *handle = static_cast<DWORD>(m_stateBlocks.size());
    return D3D_OK;
}

}  // namespace bw::render
