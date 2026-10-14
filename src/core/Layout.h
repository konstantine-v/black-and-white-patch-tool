#pragma once

#include <cstddef>

/// Compile-time checks that a decompiled type matches the original memory layout. Every class or struct shared
/// with original code must assert its size and the offset of each recovered member.
#define BW_ASSERT_SIZE(Type, size) \
    static_assert(sizeof(Type) == (size), #Type " must match the original size " #size)

// offsetof on a polymorphic type is conditionally supported; MSVC supports it and that is the only compiler we use.
#define BW_ASSERT_OFFSET(Type, member, offset) \
    static_assert(offsetof(Type, member) == (offset), #Type "::" #member " must be at original offset " #offset)
