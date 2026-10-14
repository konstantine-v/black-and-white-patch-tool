#pragma once

#include "render/Com.h"
#include "render/Gpu.h"

#include <memory>
#include <string>
#include <vector>

namespace bw::render {

class DepthView;
class DirectDraw;
class Surface;

/// What a surface is for, decided from its creation caps.
enum class SurfaceRole : std::uint8_t {
    Primary,       ///< the screen; Blt from a render target presents
    RenderTarget,  ///< 3D back buffer or render-to-texture target (GPU)
    Depth,         ///< z-buffer (GPU, always 24-bit depth + 8-bit stencil)
    Texture,       ///< texture or one level of a mipmap chain (GPU, with a CPU copy)
    System,        ///< plain CPU memory (video frames, temporary images)
};

/// An older interface version of a Surface (IDirectDrawSurface .. IDirectDrawSurface4). Every call forwards to the
/// Surface; argument types differ only in the surface-description and caps structures. Methods that a version does
/// not have become harmless extra virtuals.
template <typename Interface, typename Desc, typename Caps, typename EnumCallback, typename UnlockArgument>
class SurfaceView final : public Interface {
public:
    explicit SurfaceView(Surface& owner) noexcept : m_owner(owner) {}
    [[nodiscard]] Surface& Owner() const noexcept { return m_owner; }

