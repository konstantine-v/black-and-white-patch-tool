#include "core/Image.h"

#include <bcrypt.h>

#include <algorithm>

namespace bw::image {

std::optional<Section> FindSection(HMODULE module, std::string_view name) noexcept
{
    const auto base = reinterpret_cast<Address>(module);
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(module);
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + static_cast<Address>(dos->e_lfanew));
    const IMAGE_SECTION_HEADER* sections = IMAGE_FIRST_SECTION(nt);
    for (WORD i = 0; i < nt->FileHeader.NumberOfSections; ++i) {
        const IMAGE_SECTION_HEADER& section = sections[i];
        const std::string_view sectionName(reinterpret_cast<const char*>(section.Name),
                                           strnlen_s(reinterpret_cast<const char*>(section.Name),
                                                     IMAGE_SIZEOF_SHORT_NAME));
        if (sectionName == name) {
            const Address begin = base + section.VirtualAddress;
            return Section{begin, begin + section.Misc.VirtualSize};
        }
    }
    return std::nullopt;
}

std::optional<Sha256> ComputeSha256(std::span<const std::byte> data) noexcept
{
    Sha256 digest{};
    const NTSTATUS status = BCryptHash(BCRYPT_SHA256_ALG_HANDLE, nullptr, 0,
                                       const_cast<PUCHAR>(reinterpret_cast<const UCHAR*>(data.data())),
                                       static_cast<ULONG>(data.size()), reinterpret_cast<PUCHAR>(digest.data()),
                                       static_cast<ULONG>(digest.size()));
    if (!BCRYPT_SUCCESS(status)) {
        return std::nullopt;
    }
    return digest;
}

std::array<char, 64> ToHex(const Sha256& digest) noexcept
{
    constexpr std::string_view kDigits = "0123456789abcdef";
    std::array<char, 64> text{};
    for (std::size_t i = 0; i < digest.size(); ++i) {
        const auto value = std::to_integer<unsigned>(digest[i]);
        text[2 * i] = kDigits[value >> 4];
        text[2 * i + 1] = kDigits[value & 0xF];
    }
    return text;
}

bool IsSupportedGame(HMODULE module) noexcept
{
    if (reinterpret_cast<Address>(module) != kImageBase) {
        return false;
    }
    const std::optional<Section> text = FindSection(module, ".text");
    if (!text) {
        return false;
    }
    const std::optional<Sha256> digest =
        ComputeSha256({reinterpret_cast<const std::byte*>(text->begin), text->Size()});
    if (!digest) {
        return false;
    }
    const std::array<char, 64> hex = ToHex(*digest);
    return std::string_view(hex.data(), hex.size()) == kSupportedTextSha256;
}

}  // namespace bw::image
