#pragma once

#include "core/Call.h"
#include "core/Memory.h"

#include <cstdint>
#include <new>
#include <span>
#include <string_view>

/// Function replacement: the decompilation runs inside the original process and takes over one function at a time.
///
/// Every decompiled function registers a hook for its original address. At start-up, before the game's entry point
/// runs, HookRegistry::InstallAll writes a 5-byte `jmp rel32` over each original entry so that every caller (direct
/// calls, vtables, callbacks) lands in the decompiled code. Because the replacement must be ABI-identical to the
/// original, the registration macros derive the calling convention and stack cleanup from the C++ signature and
/// record them, so tools/check_hooks.py can verify them against the original `ret n`.
///
///     BW_HOOK_FUNCTION(0x0045d4a0, CPUCheck::MeasureSpeedMHz);   // free/static function, cdecl or stdcall
///     BW_HOOK_METHOD(0x0045c610, CPUCheck, Run);                 // member function, thiscall
///     BW_HOOK_CONSTRUCTOR(0x0045c550, CPUCheck);                 // constructor (argument types follow the class)
///     BW_HOOK_DESTRUCTOR(0x0045c5a0, CPUCheck);                  // non-deleting destructor
///     BW_HOOK_DELETING_DESTRUCTOR(0x0045c580, CPUCheck);         // scalar deleting destructor (vtable slot)
namespace bw {

enum class CallingConvention : std::uint8_t { Cdecl, Stdcall, Thiscall };

[[nodiscard]] std::string_view ToString(CallingConvention convention) noexcept;

struct HookDescriptor {
    Address original;               ///< Entry of the original function in runblack.exe.
    const void* replacement;        ///< ABI-compatible entry of the decompiled implementation.
    const char* name;               ///< Qualified name, e.g. "CPUCheck::Run".
    CallingConvention convention;
    std::uint16_t calleeStackBytes; ///< Stack bytes the callee pops (`ret n`); 0 for cdecl.
};

struct HookInstallReport {
    std::size_t installed = 0;
    std::size_t disabled = 0;
    std::size_t rejected = 0;
};

class HookRegistry {
public:
    static void Register(const HookDescriptor& descriptor) noexcept;

    /// All registered hooks, sorted by original address.
    [[nodiscard]] static std::span<const HookDescriptor> All() noexcept;