    STDMETHOD(QueryInterface)(REFIID iid, LPVOID* out);
    STDMETHOD_(ULONG, AddRef)();
    STDMETHOD_(ULONG, Release)();
    STDMETHOD(AddAttachedSurface)(Interface* surface);
    STDMETHOD(AddOverlayDirtyRect)(LPRECT) BW_UNSUPPORTED
    STDMETHOD(Blt)(LPRECT destination, Interface* source, LPRECT sourceRect, DWORD flags, LPDDBLTFX effects);
    STDMETHOD(BltBatch)(LPDDBLTBATCH, DWORD, DWORD) BW_UNSUPPORTED
    STDMETHOD(BltFast)(DWORD x, DWORD y, Interface* source, LPRECT sourceRect, DWORD flags);
    STDMETHOD(DeleteAttachedSurface)(DWORD flags, Interface* surface);
    STDMETHOD(EnumAttachedSurfaces)(LPVOID, EnumCallback) BW_UNSUPPORTED
    STDMETHOD(EnumOverlayZOrders)(DWORD, LPVOID, EnumCallback) BW_UNSUPPORTED
    STDMETHOD(Flip)(Interface* target, DWORD flags);
    STDMETHOD(GetAttachedSurface)(Caps* caps, Interface** out);
    STDMETHOD(GetBltStatus)(DWORD);
    STDMETHOD(GetCaps)(Caps* caps);
    STDMETHOD(GetClipper)(LPDIRECTDRAWCLIPPER* out);
    STDMETHOD(GetColorKey)(DWORD flags, LPDDCOLORKEY key);
    STDMETHOD(GetDC)(HDC*) BW_UNSUPPORTED
    STDMETHOD(GetFlipStatus)(DWORD);
    STDMETHOD(GetOverlayPosition)(LPLONG, LPLONG) BW_UNSUPPORTED
    STDMETHOD(GetPalette)(LPDIRECTDRAWPALETTE* out);
    STDMETHOD(GetPixelFormat)(LPDDPIXELFORMAT format);
    STDMETHOD(GetSurfaceDesc)(Desc* desc);
    STDMETHOD(Initialize)(LPDIRECTDRAW, Desc*);
    STDMETHOD(IsLost)();
    STDMETHOD(Lock)(LPRECT rect, Desc* desc, DWORD flags, HANDLE event);
    STDMETHOD(ReleaseDC)(HDC) BW_UNSUPPORTED
    STDMETHOD(Restore)();
    STDMETHOD(SetClipper)(LPDIRECTDRAWCLIPPER clipper);
    STDMETHOD(SetColorKey)(DWORD flags, LPDDCOLORKEY key);
    STDMETHOD(SetOverlayPosition)(LONG, LONG) BW_UNSUPPORTED
    STDMETHOD(SetPalette)(LPDIRECTDRAWPALETTE) BW_UNSUPPORTED
    STDMETHOD(Unlock)(UnlockArgument);
    STDMETHOD(UpdateOverlay)(LPRECT, Interface*, LPRECT, DWORD, LPDDOVERLAYFX) BW_UNSUPPORTED
    STDMETHOD(UpdateOverlayDisplay)(DWORD) BW_UNSUPPORTED
    STDMETHOD(UpdateOverlayZOrder)(DWORD, Interface*) BW_UNSUPPORTED
    // IDirectDrawSurface2 and later
    STDMETHOD(GetDDInterface)(LPVOID* out);
    STDMETHOD(PageLock)(DWORD);
    STDMETHOD(PageUnlock)(DWORD);
    // IDirectDrawSurface3 and later
    STDMETHOD(SetSurfaceDesc)(Desc*, DWORD) BW_UNSUPPORTED
    // IDirectDrawSurface4
    STDMETHOD(SetPrivateData)(REFGUID, LPVOID, DWORD, DWORD) BW_UNSUPPORTED
    STDMETHOD(GetPrivateData)(REFGUID, LPVOID, LPDWORD) BW_UNSUPPORTED
    STDMETHOD(FreePrivateData)(REFGUID) BW_UNSUPPORTED
    STDMETHOD(GetUniquenessValue)(LPDWORD) BW_UNSUPPORTED
    STDMETHOD(ChangeUniquenessValue)() BW_UNSUPPORTED

private:
    Surface& m_owner;
};

using SurfaceV1 = SurfaceView<IDirectDrawSurface, DDSURFACEDESC, DDSCAPS, LPDDENUMSURFACESCALLBACK, LPVOID>;
using SurfaceV2 = SurfaceView<IDirectDrawSurface2, DDSURFACEDESC, DDSCAPS, LPDDENUMSURFACESCALLBACK, LPVOID>;
using SurfaceV3 = SurfaceView<IDirectDrawSurface3, DDSURFACEDESC, DDSCAPS, LPDDENUMSURFACESCALLBACK, LPVOID>;
using SurfaceV4 = SurfaceView<IDirectDrawSurface4, DDSURFACEDESC2, DDSCAPS2, LPDDENUMSURFACESCALLBACK2, LPRECT>;

/// IDirect3DTexture2 of a texture surface: the game asks every texture for it. Load copies another texture's levels.
class TextureView final : public IDirect3DTexture2 {
public:
    explicit TextureView(Surface& owner) noexcept : m_owner(owner) {}
    STDMETHOD(QueryInterface)(REFIID iid, LPVOID* out) override;
    STDMETHOD_(ULONG, AddRef)() override;
    STDMETHOD_(ULONG, Release)() override;
    STDMETHOD(GetHandle)(LPDIRECT3DDEVICE2, LPD3DTEXTUREHANDLE handle) override;
    STDMETHOD(PaletteChanged)(DWORD, DWORD) override;
    STDMETHOD(Load)(LPDIRECT3DTEXTURE2 source) override;

private:
    Surface& m_owner;
};

/// A DirectDraw surface backed by Direct3D 9 (see SurfaceRole). Implements IDirectDrawSurface7; the older interface
/// versions are SurfaceViews.
class Surface final : public IDirectDrawSurface7 {
public:
    /// Creates the surface (and, for a mipmapped texture, its lower levels) described by `desc`.
    static HRESULT Create(DirectDraw& owner, const DDSURFACEDESC2& desc, Surface** out) noexcept;

    /// The Surface behind an interface pointer of any version (all are created by this renderer).
    [[nodiscard]] static Surface* From(IDirectDrawSurface7* surface) noexcept;
    [[nodiscard]] static Surface* From(IDirectDrawSurface* surface) noexcept;
    [[nodiscard]] static Surface* From(IDirectDrawSurface2* surface) noexcept;
    [[nodiscard]] static Surface* From(IDirectDrawSurface3* surface) noexcept;
    [[nodiscard]] static Surface* From(IDirectDrawSurface4* surface) noexcept;

