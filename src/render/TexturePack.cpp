#include "render/TexturePack.h"

#include "core/Log.h"
#include "core/Windows.h"
#include "render/Format.h"

#include <wincodec.h>
#include <wrl/client.h>

#include <algorithm>
#include <format>
#include <mutex>
#include <unordered_map>
#include <unordered_set>

namespace bw::render::textures {

namespace {

using Microsoft::WRL::ComPtr;

struct State {
    Settings settings;
    std::unordered_map<std::uint64_t, std::filesystem::path> replacements;
    std::unordered_set<std::uint64_t> dumped;
    std::mutex mutex;
};

State& GetState()
{
    static State state;
    return state;
}

constexpr std::wstring_view kPrefix = L"bw_";
constexpr std::size_t kHashDigits = 16;

bool IsImageFile(const std::filesystem::path& path)
{
    std::wstring extension = path.extension().wstring();
    std::transform(extension.begin(), extension.end(), extension.begin(), [](wchar_t c) { return towlower(c); });
    return extension == L".png" || extension == L".dds" || extension == L".bmp" || extension == L".jpg" ||
           extension == L".jpeg" || extension == L".tif" || extension == L".tiff";
}

/// The WIC factory of the calling thread. The game's rendering thread gets a single-threaded apartment, which is what
/// CoInitialize gives and what the game's own COM use (DirectPlay) expects.
IWICImagingFactory* Factory() noexcept
{
    thread_local ComPtr<IWICImagingFactory> factory;
    if (!factory) {
        const HRESULT apartment = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        if (FAILED(apartment) && apartment != RPC_E_CHANGED_MODE) {
            return nullptr;
        }
        if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory)))) {
            log::Warning("Textures: Windows Imaging Component is not available");
            return nullptr;
        }
    }
    return factory.Get();
}

bool WritePng(const std::filesystem::path& path, const Image& image) noexcept
{
    IWICImagingFactory* factory = Factory();
    if (factory == nullptr) {
        return false;
    }
    ComPtr<IWICStream> stream;
    ComPtr<IWICBitmapEncoder> encoder;
    ComPtr<IWICBitmapFrameEncode> frame;
    WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGRA;  // A8R8G8B8 in memory
    const UINT stride = image.width * 4;
    return SUCCEEDED(factory->CreateStream(&stream)) &&
           SUCCEEDED(stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE)) &&
           SUCCEEDED(factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder)) &&
           SUCCEEDED(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache)) &&
           SUCCEEDED(encoder->CreateNewFrame(&frame, nullptr)) && SUCCEEDED(frame->Initialize(nullptr)) &&
           SUCCEEDED(frame->SetSize(image.width, image.height)) && SUCCEEDED(frame->SetPixelFormat(&format)) &&
           format == GUID_WICPixelFormat32bppBGRA &&
           SUCCEEDED(frame->WritePixels(image.height, stride, stride * image.height,
                                        reinterpret_cast<BYTE*>(const_cast<std::uint32_t*>(image.argb.data())))) &&
           SUCCEEDED(frame->Commit()) && SUCCEEDED(encoder->Commit());
}

bool ReadImage(const std::filesystem::path& path, Image& image) noexcept
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
        FAILED(decoder->GetFrame(0, &frame)) || FAILED(frame->GetSize(&width, &height)) || width == 0 ||
        height == 0 || FAILED(factory->CreateFormatConverter(&converter)) ||
        FAILED(converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppBGRA, WICBitmapDitherTypeNone, nullptr, 0.0,
                                     WICBitmapPaletteTypeCustom))) {
        return false;
    }
    try {
        image.argb.resize(std::size_t{width} * height);
    }
    catch (const std::bad_alloc&) {
        return false;
    }
    image.width = width;
    image.height = height;
    return SUCCEEDED(converter->CopyPixels(nullptr, width * 4, width * height * 4,
                                           reinterpret_cast<BYTE*>(image.argb.data())));
}

}  // namespace

void Initialise(const Settings& settings) noexcept
{
    State& state = GetState();
    std::lock_guard lock(state.mutex);
    state.settings = settings;
    state.replacements.clear();
    if (!settings.replace) {
        return;
    }
    const std::filesystem::path folder = settings.folder / L"Replace";
    std::error_code error;
    for (auto it = std::filesystem::recursive_directory_iterator(folder, error);
         !error && it != std::filesystem::recursive_directory_iterator(); it.increment(error)) {
        std::uint64_t hash = 0;
        if (it->is_regular_file(error) && IsImageFile(it->path()) &&
            ParseHashName(it->path().filename().wstring(), hash)) {
            const auto [existing, added] = state.replacements.emplace(hash, it->path());
            if (!added) {
                log::Warning("Textures: two replacements for {}; using {}", std::filesystem::path(HashName(hash)).string(),
                             existing->second.string());
            }
        }
    }
    if (!state.replacements.empty()) {
        log::Info("Textures: {} replacements in {}", state.replacements.size(), folder.string());
    }
}

bool Active() noexcept
{
    State& state = GetState();
    std::lock_guard lock(state.mutex);
    return state.settings.dump || !state.replacements.empty();
}

