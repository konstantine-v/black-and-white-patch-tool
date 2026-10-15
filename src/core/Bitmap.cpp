#include "core/Bitmap.h"

#include "core/Log.h"
#include "core/Windows.h"

#include <wincodec.h>
#include <wrl/client.h>

#include <new>

namespace bw {

namespace {

using Microsoft::WRL::ComPtr;

/// The WIC factory of the calling thread. The thread joins a single-threaded apartment if it has none yet: that is
/// what CoInitialize gives, which is what the game's own COM use (DirectPlay) expects.
IWICImagingFactory* Factory() noexcept
{
    thread_local ComPtr<IWICImagingFactory> factory;
    if (!factory) {
        const HRESULT apartment = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        if (FAILED(apartment) && apartment != RPC_E_CHANGED_MODE) {
            return nullptr;
        }
        if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory)))) {
            log::Warning("Windows Imaging Component is not available");
            return nullptr;
        }
    }
    return factory.Get();
}

}  // namespace

bool WritePng(const std::filesystem::path& path, const Bitmap& bitmap) noexcept
{
    if (bitmap.width == 0 || bitmap.height == 0 || bitmap.width > kMaxBitmapSide || bitmap.height > kMaxBitmapSide ||
        bitmap.argb.size() != std::size_t{bitmap.width} * bitmap.height) {
        return false;
    }
    IWICImagingFactory* factory = Factory();
    if (factory == nullptr) {
        return false;
    }
    ComPtr<IWICStream> stream;
    ComPtr<IWICBitmapEncoder> encoder;
    ComPtr<IWICBitmapFrameEncode> frame;
    WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGRA;  // A8R8G8B8 in memory
    const UINT stride = bitmap.width * 4;
    const UINT size = stride * bitmap.height;  // at most 16384^2 * 4 = 1 GiB: fits
    return SUCCEEDED(factory->CreateStream(&stream)) &&
           SUCCEEDED(stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE)) &&
           SUCCEEDED(factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder)) &&
           SUCCEEDED(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache)) &&
           SUCCEEDED(encoder->CreateNewFrame(&frame, nullptr)) && SUCCEEDED(frame->Initialize(nullptr)) &&
           SUCCEEDED(frame->SetSize(bitmap.width, bitmap.height)) && SUCCEEDED(frame->SetPixelFormat(&format)) &&
           format == GUID_WICPixelFormat32bppBGRA &&
           SUCCEEDED(frame->WritePixels(bitmap.height, stride, size,
                                        reinterpret_cast<BYTE*>(const_cast<std::uint32_t*>(bitmap.argb.data())))) &&
           SUCCEEDED(frame->Commit()) && SUCCEEDED(encoder->Commit());
}

bool ReadBitmap(const std::filesystem::path& path, Bitmap& bitmap) noexcept
{
    IWICImagingFactory* factory = Factory();
    if (factory == nullptr) {
        return false;
    }
    ComPtr<IWICBitmapDecoder> decoder;
    ComPtr<IWICBitmapFrameDecode> frame;
    ComPtr<IWICFormatConverter> converter;
    UINT width = 0;
    UINT height = 0;
    if (FAILED(factory->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand,
                                                  &decoder)) ||
        FAILED(decoder->GetFrame(0, &frame)) || FAILED(frame->GetSize(&width, &height))) {
        return false;
    }
    // The size comes from the file: bound it before computing buffer sizes from it.
    if (width == 0 || height == 0 || width > kMaxBitmapSide || height > kMaxBitmapSide) {
        log::Warning("{}: {}x{} is not a usable image size (at most {} per side)", log::PathText(path), width, height,
                     kMaxBitmapSide);
        return false;
    }
    if (FAILED(factory->CreateFormatConverter(&converter)) ||
        FAILED(converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppBGRA, WICBitmapDitherTypeNone, nullptr, 0.0,
                                     WICBitmapPaletteTypeCustom))) {
        return false;
    }
    try {
        bitmap.argb.resize(std::size_t{width} * height);
    }
    catch (const std::bad_alloc&) {
        return false;
    }
    bitmap.width = width;
    bitmap.height = height;
    return SUCCEEDED(converter->CopyPixels(nullptr, width * 4, width * height * 4,
                                           reinterpret_cast<BYTE*>(bitmap.argb.data())));
}

}  // namespace bw
