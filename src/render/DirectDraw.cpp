#include "render/DirectDraw.h"

#include "core/Log.h"
#include "render/Device.h"
#include "render/Renderer.h"
#include "render/Surface.h"

#include <cstring>
#include <new>
#include <type_traits>

namespace bw::render {

namespace {

template <typename Interface>
Interface* SurfaceViewOf(Surface& surface) noexcept
{
    if constexpr (std::is_same_v<Interface, IDirectDrawSurface>) {
        return &surface.V1();
    }
    else {
        return &surface.V4();
    }
}

HRESULT CALLBACK FindHalDevice(LPSTR /*description*/, LPSTR /*name*/, LPD3DDEVICEDESC7 desc, LPVOID context)
{
    if (desc != nullptr && desc->deviceGUID == IID_IDirect3DHALDevice) {
        *static_cast<D3DDEVICEDESC7*>(context) = *desc;
        return D3DENUMRET_CANCEL;
    }
    return D3DENUMRET_OK;
}

}  // namespace

// ---------------------------------------------------------------------------------------------------- DirectDraw

HRESULT DirectDraw::Create(GUID* guid, REFIID iid, void** out) noexcept
{
    if (out == nullptr) {
        return DDERR_INVALIDPARAMS;
    }
    *out = nullptr;
    if (iid != IID_IDirectDraw7) {
        return DDERR_INVALIDPARAMS;
    }
    auto* directDraw = new (std::nothrow) DirectDraw();
    if (directDraw == nullptr) {
        return DDERR_OUTOFMEMORY;
    }
    if (const auto real = RealDirectDrawCreateEx()) {
        if (FAILED(real(guid, reinterpret_cast<void**>(&directDraw->m_real), IID_IDirectDraw7, nullptr))) {
            directDraw->m_real = nullptr;
            log::Warning("Renderer: no DirectDraw for hardware queries");
        }
    }
    if (directDraw->m_real != nullptr) {
        IDirect3D7* direct3D = nullptr;
        if (SUCCEEDED(directDraw->m_real->QueryInterface(IID_IDirect3D7, reinterpret_cast<void**>(&direct3D)))) {
            direct3D->EnumDevices(&FindHalDevice, &directDraw->m_deviceDescription);
            direct3D->Release();
        }
    }
    directDraw->m_deviceDescription.deviceGUID = IID_IDirect3DHALDevice;
    *out = static_cast<IDirectDraw7*>(directDraw);
    return DD_OK;
}

DirectDraw::DirectDraw() noexcept = default;

DirectDraw::~DirectDraw()
{
    if (m_real != nullptr) {
        m_real->Release();
    }
}

bool DirectDraw::EnsureGpu() noexcept
{
    if (gpu::Ready()) {
        return true;
    }
    if (m_window == nullptr) {
        log::Error("Renderer: a 3D surface was requested before SetCooperativeLevel gave a window");
        return false;
    }
    return gpu::Initialise(m_window, Options());
}

HRESULT DirectDraw::QueryInterface(REFIID iid, LPVOID* out)
{
    if (out == nullptr) {
        return E_POINTER;
    }
    if (iid == IID_IUnknown || iid == IID_IDirectDraw7) {
        *out = static_cast<IDirectDraw7*>(this);
    }
    else if (iid == IID_IDirect3D7) {
        *out = static_cast<IDirect3D7*>(this);
    }
    else if (iid == IID_IDirectDraw) {
        *out = static_cast<IDirectDraw*>(&m_v1);
    }
    else if (iid == IID_IDirectDraw2) {
        *out = static_cast<IDirectDraw2*>(&m_v2);
    }
    else if (iid == IID_IDirectDraw4) {
        *out = static_cast<IDirectDraw4*>(&m_v4);
    }
    else {
        *out = nullptr;
        static bool logged = false;
        if (!logged) {
            logged = true;
            log::Info("Renderer: DirectDraw asked for an interface it does not provide ({:08x}-...)", iid.Data1);
        }
        return E_NOINTERFACE;
    }
    AddRef();
    return S_OK;
}

ULONG DirectDraw::AddRef()
{
    return m_references.AddReference();
}

ULONG DirectDraw::Release()
{
    const ULONG count = m_references.ReleaseReference();
    if (count == 0) {
        delete this;
    }
    return count;
}

HRESULT DirectDraw::Compact()
{
    return DD_OK;
}

HRESULT DirectDraw::CreateClipper(DWORD /*flags*/, LPDIRECTDRAWCLIPPER* out, IUnknown* /*outer*/)
{
    if (out == nullptr) {
        return DDERR_INVALIDPARAMS;
    }
    *out = Clipper::Create();
    return *out != nullptr ? DD_OK : DDERR_OUTOFMEMORY;
}

HRESULT DirectDraw::CreateSurface(LPDDSURFACEDESC2 desc, LPDIRECTDRAWSURFACE7* out, IUnknown* /*outer*/)
{
    if (desc == nullptr || out == nullptr) {
        return DDERR_INVALIDPARAMS;
    }
    Surface* surface = nullptr;
    const HRESULT result = Surface::Create(*this, *desc, &surface);
    *out = surface;
    return result;
}

HRESULT DirectDraw::EnumDisplayModes(DWORD flags, LPDDSURFACEDESC2 filter, LPVOID context,
                                     LPDDENUMMODESCALLBACK2 callback)
{
    return m_real != nullptr ? m_real->EnumDisplayModes(flags, filter, context, callback) : DDERR_UNSUPPORTED;
}

HRESULT DirectDraw::FlipToGDISurface()
{
    return DD_OK;
}

HRESULT DirectDraw::GetCaps(LPDDCAPS driver, LPDDCAPS emulation)
{
    return m_real != nullptr ? m_real->GetCaps(driver, emulation) : DDERR_UNSUPPORTED;
}

HRESULT DirectDraw::GetDisplayMode(LPDDSURFACEDESC2 desc)
{
    return m_real != nullptr ? m_real->GetDisplayMode(desc) : DDERR_UNSUPPORTED;
}

HRESULT DirectDraw::GetMonitorFrequency(LPDWORD frequency)
{
    return m_real != nullptr ? m_real->GetMonitorFrequency(frequency) : DDERR_UNSUPPORTED;
}

HRESULT DirectDraw::Initialize(GUID*)
{
    return DDERR_ALREADYINITIALIZED;
}

HRESULT DirectDraw::RestoreDisplayMode()
{
    return DD_OK;
}

HRESULT DirectDraw::SetCooperativeLevel(HWND window, DWORD flags)
{
    if (window != nullptr) {
        m_window = window;
    }
    m_cooperativeFlags = flags;
    return DD_OK;
}

HRESULT DirectDraw::SetDisplayMode(DWORD width, DWORD height, DWORD bitsPerPixel, DWORD /*refresh*/, DWORD /*flags*/)
{
    // The display mode is never changed: the game renders at this size and the frame is scaled to the window.
    m_displaySize = SIZE{static_cast<LONG>(width), static_cast<LONG>(height)};
    m_displayBits = bitsPerPixel;
    return DD_OK;
}

HRESULT DirectDraw::WaitForVerticalBlank(DWORD, HANDLE)
{
    return DD_OK;
}

HRESULT DirectDraw::GetAvailableVidMem(LPDDSCAPS2 caps, LPDWORD total, LPDWORD free)
{
    return m_real != nullptr ? m_real->GetAvailableVidMem(caps, total, free) : DDERR_UNSUPPORTED;
}

HRESULT DirectDraw::RestoreAllSurfaces()
{
    return DD_OK;
}

HRESULT DirectDraw::TestCooperativeLevel()
{
    return DD_OK;
}

HRESULT DirectDraw::GetDeviceIdentifier(LPDDDEVICEIDENTIFIER2 identifier, DWORD flags)
{
    return m_real != nullptr ? m_real->GetDeviceIdentifier(identifier, flags) : DDERR_UNSUPPORTED;
}

// ----------------------------------------------------------------------------------------------------- IDirect3D7

HRESULT DirectDraw::EnumDevices(LPD3DENUMDEVICESCALLBACK7 callback, LPVOID context)
{
    if (callback == nullptr) {
        return DDERR_INVALIDPARAMS;
    }
    char description[] = "Direct3D 9 (Black & White decompilation)";
    char name[] = "Direct3D HAL";
    D3DDEVICEDESC7 desc = m_deviceDescription;
    callback(description, name, &desc, context);
    return D3D_OK;
}

HRESULT DirectDraw::CreateDevice(REFCLSID /*type*/, LPDIRECTDRAWSURFACE7 target, LPDIRECT3DDEVICE7* out)
{
    if (out == nullptr) {
        return DDERR_INVALIDPARAMS;
    }
    *out = nullptr;
    Surface* surface = Surface::From(target);
    if (surface == nullptr || surface->Role() != SurfaceRole::RenderTarget) {
        log::Error("Renderer: CreateDevice needs a 3D render target");
        return DDERR_INVALIDPARAMS;
    }
    Device* device = nullptr;
    const HRESULT result = Device::Create(*this, *surface, &device);
    *out = device;
    return result;
}

HRESULT DirectDraw::EnumZBufferFormats(REFCLSID /*type*/, LPD3DENUMPIXELFORMATSCALLBACK callback, LPVOID context)
{
    if (callback == nullptr) {
        return DDERR_INVALIDPARAMS;
    }
    // Reported for the game's choice; the depth buffer is always 24-bit depth + 8-bit stencil (Gpu.h).
    for (const Format format : {Format::D16, Format::D24X8, Format::D24S8, Format::D32}) {
        DDPIXELFORMAT pixelFormat = ToPixelFormat(format);
        if (callback(&pixelFormat, context) == D3DENUMRET_CANCEL) {
            break;
        }
    }
    return D3D_OK;
}

HRESULT DirectDraw::EvictManagedTextures()
{
    return D3D_OK;
}

// ------------------------------------------------------------------------------------------ Older interfaces

#define BW_DIRECTDRAW_VIEW(Return, Name)                                                                           \
    template <typename Interface, typename Desc, typename SurfaceInterface, typename ModesCallback,                 \
              typename SurfacesCallback, typename Caps, typename Identifier>                                       \
    Return STDMETHODCALLTYPE                                                                                       \
        DirectDrawView<Interface, Desc, SurfaceInterface, ModesCallback, SurfacesCallback, Caps, Identifier>::Name

BW_DIRECTDRAW_VIEW(HRESULT, QueryInterface)(REFIID iid, LPVOID* out)
{
    return m_owner.QueryInterface(iid, out);
}

BW_DIRECTDRAW_VIEW(ULONG, AddRef)()
{
    return m_owner.AddRef();
}

BW_DIRECTDRAW_VIEW(ULONG, Release)()
{
    return m_owner.Release();
}

BW_DIRECTDRAW_VIEW(HRESULT, Compact)()
{
    return DD_OK;
}

BW_DIRECTDRAW_VIEW(HRESULT, CreateClipper)(DWORD flags, LPDIRECTDRAWCLIPPER* out, IUnknown* outer)
{
    return m_owner.CreateClipper(flags, out, outer);
}

BW_DIRECTDRAW_VIEW(HRESULT, CreateSurface)(Desc* desc, SurfaceInterface** out, IUnknown* outer)
{
    if (desc == nullptr || out == nullptr) {
        return DDERR_INVALIDPARAMS;
    }
    DDSURFACEDESC2 desc2;
    if constexpr (std::is_same_v<Desc, DDSURFACEDESC2>) {
        desc2 = *desc;
    }
    else {
        desc2 = FromSurfaceDesc(*desc);
    }
    LPDIRECTDRAWSURFACE7 surface = nullptr;
    const HRESULT result = m_owner.CreateSurface(&desc2, &surface, outer);
    *out = SUCCEEDED(result) ? SurfaceViewOf<SurfaceInterface>(*Surface::From(surface)) : nullptr;
    return result;
}

BW_DIRECTDRAW_VIEW(HRESULT, FlipToGDISurface)()
{
    return DD_OK;
}

BW_DIRECTDRAW_VIEW(HRESULT, GetCaps)(LPDDCAPS driver, LPDDCAPS emulation)
{
    return m_owner.GetCaps(driver, emulation);
}

BW_DIRECTDRAW_VIEW(HRESULT, GetDisplayMode)(Desc* desc)
{
    if (desc == nullptr) {
        return DDERR_INVALIDPARAMS;
    }
    DDSURFACEDESC2 desc2{};
    desc2.dwSize = sizeof desc2;
    const HRESULT result = m_owner.GetDisplayMode(&desc2);
    if (SUCCEEDED(result)) {
        if constexpr (std::is_same_v<Desc, DDSURFACEDESC2>) {
            *desc = desc2;
        }
        else {
            ToSurfaceDesc(desc2, *desc);
        }
    }
    return result;
}

BW_DIRECTDRAW_VIEW(HRESULT, GetMonitorFrequency)(LPDWORD frequency)
{
    return m_owner.GetMonitorFrequency(frequency);
}

BW_DIRECTDRAW_VIEW(HRESULT, Initialize)(GUID*)
{
    return DDERR_ALREADYINITIALIZED;
}

BW_DIRECTDRAW_VIEW(HRESULT, RestoreDisplayMode)()
{
    return m_owner.RestoreDisplayMode();
}

BW_DIRECTDRAW_VIEW(HRESULT, SetCooperativeLevel)(HWND window, DWORD flags)
{
    return m_owner.SetCooperativeLevel(window, flags);
}

BW_DIRECTDRAW_VIEW(HRESULT, SetDisplayMode)(DWORD width, DWORD height, DWORD bitsPerPixel)
{
    return m_owner.SetDisplayMode(width, height, bitsPerPixel, 0, 0);
}

BW_DIRECTDRAW_VIEW(HRESULT, SetDisplayMode)(DWORD width, DWORD height, DWORD bitsPerPixel, DWORD refresh, DWORD flags)
{
    return m_owner.SetDisplayMode(width, height, bitsPerPixel, refresh, flags);
}

BW_DIRECTDRAW_VIEW(HRESULT, WaitForVerticalBlank)(DWORD flags, HANDLE event)
{
    return m_owner.WaitForVerticalBlank(flags, event);
}

BW_DIRECTDRAW_VIEW(HRESULT, GetAvailableVidMem)(Caps* caps, LPDWORD total, LPDWORD free)
{
    DDSCAPS2 caps2{};
    if (caps != nullptr) {
        caps2.dwCaps = caps->dwCaps;
    }
    return m_owner.GetAvailableVidMem(&caps2, total, free);
}

BW_DIRECTDRAW_VIEW(HRESULT, RestoreAllSurfaces)()
{
    return DD_OK;
}

BW_DIRECTDRAW_VIEW(HRESULT, TestCooperativeLevel)()
{
    return DD_OK;
}

BW_DIRECTDRAW_VIEW(HRESULT, GetDeviceIdentifier)(Identifier* identifier, DWORD flags)
{
    if (identifier == nullptr) {
        return DDERR_INVALIDPARAMS;
    }
    DDDEVICEIDENTIFIER2 identifier2{};
    const HRESULT result = m_owner.GetDeviceIdentifier(&identifier2, flags);
    if (SUCCEEDED(result)) {
        std::memcpy(identifier->szDriver, identifier2.szDriver, sizeof identifier->szDriver);
        std::memcpy(identifier->szDescription, identifier2.szDescription, sizeof identifier->szDescription);
        identifier->liDriverVersion = identifier2.liDriverVersion;
        identifier->dwVendorId = identifier2.dwVendorId;
        identifier->dwDeviceId = identifier2.dwDeviceId;
        identifier->dwSubSysId = identifier2.dwSubSysId;
        identifier->dwRevision = identifier2.dwRevision;
        identifier->guidDeviceIdentifier = identifier2.guidDeviceIdentifier;
    }
    return result;
}

#undef BW_DIRECTDRAW_VIEW

template class DirectDrawView<IDirectDraw, DDSURFACEDESC, IDirectDrawSurface, LPDDENUMMODESCALLBACK,
                              LPDDENUMSURFACESCALLBACK, DDSCAPS, DDDEVICEIDENTIFIER>;
template class DirectDrawView<IDirectDraw2, DDSURFACEDESC, IDirectDrawSurface, LPDDENUMMODESCALLBACK,
                              LPDDENUMSURFACESCALLBACK, DDSCAPS, DDDEVICEIDENTIFIER>;
template class DirectDrawView<IDirectDraw4, DDSURFACEDESC2, IDirectDrawSurface4, LPDDENUMMODESCALLBACK2,
                              LPDDENUMSURFACESCALLBACK2, DDSCAPS2, DDDEVICEIDENTIFIER>;

// ------------------------------------------------------------------------------------------------------- Clipper

HRESULT Clipper::QueryInterface(REFIID iid, LPVOID* out)
{
    if (out == nullptr) {
        return E_POINTER;
    }
    if (iid == IID_IUnknown || iid == IID_IDirectDrawClipper) {
        *out = static_cast<IDirectDrawClipper*>(this);
        AddRef();
        return S_OK;
    }
    *out = nullptr;
    return E_NOINTERFACE;
}

ULONG Clipper::AddRef()
{
    return m_references.AddReference();
}

ULONG Clipper::Release()
{
    const ULONG count = m_references.ReleaseReference();
    if (count == 0) {
        delete this;
    }
    return count;
}

HRESULT Clipper::GetHWnd(HWND* window)
{
    if (window == nullptr) {
        return DDERR_INVALIDPARAMS;
    }
    *window = m_window;
    return DD_OK;
}

HRESULT Clipper::Initialize(LPDIRECTDRAW, DWORD)
{
    return DDERR_ALREADYINITIALIZED;
}

HRESULT Clipper::IsClipListChanged(BOOL* changed)
{
    if (changed != nullptr) {
        *changed = FALSE;
    }
    return DD_OK;
}

HRESULT Clipper::SetHWnd(DWORD, HWND window)
{
    m_window = window;
    return DD_OK;
}

}  // namespace bw::render