std::uint64_t Hash(Format format, std::uint32_t width, std::uint32_t height, const std::uint8_t* bits,
                   std::int32_t pitch) noexcept
{
    constexpr std::uint64_t kPrime = 0x100000001b3ull;
    std::uint64_t hash = 0xcbf29ce484222325ull;
    auto add = [&](std::uint8_t byte) {
        hash ^= byte;
        hash *= kPrime;
    };
    for (const std::uint32_t value : {static_cast<std::uint32_t>(format), width, height}) {
        for (int shift = 0; shift < 32; shift += 8) {
            add(static_cast<std::uint8_t>(value >> shift));
        }
    }
    const std::uint32_t rowBytes = RowBytes(format, width);
    const std::uint32_t rows = StorageRows(format, height);
    for (std::uint32_t row = 0; row < rows; ++row) {
        const std::uint8_t* data = bits + std::size_t{row} * pitch;
        for (std::uint32_t i = 0; i < rowBytes; ++i) {
            add(data[i]);
        }
    }
    return hash;
}

std::wstring HashName(std::uint64_t hash)
{
    return std::format(L"bw_{:016x}", hash);
}

bool ParseHashName(std::wstring_view fileName, std::uint64_t& hash) noexcept
{
    if (fileName.size() < kPrefix.size() + kHashDigits ||
        !std::equal(kPrefix.begin(), kPrefix.end(), fileName.begin(),
                    [](wchar_t prefix, wchar_t c) { return prefix == towlower(c); })) {
        return false;
    }
    std::uint64_t value = 0;
    for (const wchar_t c : fileName.substr(kPrefix.size(), kHashDigits)) {
        const wchar_t lower = towlower(c);
        if (lower >= L'0' && lower <= L'9') {
            value = value * 16 + static_cast<std::uint64_t>(lower - L'0');
        }
        else if (lower >= L'a' && lower <= L'f') {
            value = value * 16 + static_cast<std::uint64_t>(lower - L'a' + 10);
        }
        else {
            return false;
        }
    }
    // The hash must end there (a longer run of hex digits is a different name).
    if (fileName.size() > kPrefix.size() + kHashDigits && iswxdigit(fileName[kPrefix.size() + kHashDigits])) {
        return false;
    }
    hash = value;
    return true;
}

bool WantsDump(std::uint64_t hash) noexcept
{
    State& state = GetState();
    std::lock_guard lock(state.mutex);
    return state.settings.dump && !state.dumped.contains(hash);
}

void Dump(std::uint64_t hash, Format format, const Image& image) noexcept
{
    State& state = GetState();
    std::filesystem::path path;
    {
        std::lock_guard lock(state.mutex);
        if (!state.settings.dump || !state.dumped.insert(hash).second) {
            return;
        }
        const std::string formatName(Name(format));
        path = state.settings.folder / L"Dump" /
               std::format(L"{}_{}x{}_{}.png", HashName(hash), image.width, image.height,
                           std::wstring(formatName.begin(), formatName.end()));
    }
    std::error_code error;
    if (std::filesystem::exists(path, error)) {
        return;
    }
    std::filesystem::create_directories(path.parent_path(), error);
    if (!WritePng(path, image)) {
        log::Warning("Textures: cannot write {}", path.string());
    }
}

bool HasReplacement(std::uint64_t hash) noexcept
{
    State& state = GetState();
    std::lock_guard lock(state.mutex);
    return state.replacements.contains(hash);
}

bool LoadReplacement(std::uint64_t hash, Image& image) noexcept
{
    std::filesystem::path path;
    {
        State& state = GetState();
        std::lock_guard lock(state.mutex);
        const auto it = state.replacements.find(hash);
        if (it == state.replacements.end()) {
            return false;
        }
        path = it->second;
    }
    if (!ReadImage(path, image)) {
        log::Warning("Textures: cannot read {}", path.string());
        return false;
    }
    return true;
}

Image HalveImage(const Image& image)
{
    Image half;
    half.width = std::max<std::uint32_t>(image.width / 2, 1);
    half.height = std::max<std::uint32_t>(image.height / 2, 1);
    half.argb.resize(std::size_t{half.width} * half.height);
    for (std::uint32_t y = 0; y < half.height; ++y) {
        const std::uint32_t y0 = std::min(y * 2, image.height - 1);
        const std::uint32_t y1 = std::min(y * 2 + 1, image.height - 1);
        for (std::uint32_t x = 0; x < half.width; ++x) {
            const std::uint32_t x0 = std::min(x * 2, image.width - 1);
            const std::uint32_t x1 = std::min(x * 2 + 1, image.width - 1);
            const std::uint32_t texels[4] = {
                image.argb[std::size_t{y0} * image.width + x0], image.argb[std::size_t{y0} * image.width + x1],
                image.argb[std::size_t{y1} * image.width + x0], image.argb[std::size_t{y1} * image.width + x1]};
            std::uint32_t result = 0;
            for (int shift = 0; shift < 32; shift += 8) {
                std::uint32_t sum = 2;  // rounding
                for (const std::uint32_t texel : texels) {
                    sum += (texel >> shift) & 255;
                }
                result |= (sum / 4) << shift;
            }
            half.argb[std::size_t{y} * half.width + x] = result;
        }
    }
    return half;
}

}  // namespace bw::render::textures
