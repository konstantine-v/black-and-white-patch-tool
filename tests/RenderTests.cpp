// Tests for the Direct3D 9 renderer's pure translation code.

#include "Test.h"

#include "render/Com.h"
#include "render/Pixels.h"
#include "render/TexturePack.h"
#include "render/Translate.h"

#include <bit>
#include <cstring>

using namespace bw::render;

TEST(RenderStatesKeepTheirNumbers)
{
    const StateWrites writes = TranslateRenderState(D3DRENDERSTATE_ALPHABLENDENABLE, TRUE);
    CHECK(writes[0] == (StateWrite{StateWrite::Kind::RenderState, 27, TRUE}));
    CHECK(writes[1].kind == StateWrite::Kind::None);
    CHECK(TranslateRenderState(D3DRENDERSTATE_LIGHTING, FALSE)[0].state == d3d9::kRsLighting);
}

TEST(RetiredRenderStatesAreDropped)
{
    CHECK(TranslateRenderState(D3DRENDERSTATE_TEXTUREPERSPECTIVE, TRUE)[0].kind == StateWrite::Kind::None);
    CHECK(TranslateRenderState(D3DRENDERSTATE_COLORKEYENABLE, TRUE)[0].kind == StateWrite::Kind::None);
    CHECK(TranslateRenderState(D3DRENDERSTATE_ANTIALIAS, 1)[0].kind == StateWrite::Kind::None);
    CHECK(IsRetiredRenderState(D3DRENDERSTATE_EXTENTS));
    CHECK(!IsRetiredRenderState(D3DRENDERSTATE_ZENABLE));
}

TEST(ZBiasBecomesNegativeDepthBias)
{
    const StateWrite write = TranslateRenderState(D3DRENDERSTATE_ZBIAS, 2)[0];
    CHECK_EQ(write.state, d3d9::kRsDepthBias);
    CHECK(std::bit_cast<float>(write.value) < 0.0f);
    CHECK_EQ(std::bit_cast<float>(TranslateRenderState(D3DRENDERSTATE_ZBIAS, 0)[0].value), 0.0f);
}

TEST(AddressingAndFiltersMoveToSamplers)
{
    const StateWrites address = TranslateTextureStageState(D3DTSS_ADDRESS, D3DTADDRESS_CLAMP);
    CHECK(address[0] == (StateWrite{StateWrite::Kind::SamplerState, d3d9::kSampAddressU, D3DTADDRESS_CLAMP}));
    CHECK(address[1] == (StateWrite{StateWrite::Kind::SamplerState, d3d9::kSampAddressV, D3DTADDRESS_CLAMP}));
    CHECK_EQ(TranslateTextureStageState(D3DTSS_MAGFILTER, D3DTFG_LINEAR)[0].value, d3d9::kTexfLinear);
    CHECK_EQ(TranslateTextureStageState(D3DTSS_MAGFILTER, D3DTFG_ANISOTROPIC)[0].value, d3d9::kTexfAnisotropic);
    CHECK_EQ(TranslateTextureStageState(D3DTSS_MINFILTER, D3DTFN_POINT)[0].value, d3d9::kTexfPoint);
    CHECK_EQ(TranslateTextureStageState(D3DTSS_MIPFILTER, D3DTFP_NONE)[0].value, d3d9::kTexfNone);
    CHECK_EQ(TranslateTextureStageState(D3DTSS_MIPFILTER, D3DTFP_LINEAR)[0].value, d3d9::kTexfLinear);
    CHECK(TranslateTextureStageState(D3DTSS_COLOROP, D3DTOP_MODULATE)[0] ==
          (StateWrite{StateWrite::Kind::TextureStageState, 1, D3DTOP_MODULATE}));
}

TEST(TransformsMapWorldToWorldMatrixZero)
{
    CHECK_EQ(TranslateTransform(D3DTRANSFORMSTATE_WORLD), 256u);
    CHECK_EQ(TranslateTransform(D3DTRANSFORMSTATE_VIEW), 2u);
    CHECK_EQ(TranslateTransform(D3DTRANSFORMSTATE_PROJECTION), 3u);
    CHECK_EQ(TranslateTransform(D3DTRANSFORMSTATE_WORLD1), 257u);
    CHECK_EQ(TranslateTransform(D3DTRANSFORMSTATE_TEXTURE3), 19u);
    CHECK_EQ(TranslateTransform(99), 0u);
}

