#pragma once

#include "render/Com.h"

#include <wrl/client.h>

namespace bw::render {

class DirectDraw;
class Surface;

/// An older interface version of DirectDraw (IDirectDraw, IDirectDraw2, IDirectDraw4), forwarding to DirectDraw.
template <typename Interface, typename Desc, typename SurfaceInterface, typename ModesCallback,
          typename SurfacesCallback, typename Caps, typename Identifier>
class DirectDrawView final : public Interface {
public:
    explicit DirectDrawView(DirectDraw& owner) noexcept : m_owner(owner) {}

    STDMETHOD(QueryInterface)(REFIID iid, LPVOID* out);
    STDMETHOD_(ULONG, AddRef)();
    STDMETHOD_(ULONG, Release)();
    STDMETHOD(Compact)();
    STDMETHOD(CreateClipper)(DWORD flags, LPDIRECTDRAWCLIPPER* out, IUnknown* outer);
    STDMETHOD(CreatePalette)(DWORD, LPPALETTEENTRY, LPDIRECTDRAWPALETTE*, IUnknown*) BW_UNSUPPORTED
    STDMETHOD(CreateSurface)(Desc* desc, SurfaceInterface** out, IUnknown* outer);
    STDMETHOD(DuplicateSurface)(SurfaceInterface*, SurfaceInterface**) BW_UNSUPPORTED
    STDMETHOD(EnumDisplayModes)(DWORD, Desc*, LPVOID, ModesCallback) BW_UNSUPPORTED
    STDMETHOD(EnumSurfaces)(DWORD, Desc*, LPVOID, SurfacesCallback) BW_UNSUPPORTED
    STDMETHOD(FlipToGDISurface)();
    STDMETHOD(GetCaps)(LPDDCAPS driver, LPDDCAPS emulation);
    STDMETHOD(GetDisplayMode)(Desc* desc);
    STDMETHOD(GetFourCCCodes)(LPDWORD, LPDWORD) BW_UNSUPPORTED
    STDMETHOD(GetGDISurface)(SurfaceInterface**) BW_UNSUPPORTED
    STDMETHOD(GetMonitorFrequency)(LPDWORD frequency);
    STDMETHOD(GetScanLine)(LPDWORD) BW_UNSUPPORTED
    STDMETHOD(GetVerticalBlankStatus)(LPBOOL) BW_UNSUPPORTED
    STDMETHOD(Initialize)(GUID*);
    STDMETHOD(RestoreDisplayMode)();
    STDMETHOD(SetCooperativeLevel)(HWND window, DWORD flags);
    STDMETHOD(SetDisplayMode)(DWORD width, DWORD height, DWORD bitsPerPixel);
    STDMETHOD(SetDisplayMode)(DWORD width, DWORD height, DWORD bitsPerPixel, DWORD refresh, DWORD flags);
    STDMETHOD(WaitForVerticalBlank)(DWORD, HANDLE);
    // IDirectDraw2 and later
    STDMETHOD(GetAvailableVidMem)(Caps* caps, LPDWORD total, LPDWORD free);
    // IDirectDraw4
    STDMETHOD(GetSurfaceFromDC)(HDC, SurfaceInterface**) BW_UNSUPPORTED
    STDMETHOD(RestoreAllSurfaces)();
    STDMETHOD(TestCooperativeLevel)();
    STDMETHOD(GetDeviceIdentifier)(Identifier* identifier, DWORD flags);

private:
    DirectDraw& m_owner;
};

using DirectDrawV1 = DirectDrawView<IDirectDraw, DDSURFACEDESC, IDirectDrawSurface, LPDDENUMMODESCALLBACK,
                                    LPDDENUMSURFACESCALLBACK, DDSCAPS, DDDEVICEIDENTIFIER>;
using DirectDrawV2 = DirectDrawView<IDirectDraw2, DDSURFACEDESC, IDirectDrawSurface, LPDDENUMMODESCALLBACK,
                                    LPDDENUMSURFACESCALLBACK, DDSCAPS, DDDEVICEIDENTIFIER>;
using DirectDrawV4 = DirectDrawView<IDirectDraw4, DDSURFACEDESC2, IDirectDrawSurface4, LPDDENUMMODESCALLBACK2,
                                    LPDDENUMSURFACESCALLBACK2, DDSCAPS2, DDDEVICEIDENTIFIER>;

/// The DirectDraw object of the Direct3D 9 renderer; also its IDirect3D7. A real DirectDraw object is kept only to
/// answer hardware queries (caps, device identifier, display modes) the way the original did.
class DirectDraw final : public IDirectDraw7, public IDirect3D7 {
public:
    /// DirectDrawCreateEx replacement: `iid` must be IID_IDirectDraw7.
    static HRESULT Create(GUID* guid, REFIID iid, void** out) noexcept;

    // IUnknown (shared by both interfaces)
    STDMETHOD(QueryInterface)(REFIID iid, LPVOID* out) override;
    STDMETHOD_(ULONG, AddRef)() override;
    STDMETHOD_(ULONG, Release)() override;

