#pragma once

#include "core/Call.h"

#include <cstddef>

/// The original game's heap (its statically linked MSVC 6 CRT).
///
/// The decompilation links its own modern CRT with a separate heap. Memory that original code may free or
/// reallocate, or that decompiled code frees after original code allocated it, must therefore come from the game's
/// operator new/delete. Classes shared with original code derive from GameAllocated.
namespace bw {

inline constexpr CdeclFunction<0x0073445e, void*(std::size_t)> GameOperatorNew{};
inline constexpr CdeclFunction<0x007290de, void(void*)> GameOperatorDelete{};

/// Allocates `size` bytes on the game heap; nullptr on failure (the game's new handler does not throw).
[[nodiscard]] inline void* GameAllocate(std::size_t size) noexcept
{
    return GameOperatorNew(size);
}

/// Frees memory from the game heap; nullptr is ignored.
inline void GameFree(void* memory) noexcept
{
    if (memory != nullptr) {
        GameOperatorDelete(memory);
    }
}

/// Allocates a zero-terminated character buffer of `size` bytes, initially empty.
[[nodiscard]] char* GameAllocateString(std::size_t size) noexcept;

/// Base for decompiled classes whose objects original code may create or destroy.
struct GameAllocated {
    static void* operator new(std::size_t size) { return GameAllocate(size); }
    static void operator delete(void* memory) noexcept { GameFree(memory); }
    static void* operator new[](std::size_t size) = delete;
    static void operator delete[](void* memory) = delete;
};

}  // namespace bw
