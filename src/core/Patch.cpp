#include "core/Patch.h"

#include <cstring>

namespace bw::patch {

namespace {

bool EqualsIgnoreCase(std::string_view a, std::string_view b) noexcept
{
    return a.size() == b.size() && _strnicmp(a.data(), b.data(), a.size()) == 0;
}

template <typename T>
const T* AtRva(HMODULE module, DWORD rva) noexcept
{
    return reinterpret_cast<const T*>(reinterpret_cast<const std::byte*>(module) + rva);
}

}  // namespace

ScopedProtection::ScopedProtection(Address begin, std::size_t size, Protection protection) noexcept
    : m_begin(reinterpret_cast<void*>(begin)), m_size(size)
{
    m_changed = VirtualProtect(m_begin, m_size, static_cast<DWORD>(protection), &m_previous) != FALSE;
}

ScopedProtection::~ScopedProtection()
{
    if (m_changed) {
        DWORD ignored = 0;
        VirtualProtect(m_begin, m_size, m_previous, &ignored);
    }
}

bool SetProtection(Address begin, std::size_t size, Protection protection) noexcept
{
    DWORD previous = 0;
    return VirtualProtect(reinterpret_cast<void*>(begin), size, static_cast<DWORD>(protection), &previous) != FALSE;
}

void WriteJump(Address from, const void* to) noexcept
{
    const auto displacement =
        static_cast<std::int32_t>(reinterpret_cast<Address>(to) - (from + kJumpSize));
    auto* code = reinterpret_cast<std::uint8_t*>(from);
    code[0] = 0xE9;
    std::memcpy(code + 1, &displacement, sizeof displacement);
}

void FlushCode(Address begin, std::size_t size) noexcept
{
    FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<const void*>(begin), size);
}

namespace {

/// Walks `module`'s imports from `dll` and replaces the first one for which `matches(thunk)` holds.
template <typename Matches>
void* ReplaceImportWhere(HMODULE module, std::string_view dll, Matches matches, const void* replacement) noexcept
{
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(module);
    const auto* nt = AtRva<IMAGE_NT_HEADERS>(module, static_cast<DWORD>(dos->e_lfanew));
    const IMAGE_DATA_DIRECTORY& directory = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (directory.VirtualAddress == 0) {
        return nullptr;
    }

    for (const auto* descriptor = AtRva<IMAGE_IMPORT_DESCRIPTOR>(module, directory.VirtualAddress);
         descriptor->Name != 0; ++descriptor) {
        if (!EqualsIgnoreCase(AtRva<char>(module, descriptor->Name), dll)) {
            continue;
        }
        // OriginalFirstThunk keeps the names; FirstThunk is the bound address table we patch.
        const auto* names = AtRva<IMAGE_THUNK_DATA>(module, descriptor->OriginalFirstThunk != 0
                                                                ? descriptor->OriginalFirstThunk
                                                                : descriptor->FirstThunk);
        auto* slots = const_cast<IMAGE_THUNK_DATA*>(AtRva<IMAGE_THUNK_DATA>(module, descriptor->FirstThunk));
        for (; names->u1.AddressOfData != 0; ++names, ++slots) {
            if (!matches(*names)) {
                continue;
            }
            void* previous = reinterpret_cast<void*>(slots->u1.Function);
            const ScopedProtection writable(reinterpret_cast<Address>(&slots->u1.Function), sizeof(void*),
                                            Protection::ReadWriteExecute);
            if (!writable) {
                return nullptr;
            }
            slots->u1.Function = reinterpret_cast<Address>(replacement);
            return previous;
        }
    }
    return nullptr;
}

}  // namespace

void* ReplaceImport(HMODULE module, std::string_view dll, std::string_view function, const void* replacement) noexcept
{
    return ReplaceImportWhere(
        module, dll,
        [&](const IMAGE_THUNK_DATA& thunk) {
            if (IMAGE_SNAP_BY_ORDINAL(thunk.u1.Ordinal)) {
                return false;
            }
            const auto* byName = AtRva<IMAGE_IMPORT_BY_NAME>(module, static_cast<DWORD>(thunk.u1.AddressOfData));
            return function == reinterpret_cast<const char*>(byName->Name);
        },
        replacement);
}

void* ReplaceImport(HMODULE module, std::string_view dll, std::uint16_t ordinal, const void* replacement) noexcept
{
    return ReplaceImportWhere(
        module, dll,
        [&](const IMAGE_THUNK_DATA& thunk) {
            return IMAGE_SNAP_BY_ORDINAL(thunk.u1.Ordinal) && IMAGE_ORDINAL(thunk.u1.Ordinal) == ordinal;
        },
        replacement);
}

}  // namespace bw::patch