    // IUnknown
    STDMETHOD(QueryInterface)(REFIID iid, LPVOID* out) override;
    STDMETHOD_(ULONG, AddRef)() override;
    STDMETHOD_(ULONG, Release)() override;
    // IDirectDrawSurface7
    STDMETHOD(AddAttachedSurface)(LPDIRECTDRAWSURFACE7 surface) override;
    STDMETHOD(AddOverlayDirtyRect)(LPRECT) override BW_UNSUPPORTED
    STDMETHOD(Blt)(LPRECT destination, LPDIRECTDRAWSURFACE7 source, LPRECT sourceRect, DWORD flags,
                   LPDDBLTFX effects) override;
    STDMETHOD(BltBatch)(LPDDBLTBATCH, DWORD, DWORD) override BW_UNSUPPORTED
    STDMETHOD(BltFast)(DWORD x, DWORD y, LPDIRECTDRAWSURFACE7 source, LPRECT sourceRect, DWORD flags) override;
    STDMETHOD(DeleteAttachedSurface)(DWORD flags, LPDIRECTDRAWSURFACE7 surface) override;
    STDMETHOD(EnumAttachedSurfaces)(LPVOID, LPDDENUMSURFACESCALLBACK7) override BW_UNSUPPORTED
    STDMETHOD(EnumOverlayZOrders)(DWORD, LPVOID, LPDDENUMSURFACESCALLBACK7) override BW_UNSUPPORTED
    STDMETHOD(Flip)(LPDIRECTDRAWSURFACE7 target, DWORD flags) override;
    STDMETHOD(GetAttachedSurface)(LPDDSCAPS2 caps, LPDIRECTDRAWSURFACE7* out) override;
    STDMETHOD(GetBltStatus)(DWORD) override;
    STDMETHOD(GetCaps)(LPDDSCAPS2 caps) override;
    STDMETHOD(GetClipper)(LPDIRECTDRAWCLIPPER* out) override;
    STDMETHOD(GetColorKey)(DWORD flags, LPDDCOLORKEY key) override;
    STDMETHOD(GetDC)(HDC*) override BW_UNSUPPORTED
    STDMETHOD(GetFlipStatus)(DWORD) override;
    STDMETHOD(GetOverlayPosition)(LPLONG, LPLONG) override BW_UNSUPPORTED
    STDMETHOD(GetPalette)(LPDIRECTDRAWPALETTE* out) override;
    STDMETHOD(GetPixelFormat)(LPDDPIXELFORMAT format) override;
    STDMETHOD(GetSurfaceDesc)(LPDDSURFACEDESC2 desc) override;
    STDMETHOD(Initialize)(LPDIRECTDRAW, LPDDSURFACEDESC2) override;
    STDMETHOD(IsLost)() override;
    STDMETHOD(Lock)(LPRECT rect, LPDDSURFACEDESC2 desc, DWORD flags, HANDLE event) override;
    STDMETHOD(ReleaseDC)(HDC) override BW_UNSUPPORTED
    STDMETHOD(Restore)() override;
    STDMETHOD(SetClipper)(LPDIRECTDRAWCLIPPER clipper) override;
    STDMETHOD(SetColorKey)(DWORD flags, LPDDCOLORKEY key) override;
    STDMETHOD(SetOverlayPosition)(LONG, LONG) override BW_UNSUPPORTED
    STDMETHOD(SetPalette)(LPDIRECTDRAWPALETTE) override BW_UNSUPPORTED
    STDMETHOD(Unlock)(LPRECT rect) override;
    STDMETHOD(UpdateOverlay)(LPRECT, LPDIRECTDRAWSURFACE7, LPRECT, DWORD, LPDDOVERLAYFX) override BW_UNSUPPORTED
    STDMETHOD(UpdateOverlayDisplay)(DWORD) override BW_UNSUPPORTED
    STDMETHOD(UpdateOverlayZOrder)(DWORD, LPDIRECTDRAWSURFACE7) override BW_UNSUPPORTED
    STDMETHOD(GetDDInterface)(LPVOID* out) override;
    STDMETHOD(PageLock)(DWORD) override;
    STDMETHOD(PageUnlock)(DWORD) override;
    STDMETHOD(SetSurfaceDesc)(LPDDSURFACEDESC2, DWORD) override BW_UNSUPPORTED
    STDMETHOD(SetPrivateData)(REFGUID, LPVOID, DWORD, DWORD) override BW_UNSUPPORTED
    STDMETHOD(GetPrivateData)(REFGUID, LPVOID, LPDWORD) override BW_UNSUPPORTED
    STDMETHOD(FreePrivateData)(REFGUID) override BW_UNSUPPORTED
    STDMETHOD(GetUniquenessValue)(LPDWORD) override BW_UNSUPPORTED
    STDMETHOD(ChangeUniquenessValue)() override BW_UNSUPPORTED
    STDMETHOD(SetPriority)(DWORD priority) override;
    STDMETHOD(GetPriority)(LPDWORD priority) override;
    STDMETHOD(SetLOD)(DWORD lod) override;
    STDMETHOD(GetLOD)(LPDWORD lod) override;

