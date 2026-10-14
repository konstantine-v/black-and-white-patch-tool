#pragma once

#include "core/Memory.h"
#include "core/Windows.h"

#include <cstddef>
#include <string_view>

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

/// Typed form of ReplaceImport: the replacement must have exactly the imported function's type.
template <typename Function>
[[nodiscard]] Function ReplaceImport(HMODULE module, std::string_view dll, std::string_view function,
                                     Function replacement) noexcept
{
    return reinterpret_cast<Function>(
        ReplaceImport(module, dll, function, reinterpret_cast<const void*>(replacement)));
}

}  // namespace bw::patch
