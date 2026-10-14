#pragma once

#include "core/Memory.h"

#include <type_traits>

/// Typed calls into functions that have not been decompiled yet.
///
/// Each wrapper is a stateless constant whose type carries the full signature, so a call site cannot pass the wrong
/// argument types or use the wrong calling convention:
///
///     inline constexpr bw::CdeclFunction<0x0073445e, void*(std::size_t)> GameOperatorNew{};
///     inline constexpr bw::ThiscallFunction<0x00748d70, int(LHScreen*, int, int, int)> SetMode{};
///
/// __thiscall cannot be named on a non-member function pointer, so thiscall targets are called as __fastcall with an
/// unused EDX argument: both put `this` in ECX, pass the remaining arguments on the stack and let the callee pop them.
namespace bw {

namespace detail {

/// Calls through ECX/EDX conventions are only ABI-identical when the result comes back in EAX/EDX:ST0. Class types
/// returned by value use a hidden pointer whose position differs between thiscall and fastcall.
template <typename R>
inline constexpr bool kRegisterReturnable = std::is_void_v<R> || std::is_scalar_v<R> || std::is_reference_v<R>;

}  // namespace detail

template <Address A, typename Signature>
struct CdeclFunction;

template <Address A, typename R, typename... Args>
struct CdeclFunction<A, R(Args...)> {
    using Pointer = R(__cdecl*)(Args...);
    R operator()(Args... args) const { return reinterpret_cast<Pointer>(A)(args...); }
};

template <Address A, typename R, typename... Args>
struct CdeclFunction<A, R(Args..., ...)> {
    using Pointer = R(__cdecl*)(Args..., ...);
    template <typename... Variadic>
    R operator()(Args... args, Variadic... variadic) const
    {
        return reinterpret_cast<Pointer>(A)(args..., variadic...);
    }
};

template <Address A, typename Signature>
struct StdcallFunction;

template <Address A, typename R, typename... Args>
struct StdcallFunction<A, R(Args...)> {
    using Pointer = R(__stdcall*)(Args...);
    R operator()(Args... args) const { return reinterpret_cast<Pointer>(A)(args...); }
};

/// __fastcall: the first two integer or pointer arguments in ECX and EDX, the rest on the stack, popped by the callee.
template <Address A, typename Signature>
struct FastcallFunction;

template <Address A, typename R, typename... Args>
struct FastcallFunction<A, R(Args...)> {
    using Pointer = R(__fastcall*)(Args...);
    R operator()(Args... args) const { return reinterpret_cast<Pointer>(A)(args...); }
};

template <Address A, typename Signature>
struct ThiscallFunction;

template <Address A, typename R, typename Self, typename... Args>
struct ThiscallFunction<A, R(Self*, Args...)> {
    static_assert(detail::kRegisterReturnable<R>, "class return types need a dedicated thiscall wrapper");
    using Pointer = R(__fastcall*)(Self*, void*, Args...);
    R operator()(Self* self, Args... args) const { return reinterpret_cast<Pointer>(A)(self, nullptr, args...); }
};

}  // namespace bw
