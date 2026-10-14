#include "render/Translate.h"

#include <bit>

namespace bw::render {

namespace {

// Direct3D 7 values (d3dtypes.h).
constexpr std::uint32_t kRsAntialias = 2;
constexpr std::uint32_t kRsTexturePerspective = 4;
constexpr std::uint32_t kRsLinePattern = 10;
constexpr std::uint32_t kRsZVisible = 30;
constexpr std::uint32_t kRsStippledAlpha = 33;
constexpr std::uint32_t kRsEdgeAntialias = 40;
constexpr std::uint32_t kRsColorKeyEnable = 41;
constexpr std::uint32_t kRsZBias = 47;
constexpr std::uint32_t kRsExtents = 138;
constexpr std::uint32_t kRsColorKeyBlendEnable = 144;

constexpr std::uint32_t kTssAddress = 12;
constexpr std::uint32_t kTssAddressU = 13;
constexpr std::uint32_t kTssAddressV = 14;
constexpr std::uint32_t kTssBorderColor = 15;
constexpr std::uint32_t kTssMagFilter = 16;
constexpr std::uint32_t kTssMinFilter = 17;
constexpr std::uint32_t kTssMipFilter = 18;
constexpr std::uint32_t kTssMipMapLodBias = 19;
constexpr std::uint32_t kTssMaxMipLevel = 20;
constexpr std::uint32_t kTssMaxAnisotropy = 21;

/// One Direct3D 7 ZBIAS step in Direct3D 9 DEPTHBIAS units: one step of the 16-bit depth buffers the game was
/// tuned for.
constexpr float kZBiasStep = 1.0f / 65535.0f;

constexpr StateWrites Single(StateWrite::Kind kind, std::uint32_t state, std::uint32_t value) noexcept
{
    return {StateWrite{kind, state, value}, StateWrite{}};
}

constexpr std::uint32_t MagFilter(std::uint32_t value) noexcept
{
    switch (value) {
    case 1:  // D3DTFG_POINT
        return d3d9::kTexfPoint;
    case 5:  // D3DTFG_ANISOTROPIC
        return d3d9::kTexfAnisotropic;
    default:  // LINEAR, FLATCUBIC, GAUSSIANCUBIC
        return d3d9::kTexfLinear;
    }
}

constexpr std::uint32_t MinFilter(std::uint32_t value) noexcept
{
    switch (value) {
    case 1:  // D3DTFN_POINT
        return d3d9::kTexfPoint;
    case 3:  // D3DTFN_ANISOTROPIC
        return d3d9::kTexfAnisotropic;
    default:
        return d3d9::kTexfLinear;
    }
}

constexpr std::uint32_t MipFilter(std::uint32_t value) noexcept
{
    switch (value) {
    case 2:  // D3DTFP_POINT
        return d3d9::kTexfPoint;
    case 3:  // D3DTFP_LINEAR
        return d3d9::kTexfLinear;
    default:  // D3DTFP_NONE
        return d3d9::kTexfNone;
    }
}

}  // namespace

bool IsRetiredRenderState(std::uint32_t state) noexcept
{
    switch (state) {
    case kRsAntialias:
    case kRsTexturePerspective:
    case kRsLinePattern:
    case kRsZVisible:
    case kRsStippledAlpha:
    case kRsEdgeAntialias:
    case kRsColorKeyEnable:
    case kRsExtents:
    case kRsColorKeyBlendEnable:
        return true;
    default:
        // Pre-DirectX 7 states (texture handles, legacy filters, stipple patterns) are invalid on a DX7 device.
        return state < 7 || (state >= 11 && state <= 13) || state == 17 || state == 18 || state == 21 ||
               state == 31 || state == 32 || state == 39 || (state >= 43 && state <= 46) || state == 49 ||
               state == 50 || state == 51 || (state >= 64 && state <= 95);
    }
}

StateWrites TranslateRenderState(std::uint32_t state, std::uint32_t value) noexcept
{
    using enum StateWrite::Kind;
    if (state == kRsZBias) {
        const float bias = -static_cast<float>(value) * kZBiasStep;
        return Single(RenderState, d3d9::kRsDepthBias, std::bit_cast<std::uint32_t>(bias));
    }
    if (IsRetiredRenderState(state)) {
        return {};
    }
    // Every other Direct3D 7 render state kept its number in Direct3D 9.
    return Single(RenderState, state, value);
}

StateWrites TranslateTextureStageState(std::uint32_t type, std::uint32_t value) noexcept
{
    using enum StateWrite::Kind;
    switch (type) {
    case kTssAddress:
        return {StateWrite{SamplerState, d3d9::kSampAddressU, value},
                StateWrite{SamplerState, d3d9::kSampAddressV, value}};
    case kTssAddressU:
        return Single(SamplerState, d3d9::kSampAddressU, value);
    case kTssAddressV:
        return Single(SamplerState, d3d9::kSampAddressV, value);
    case kTssBorderColor:
        return Single(SamplerState, d3d9::kSampBorderColor, value);
    case kTssMagFilter:
        return Single(SamplerState, d3d9::kSampMagFilter, MagFilter(value));
    case kTssMinFilter:
        return Single(SamplerState, d3d9::kSampMinFilter, MinFilter(value));
    case kTssMipFilter:
        return Single(SamplerState, d3d9::kSampMipFilter, MipFilter(value));
    case kTssMipMapLodBias:
        return Single(SamplerState, d3d9::kSampMipMapLodBias, value);
    case kTssMaxMipLevel:
        return Single(SamplerState, d3d9::kSampMaxMipLevel, value);
    case kTssMaxAnisotropy:
        return Single(SamplerState, d3d9::kSampMaxAnisotropy, value);
    default:
        // COLOROP .. TEXCOORDINDEX (1-11) and BUMPENVLSCALE .. TEXTURETRANSFORMFLAGS (22-24) are unchanged.
        if ((type >= 1 && type <= 11) || (type >= 22 && type <= 24)) {
            return Single(TextureStageState, type, value);
        }
        return {};
    }
}

std::uint32_t TranslateTransform(std::uint32_t state) noexcept
{
    switch (state) {
    case 1:  // WORLD
        return d3d9::kTsWorld;
    case 2:  // VIEW
    case 3:  // PROJECTION
        return state;
    case 4:  // WORLD1..3
    case 5:
    case 6:
        return d3d9::kTsWorld + state - 3;
    default:
        return state >= 16 && state <= 23 ? state : 0;  // TEXTURE0..7
    }
}

std::uint32_t VertexStride(std::uint32_t fvf) noexcept
{
    std::uint32_t size = 0;
    switch (fvf & 0x00e) {  // D3DFVF_POSITION_MASK
    case 0x002:             // XYZ
        size = 12;
        break;
    case 0x004:  // XYZRHW
        size = 16;
        break;
    case 0x006:  // XYZB1
    case 0x008:
    case 0x00a:
    case 0x00c:
    case 0x00e:  // XYZB5
        size = 12 + 4 * (((fvf & 0x00e) - 0x004) / 2);
        break;
    default:
        break;
    }
    if (fvf & 0x010) {  // NORMAL
        size += 12;
    }
    if (fvf & 0x020) {  // RESERVED1 (Direct3D 9: PSIZE)
        size += 4;
    }
    if (fvf & 0x040) {  // DIFFUSE
        size += 4;
    }
    if (fvf & 0x080) {  // SPECULAR
        size += 4;
    }
    const std::uint32_t textures = (fvf >> 8) & 0xf;
    for (std::uint32_t index = 0; index < textures; ++index) {
        // D3DFVF_TEXCOORDSIZEn: 0 = 2 floats, 1 = 3, 2 = 4, 3 = 1.
        constexpr std::uint32_t kFloats[] = {2, 3, 4, 1};
        size += 4 * kFloats[(fvf >> (16 + 2 * index)) & 3];
    }
    return size;
}

bool IsUnlitFormat(std::uint32_t fvf) noexcept
{
    const bool transformed = (fvf & 0x00e) == 0x004;
    return !transformed && (fvf & 0x010) == 0;
}

std::uint32_t PrimitiveCount(std::uint32_t type, std::uint32_t count) noexcept
{
    switch (type) {
    case 1:  // POINTLIST
        return count;
    case 2:  // LINELIST
        return count / 2;
    case 3:  // LINESTRIP
        return count > 1 ? count - 1 : 0;
    case 4:  // TRIANGLELIST
        return count / 3;
    case 5:  // TRIANGLESTRIP
    case 6:  // TRIANGLEFAN
        return count > 2 ? count - 2 : 0;
    default:
        return 0;
    }
}

}  // namespace bw::render
