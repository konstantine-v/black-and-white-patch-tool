#pragma once

#include "core/Windows.h"

#include <utility>

namespace bw {

/// Owns a kernel handle and closes it. Both "no handle" values of the Windows API (nullptr and
/// INVALID_HANDLE_VALUE) count as empty.
class UniqueHandle {
public:
    UniqueHandle() noexcept = default;
    explicit UniqueHandle(HANDLE handle) noexcept : m_handle(handle) {}
    ~UniqueHandle() { Reset(); }
    UniqueHandle(UniqueHandle&& other) noexcept : m_handle(std::exchange(other.m_handle, nullptr)) {}
    UniqueHandle& operator=(UniqueHandle&& other) noexcept
    {
        if (this != &other) {
            Reset(std::exchange(other.m_handle, nullptr));
        }
        return *this;
    }
    UniqueHandle(const UniqueHandle&) = delete;
    UniqueHandle& operator=(const UniqueHandle&) = delete;

    [[nodiscard]] HANDLE Get() const noexcept { return m_handle; }
    explicit operator bool() const noexcept { return m_handle != nullptr && m_handle != INVALID_HANDLE_VALUE; }

    /// Gives up ownership without closing.
    [[nodiscard]] HANDLE Release() noexcept { return std::exchange(m_handle, nullptr); }
    void Reset(HANDLE handle = nullptr) noexcept
    {
        if (*this) {
            CloseHandle(m_handle);
        }
        m_handle = handle;
    }

private:
    HANDLE m_handle = nullptr;
};

}  // namespace bw
