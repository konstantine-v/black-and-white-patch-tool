#include "render/Com.h"

#include "core/Log.h"

#include <cstring>

namespace bw::render {

HRESULT Unsupported(const char* method, bool& logged) noexcept
{
    if (!logged) {
        logged = true;
        log::Warning("Renderer: {} is not implemented", method);
    }
    return DDERR_UNSUPPORTED;
}

Format FromPixelFormat(const DDPIXELFORMAT& format) noexcept
{
    const DWORD flags = format.dwFlags;
    if (flags & DDPF_FOURCC) {
        switch (format.dwFourCC) {
        case MAKEFOURCC('D', 'X', 'T', '1'):
            return Format::Dxt1;
        case MAKEFOURCC('D', 'X', 'T', '2'):
            return Format::Dxt2;
        case MAKEFOURCC('D', 'X', 'T', '3'):
            return Format::Dxt3;
        case MAKEFOURCC('D', 'X', 'T', '4'):
            return Format::Dxt4;
        case MAKEFOURCC('D', 'X', 'T', '5'):
            return Format::Dxt5;
        default:
            return Format::Unknown;
        }
    }
    if (flags & DDPF_ZBUFFER) {
        const DWORD depth = format.dwZBufferBitDepth;
        const DWORD stencil = (flags & DDPF_STENCILBUFFER) ? format.dwStencilBitDepth : 0;
        if (depth == 16) {
            return Format::D16;
        }
        if (stencil != 0) {
            return Format::D24S8;
        }
        if (depth == 24 || (depth == 32 && format.dwZBitMask == 0x00ffffff) || (depth == 32 && format.dwZBitMask == 0xffffff00)) {
            return Format::D24X8;
        }
        return depth == 32 ? Format::D32 : Format::Unknown;
    }
    if (flags & DDPF_BUMPDUDV) {
        return (flags & DDPF_BUMPLUMINANCE) ? Format::X8L8V8U8 : Format::V8U8;
    }
    if (flags & DDPF_LUMINANCE) {
        return (flags & DDPF_ALPHAPIXELS) ? Format::A8L8 : Format::L8;
    }
    if ((flags & DDPF_ALPHA) && !(flags & DDPF_RGB)) {
        return Format::A8;
    }
    if (flags & DDPF_RGB) {
        const bool alpha = (flags & DDPF_ALPHAPIXELS) != 0 && format.dwRGBAlphaBitMask != 0;
        switch (format.dwRGBBitCount) {
        case 16:
            if (format.dwRBitMask == 0xf800) {
                return Format::R5G6B5;
            }
            if (format.dwRBitMask == 0x7c00) {
                return alpha ? Format::A1R5G5B5 : Format::X1R5G5B5;
            }
            if (format.dwRBitMask == 0x0f00) {
                return Format::A4R4G4B4;
            }
            return Format::Unknown;
        case 24:
            return Format::R8G8B8;
        case 32:
            return format.dwRBitMask == 0x00ff0000 ? (alpha ? Format::A8R8G8B8 : Format::X8R8G8B8) : Format::Unknown;
        default:
            return Format::Unknown;
        }
    }
    return Format::Unknown;
}

DDPIXELFORMAT ToPixelFormat(Format format) noexcept
{
    DDPIXELFORMAT pf{};
    pf.dwSize = sizeof pf;
    auto rgb = [&](DWORD bits, DWORD r, DWORD g, DWORD b, DWORD a) {
        pf.dwFlags = DDPF_RGB | (a != 0 ? DDPF_ALPHAPIXELS : 0);
        pf.dwRGBBitCount = bits;
        pf.dwRBitMask = r;
        pf.dwGBitMask = g;
        pf.dwBBitMask = b;
        pf.dwRGBAlphaBitMask = a;
    };
    auto depth = [&](DWORD bits, DWORD mask, DWORD stencilBits, DWORD stencilMask) {
        pf.dwFlags = DDPF_ZBUFFER | (stencilBits != 0 ? DDPF_STENCILBUFFER : 0);
        pf.dwZBufferBitDepth = bits;
        pf.dwStencilBitDepth = stencilBits;
        pf.dwZBitMask = mask;
        pf.dwStencilBitMask = stencilMask;
    };
    auto fourCC = [&](DWORD code) {
        pf.dwFlags = DDPF_FOURCC;
        pf.dwFourCC = code;
    };
    switch (format) {
    case Format::R5G6B5:
        rgb(16, 0xf800, 0x07e0, 0x001f, 0);
        break;
    case Format::X1R5G5B5:
        rgb(16, 0x7c00, 0x03e0, 0x001f, 0);
        break;
    case Format::A1R5G5B5:
        rgb(16, 0x7c00, 0x03e0, 0x001f, 0x8000);
        break;
    case Format::A4R4G4B4:
        rgb(16, 0x0f00, 0x00f0, 0x000f, 0xf000);
        break;
    case Format::R8G8B8:
        rgb(24, 0xff0000, 0x00ff00, 0x0000ff, 0);
        break;
    case Format::X8R8G8B8:
        rgb(32, 0xff0000, 0x00ff00, 0x0000ff, 0);
        break;
    case Format::A8R8G8B8:
        rgb(32, 0xff0000, 0x00ff00, 0x0000ff, 0xff000000);
        break;
    case Format::L8:
        pf.dwFlags = DDPF_LUMINANCE;
        pf.dwLuminanceBitCount = 8;
        pf.dwLuminanceBitMask = 0xff;
        break;
    case Format::A8L8:
        pf.dwFlags = DDPF_LUMINANCE | DDPF_ALPHAPIXELS;
        pf.dwLuminanceBitCount = 16;
        pf.dwLuminanceBitMask = 0x00ff;
        pf.dwLuminanceAlphaBitMask = 0xff00;
        break;
    case Format::A8:
        pf.dwFlags = DDPF_ALPHA;
        pf.dwAlphaBitDepth = 8;
        break;
    case Format::V8U8:
        pf.dwFlags = DDPF_BUMPDUDV;
        pf.dwBumpBitCount = 16;
        pf.dwBumpDuBitMask = 0x00ff;
        pf.dwBumpDvBitMask = 0xff00;
        break;
    case Format::X8L8V8U8:
        pf.dwFlags = DDPF_BUMPDUDV | DDPF_BUMPLUMINANCE;
        pf.dwBumpBitCount = 32;
        pf.dwBumpDuBitMask = 0x0000ff;
        pf.dwBumpDvBitMask = 0x00ff00;
        pf.dwBumpLuminanceBitMask = 0xff0000;
        break;
    case Format::Dxt1:
        fourCC(MAKEFOURCC('D', 'X', 'T', '1'));
        break;
    case Format::Dxt2:
        fourCC(MAKEFOURCC('D', 'X', 'T', '2'));
        break;
    case Format::Dxt3:
        fourCC(MAKEFOURCC('D', 'X', 'T', '3'));
        break;
    case Format::Dxt4:
        fourCC(MAKEFOURCC('D', 'X', 'T', '4'));
        break;
    case Format::Dxt5:
        fourCC(MAKEFOURCC('D', 'X', 'T', '5'));
        break;
    case Format::D16:
        depth(16, 0xffff, 0, 0);
        break;
    case Format::D24X8:
        depth(32, 0xffffff00, 0, 0);
        break;
    case Format::D24S8:
        depth(32, 0xffffff00, 8, 0x000000ff);
        break;
    case Format::D32:
        depth(32, 0xffffffff, 0, 0);
        break;
    default:
        break;
    }
    return pf;
}

void ToSurfaceDesc(const DDSURFACEDESC2& from, DDSURFACEDESC& to) noexcept
{
    to = {};
    to.dwSize = sizeof to;
    to.dwFlags = from.dwFlags & ~static_cast<DWORD>(DDSD_TEXTURESTAGE | DDSD_FVF | DDSD_SRCVBHANDLE | DDSD_DEPTH);
    to.dwHeight = from.dwHeight;
    to.dwWidth = from.dwWidth;
    to.lPitch = from.lPitch;
    to.dwBackBufferCount = from.dwBackBufferCount;
    to.dwMipMapCount = from.dwMipMapCount;
    to.dwAlphaBitDepth = from.dwAlphaBitDepth;
    to.lpSurface = from.lpSurface;
    to.ddckCKDestOverlay = from.ddckCKDestOverlay;
    to.ddckCKDestBlt = from.ddckCKDestBlt;
    to.ddckCKSrcOverlay = from.ddckCKSrcOverlay;
    to.ddckCKSrcBlt = from.ddckCKSrcBlt;
    to.ddpfPixelFormat = from.ddpfPixelFormat;
    to.ddsCaps.dwCaps = from.ddsCaps.dwCaps;
}

DDSURFACEDESC2 FromSurfaceDesc(const DDSURFACEDESC& from) noexcept
{
    DDSURFACEDESC2 to{};
    to.dwSize = sizeof to;
    to.dwFlags = from.dwFlags;
    to.dwHeight = from.dwHeight;
    to.dwWidth = from.dwWidth;
    to.lPitch = from.lPitch;
    to.dwBackBufferCount = from.dwBackBufferCount;
    to.dwMipMapCount = from.dwMipMapCount;
    to.dwAlphaBitDepth = from.dwAlphaBitDepth;
    to.lpSurface = from.lpSurface;
    to.ddckCKDestOverlay = from.ddckCKDestOverlay;
    to.ddckCKDestBlt = from.ddckCKDestBlt;
    to.ddckCKSrcOverlay = from.ddckCKSrcOverlay;
    to.ddckCKSrcBlt = from.ddckCKSrcBlt;
    to.ddpfPixelFormat = from.ddpfPixelFormat;
    to.ddsCaps.dwCaps = from.ddsCaps.dwCaps;
    return to;
}

}  // namespace bw::render
