#pragma once

#include "core/Memory.h"
#include "core/Windows.h"

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <type_traits>

/// Low-level code patching: protection changes, jump writes and import-address-table replacement.
namespace bw::patch {

/// Size of the `jmp rel32` written over an original function entry.
inline constexpr std::size_t kJumpSize = 5;

enum class Protection : DWORD {
    ReadWriteExecute = PAGE_EXECUTE_READWRITE,
};

/// Changes page protection for its lifetime and restores the previous protection afterwards.
class ScopedProtection {
public:
    ScopedProtection(Address begin, std::size_t size, Protection protection) noexcept;
    ~ScopedProtection();
    ScopedProtection(const ScopedProtection&) = delete;
    ScopedProtection& operator=(const ScopedProtection&) = delete;

    explicit operator bool() const noexcept { return m_changed; }

private:
    void* m_begin;
    std::size_t m_size;
    DWORD m_previous = 0;
    bool m_changed = false;
};

/// Permanently changes page protection; returns false on failure.
bool SetProtection(Address begin, std::size_t size, Protection protection) noexcept;

/// Writes `jmp rel32` from `from` to `to`. The caller must have made the bytes writable.
void WriteJump(Address from, const void* to) noexcept;

void FlushCode(Address begin, std::size_t size) noexcept;

/// Replaces `module`'s import of `dll`!`function` (matched case-insensitively for the DLL) with `replacement`.
/// Returns the previous target, or nullptr when the module does not import that function.
[[nodiscard]] void* ReplaceImport(HMODULE module, std::string_view dll, std::string_view function,
                                  const void* replacement) noexcept;

/// Same for an import by ordinal (e.g. DSETUP.dll, which the game imports without names).
[[nodiscard]] void* ReplaceImport(HMODULE module, std::string_view dll, std::uint16_t ordinal,
                                  const void* replacement) noexcept;

/// Typed forms of ReplaceImport: the replacement must have exactly the imported function's type. `Id` is the
/// function name or its ordinal.
template <typename Function, typename Id>
    requires std::is_pointer_v<Function> && std::is_function_v<std::remove_pointer_t<Function>>
[[nodiscard]] Function ReplaceImport(HMODULE module, std::string_view dll, Id id, Function replacement) noexcept
{
    return reinterpret_cast<Function>(ReplaceImport(module, dll, id, reinterpret_cast<const void*>(replacement)));
}

}  // namespace bw::patch