    [[nodiscard]] SurfaceRole Role() const noexcept { return m_role; }
    [[nodiscard]] Format PixelFormat() const noexcept { return m_format; }
    [[nodiscard]] std::uint32_t Width() const noexcept { return m_desc.dwWidth; }
    [[nodiscard]] std::uint32_t Height() const noexcept { return m_desc.dwHeight; }
    [[nodiscard]] RECT Bounds() const noexcept { return {0, 0, static_cast<LONG>(Width()), static_cast<LONG>(Height())}; }
    [[nodiscard]] gpu::Texture* GpuTexture() const noexcept;
    [[nodiscard]] gpu::RenderTarget* GpuTarget() const noexcept { return m_target.get(); }
    [[nodiscard]] gpu::DepthBuffer* GpuDepth() const noexcept { return m_depth.get(); }
    [[nodiscard]] Surface* AttachedDepth() const noexcept { return m_attachedDepth; }
    [[nodiscard]] const DDSURFACEDESC2& Description() const noexcept { return m_desc; }

    SurfaceV1& V1() noexcept { return m_v1; }
    SurfaceV2& V2() noexcept { return m_v2; }
    SurfaceV3& V3() noexcept { return m_v3; }
    SurfaceV4& V4() noexcept { return m_v4; }
    TextureView& AsTexture() noexcept { return m_texture2; }

    /// IDirect3DTexture2::Load: copies every mipmap level of `source` (same size and format) into this texture.
    HRESULT LoadFrom(Surface& source) noexcept;

    /// What the game loaded into this surface (a texture pack source name, render/TexturePack.h). Whole-level copies
    /// into textures register the copy's contents under this name.
    void SetSourceName(std::string name) { m_sourceName = std::move(name); }

private:
    Surface(DirectDraw& owner, const DDSURFACEDESC2& desc, SurfaceRole role, Format format) noexcept;
    ~Surface();

    HRESULT AllocateStorage() noexcept;
    /// Pixels of this surface readable by the CPU: locks it read-only (reading back a render target). Returns
    /// nullptr when that is not possible.
    struct CpuView {
        const std::uint8_t* bits = nullptr;
        std::int32_t pitch = 0;
        std::vector<std::uint8_t> copy;  // holds read-back render target pixels
    };
    bool ReadPixels(CpuView& view) noexcept;
    HRESULT Fill(const RECT& rect, DWORD color) noexcept;
    HRESULT CopyFrom(Surface& source, const RECT& sourceRect, const RECT& destination, bool sourceKey) noexcept;
    /// CopyFrom into CPU storage (texture or system memory); `decoded` receives a compressed source's pixels.
    HRESULT CopyPixels(Surface& source, const RECT& sourceRect, const RECT& destination, bool sourceKey,
                       std::vector<std::uint32_t>& decoded) noexcept;
    HRESULT Present(Surface& frame, const RECT& source, const RECT& destination) noexcept;
    [[nodiscard]] HWND PresentationWindow() const noexcept;

    RefCount m_references;
    DirectDraw& m_owner;
    DDSURFACEDESC2 m_desc{};
    SurfaceRole m_role;
    Format m_format;

    // Storage, by role.
    std::vector<std::uint8_t> m_pixels;  // System, Primary, and the CPU copy of a locked render target
    std::unique_ptr<DepthView> m_depthView;  // CPU view of a locked depth buffer
    std::int32_t m_pitch = 0;
    std::unique_ptr<gpu::Texture> m_texture;  // top level of a texture owns the chain
    Surface* m_textureOwner = nullptr;        // the top level (this, for the top level)
    std::uint32_t m_level = 0;                // mipmap level
    std::vector<Surface*> m_mipLevels;        // lower levels (top level only)
    std::unique_ptr<gpu::RenderTarget> m_target;
    std::unique_ptr<gpu::DepthBuffer> m_depth;

    Surface* m_attachedDepth = nullptr;
    LPDIRECTDRAWCLIPPER m_clipper = nullptr;
    DDCOLORKEY m_sourceKey{};
    bool m_hasSourceKey = false;
    DWORD m_priority = 0;
    std::string m_sourceName;
    DWORD m_lod = 0;

    bool m_locked = false;
    bool m_lockReadOnly = false;

    SurfaceV1 m_v1{*this};
    SurfaceV2 m_v2{*this};
    SurfaceV3 m_v3{*this};
    SurfaceV4 m_v4{*this};
    TextureView m_texture2{*this};
};

}  // namespace bw::render
