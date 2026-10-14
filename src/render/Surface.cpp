#include "render/Surface.h"

#include "core/Log.h"
#include "render/DepthView.h"
#include "render/Device.h"
#include "render/DirectDraw.h"
#include "render/Pixels.h"
#include "render/TexturePack.h"

#include <algorithm>
#include <cstring>
#include <format>
#include <mutex>
#include <set>
#include <new>
#include <type_traits>

namespace bw::render {

namespace {

SurfaceRole RoleFor(const DDSURFACEDESC2& desc) noexcept
{
    const DWORD caps = (desc.dwFlags & DDSD_CAPS) ? desc.ddsCaps.dwCaps : 0;
    if (caps & DDSCAPS_PRIMARYSURFACE) {
        return SurfaceRole::Primary;
    }
    if (caps & DDSCAPS_ZBUFFER) {
        return SurfaceRole::Depth;
    }
    if (caps & DDSCAPS_TEXTURE) {
        return SurfaceRole::Texture;
    }
    if (caps & DDSCAPS_3DDEVICE) {
        return SurfaceRole::RenderTarget;
    }
    return SurfaceRole::System;
}

std::uint32_t MipLevels(const DDSURFACEDESC2& desc) noexcept
{
    if ((desc.ddsCaps.dwCaps & DDSCAPS_MIPMAP) == 0) {
        return 1;
    }
    if ((desc.dwFlags & DDSD_MIPMAPCOUNT) && desc.dwMipMapCount != 0) {
        return desc.dwMipMapCount;
    }
    std::uint32_t levels = 1;
    for (DWORD width = desc.dwWidth, height = desc.dwHeight; width > 1 || height > 1; ++levels) {
        width = std::max<DWORD>(width / 2, 1);
        height = std::max<DWORD>(height / 2, 1);
    }
    return levels;
}

std::int32_t AlignedPitch(Format format, std::uint32_t width) noexcept
{
    return static_cast<std::int32_t>((RowBytes(format, width) + 3) & ~3u);
}

/// Byte offset of pixel (x, y) in rows of `pitch` (block rows for compressed formats).
std::size_t Offset(Format format, std::int32_t pitch, LONG x, LONG y) noexcept
{
    if (IsBlockCompressed(format)) {
        return std::size_t(y / 4) * pitch + std::size_t(x / 4) * (format == Format::Dxt1 ? 8 : 16);
    }
    return std::size_t(y) * pitch + std::size_t(x) * BytesPerPixel(format);
}

bool Contains(const RECT& outer, const RECT& inner) noexcept
{
    return inner.left >= outer.left && inner.top >= outer.top && inner.right <= outer.right &&
           inner.bottom <= outer.bottom && inner.left < inner.right && inner.top < inner.bottom;
}

void LogOnce(bool& logged, std::string_view message) noexcept
{
    if (!logged) {
        logged = true;
        log::Info("Renderer: {}", message);
    }
}

const char* RoleName(SurfaceRole role) noexcept
{
    constexpr const char* kNames[] = {"primary", "render target", "depth", "texture", "system"};
    return kNames[static_cast<int>(role)];
}

/// Logs (at Debug level, once per distinct text) what kinds of surfaces and blits the game uses.
void LogKind(std::string text)
{
    static std::mutex mutex;
    static std::set<std::string> seen;
    std::lock_guard lock(mutex);
    if (seen.insert(text).second) {
        log::Debug("Renderer: {}", text);
    }
}

/// DirectDraw describes compressed surfaces by their total size (dwLinearSize, sharing lPitch's storage) and
/// uncompressed ones by their pitch. The game copies DXT data using the size.
void SetPitchOrSize(DDSURFACEDESC2& desc, Format format, std::int32_t pitch) noexcept
{
    if (IsBlockCompressed(format)) {
        desc.dwFlags = (desc.dwFlags & ~static_cast<DWORD>(DDSD_PITCH)) | DDSD_LINEARSIZE;
        desc.dwLinearSize = static_cast<DWORD>(pitch) * StorageRows(format, desc.dwHeight);
    }
    else {
        desc.dwFlags = (desc.dwFlags & ~static_cast<DWORD>(DDSD_LINEARSIZE)) | DDSD_PITCH;
        desc.lPitch = pitch;
    }
}

std::string GuidText(REFIID iid)
{
    return std::format("{{{:08x}-{:04x}-{:04x}-{:02x}{:02x}-{:02x}{:02x}{:02x}{:02x}{:02x}{:02x}}}", iid.Data1, iid.Data2,
                       iid.Data3, iid.Data4[0], iid.Data4[1], iid.Data4[2], iid.Data4[3], iid.Data4[4], iid.Data4[5],
                       iid.Data4[6], iid.Data4[7]);
}

/// A surface locked for CPU access for the duration of a blit.
class CpuLock {
public:
    CpuLock(Surface& surface, bool readOnly) noexcept : m_surface(surface)
    {
        DDSURFACEDESC2 desc{};
        desc.dwSize = sizeof desc;
        const DWORD flags = DDLOCK_WAIT | (readOnly ? DDLOCK_READONLY : 0);
        if (SUCCEEDED(surface.Lock(nullptr, &desc, flags, nullptr))) {
            m_bits = static_cast<std::uint8_t*>(desc.lpSurface);
            m_pitch = (desc.dwFlags & DDSD_LINEARSIZE)
                          ? static_cast<std::int32_t>(desc.dwLinearSize /
                                                      std::max(StorageRows(surface.PixelFormat(), surface.Height()), 1u))
                          : desc.lPitch;
        }
    }
    ~CpuLock()
    {
        if (m_bits != nullptr) {
            m_surface.Unlock(nullptr);
        }
    }
    CpuLock(const CpuLock&) = delete;
    CpuLock& operator=(const CpuLock&) = delete;