    // IDirectDraw7
    STDMETHOD(Compact)() override;
    STDMETHOD(CreateClipper)(DWORD flags, LPDIRECTDRAWCLIPPER* out, IUnknown* outer) override;
    STDMETHOD(CreatePalette)(DWORD, LPPALETTEENTRY, LPDIRECTDRAWPALETTE*, IUnknown*) override BW_UNSUPPORTED
    STDMETHOD(CreateSurface)(LPDDSURFACEDESC2 desc, LPDIRECTDRAWSURFACE7* out, IUnknown* outer) override;
    STDMETHOD(DuplicateSurface)(LPDIRECTDRAWSURFACE7, LPDIRECTDRAWSURFACE7*) override BW_UNSUPPORTED
    STDMETHOD(EnumDisplayModes)(DWORD flags, LPDDSURFACEDESC2 filter, LPVOID context,
                                LPDDENUMMODESCALLBACK2 callback) override;
    STDMETHOD(EnumSurfaces)(DWORD, LPDDSURFACEDESC2, LPVOID, LPDDENUMSURFACESCALLBACK7) override BW_UNSUPPORTED
    STDMETHOD(FlipToGDISurface)() override;
    STDMETHOD(GetCaps)(LPDDCAPS driver, LPDDCAPS emulation) override;
    STDMETHOD(GetDisplayMode)(LPDDSURFACEDESC2 desc) override;
    STDMETHOD(GetFourCCCodes)(LPDWORD, LPDWORD) override BW_UNSUPPORTED
    STDMETHOD(GetGDISurface)(LPDIRECTDRAWSURFACE7*) override BW_UNSUPPORTED
    STDMETHOD(GetMonitorFrequency)(LPDWORD frequency) override;
    STDMETHOD(GetScanLine)(LPDWORD) override BW_UNSUPPORTED
    STDMETHOD(GetVerticalBlankStatus)(LPBOOL) override BW_UNSUPPORTED
    STDMETHOD(Initialize)(GUID*) override;
    STDMETHOD(RestoreDisplayMode)() override;
    STDMETHOD(SetCooperativeLevel)(HWND window, DWORD flags) override;
    STDMETHOD(SetDisplayMode)(DWORD width, DWORD height, DWORD bitsPerPixel, DWORD refresh, DWORD flags) override;
    STDMETHOD(WaitForVerticalBlank)(DWORD, HANDLE) override;
    STDMETHOD(GetAvailableVidMem)(LPDDSCAPS2 caps, LPDWORD total, LPDWORD free) override;
    STDMETHOD(GetSurfaceFromDC)(HDC, LPDIRECTDRAWSURFACE7*) override BW_UNSUPPORTED
    STDMETHOD(RestoreAllSurfaces)() override;
    STDMETHOD(TestCooperativeLevel)() override;
    STDMETHOD(GetDeviceIdentifier)(LPDDDEVICEIDENTIFIER2 identifier, DWORD flags) override;
    STDMETHOD(StartModeTest)(LPSIZE, DWORD, DWORD) override BW_UNSUPPORTED
    STDMETHOD(EvaluateMode)(DWORD, DWORD*) override BW_UNSUPPORTED

    // IDirect3D7
    STDMETHOD(EnumDevices)(LPD3DENUMDEVICESCALLBACK7 callback, LPVOID context) override;
    STDMETHOD(CreateDevice)(REFCLSID type, LPDIRECTDRAWSURFACE7 target, LPDIRECT3DDEVICE7* out) override;
    STDMETHOD(CreateVertexBuffer)(LPD3DVERTEXBUFFERDESC, LPDIRECT3DVERTEXBUFFER7*, DWORD) override BW_UNSUPPORTED
    STDMETHOD(EnumZBufferFormats)(REFCLSID type, LPD3DENUMPIXELFORMATSCALLBACK callback, LPVOID context) override;
    STDMETHOD(EvictManagedTextures)() override;

    /// The window given to SetCooperativeLevel (where frames are presented).
    [[nodiscard]] HWND Window() const noexcept { return m_window; }
    /// Creates the Direct3D 9 device on first use; false when it cannot be created.
    [[nodiscard]] bool EnsureGpu() noexcept;
    [[nodiscard]] const D3DDEVICEDESC7& DeviceDescription() const noexcept { return m_deviceDescription; }
    /// The display mode the game set (the real display mode is never changed).
    [[nodiscard]] SIZE DisplaySize() const noexcept { return m_displaySize; }

    DirectDrawV1& V1() noexcept { return m_v1; }
    DirectDrawV2& V2() noexcept { return m_v2; }
    DirectDrawV4& V4() noexcept { return m_v4; }

private:
    DirectDraw() noexcept;
    ~DirectDraw();

    RefCount m_references;
    Microsoft::WRL::ComPtr<IDirectDraw7> m_real;  // ddraw.dll's object, for hardware queries
    D3DDEVICEDESC7 m_deviceDescription{};
    HWND m_window = nullptr;
    DWORD m_cooperativeFlags = 0;
    SIZE m_displaySize{};
    DWORD m_displayBits = 32;

    DirectDrawV1 m_v1{*this};
    DirectDrawV2 m_v2{*this};
    DirectDrawV4 m_v4{*this};
};

/// IDirectDrawClipper: remembers its window (the renderer always presents into the whole client area).
class Clipper final : public IDirectDrawClipper {
public:
    static Clipper* Create() noexcept { return new (std::nothrow) Clipper(); }

    STDMETHOD(QueryInterface)(REFIID iid, LPVOID* out) override;
    STDMETHOD_(ULONG, AddRef)() override;
    STDMETHOD_(ULONG, Release)() override;
    STDMETHOD(GetClipList)(LPRECT, LPRGNDATA, LPDWORD) override BW_UNSUPPORTED
    STDMETHOD(GetHWnd)(HWND* window) override;
    STDMETHOD(Initialize)(LPDIRECTDRAW, DWORD) override;
    STDMETHOD(IsClipListChanged)(BOOL* changed) override;
    STDMETHOD(SetClipList)(LPRGNDATA, DWORD) override BW_UNSUPPORTED
    STDMETHOD(SetHWnd)(DWORD, HWND window) override;

    [[nodiscard]] HWND Window() const noexcept { return m_window; }

private:
    Clipper() noexcept = default;
    RefCount m_references;
    HWND m_window = nullptr;
};

}  // namespace bw::render