TEST(VertexStrides)
{
    CHECK_EQ(VertexStride(D3DFVF_TLVERTEX), 32u);  // XYZRHW, DIFFUSE, SPECULAR, TEX1
    CHECK_EQ(VertexStride(D3DFVF_LVERTEX), 32u);   // XYZ, RESERVED1, DIFFUSE, SPECULAR, TEX1
    CHECK_EQ(VertexStride(D3DFVF_VERTEX), 32u);    // XYZ, NORMAL, TEX1
    CHECK_EQ(VertexStride(D3DFVF_XYZ | D3DFVF_TEX2 | D3DFVF_TEXCOORDSIZE3(1)), 12u + 8u + 12u);
    CHECK(IsUnlitFormat(D3DFVF_LVERTEX));
    CHECK(!IsUnlitFormat(D3DFVF_VERTEX));
    CHECK(!IsUnlitFormat(D3DFVF_TLVERTEX));
}

TEST(PrimitiveCounts)
{
    CHECK_EQ(PrimitiveCount(D3DPT_TRIANGLELIST, 9), 3u);
    CHECK_EQ(PrimitiveCount(D3DPT_TRIANGLESTRIP, 5), 3u);
    CHECK_EQ(PrimitiveCount(D3DPT_TRIANGLEFAN, 2), 0u);
    CHECK_EQ(PrimitiveCount(D3DPT_LINELIST, 4), 2u);
}

TEST(PixelFormatsRoundTrip)
{
    for (const Format format : {Format::R5G6B5, Format::X1R5G5B5, Format::A1R5G5B5, Format::A4R4G4B4,
                                Format::X8R8G8B8, Format::A8R8G8B8, Format::L8, Format::A8, Format::A8L8,
                                Format::V8U8, Format::Dxt1, Format::Dxt5, Format::D16, Format::D24S8}) {
        CHECK(FromPixelFormat(ToPixelFormat(format)) == format);
    }
}

TEST(PixelConversion)
{
    std::uint8_t pixel[4]{};
    WriteArgb(Format::R5G6B5, 0xffff0000, pixel);
    CHECK_EQ(ReadArgb(Format::R5G6B5, pixel), 0xffff0000u);
    WriteArgb(Format::A4R4G4B4, 0x80ffffff, pixel);
    CHECK_EQ(ReadArgb(Format::A4R4G4B4, pixel) >> 24, 0x88u);
    WriteArgb(Format::A1R5G5B5, 0x00000000, pixel);
    CHECK_EQ(ReadArgb(Format::A1R5G5B5, pixel), 0u);
    WriteArgb(Format::X1R5G5B5, 0x0000ff00, pixel);
    CHECK_EQ(ReadArgb(Format::X1R5G5B5, pixel), 0xff00ff00u);
    CHECK(CanConvert(Format::R5G6B5, Format::A8R8G8B8));
    CHECK(CanConvert(Format::Dxt1, Format::A8R8G8B8));  // decoded
    CHECK(CanConvert(Format::A8R8G8B8, Format::Dxt1));  // encoded
    CHECK(!CanConvert(Format::D16, Format::A8R8G8B8));
}

TEST(DxtBlocksDecode)
{
    // DXT1: endpoints pure red (0xf800) and pure blue (0x001f), all indices 0 -> red; index 1 -> blue.
    const std::uint8_t dxt1[8] = {0x00, 0xf8, 0x1f, 0x00, 0x00, 0x00, 0x00, 0x40};
    std::uint32_t pixels[16];
    DecodeBlock(Format::Dxt1, dxt1, pixels);
    CHECK_EQ(pixels[0], 0xffff0000u);
    CHECK_EQ(pixels[15], 0xff0000ffu);  // last index (bits 30-31) = 1
    // DXT3: explicit alpha 0xf for the first pixel, 0 for the second.
    std::uint8_t dxt3[16] = {0x0f};
    std::memcpy(dxt3 + 8, dxt1, 8);
    DecodeBlock(Format::Dxt3, dxt3, pixels);
    CHECK_EQ(pixels[0] >> 24, 255u);
    CHECK_EQ(pixels[1] >> 24, 0u);
    // DXT5: alpha endpoints 255 and 0, all indices 0 -> 255.
    std::uint8_t dxt5[16] = {255, 0};
    std::memcpy(dxt5 + 8, dxt1, 8);
    DecodeBlock(Format::Dxt5, dxt5, pixels);
    CHECK_EQ(pixels[5] >> 24, 255u);
}