    explicit operator bool() const noexcept { return m_bits != nullptr; }
    [[nodiscard]] std::uint8_t* Bits() const noexcept { return m_bits; }
    [[nodiscard]] std::int32_t Pitch() const noexcept { return m_pitch; }

private:
    Surface& m_surface;
    std::uint8_t* m_bits = nullptr;
    std::int32_t m_pitch = 0;
};

}  // namespace

// ------------------------------------------------------------------------------------------------- Creation

HRESULT Surface::Create(DirectDraw& owner, const DDSURFACEDESC2& requested, Surface** out) noexcept
{
    if (out == nullptr) {
        return DDERR_INVALIDPARAMS;
    }
    *out = nullptr;
    DDSURFACEDESC2 desc = requested;
    desc.dwSize = sizeof desc;
    const SurfaceRole role = RoleFor(desc);

    if (role == SurfaceRole::Primary) {
        desc.dwWidth = static_cast<DWORD>(GetSystemMetrics(SM_CXSCREEN));
        desc.dwHeight = static_cast<DWORD>(GetSystemMetrics(SM_CYSCREEN));
        if (desc.dwFlags & DDSD_BACKBUFFERCOUNT) {
            log::Warning("Renderer: primary flip chains are not implemented (the game runs windowed)");
        }
    }
    else if ((desc.dwFlags & (DDSD_WIDTH | DDSD_HEIGHT)) != (DDSD_WIDTH | DDSD_HEIGHT) || desc.dwWidth == 0 ||
             desc.dwHeight == 0) {
        return DDERR_INVALIDPARAMS;
    }

    Format format = Format::X8R8G8B8;
    if (desc.dwFlags & DDSD_PIXELFORMAT) {
        format = FromPixelFormat(desc.ddpfPixelFormat);
    }
    else if (role == SurfaceRole::Depth) {
        format = Format::D16;
    }
    if (format == Format::Unknown) {
        log::Warning("Renderer: unsupported pixel format (flags 0x{:x}, {} bits, masks {:x}/{:x}/{:x}/{:x})",
                     desc.ddpfPixelFormat.dwFlags, desc.ddpfPixelFormat.dwRGBBitCount, desc.ddpfPixelFormat.dwRBitMask,
                     desc.ddpfPixelFormat.dwGBitMask, desc.ddpfPixelFormat.dwBBitMask,
                     desc.ddpfPixelFormat.dwRGBAlphaBitMask);
        return DDERR_INVALIDPIXELFORMAT;
    }
    const bool needsGpu = role == SurfaceRole::RenderTarget || role == SurfaceRole::Depth || role == SurfaceRole::Texture;
    if (needsGpu && !owner.EnsureGpu()) {
        log::Error("Renderer: no Direct3D 9 device for a {} surface", role == SurfaceRole::Texture ? "texture" : "3D");
        return DDERR_GENERIC;
    }

    const std::uint32_t levels = role == SurfaceRole::Texture ? MipLevels(desc) : 1;
    desc.dwFlags |= DDSD_CAPS | DDSD_WIDTH | DDSD_HEIGHT | DDSD_PIXELFORMAT;
    desc.ddpfPixelFormat = ToPixelFormat(format);
    SetPitchOrSize(desc, format, AlignedPitch(format, desc.dwWidth));
    if (role != SurfaceRole::System || (desc.ddsCaps.dwCaps & DDSCAPS_SYSTEMMEMORY) == 0) {
        if ((desc.ddsCaps.dwCaps & DDSCAPS_SYSTEMMEMORY) == 0) {
            desc.ddsCaps.dwCaps |= DDSCAPS_VIDEOMEMORY | DDSCAPS_LOCALVIDMEM;
        }
    }
    if (levels > 1) {
        desc.dwFlags |= DDSD_MIPMAPCOUNT;
        desc.dwMipMapCount = levels;
    }

    auto* surface = new (std::nothrow) Surface(owner, desc, role, format);
    if (surface == nullptr) {
        return DDERR_OUTOFMEMORY;
    }
    if (const HRESULT result = surface->AllocateStorage(); FAILED(result)) {
        surface->Release();
        return result;
    }

    // Lower mipmap levels: surfaces sharing the top level's texture.
    DWORD width = desc.dwWidth;
    DWORD height = desc.dwHeight;
    for (std::uint32_t level = 1; level < levels; ++level) {
        width = std::max<DWORD>(width / 2, 1);
        height = std::max<DWORD>(height / 2, 1);
        DDSURFACEDESC2 levelDesc = desc;
        levelDesc.dwWidth = width;
        levelDesc.dwHeight = height;
        levelDesc.dwMipMapCount = levels - level;
        SetPitchOrSize(levelDesc, format, AlignedPitch(format, width));
        levelDesc.ddsCaps.dwCaps2 |= DDSCAPS2_MIPMAPSUBLEVEL;
        auto* child = new (std::nothrow) Surface(owner, levelDesc, role, format);
        if (child == nullptr) {
            surface->Release();
            return DDERR_OUTOFMEMORY;
        }
        child->m_textureOwner = surface;
        child->m_level = level;
        surface->m_mipLevels.push_back(child);
    }
    LogKind(std::format("surface: {} {} caps 0x{:08x}/0x{:08x} levels {}", RoleName(role), Name(format),
                        desc.ddsCaps.dwCaps, desc.ddsCaps.dwCaps2, levels));
    *out = surface;
    return DD_OK;
}

Surface::Surface(DirectDraw& owner, const DDSURFACEDESC2& desc, SurfaceRole role, Format format) noexcept
    : m_owner(owner), m_desc(desc), m_role(role), m_format(format), m_textureOwner(this)
{
    static_cast<IDirectDraw7&>(m_owner).AddRef();
}

Surface::~Surface()
{
    if (Device* device = CurrentDevice()) {
        device->Unbind(*this);
    }
    for (Surface* level : m_mipLevels) {
        level->m_textureOwner = nullptr;
        delete level;
    }
    if (m_attachedDepth != nullptr) {
        m_attachedDepth->Release();
    }
    if (m_clipper != nullptr) {
        m_clipper->Release();
    }
    static_cast<IDirectDraw7&>(m_owner).Release();
}

HRESULT Surface::AllocateStorage() noexcept
{
    switch (m_role) {
    case SurfaceRole::System:
        m_pitch = AlignedPitch(m_format, Width());
        try {
            m_pixels.assign(std::size_t(m_pitch) * StorageRows(m_format, Height()), 0);
        }
        catch (const std::bad_alloc&) {
            return DDERR_OUTOFMEMORY;
        }
        return DD_OK;
    case SurfaceRole::Primary:
        m_pitch = AlignedPitch(m_format, Width());
        return DD_OK;  // CPU copy allocated on first Lock
    case SurfaceRole::Texture:
        m_texture = gpu::Texture::Create(Width(), Height(), m_desc.dwMipMapCount != 0 ? m_desc.dwMipMapCount : 1,
                                         m_format);
        return m_texture ? DD_OK : DDERR_OUTOFVIDEOMEMORY;
    case SurfaceRole::RenderTarget:
        m_pitch = AlignedPitch(m_format, Width());
        m_target = gpu::RenderTarget::Create(Width(), Height(), m_format);
        return m_target ? DD_OK : DDERR_OUTOFVIDEOMEMORY;
    case SurfaceRole::Depth:
        m_depth = gpu::DepthBuffer::Create(Width(), Height());
        return m_depth ? DD_OK : DDERR_OUTOFVIDEOMEMORY;
    }
    return DDERR_GENERIC;
}

gpu::Texture* Surface::GpuTexture() const noexcept
{
    return m_textureOwner != nullptr ? m_textureOwner->m_texture.get() : nullptr;
}

Surface* Surface::From(IDirectDrawSurface7* surface) noexcept
{
    return static_cast<Surface*>(surface);
}

Surface* Surface::From(IDirectDrawSurface* surface) noexcept
{
    return surface != nullptr ? &static_cast<SurfaceV1*>(surface)->Owner() : nullptr;
}

Surface* Surface::From(IDirectDrawSurface2* surface) noexcept
{
    return surface != nullptr ? &static_cast<SurfaceV2*>(surface)->Owner() : nullptr;
}

Surface* Surface::From(IDirectDrawSurface3* surface) noexcept
{
    return surface != nullptr ? &static_cast<SurfaceV3*>(surface)->Owner() : nullptr;
}

Surface* Surface::From(IDirectDrawSurface4* surface) noexcept
{
    return surface != nullptr ? &static_cast<SurfaceV4*>(surface)->Owner() : nullptr;
}

// ------------------------------------------------------------------------------------------------- IUnknown

HRESULT Surface::QueryInterface(REFIID iid, LPVOID* out)
{
    if (out == nullptr) {
        return E_POINTER;
    }
    if (iid == IID_IUnknown || iid == IID_IDirectDrawSurface7) {
        *out = static_cast<IDirectDrawSurface7*>(this);
    }
    else if (iid == IID_IDirectDrawSurface) {
        *out = static_cast<IDirectDrawSurface*>(&m_v1);
    }
    else if (iid == IID_IDirectDrawSurface2) {
        *out = static_cast<IDirectDrawSurface2*>(&m_v2);
    }
    else if (iid == IID_IDirectDrawSurface3) {
        *out = static_cast<IDirectDrawSurface3*>(&m_v3);
    }
    else if (iid == IID_IDirectDrawSurface4) {
        *out = static_cast<IDirectDrawSurface4*>(&m_v4);
    }
    else if (iid == IID_IDirect3DTexture2 && m_role == SurfaceRole::Texture) {
        *out = static_cast<IDirect3DTexture2*>(&m_texture2);
    }
    else {
        *out = nullptr;
        static bool logged = false;
        if (!logged) {
            logged = true;
            log::Info("Renderer: surface asked for interface {} (not provided)", GuidText(iid));
        }
        return E_NOINTERFACE;
    }
    AddRef();
    return S_OK;
}

ULONG Surface::AddRef()
{
    return m_references.AddReference();
}

ULONG Surface::Release()
{
    const ULONG count = m_references.ReleaseReference();
    if (count == 0 && m_textureOwner == this) {
        delete this;
    }
    // Lower mipmap levels live as long as their top level, as in DirectDraw.
    return count;
}

// ---------------------------------------------------------------------------------------------------- Locks

HRESULT Surface::Lock(LPRECT rect, LPDDSURFACEDESC2 desc, DWORD flags, HANDLE /*event*/)
{
    if (desc == nullptr) {
        return DDERR_INVALIDPARAMS;
    }
    if (m_locked) {
        return DDERR_SURFACEBUSY;
    }
    const RECT area = rect != nullptr ? *rect : Bounds();
    if (!Contains(Bounds(), area)) {
        return DDERR_INVALIDRECT;
    }
    const bool readOnly = (flags & DDLOCK_READONLY) != 0;
    std::uint8_t* bits = nullptr;
    std::int32_t pitch = 0;

    switch (m_role) {
    case SurfaceRole::Texture: {
        gpu::LockedRect locked;
        if (GpuTexture() == nullptr || !GpuTexture()->Lock(m_level, rect, readOnly, locked)) {
            return DDERR_GENERIC;
        }
        bits = static_cast<std::uint8_t*>(locked.bits);
        pitch = locked.pitch;
        break;
    }
    case SurfaceRole::System:
        bits = m_pixels.data() + Offset(m_format, m_pitch, area.left, area.top);
        pitch = m_pitch;
        break;
    case SurfaceRole::RenderTarget:
    case SurfaceRole::Primary: {
        try {
            m_pixels.resize(std::size_t(m_pitch) * Height());
        }
        catch (const std::bad_alloc&) {
            return DDERR_OUTOFMEMORY;
        }
        if (m_role == SurfaceRole::RenderTarget) {
            static bool logged = false;
            LogOnce(logged, "CPU access to a render target (read back from the GPU)");
            if ((flags & DDLOCK_WRITEONLY) == 0 && !m_target->Read(m_pixels.data(), m_pitch)) {
                return DDERR_GENERIC;
            }
        }
        else {
            static bool logged = false;
            LogOnce(logged, "CPU access to the primary surface (not shown)");
        }
        bits = m_pixels.data() + Offset(m_format, m_pitch, area.left, area.top);
        pitch = m_pitch;
        break;
    }
    case SurfaceRole::Depth: {
        // The game reads depth to test whether the sun is hidden; DepthView fills only the pages it touches.
        if (!m_depthView) {
            m_depthView = std::make_unique<DepthView>();
        }
        m_pitch = AlignedPitch(m_format, Width());
        std::uint8_t* view =
            m_depth->Readable() ? m_depthView->Begin(*m_depth, m_format, Width(), Height(), m_pitch) : nullptr;
        if (view == nullptr) {
            static bool logged = false;
            LogOnce(logged, "the depth buffer cannot be read by the CPU");
            return DDERR_UNSUPPORTED;
        }
        bits = view + Offset(m_format, m_pitch, area.left, area.top);
        pitch = m_pitch;
        break;
    }
    }

    const DWORD size = desc->dwSize;
    *desc = m_desc;
    desc->dwSize = size;
    desc->dwFlags |= DDSD_LPSURFACE;
    desc->lpSurface = bits;
    SetPitchOrSize(*desc, m_format, pitch);
    m_locked = true;
    m_lockReadOnly = readOnly;
    return DD_OK;
}

HRESULT Surface::Unlock(LPRECT /*rect*/)
{
    if (!m_locked) {
        return DDERR_NOTLOCKED;
    }
    m_locked = false;
    switch (m_role) {
    case SurfaceRole::Texture:
        GpuTexture()->Unlock(m_level);
        break;
    case SurfaceRole::RenderTarget:
        if (!m_lockReadOnly) {
            (void)m_target->Write(m_pixels.data(), m_pitch);
        }
        break;
    default:
        break;
    }
    return DD_OK;
}

// ---------------------------------------------------------------------------------------------------- Blits

HRESULT Surface::Blt(LPRECT destination, LPDIRECTDRAWSURFACE7 source, LPRECT sourceRect, DWORD flags,
                     LPDDBLTFX effects)
{
    const RECT target = destination != nullptr ? *destination : Bounds();
    if (flags & DDBLT_COLORFILL) {
        return Fill(target, effects != nullptr ? effects->dwFillColor : 0);
    }
    if (flags & DDBLT_DEPTHFILL) {
        static bool logged = false;
        LogOnce(logged, "depth fills are ignored");
        return DD_OK;
    }
    Surface* from = From(source);
    if (from == nullptr) {
        return DDERR_INVALIDPARAMS;
    }
    if (flags & (DDBLT_DDFX | DDBLT_ROP | DDBLT_KEYDEST | DDBLT_KEYDESTOVERRIDE | DDBLT_ALPHASRC | DDBLT_ALPHADEST)) {
        static bool logged = false;
        LogOnce(logged, std::format("Blt flags 0x{:08x} partly ignored", flags));
    }
    const RECT fromRect = sourceRect != nullptr ? *sourceRect : from->Bounds();
    LogKind(std::format("blit: {} {} -> {} {} (flags 0x{:x})", RoleName(from->m_role), Name(from->m_format),
                        RoleName(m_role), Name(m_format), flags));
    if (log::DebugEnabled()) {
        static int logged = 0;
        if (m_role != SurfaceRole::Primary && ++logged <= 80) {
            log::Debug("Renderer: blit {}x{} {} [{},{},{},{}] -> {}x{} {} [{},{},{},{}]", from->Width(), from->Height(),
                       Name(from->m_format), fromRect.left, fromRect.top, fromRect.right, fromRect.bottom, Width(),
                       Height(), Name(m_format), target.left, target.top, target.right, target.bottom);
        }
    }
    const bool key = ((flags & DDBLT_KEYSRC) != 0 && from->m_hasSourceKey) ||
                     ((flags & DDBLT_KEYSRCOVERRIDE) != 0 && effects != nullptr);
    if ((flags & DDBLT_KEYSRCOVERRIDE) && effects != nullptr) {
        from->m_sourceKey = effects->ddckSrcColorkey;
    }
    return CopyFrom(*from, fromRect, target, key);
}

HRESULT Surface::BltFast(DWORD x, DWORD y, LPDIRECTDRAWSURFACE7 source, LPRECT sourceRect, DWORD flags)
{
    Surface* from = From(source);
    if (from == nullptr) {
        return DDERR_INVALIDPARAMS;
    }
    const RECT fromRect = sourceRect != nullptr ? *sourceRect : from->Bounds();
    const RECT target{static_cast<LONG>(x), static_cast<LONG>(y), static_cast<LONG>(x) + fromRect.right - fromRect.left,
                      static_cast<LONG>(y) + fromRect.bottom - fromRect.top};
    return CopyFrom(*from, fromRect, target, (flags & DDBLTFAST_SRCCOLORKEY) != 0 && from->m_hasSourceKey);
}

HRESULT Surface::Fill(const RECT& rect, DWORD color) noexcept
{
    if (!Contains(Bounds(), rect)) {
        return DDERR_INVALIDRECT;
    }
    switch (m_role) {
    case SurfaceRole::Primary:
        return DD_OK;  // presentation clears the window
    case SurfaceRole::RenderTarget: {
        std::uint8_t raw[4]{};
        std::memcpy(raw, &color, sizeof raw);
        m_target->Fill(&rect, ReadArgb(m_format, raw));
        return DD_OK;
    }
    case SurfaceRole::Depth: {
        static bool logged = false;
        LogOnce(logged, "colour fills of depth buffers are ignored");
        return DD_OK;
    }
    default:
        break;
    }
    const std::uint32_t step = BytesPerPixel(m_format);
    if (step == 0) {
        return DDERR_UNSUPPORTED;
    }
    CpuLock lock(*this, false);
    if (!lock) {
        return DDERR_GENERIC;
    }
    for (LONG y = rect.top; y < rect.bottom; ++y) {
        std::uint8_t* row = lock.Bits() + std::size_t(y) * lock.Pitch();
        for (LONG x = rect.left; x < rect.right; ++x) {
            std::memcpy(row + std::size_t(x) * step, &color, step);
        }
    }
    return DD_OK;
}

HRESULT Surface::CopyFrom(Surface& source, const RECT& sourceRect, const RECT& destination, bool sourceKey) noexcept
{
    if (!Contains(source.Bounds(), sourceRect) || !Contains(Bounds(), destination)) {
        return DDERR_INVALIDRECT;
    }
    if (m_role == SurfaceRole::Primary) {
        if (source.m_role == SurfaceRole::RenderTarget) {
            return Present(source, sourceRect, destination);
        }
        static bool logged = false;
        LogOnce(logged, "blits from CPU surfaces to the primary surface are not shown");
        return DD_OK;
    }
    if (m_role == SurfaceRole::RenderTarget) {
        if (source.m_role == SurfaceRole::RenderTarget) {
            m_target->CopyFrom(*source.m_target, sourceRect, destination);
            return DD_OK;
        }
        if (sourceKey) {
            static bool logged = false;
            LogOnce(logged, "colour-keyed blits to render targets ignore the key");
        }
        CpuLock from(source, true);
        if (!from) {
            return DDERR_GENERIC;
        }
        m_target->Draw(from.Bits(), from.Pitch(), source.m_format, source.Width(), source.Height(), sourceRect,
                       destination);
        return DD_OK;
    }
    if (m_role == SurfaceRole::Depth || source.m_role == SurfaceRole::Depth) {
        static bool logged = false;
        LogOnce(logged, "blits between depth buffers are not supported");
        return DDERR_UNSUPPORTED;
    }

    // CPU destination (texture or system memory).
    std::vector<std::uint32_t> decoded;
    const HRESULT result = CopyPixels(source, sourceRect, destination, sourceKey, decoded);
    // A whole texture level copied without scaling keeps the precision its 16-bit format loses: the decoded pixels of
    // a compressed source, or the full-precision pixels of a texture source.
    const RECT sourceBounds = source.Bounds();
    const RECT bounds = Bounds();
    const bool wholeLevel = !sourceKey && EqualRect(&sourceRect, &sourceBounds) && EqualRect(&destination, &bounds);
    if (SUCCEEDED(result) && wholeLevel && m_role == SurfaceRole::Texture && GpuTexture() != nullptr) {
        if (!decoded.empty()) {
            (void)GpuTexture()->SetFullPrecision(m_level, decoded.data(),
                                                 static_cast<std::int32_t>(source.Width() * sizeof(std::uint32_t)));
        }
        else if (source.m_role == SurfaceRole::Texture && source.GpuTexture() != nullptr) {
            source.GpuTexture()->CopyFullPrecision(source.m_level, *GpuTexture(), m_level);
        }
        std::uint64_t hash = 0;
        if (!source.m_sourceName.empty() && m_level == 0 && GpuTexture()->TopLevelHash(hash)) {
            textures::RegisterSource(hash, {source.m_sourceName, nullptr});
        }
    }
    return result;
}

HRESULT Surface::CopyPixels(Surface& source, const RECT& sourceRect, const RECT& destination, bool sourceKey,
                            std::vector<std::uint32_t>& decoded) noexcept
{
    CpuLock from(source, true);
    CpuLock to(*this, false);
    if (!from || !to) {
        return DDERR_GENERIC;
    }
    const LONG sourceWidth = sourceRect.right - sourceRect.left;
    const LONG sourceHeight = sourceRect.bottom - sourceRect.top;
    const LONG width = destination.right - destination.left;
    const LONG height = destination.bottom - destination.top;
    if (source.m_format == m_format && sourceWidth == width && sourceHeight == height && !sourceKey) {
        const std::uint32_t rowBytes = RowBytes(m_format, static_cast<std::uint32_t>(width));
        const std::uint32_t rows = StorageRows(m_format, static_cast<std::uint32_t>(height));
        const LONG rowStep = IsBlockCompressed(m_format) ? 4 : 1;
        for (std::uint32_t row = 0; row < rows; ++row) {
            std::memcpy(to.Bits() + Offset(m_format, to.Pitch(), destination.left, destination.top + LONG(row) * rowStep),
                        from.Bits() + Offset(m_format, from.Pitch(), sourceRect.left, sourceRect.top + LONG(row) * rowStep),
                        rowBytes);
        }
        return DD_OK;
    }
    if (!CanConvert(source.m_format, m_format)) {
        static bool logged = false;
        LogOnce(logged, std::format("blit from {} to {} is not supported", Name(source.m_format), Name(m_format)));
        return DDERR_UNSUPPORTED;
    }
    // Block-compressed sources are decoded first (DirectDraw decompressed DXT surfaces in blits the same way).
    if (IsBlockCompressed(source.m_format)) {
        try {
            decoded.resize(std::size_t(sourceWidth) * sourceHeight);
        }
        catch (const std::bad_alloc&) {
            return DDERR_OUTOFMEMORY;
        }
        std::uint32_t block[16];
        for (LONG by = sourceRect.top & ~3; by < sourceRect.bottom; by += 4) {
            for (LONG bx = sourceRect.left & ~3; bx < sourceRect.right; bx += 4) {
                DecodeBlock(source.m_format, from.Bits() + Offset(source.m_format, from.Pitch(), bx, by), block);
                for (LONG py = 0; py < 4; ++py) {
                    for (LONG px = 0; px < 4; ++px) {
                        const LONG x = bx + px;
                        const LONG y = by + py;
                        if (x >= sourceRect.left && x < sourceRect.right && y >= sourceRect.top && y < sourceRect.bottom) {
                            decoded[std::size_t(y - sourceRect.top) * sourceWidth + (x - sourceRect.left)] = block[py * 4 + px];
                        }
                    }
                }
            }
        }
    }
    const std::uint32_t sourceStep = BytesPerPixel(source.m_format);
    auto sourcePixel = [&](LONG x, LONG y) -> std::uint32_t {
        const LONG sx = sourceRect.left + x * sourceWidth / width;
        const LONG sy = sourceRect.top + y * sourceHeight / height;
        if (!decoded.empty()) {
            return decoded[std::size_t(sy - sourceRect.top) * sourceWidth + (sx - sourceRect.left)];
        }
        return ReadArgb(source.m_format, from.Bits() + std::size_t(sy) * from.Pitch() + std::size_t(sx) * sourceStep);
    };
    if (IsBlockCompressed(m_format)) {
        // Compressing blit: whole 4x4 blocks of the destination, encoded from the (scaled) source.
        if ((destination.left | destination.top) & 3) {
            static bool logged = false;
            LogOnce(logged, "compressing blits to unaligned rectangles are not supported");
            return DDERR_UNSUPPORTED;
        }
        std::uint32_t pixels[16];
        for (LONG by = 0; by < height; by += 4) {
            for (LONG bx = 0; bx < width; bx += 4) {
                for (LONG i = 0; i < 16; ++i) {
                    pixels[i] = sourcePixel(std::min(bx + i % 4, width - 1), std::min(by + i / 4, height - 1));
                }
                EncodeBlock(m_format, pixels,
                            to.Bits() + Offset(m_format, to.Pitch(), destination.left + bx, destination.top + by));
            }
        }
        return DD_OK;
    }
    const std::uint32_t step = BytesPerPixel(m_format);
    for (LONG y = 0; y < height; ++y) {
        const LONG sy = sourceRect.top + y * sourceHeight / height;
        std::uint8_t* row = to.Bits() + std::size_t(destination.top + y) * to.Pitch();
        if (!decoded.empty()) {
            const std::uint32_t* sourceRow = decoded.data() + std::size_t(sy - sourceRect.top) * sourceWidth;
            for (LONG x = 0; x < width; ++x) {
                WriteArgb(m_format, sourceRow[x * sourceWidth / width], row + std::size_t(destination.left + x) * step);
            }
            continue;
        }
        const std::uint8_t* sourceRow = from.Bits() + std::size_t(sy) * from.Pitch();
        for (LONG x = 0; x < width; ++x) {
            const LONG sx = sourceRect.left + x * sourceWidth / width;
            const std::uint8_t* pixel = sourceRow + std::size_t(sx) * sourceStep;
            if (sourceKey) {
                DWORD raw = 0;
                std::memcpy(&raw, pixel, sourceStep);
                if (raw >= source.m_sourceKey.dwColorSpaceLowValue && raw <= source.m_sourceKey.dwColorSpaceHighValue) {
                    continue;
                }
            }
            WriteArgb(m_format, ReadArgb(source.m_format, pixel), row + std::size_t(destination.left + x) * step);
        }
    }
    return DD_OK;
}

HWND Surface::PresentationWindow() const noexcept
{
    if (m_clipper != nullptr) {
        if (const HWND window = static_cast<Clipper*>(m_clipper)->Window()) {
            return window;
        }
    }
    return m_owner.Window();
}

HRESULT Surface::Present(Surface& frame, const RECT& source, const RECT& destination) noexcept
{
    const HWND window = PresentationWindow();
    if (window == nullptr) {
        return DDERR_GENERIC;
    }
    // The primary surface is the whole screen: the game blits to the window's client area in screen coordinates.
    POINT topLeft{destination.left, destination.top};
    POINT bottomRight{destination.right, destination.bottom};
    ScreenToClient(window, &topLeft);
    ScreenToClient(window, &bottomRight);
    gpu::Present(*frame.m_target, source, window, RECT{topLeft.x, topLeft.y, bottomRight.x, bottomRight.y});
    return DD_OK;
}

HRESULT Surface::Flip(LPDIRECTDRAWSURFACE7 /*target*/, DWORD /*flags*/)
{
    static bool logged = false;
    LogOnce(logged, "Flip is not implemented (the game presents by Blt in windowed mode)");
    return DD_OK;
}

// ----------------------------------------------------------------------------------------- Attached surfaces

HRESULT Surface::AddAttachedSurface(LPDIRECTDRAWSURFACE7 surface)
{
    Surface* attached = From(surface);
    if (attached == nullptr || attached->m_role != SurfaceRole::Depth) {
        return DDERR_CANNOTATTACHSURFACE;
    }
    attached->AddRef();
    if (m_attachedDepth != nullptr) {
        m_attachedDepth->Release();
    }
    m_attachedDepth = attached;
    return DD_OK;
}

HRESULT Surface::DeleteAttachedSurface(DWORD /*flags*/, LPDIRECTDRAWSURFACE7 surface)
{
    Surface* attached = From(surface);
    if (m_attachedDepth == nullptr || (attached != nullptr && attached != m_attachedDepth)) {
        return DDERR_SURFACENOTATTACHED;
    }
    m_attachedDepth->Release();
    m_attachedDepth = nullptr;
    return DD_OK;
}

HRESULT Surface::GetAttachedSurface(LPDDSCAPS2 caps, LPDIRECTDRAWSURFACE7* out)
{
    if (caps == nullptr || out == nullptr) {
        return DDERR_INVALIDPARAMS;
    }
    *out = nullptr;
    Surface* found = nullptr;
    if ((caps->dwCaps & DDSCAPS_ZBUFFER) && m_attachedDepth != nullptr) {
        found = m_attachedDepth;
    }
    else if ((caps->dwCaps & DDSCAPS_MIPMAP) && m_textureOwner != nullptr &&
             m_level < m_textureOwner->m_mipLevels.size()) {
        found = m_textureOwner->m_mipLevels[m_level];
    }
    if (found == nullptr) {
        return DDERR_NOTFOUND;
    }
    found->AddRef();
    *out = found;
    return DD_OK;
}

// ------------------------------------------------------------------------------------------------- Queries

HRESULT Surface::GetBltStatus(DWORD)
{
    return DD_OK;
}

HRESULT Surface::GetFlipStatus(DWORD)
{
    return DD_OK;
}

HRESULT Surface::GetCaps(LPDDSCAPS2 caps)
{
    if (caps == nullptr) {
        return DDERR_INVALIDPARAMS;
    }
    *caps = m_desc.ddsCaps;
    return DD_OK;
}

HRESULT Surface::GetClipper(LPDIRECTDRAWCLIPPER* out)
{
    if (out == nullptr) {
        return DDERR_INVALIDPARAMS;
    }
    *out = m_clipper;
    if (m_clipper == nullptr) {
        return DDERR_NOCLIPPERATTACHED;
    }
    m_clipper->AddRef();
    return DD_OK;
}

HRESULT Surface::SetClipper(LPDIRECTDRAWCLIPPER clipper)
{
    if (clipper != nullptr) {
        clipper->AddRef();
    }
    if (m_clipper != nullptr) {
        m_clipper->Release();
    }
    m_clipper = clipper;
    return DD_OK;
}

HRESULT Surface::GetColorKey(DWORD flags, LPDDCOLORKEY key)
{
    if (key == nullptr) {
        return DDERR_INVALIDPARAMS;
    }
    if ((flags & DDCKEY_SRCBLT) && m_hasSourceKey) {
        *key = m_sourceKey;
        return DD_OK;
    }
    return DDERR_NOCOLORKEY;
}

HRESULT Surface::SetColorKey(DWORD flags, LPDDCOLORKEY key)
{
    if (flags & DDCKEY_SRCBLT) {
        m_hasSourceKey = key != nullptr;
        if (key != nullptr) {
            m_sourceKey = *key;
            if ((flags & DDCKEY_COLORSPACE) == 0) {
                m_sourceKey.dwColorSpaceHighValue = m_sourceKey.dwColorSpaceLowValue;
            }
        }
        if (m_role == SurfaceRole::Texture && key != nullptr) {
            static bool logged = false;
            LogOnce(logged, "colour-keyed textures are drawn without their key");
        }
        return DD_OK;
    }
    static bool logged = false;
    LogOnce(logged, std::format("colour key type 0x{:x} ignored", flags));
    return DD_OK;
}

HRESULT Surface::GetPalette(LPDIRECTDRAWPALETTE* out)
{
    if (out != nullptr) {
        *out = nullptr;
    }
    return DDERR_NOPALETTEATTACHED;
}

HRESULT Surface::GetPixelFormat(LPDDPIXELFORMAT format)
{
    if (format == nullptr) {
        return DDERR_INVALIDPARAMS;
    }
    *format = m_desc.ddpfPixelFormat;
    return DD_OK;
}

HRESULT Surface::GetSurfaceDesc(LPDDSURFACEDESC2 desc)
{
    if (desc == nullptr) {
        return DDERR_INVALIDPARAMS;
    }
    *desc = m_desc;
    return DD_OK;
}

HRESULT Surface::Initialize(LPDIRECTDRAW, LPDDSURFACEDESC2)
{
    return DDERR_ALREADYINITIALIZED;
}

HRESULT Surface::IsLost()
{
    return DD_OK;
}

HRESULT Surface::Restore()
{
    return DD_OK;
}

HRESULT Surface::GetDDInterface(LPVOID* out)
{
    if (out == nullptr) {
        return DDERR_INVALIDPARAMS;
    }
    *out = static_cast<IDirectDraw7*>(&m_owner);
    static_cast<IDirectDraw7&>(m_owner).AddRef();
    return DD_OK;
}

HRESULT Surface::PageLock(DWORD)
{
    return DD_OK;
}

HRESULT Surface::PageUnlock(DWORD)
{
    return DD_OK;
}

HRESULT Surface::SetPriority(DWORD priority)
{
    m_priority = priority;
    return DD_OK;
}

HRESULT Surface::GetPriority(LPDWORD priority)
{
    if (priority == nullptr) {
        return DDERR_INVALIDPARAMS;
    }
    *priority = m_priority;
    return DD_OK;
}

HRESULT Surface::SetLOD(DWORD lod)
{
    m_lod = lod;
    return DD_OK;
}

HRESULT Surface::GetLOD(LPDWORD lod)
{
    if (lod == nullptr) {
        return DDERR_INVALIDPARAMS;
    }
    *lod = m_lod;
    return DD_OK;
}

HRESULT Surface::LoadFrom(Surface& source) noexcept
{
    if (source.m_role != SurfaceRole::Texture || m_role != SurfaceRole::Texture || source.Width() != Width() ||
        source.Height() != Height()) {
        return DDERR_INVALIDPARAMS;
    }
    Surface* to = this;
    Surface* from = &source;
    for (std::size_t level = 0; to != nullptr && from != nullptr; ++level) {
        if (const HRESULT result = to->CopyFrom(*from, from->Bounds(), to->Bounds(), false); FAILED(result)) {
            return result;
        }
        to = level < m_mipLevels.size() ? m_mipLevels[level] : nullptr;
        from = level < source.m_mipLevels.size() ? source.m_mipLevels[level] : nullptr;
    }
    return D3D_OK;
}

HRESULT TextureView::QueryInterface(REFIID iid, LPVOID* out)
{
    return m_owner.QueryInterface(iid, out);
}

ULONG TextureView::AddRef()
{
    return m_owner.AddRef();
}

ULONG TextureView::Release()
{
    return m_owner.Release();
}

HRESULT TextureView::GetHandle(LPDIRECT3DDEVICE2, LPD3DTEXTUREHANDLE handle)
{
    if (handle == nullptr) {
        return DDERR_INVALIDPARAMS;
    }
    *handle = static_cast<D3DTEXTUREHANDLE>(reinterpret_cast<std::uintptr_t>(&m_owner));
    return D3D_OK;
}

HRESULT TextureView::PaletteChanged(DWORD, DWORD)
{
    return D3D_OK;
}

HRESULT TextureView::Load(LPDIRECT3DTEXTURE2 source)
{
    if (source == nullptr) {
        return DDERR_INVALIDPARAMS;
    }
    static bool logged = false;
    LogOnce(logged, "IDirect3DTexture2::Load used");
    return m_owner.LoadFrom(static_cast<TextureView*>(source)->m_owner);
}

// --------------------------------------------------------------------------------------- Older interfaces

namespace {

template <typename Interface>
Interface* ViewOf(Surface& surface) noexcept
{
    if constexpr (std::is_same_v<Interface, IDirectDrawSurface>) {
        return &surface.V1();
    }
    else if constexpr (std::is_same_v<Interface, IDirectDrawSurface2>) {
        return &surface.V2();
    }
    else if constexpr (std::is_same_v<Interface, IDirectDrawSurface3>) {
        return &surface.V3();
    }
    else {
        return &surface.V4();
    }
}

template <typename Desc>
void FromDesc2(const DDSURFACEDESC2& from, Desc& to) noexcept
{
    if constexpr (std::is_same_v<Desc, DDSURFACEDESC2>) {
        to = from;
    }
    else {
        ToSurfaceDesc(from, to);
    }
}

}  // namespace

#define BW_SURFACE_VIEW(Return, Name)                                                                              \
    template <typename Interface, typename Desc, typename Caps, typename EnumCallback, typename UnlockArgument>    \
    Return STDMETHODCALLTYPE SurfaceView<Interface, Desc, Caps, EnumCallback, UnlockArgument>::Name

BW_SURFACE_VIEW(HRESULT, QueryInterface)(REFIID iid, LPVOID* out)
{
    return m_owner.QueryInterface(iid, out);
}

BW_SURFACE_VIEW(ULONG, AddRef)()
{
    return m_owner.AddRef();
}

BW_SURFACE_VIEW(ULONG, Release)()
{
    return m_owner.Release();
}

BW_SURFACE_VIEW(HRESULT, AddAttachedSurface)(Interface* surface)
{
    return m_owner.AddAttachedSurface(Surface::From(surface));
}

BW_SURFACE_VIEW(HRESULT, Blt)(LPRECT destination, Interface* source, LPRECT sourceRect, DWORD flags, LPDDBLTFX effects)
{
    return m_owner.Blt(destination, Surface::From(source), sourceRect, flags, effects);
}

BW_SURFACE_VIEW(HRESULT, BltFast)(DWORD x, DWORD y, Interface* source, LPRECT sourceRect, DWORD flags)
{
    return m_owner.BltFast(x, y, Surface::From(source), sourceRect, flags);
}

BW_SURFACE_VIEW(HRESULT, DeleteAttachedSurface)(DWORD flags, Interface* surface)
{
    return m_owner.DeleteAttachedSurface(flags, Surface::From(surface));
}

BW_SURFACE_VIEW(HRESULT, Flip)(Interface* target, DWORD flags)
{
    return m_owner.Flip(Surface::From(target), flags);
}

BW_SURFACE_VIEW(HRESULT, GetAttachedSurface)(Caps* caps, Interface** out)
{
    if (caps == nullptr || out == nullptr) {
        return DDERR_INVALIDPARAMS;
    }
    DDSCAPS2 caps2{};
    caps2.dwCaps = caps->dwCaps;
    LPDIRECTDRAWSURFACE7 found = nullptr;
    const HRESULT result = m_owner.GetAttachedSurface(&caps2, &found);
    *out = SUCCEEDED(result) ? ViewOf<Interface>(*Surface::From(found)) : nullptr;
    return result;
}

BW_SURFACE_VIEW(HRESULT, GetBltStatus)(DWORD flags)
{
    return m_owner.GetBltStatus(flags);
}

BW_SURFACE_VIEW(HRESULT, GetCaps)(Caps* caps)
{
    if (caps == nullptr) {
        return DDERR_INVALIDPARAMS;
    }
    DDSCAPS2 caps2{};
    m_owner.GetCaps(&caps2);
    if constexpr (std::is_same_v<Caps, DDSCAPS2>) {
        *caps = caps2;
    }
    else {
        caps->dwCaps = caps2.dwCaps;
    }
    return DD_OK;
}

BW_SURFACE_VIEW(HRESULT, GetClipper)(LPDIRECTDRAWCLIPPER* out)
{
    return m_owner.GetClipper(out);
}

BW_SURFACE_VIEW(HRESULT, GetColorKey)(DWORD flags, LPDDCOLORKEY key)
{
    return m_owner.GetColorKey(flags, key);
}

BW_SURFACE_VIEW(HRESULT, GetFlipStatus)(DWORD flags)
{
    return m_owner.GetFlipStatus(flags);
}

BW_SURFACE_VIEW(HRESULT, GetPalette)(LPDIRECTDRAWPALETTE* out)
{
    return m_owner.GetPalette(out);
}

BW_SURFACE_VIEW(HRESULT, GetPixelFormat)(LPDDPIXELFORMAT format)
{
    return m_owner.GetPixelFormat(format);
}

BW_SURFACE_VIEW(HRESULT, GetSurfaceDesc)(Desc* desc)
{
    if (desc == nullptr) {
        return DDERR_INVALIDPARAMS;
    }
    FromDesc2(m_owner.Description(), *desc);
    return DD_OK;
}

BW_SURFACE_VIEW(HRESULT, Initialize)(LPDIRECTDRAW, Desc*)
{
    return DDERR_ALREADYINITIALIZED;
}

BW_SURFACE_VIEW(HRESULT, IsLost)()
{
    return DD_OK;
}

BW_SURFACE_VIEW(HRESULT, Lock)(LPRECT rect, Desc* desc, DWORD flags, HANDLE event)
{
    if (desc == nullptr) {
        return DDERR_INVALIDPARAMS;
    }
    DDSURFACEDESC2 desc2{};
    desc2.dwSize = sizeof desc2;
    const HRESULT result = m_owner.Lock(rect, &desc2, flags, event);
    if (SUCCEEDED(result)) {
        FromDesc2(desc2, *desc);
    }
    return result;
}

BW_SURFACE_VIEW(HRESULT, Restore)()
{
    return DD_OK;
}

BW_SURFACE_VIEW(HRESULT, SetClipper)(LPDIRECTDRAWCLIPPER clipper)
{
    return m_owner.SetClipper(clipper);
}

BW_SURFACE_VIEW(HRESULT, SetColorKey)(DWORD flags, LPDDCOLORKEY key)
{
    return m_owner.SetColorKey(flags, key);
}

BW_SURFACE_VIEW(HRESULT, Unlock)(UnlockArgument)
{
    return m_owner.Unlock(nullptr);
}

BW_SURFACE_VIEW(HRESULT, GetDDInterface)(LPVOID* out)
{
    return m_owner.GetDDInterface(out);
}

BW_SURFACE_VIEW(HRESULT, PageLock)(DWORD flags)
{
    return m_owner.PageLock(flags);
}

BW_SURFACE_VIEW(HRESULT, PageUnlock)(DWORD flags)
{
    return m_owner.PageUnlock(flags);
}

#undef BW_SURFACE_VIEW

template class SurfaceView<IDirectDrawSurface, DDSURFACEDESC, DDSCAPS, LPDDENUMSURFACESCALLBACK, LPVOID>;
template class SurfaceView<IDirectDrawSurface2, DDSURFACEDESC, DDSCAPS, LPDDENUMSURFACESCALLBACK, LPVOID>;
template class SurfaceView<IDirectDrawSurface3, DDSURFACEDESC, DDSCAPS, LPDDENUMSURFACESCALLBACK, LPVOID>;
template class SurfaceView<IDirectDrawSurface4, DDSURFACEDESC2, DDSCAPS2, LPDDENUMSURFACESCALLBACK2, LPRECT>;

}  // namespace bw::render
