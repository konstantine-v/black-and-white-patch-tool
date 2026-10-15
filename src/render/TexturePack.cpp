#include "render/TexturePack.h"

#include "core/Log.h"
#include "core/Windows.h"
#include "render/Format.h"

#include <algorithm>
#include <cctype>
#include <format>
#include <mutex>
#include <unordered_map>
#include <unordered_set>

namespace bw::render::textures {

namespace {

struct State {
    Settings settings;
    std::unordered_map<std::uint64_t, std::filesystem::path> replacementsByHash;
    std::unordered_map<std::string, std::filesystem::path> replacementsByName;  // NormaliseName of the relative path
    std::unordered_map<std::uint64_t, Source> sources;
    std::size_t fullColourSources = 0;
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

/// A source name usable as a path below Dump\: relative, without ".." components.
bool IsSafeRelativeName(std::string_view name)
{
    if (name.empty()) {
        return false;
    }
    const std::filesystem::path path(name);
    if (path.has_root_name() || path.has_root_directory()) {
        return false;
    }
    for (const std::filesystem::path& part : path) {
        if (part == "..") {
            return false;
        }
    }
    return true;
}

void IndexReplacements(State& state, const std::filesystem::path& folder);

bool IsImageFile(const std::filesystem::path& path)
{
    std::wstring extension = path.extension().wstring();
    std::transform(extension.begin(), extension.end(), extension.begin(), [](wchar_t c) { return towlower(c); });
    return extension == L".png" || extension == L".dds" || extension == L".bmp" || extension == L".jpg" ||
           extension == L".jpeg" || extension == L".tif" || extension == L".tiff";
}

}  // namespace

void Initialise(const Settings& settings) noexcept
{
    State& state = GetState();
    std::lock_guard lock(state.mutex);
    state.settings = settings;
    state.replacementsByHash.clear();
    state.replacementsByName.clear();
    if (!settings.replace) {
        return;
    }
    try {
        IndexReplacements(state, settings.folder / L"Replace");
    }
    catch (const std::exception&) {
        log::Warning("Textures: indexing the replacements failed");
    }
}

namespace {

void IndexReplacements(State& state, const std::filesystem::path& folder)
{
    std::error_code error;
    for (auto it = std::filesystem::recursive_directory_iterator(folder, error);
         !error && it != std::filesystem::recursive_directory_iterator(); it.increment(error)) {
        if (!it->is_regular_file(error) || !IsImageFile(it->path())) {
            continue;
        }
        std::uint64_t hash = 0;
        if (ParseHashName(it->path().filename().wstring(), hash)) {
            const auto [existing, added] = state.replacementsByHash.emplace(hash, it->path());
            if (!added) {
                log::Warning("Textures: two replacements for {}; using {}", log::PathText(HashName(hash)),
                             log::PathText(existing->second));
            }
            continue;
        }
        const std::string name = NormaliseName(log::PathText(it->path().lexically_relative(folder)));
        const auto [existing, added] = state.replacementsByName.emplace(name, it->path());
        if (!added) {
            log::Warning("Textures: two replacements for {}; using {}", name, log::PathText(existing->second));
        }
    }
    const std::size_t count = state.replacementsByHash.size() + state.replacementsByName.size();
    if (count != 0) {
        log::Info("Textures: {} replacements in {}", count, log::PathText(folder));
    }
}

}  // namespace

bool Active() noexcept
{
    State& state = GetState();
    std::lock_guard lock(state.mutex);
    return state.settings.dump || !state.replacementsByHash.empty() || !state.replacementsByName.empty() ||
           state.fullColourSources != 0;
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

std::string NormaliseName(std::string_view path)
{
    std::string name(path);
    std::replace(name.begin(), name.end(), '/', '\\');
    std::transform(name.begin(), name.end(), name.begin(),
                   [](char c) { return static_cast<char>(std::tolower(static_cast<unsigned char>(c))); });
    while (name.starts_with(".\\")) {
        name.erase(0, 2);
    }
    const std::size_t dot = name.find_last_of('.');
    if (dot != std::string::npos && name.find('\\', dot) == std::string::npos) {
        name.erase(dot);
    }
    return name;
}

void RegisterSource(std::uint64_t hash, Source source) noexcept
{
    State& state = GetState();
    std::lock_guard lock(state.mutex);
    try {
        Source& entry = state.sources[hash];
        state.fullColourSources -= entry.image ? 1 : 0;
        state.fullColourSources += source.image ? 1 : 0;
        entry = std::move(source);
    }
    catch (const std::bad_alloc&) {
        // Not registered: the texture keeps its hash name.
    }
}

bool FindSource(std::uint64_t hash, Source& source) noexcept
{
    State& state = GetState();
    std::lock_guard lock(state.mutex);
    const auto it = state.sources.find(hash);
    if (it == state.sources.end()) {
        return false;
    }
    try {
        source = it->second;
    }
    catch (const std::bad_alloc&) {
        return false;
    }
    return true;
}

bool WantsDump(std::uint64_t hash) noexcept
{
    State& state = GetState();
    std::lock_guard lock(state.mutex);
    return state.settings.dump && !state.dumped.contains(hash);
}

void Dump(std::uint64_t hash, std::string_view name, Format format, const Image& image) noexcept
{
    try {
        State& state = GetState();
        std::filesystem::path path;
        {
            std::lock_guard lock(state.mutex);
            if (!state.settings.dump || !state.dumped.insert(hash).second) {
                return;
            }
            if (IsSafeRelativeName(name)) {
                path = state.settings.folder / L"Dump" / std::filesystem::path(std::string(name) + ".png");
            }
            else {
                const std::string formatName(Name(format));
                path = state.settings.folder / L"Dump" /
                       std::format(L"{}_{}x{}_{}.png", HashName(hash), image.width, image.height,
                                   std::wstring(formatName.begin(), formatName.end()));
            }
        }
        std::error_code error;
        if (std::filesystem::exists(path, error)) {
            return;
        }
        std::filesystem::create_directories(path.parent_path(), error);
        if (!bw::WritePng(path, image)) {
            log::Warning("Textures: cannot write {}", log::PathText(path));
        }
    }
    catch (const std::exception&) {
        log::Warning("Textures: a dump failed");
    }
}

std::filesystem::path FindReplacement(std::uint64_t hash, std::string_view name) noexcept
{
    State& state = GetState();
    std::lock_guard lock(state.mutex);
    try {
        if (!name.empty()) {
            if (const auto it = state.replacementsByName.find(std::string(name)); it != state.replacementsByName.end()) {
                return it->second;
            }
        }
        const auto it = state.replacementsByHash.find(hash);
        return it != state.replacementsByHash.end() ? it->second : std::filesystem::path();
    }
    catch (const std::bad_alloc&) {
        return {};
    }
}

bool LoadImage(const std::filesystem::path& path, Image& image) noexcept
{
    if (!bw::ReadBitmap(path, image)) {
        log::Warning("Textures: cannot read {}", log::PathText(path));
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