TEST(DxtEncodeRoundTrips)
{
    std::uint32_t pixels[16];
    for (int i = 0; i < 16; ++i) {
        pixels[i] = i < 8 ? 0xffff0000u : 0xff0000ffu;  // half red, half blue
    }
    for (const Format format : {Format::Dxt1, Format::Dxt3, Format::Dxt5}) {
        std::uint8_t block[16]{};
        EncodeBlock(format, pixels, block);
        std::uint32_t decoded[16];
        DecodeBlock(format, block, decoded);
        CHECK_EQ(decoded[0], 0xffff0000u);
        CHECK_EQ(decoded[15], 0xff0000ffu);
    }
    pixels[3] = 0x00000000;  // transparent pixel in DXT1
    std::uint8_t block[8]{};
    EncodeBlock(Format::Dxt1, pixels, block);
    std::uint32_t decoded[16];
    DecodeBlock(Format::Dxt1, block, decoded);
    CHECK_EQ(decoded[3] >> 24, 0u);
    CHECK_EQ(decoded[0], 0xffff0000u);
}

TEST(ImagesConvertWholeIncludingBlocks)
{
    // A 2x1 R5G6B5 image with a padded pitch, and a 4x4 DXT1 block of one colour cut to 3x2.
    const std::uint8_t rgb565[8] = {0x00, 0xf8, 0x1f, 0x00, 0xaa, 0xaa, 0xaa, 0xaa};
    std::uint32_t out[6]{};
    ImageToArgb(Format::R5G6B5, rgb565, 8, 2, 1, out);
    CHECK_EQ(out[0], 0xffff0000u);
    CHECK_EQ(out[1], 0xff0000ffu);
    const std::uint8_t dxt1[8] = {0xe0, 0x07, 0xe0, 0x07, 0, 0, 0, 0};  // pure green, index 0 everywhere
    ImageToArgb(Format::Dxt1, dxt1, 8, 3, 2, out);
    for (const std::uint32_t pixel : out) {
        CHECK_EQ(pixel, 0xff00ff00u);
    }
    CHECK(HasAlpha(Format::A4R4G4B4));
    CHECK(!HasAlpha(Format::X1R5G5B5));
}

TEST(TextureHashesIgnorePaddingAndNameRoundTrips)
{
    const std::uint8_t a[8] = {1, 2, 3, 4, 9, 9, 9, 9};
    const std::uint8_t b[8] = {1, 2, 3, 4, 7, 7, 7, 7};
    const std::uint64_t hash = textures::Hash(Format::A8R8G8B8, 1, 1, a, 8);
    CHECK_EQ(hash, textures::Hash(Format::A8R8G8B8, 1, 1, b, 8));
    CHECK(hash != textures::Hash(Format::X8R8G8B8, 1, 1, a, 8));
    CHECK(hash != textures::Hash(Format::A8R8G8B8, 1, 2, a, 4));

    std::uint64_t parsed = 0;
    CHECK(textures::ParseHashName(textures::HashName(hash) + L"_256x256_A4R4G4B4.png", parsed));
    CHECK_EQ(parsed, hash);
    CHECK(textures::ParseHashName(L"BW_00000000000000FF.dds", parsed));
    CHECK_EQ(parsed, 0xffull);
    CHECK(!textures::ParseHashName(L"bw_00000000000000ff0.png", parsed));
    CHECK(!textures::ParseHashName(L"bw_0000000000000xff.png", parsed));
    CHECK(!textures::ParseHashName(L"grass.png", parsed));
}

TEST(MipmapsAverageTwoByTwo)
{
    textures::Image image;
    image.width = 3;
    image.height = 2;
    image.argb = {0xff000000u, 0xff0000ffu, 0x00ffffffu, 0xff000000u, 0xff0000ffu, 0x00ffffffu};
    const textures::Image half = textures::HalveImage(image);
    CHECK_EQ(half.width, 1u);
    CHECK_EQ(half.height, 1u);
    CHECK_EQ(half.argb[0], 0xff000080u);  // blue 255 + 0 over two columns, both rows
    const textures::Image last = textures::HalveImage(half);
    CHECK_EQ(last.width, 1u);
    CHECK_EQ(last.argb[0], half.argb[0]);
}

TEST(TextureSourceNamesNormalise)
{
    CHECK_EQ(textures::NormaliseName(R"(.\Data\Textures\Sky.raw)"), std::string(R"(data\textures\sky)"));
    CHECK_EQ(textures::NormaliseName("Data/Textures/Front_end_buttons.RAW"),
             std::string(R"(data\textures\front_end_buttons)"));
    CHECK_EQ(textures::NormaliseName(R"(segments\1a3)"), std::string(R"(segments\1a3)"));
    CHECK_EQ(textures::NormaliseName(R"(data\v1.0\grass)"), std::string(R"(data\v1.0\grass)"));
}