    /// Patches every registered original entry except the addresses in `disabled` (used to bisect regressions by
    /// falling back to original code). Rejects duplicates and entries outside [textBegin, textEnd - 5].
    static HookInstallReport InstallAll(Address textBegin, Address textEnd, std::span<const Address> disabled) noexcept;
};

namespace detail {

template <typename T>
inline constexpr std::uint16_t kStackSlot = static_cast<std::uint16_t>((sizeof(T) + 3u) & ~3u);

template <typename... Args>
inline constexpr std::uint16_t kStackBytes = static_cast<std::uint16_t>((0u + ... + kStackSlot<Args>));

struct HookRegistrar {
    explicit HookRegistrar(const HookDescriptor& descriptor) noexcept { HookRegistry::Register(descriptor); }
};

// ---- Free and static member functions -------------------------------------------------------------------------

template <typename Function>
struct FreeFunctionTraits;

#define BW_DETAIL_FREE_FUNCTION_TRAITS(CONVENTION_KEYWORD, NOEXCEPT, CONVENTION, CALLEE_POPS)                     \
    template <typename R, typename... Args>                                                                       \
    struct FreeFunctionTraits<R(CONVENTION_KEYWORD*)(Args...) NOEXCEPT> {                                         \
        static constexpr CallingConvention kConvention = CallingConvention::CONVENTION;                           \
        static constexpr std::uint16_t kCalleeStackBytes = (CALLEE_POPS) ? kStackBytes<Args...> : std::uint16_t{0}; \
    };

BW_DETAIL_FREE_FUNCTION_TRAITS(__cdecl, , Cdecl, false)
BW_DETAIL_FREE_FUNCTION_TRAITS(__cdecl, noexcept, Cdecl, false)
BW_DETAIL_FREE_FUNCTION_TRAITS(__stdcall, , Stdcall, true)
BW_DETAIL_FREE_FUNCTION_TRAITS(__stdcall, noexcept, Stdcall, true)
#undef BW_DETAIL_FREE_FUNCTION_TRAITS

template <typename Function>
[[nodiscard]] HookDescriptor MakeFunctionHook(Address original, Function function, const char* name) noexcept
{
    using Traits = FreeFunctionTraits<Function>;
    return {original, reinterpret_cast<const void*>(function), name, Traits::kConvention, Traits::kCalleeStackBytes};
}

// ---- Member functions -------------------------------------------------------------------------------------------
//
// A member-function pointer cannot be jumped to directly (for a virtual function it is a vcall thunk that would
// dispatch back through the vtable). Instead each hook gets a static __fastcall thunk with an unused EDX parameter,
// which is ABI-identical to __thiscall, and the thunk makes a qualified, non-virtual call through `Invoker`.

template <typename Method, typename Invoker>
struct MethodThunk;

#define BW_DETAIL_METHOD_THUNK(QUALIFIERS)                                                                       \
    template <typename C, typename R, typename... Args, typename Invoker>                                       \
    struct MethodThunk<R (C::*)(Args...) QUALIFIERS, Invoker> {                                                 \
        static_assert(kRegisterReturnable<R>, "class return types need a dedicated thunk");                     \
        static constexpr std::uint16_t kCalleeStackBytes = kStackBytes<Args...>;                                \
        static R __fastcall Entry(C* self, void* /*edx*/, Args... args) noexcept { return Invoker{}(self, args...); } \
    };

BW_DETAIL_METHOD_THUNK()
BW_DETAIL_METHOD_THUNK(const)
BW_DETAIL_METHOD_THUNK(noexcept)
BW_DETAIL_METHOD_THUNK(const noexcept)
#undef BW_DETAIL_METHOD_THUNK

template <typename Method, typename Invoker>
[[nodiscard]] HookDescriptor MakeMethodHook(Address original, const char* name, Invoker /*invoker*/) noexcept
{
    using Thunk = MethodThunk<Method, Invoker>;
    return {original, reinterpret_cast<const void*>(&Thunk::Entry), name, CallingConvention::Thiscall,
            Thunk::kCalleeStackBytes};
}

template <typename C, typename... Args>
struct ConstructorThunk {
    /// MSVC constructors return `this` in EAX.
    static C* __fastcall Entry(C* self, void* /*edx*/, Args... args) noexcept
    {
        return ::new (static_cast<void*>(self)) C(args...);
    }
};

template <typename C>
struct DestructorThunk {
    static void __fastcall Entry(C* self, void* /*edx*/) noexcept { self->C::~C(); }
};

template <typename C>
struct DeletingDestructorThunk {
    /// Scalar deleting destructor: bit 0 of `flags` frees the object through the class's operator delete, which
    /// must be the game heap (see bw::GameAllocated) because original code allocated it.
    static C* __fastcall Entry(C* self, void* /*edx*/, unsigned int flags) noexcept
    {
        self->C::~C();
        if ((flags & 1u) != 0) {
            C::operator delete(self);
        }
        return self;
    }
};

template <typename C, typename... Args>
[[nodiscard]] HookDescriptor MakeConstructorHook(Address original, const char* name) noexcept
{
    return {original, reinterpret_cast<const void*>(&ConstructorThunk<C, Args...>::Entry), name,
            CallingConvention::Thiscall, kStackBytes<Args...>};
}

template <typename C>
[[nodiscard]] HookDescriptor MakeDestructorHook(Address original, const char* name) noexcept
{
    return {original, reinterpret_cast<const void*>(&DestructorThunk<C>::Entry), name, CallingConvention::Thiscall, 0};
}

template <typename C>
[[nodiscard]] HookDescriptor MakeDeletingDestructorHook(Address original, const char* name) noexcept
{
    return {original, reinterpret_cast<const void*>(&DeletingDestructorThunk<C>::Entry), name,
            CallingConvention::Thiscall, kStackBytes<unsigned int>};
}

}  // namespace detail
}  // namespace bw

#define BW_DETAIL_CONCAT_INNER(a, b) a##b
#define BW_DETAIL_CONCAT(a, b) BW_DETAIL_CONCAT_INNER(a, b)
#define BW_DETAIL_HOOK_VARIABLE BW_DETAIL_CONCAT(bwHook_, __COUNTER__)

#define BW_HOOK_FUNCTION(address, function)                                                                \
    static const ::bw::detail::HookRegistrar BW_DETAIL_HOOK_VARIABLE                                       \
    {                                                                                                      \
        ::bw::detail::MakeFunctionHook(address, &function, #function)                                      \
    }

#define BW_HOOK_METHOD(address, Class, Method)                                                             \
    static const ::bw::detail::HookRegistrar BW_DETAIL_HOOK_VARIABLE                                       \
    {                                                                                                      \
        ::bw::detail::MakeMethodHook<decltype(&Class::Method)>(                                            \
            address, #Class "::" #Method, [](Class* self, auto&&... args) -> decltype(auto) {              \
                return self->Class::Method(static_cast<decltype(args)&&>(args)...);                        \
            })                                                                                             \
    }

#define BW_HOOK_CONSTRUCTOR(address, Class, ...)                                                           \
    static const ::bw::detail::HookRegistrar BW_DETAIL_HOOK_VARIABLE                                       \
    {                                                                                                      \
        ::bw::detail::MakeConstructorHook<Class __VA_OPT__(, ) __VA_ARGS__>(address, #Class "::" #Class)   \
    }

#define BW_HOOK_DESTRUCTOR(address, Class)                                                                 \
    static const ::bw::detail::HookRegistrar BW_DETAIL_HOOK_VARIABLE                                       \
    {                                                                                                      \
        ::bw::detail::MakeDestructorHook<Class>(address, #Class "::~" #Class)                              \
    }

#define BW_HOOK_DELETING_DESTRUCTOR(address, Class)                                                        \
    static const ::bw::detail::HookRegistrar BW_DETAIL_HOOK_VARIABLE                                       \
    {                                                                                                      \
        ::bw::detail::MakeDeletingDestructorHook<Class>(address, #Class "::`scalar deleting destructor'")  \
    }
