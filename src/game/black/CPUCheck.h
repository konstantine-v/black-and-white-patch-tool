#pragma once

// Processor and display-adapter report written to Config<n>.txt the first time a detail level is configured
// (Config.cpp, 0x00455f80). Original source file unknown (no assertions); the class sits between Config.cpp and
// the camera code in the link order.

#include "core/GameHeap.h"
#include "core/Layout.h"
#include "core/Windows.h"

#include <cstdint>
#include <string_view>

namespace black {

/// Vendor as classified by CPUCheck::Identify. Values 0..6 are the original's; Unknown replaces the uninitialised
/// value the original left for unlisted vendor strings.
enum class CpuVendor : std::int32_t {
    Intel = 0,
    Amd = 1,
    Umc = 2,
    Cyrix = 3,
    NexGen = 4,
    Centaur = 5,
    Rise = 6,
    Unknown = 7,
};

/// CPUID leaf 1 EAX restricted to the bits the game keeps (0..13).
struct CpuSignature {
    std::uint32_t raw = 0;

    [[nodiscard]] constexpr std::uint32_t Stepping() const noexcept { return raw & 0xF; }
    [[nodiscard]] constexpr std::uint32_t Model() const noexcept { return (raw >> 4) & 0xF; }
    [[nodiscard]] constexpr std::uint32_t Family() const noexcept { return (raw >> 8) & 0xF; }
    [[nodiscard]] constexpr std::uint32_t Type() const noexcept { return (raw >> 12) & 0x3; }
};

/// Matches the 12-character CPUID vendor id against the vendors the game knows.
[[nodiscard]] CpuVendor ParseCpuVendor(std::string_view vendorId) noexcept;

/// "Primary Processor", "Overdrive Processor", ... from the processor type bits.
[[nodiscard]] std::string_view CpuTypeName(CpuSignature signature) noexcept;

/// The game's 2001 processor table. Returns an empty view when the table has no entry for a family 4/5/6 model
/// (the original then left the name buffer uninitialised).
[[nodiscard]] std::string_view CpuModelName(CpuVendor vendor, CpuSignature signature) noexcept;

/// The original "early Pentium" test: family 5. The shipped code tested `(eax & 0x500) == 0x500`, which is also
/// true for families 7 and 15 (every modern AMD processor reports base family 15).
[[nodiscard]] constexpr bool IsEarlyPentiumSignature(std::uint32_t cpuidLeaf1Eax) noexcept
{
    return CpuSignature{cpuidLeaf1Eax}.Family() == 5;
}

class CPUCheck : public bw::GameAllocated {
public:
    static constexpr std::size_t kDisplayDevicesCapacity = 5000;

    CPUCheck() noexcept;                       // 0x0045c550
    virtual ~CPUCheck();                       // 0x0045c5a0; vftable 0x0080ae04, slot 0 = 0x0045c580

    CPUCheck(const CPUCheck&) = delete;
    CPUCheck& operator=(const CPUCheck&) = delete;

    /// Builds the report (processor, processor count and display adapters) into Report().
    void Run() noexcept;                       // 0x0045c610
    /// Reads CPUID and fills the vendor, signature, type and model name.
    void Identify() noexcept;                  // 0x0045c6e0
    /// Lists every DirectDraw device with its driver identification.
    void DescribeDisplayDevices() noexcept;    // 0x0045d1d0

    [[nodiscard]] const char* Report() const noexcept { return m_report; }

    [[nodiscard]] static bool IsEarlyPentium() noexcept;             // 0x0045c5e0
    [[nodiscard]] static bool HasCpuid() noexcept;                   // 0x0045d180
    [[nodiscard]] static std::uint32_t GetProcessorCount() noexcept; // 0x0045d1b0
    /// Core clock in MHz measured against the time-stamp counter over 100 ms. PCMain uses it to choose the default
    /// detail level when the registry has none.
    [[nodiscard]] static std::int32_t MeasureSpeedMHz() noexcept;    // 0x0045d4a0

    /// DirectDrawEnumerateA callback; `context` is the m_displayDevices buffer.
    static BOOL WINAPI EnumerateDisplayDevice(GUID* guid, char* description, char* name,
                                              void* context) noexcept; // 0x0045d260

private:
    friend struct CPUCheckLayout;

    CpuSignature m_signature;                  // 0x0004
    char m_vendorId[13];                       // 0x0008
    char* m_modelName;                         // 0x0018  200 bytes, game heap
    char* m_typeName;                          // 0x001c  100 bytes, game heap
    char* m_report;                            // 0x0020  1000 or 4000 bytes, game heap
    std::uint32_t m_unused[10];                // 0x0024  zeroed by the constructor, never read
    char m_displayDevices[kDisplayDevicesCapacity];  // 0x004c
    CpuVendor m_vendor;                        // 0x13d4
};

struct CPUCheckLayout {
    BW_ASSERT_OFFSET(CPUCheck, m_signature, 0x0004);
    BW_ASSERT_OFFSET(CPUCheck, m_vendorId, 0x0008);
    BW_ASSERT_OFFSET(CPUCheck, m_modelName, 0x0018);
    BW_ASSERT_OFFSET(CPUCheck, m_typeName, 0x001c);
    BW_ASSERT_OFFSET(CPUCheck, m_report, 0x0020);
    BW_ASSERT_OFFSET(CPUCheck, m_unused, 0x0024);
    BW_ASSERT_OFFSET(CPUCheck, m_displayDevices, 0x004c);
    BW_ASSERT_OFFSET(CPUCheck, m_vendor, 0x13d4);
    BW_ASSERT_SIZE(CPUCheck, 0x13d8);
};

}  // namespace black
