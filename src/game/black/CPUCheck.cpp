#include "game/black/CPUCheck.h"

#include "core/Hook.h"
#include "core/Log.h"
#include "core/Text.h"
#include "game/Imports.h"

#include <ddraw.h>
#include <intrin.h>

#include <algorithm>
#include <array>
#include <limits>

namespace black {

namespace {

constexpr std::size_t kModelNameSize = 200;
constexpr std::size_t kTypeNameSize = 100;
constexpr std::size_t kShortReportSize = 1000;
constexpr std::size_t kFullReportSize = 4000;

constexpr std::string_view kUnknownProcessor = "Unknown processor type";
constexpr std::string_view kFamily15Processor = "Intel P4 or some variant";

struct VendorEntry {
    std::string_view id;
    CpuVendor vendor;
};

// Checked in the original's order.
constexpr std::array kVendors{
    VendorEntry{"GenuineIntel", CpuVendor::Intel},
    // The original compared against "UMC UMC UMC", which can never equal the 12-character id; the real id ends in a
    // space.
    VendorEntry{"UMC UMC UMC ", CpuVendor::Umc},
    VendorEntry{"CyrixInstead", CpuVendor::Cyrix},
    VendorEntry{"AuthenticAMD", CpuVendor::Amd},
    VendorEntry{"RiseRiseRise", CpuVendor::Rise},
    VendorEntry{"NexGenDriven", CpuVendor::NexGen},
    VendorEntry{"CentaurHauls", CpuVendor::Centaur},
};

struct ModelEntry {
    CpuVendor vendor;
    std::uint8_t family;
    std::uint8_t model;
    std::string_view name;
};

using enum CpuVendor;

constexpr std::array kModels{
    // Family 4
    ModelEntry{Intel, 4, 0x0, "Intel 486DX 25/33"},
    ModelEntry{Intel, 4, 0x1, "Intel 486DX 50"},
    ModelEntry{Intel, 4, 0x2, "Intel 486SX"},
    ModelEntry{Intel, 4, 0x3, "Intel 486DX2"},
    ModelEntry{Intel, 4, 0x4, "Intel 486SL"},
    ModelEntry{Intel, 4, 0x5, "Intel 486SX2"},
    ModelEntry{Intel, 4, 0x7, "Intel 486DX2WB"},
    ModelEntry{Intel, 4, 0x8, "Intel 486DX4"},
    ModelEntry{Intel, 4, 0x9, "Intel 486DX4WB"},
    ModelEntry{Amd, 4, 0x3, "AMD 486DX2"},
    ModelEntry{Amd, 4, 0x7, "AMD 486DX2WB"},
    ModelEntry{Amd, 4, 0x8, "AMD 486DX4"},
    ModelEntry{Amd, 4, 0x9, "AMD 486DX4WB"},
    ModelEntry{Amd, 4, 0xE, "AMD 5x86"},
    ModelEntry{Amd, 4, 0xF, "AMD 5x86WB"},
    ModelEntry{Umc, 4, 0x1, "UMC U5D (486)"},
    ModelEntry{Umc, 4, 0x2, "UMC U5S (486)"},
    ModelEntry{Cyrix, 4, 0x4, "Cyrix MediaGX (GX/GXm)"},
    ModelEntry{Cyrix, 4, 0x9, "Cyrix 5x86"},
    // Family 5
    ModelEntry{Intel, 5, 0x0, "Intel P5 A-step"},
    ModelEntry{Intel, 5, 0x1, "Intel P5"},
    ModelEntry{Intel, 5, 0x2, "Intel P54C"},
    ModelEntry{Intel, 5, 0x3, "Intel P24T Overdrive"},
    ModelEntry{Intel, 5, 0x4, "Intel P55C"},
    ModelEntry{Intel, 5, 0x7, "Intel P54C"},
    ModelEntry{Intel, 5, 0x8, "Intel P55C (0.25 Micrometer process)"},
    ModelEntry{Amd, 5, 0x0, "AMD SSA5 (PR75/PR90/PR100)"},
    ModelEntry{Amd, 5, 0x1, "AMD 5k86 (PR120/PR133)"},
    ModelEntry{Amd, 5, 0x2, "AMD 5k86 (PR166)"},
    ModelEntry{Amd, 5, 0x3, "AMD 5k86 (PR200)"},
    ModelEntry{Amd, 5, 0x6, "AMD K6(0.30 Micrometer process)"},
    ModelEntry{Amd, 5, 0x7, "AMD K6(0.25 Micrometer process)"},
    ModelEntry{Amd, 5, 0x8, "AMD K6/2"},
    ModelEntry{Amd, 5, 0x9, "AMD K6-III"},
    ModelEntry{Amd, 5, 0xD, "AMD K6-2+ or K6-III+  (0.18 Micrometer process)"},
    ModelEntry{Cyrix, 5, 0x0, "Cyrix 686MX"},
    ModelEntry{Cyrix, 5, 0x2, "Cyrix 686"},
    ModelEntry{Cyrix, 5, 0x5, "VIA Cyrix III"},
    ModelEntry{NexGen, 5, 0x0, "NexGen Nx586/Nx586FPU (only later ones)"},
    ModelEntry{Centaur, 5, 0x4, "Centaur/IDT C6"},
    ModelEntry{Centaur, 5, 0x8, "Centaur/IDT C2"},
    ModelEntry{Centaur, 5, 0x9, "Centaur/IDT C3"},
    ModelEntry{Rise, 5, 0x0, "RISE mP6 (0.25 Micrometer process)"},
    ModelEntry{Rise, 5, 0x2, "RISE mP6 (0.18 Micrometer process)"},
    // Family 6
    ModelEntry{Intel, 6, 0x0, "Intel P6 A-Step"},
    ModelEntry{Intel, 6, 0x1, "Intel P6"},
    ModelEntry{Intel, 6, 0x3, "Intel P2 (0.28 Micrometer process)"},
    ModelEntry{Intel, 6, 0x5, "Intel P2 (0.25 Micrometer process)"},
    ModelEntry{Intel, 6, 0x6, "Intel P2 with on-die L2 cache"},
    ModelEntry{Intel, 6, 0x7, "Intel P3 (0.25 Micrometer process)"},
    ModelEntry{Intel, 6, 0x8, "Intel P3 (0.18 Micrometer process) with 256 K on-die L2 cache"},
    ModelEntry{Intel, 6, 0xA, "Intel P3 (0.18 Micrometer process) with 1/2 Meg on-die L2 cache"},
    ModelEntry{Amd, 6, 0x1, "AMD K7 (0.25 Micrometer process)"},
    ModelEntry{Amd, 6, 0x2, "AMD K7 (0.18 Micrometer process)"},
    ModelEntry{Amd, 6, 0x3, "AMD K7 (0.18 Micrometer process) with 64 K on-die L2 cache"},
    ModelEntry{Amd, 6, 0x4, "AMD K7 (0.18 Micrometer process) with 256 K on-die L2 cache"},
};

struct CpuidRegisters {
    std::uint32_t eax, ebx, ecx, edx;
};

CpuidRegisters Cpuid(std::uint32_t leaf) noexcept
{
    std::array<int, 4> registers{};
    __cpuid(registers.data(), static_cast<int>(leaf));
    return {static_cast<std::uint32_t>(registers[0]), static_cast<std::uint32_t>(registers[1]),
            static_cast<std::uint32_t>(registers[2]), static_cast<std::uint32_t>(registers[3])};
}

/// The processor brand string (CPUID 0x80000002..4), or empty when the processor has none.
std::array<char, 49> BrandString() noexcept
{
    std::array<char, 49> brand{};
    if (Cpuid(0x8000'0000).eax < 0x8000'0004) {
        return brand;
    }
    for (std::uint32_t leaf = 0; leaf < 3; ++leaf) {
        const CpuidRegisters registers = Cpuid(0x8000'0002 + leaf);
        std::memcpy(brand.data() + 16 * leaf, &registers, sizeof registers);
    }
    return brand;
}

void ReplaceString(char*& text, std::size_t size) noexcept
{
    bw::GameFree(text);
    text = bw::GameAllocateString(size);
}

}  // namespace

CpuVendor ParseCpuVendor(std::string_view vendorId) noexcept
{
    const auto entry = std::ranges::find(kVendors, vendorId, &VendorEntry::id);
    return entry != kVendors.end() ? entry->vendor : CpuVendor::Unknown;
}

std::string_view CpuTypeName(CpuSignature signature) noexcept
{
    switch (signature.Type()) {
    case 0: return "Primary Processor";
    case 1: return "Overdrive Processor";
    case 2: return "Secondary Processor";
    default: return "Wierd - this bitstring (11b) is reserved.";
    }
}

std::string_view CpuModelName(CpuVendor vendor, CpuSignature signature) noexcept
{
    const std::uint32_t family = signature.Family();
    if (family == 15) {
        return kFamily15Processor;
    }
    if (family < 4 || family > 6) {
        return kUnknownProcessor;
    }
    const auto entry = std::ranges::find_if(kModels, [&](const ModelEntry& model) {
        return model.vendor == vendor && model.family == family && model.model == signature.Model();
    });
    return entry != kModels.end() ? entry->name : std::string_view{};
}

CPUCheck::CPUCheck() noexcept
    : m_signature{}, m_vendorId{}, m_modelName(nullptr), m_typeName(nullptr), m_report(nullptr), m_unused{},
      m_displayDevices{}, m_vendor(CpuVendor::Unknown)
{
}

CPUCheck::~CPUCheck()
{
    bw::GameFree(m_modelName);
    bw::GameFree(m_typeName);
    bw::GameFree(m_report);
}

void CPUCheck::Run() noexcept
{
    bw::GameFree(m_report);
    m_report = nullptr;

    DescribeDisplayDevices();
    const std::string_view displayDevices = m_displayDevices;

    if (!HasCpuid()) {
        m_report = bw::GameAllocateString(kShortReportSize);
        bw::text::FormatInto(
            m_report, kShortReportSize,
            "CPUID Is not present on this PC. (Either its a poor Intel clone, or a pre-pentium chip).\n{}",
            displayDevices);
        return;
    }
    if (IsEarlyPentium()) {
        m_report = bw::GameAllocateString(kShortReportSize);
        bw::text::FormatInto(m_report, kShortReportSize, "Processor is an Early Pentium\n{}", displayDevices);
        return;
    }

    m_report = bw::GameAllocateString(kFullReportSize);
    Identify();
    bw::text::FormatInto(m_report, kFullReportSize,
                         "Number of processors:{}\nProcessor Manufacturer:{}\nProcessor Model:{}\n"
                         "Processor Stepping:{:x}\n{}",
                         GetProcessorCount(), std::string_view(m_vendorId), std::string_view(m_modelName),
                         m_signature.Stepping(), displayDevices);
}

void CPUCheck::Identify() noexcept
{
    ReplaceString(m_typeName, kTypeNameSize);
    ReplaceString(m_modelName, kModelNameSize);

    m_signature = CpuSignature{Cpuid(1).eax & 0x3FFF};

    // CPUID 0 returns the vendor id in EBX, EDX, ECX order.
    const CpuidRegisters leaf0 = Cpuid(0);
    std::memcpy(m_vendorId + 0, &leaf0.ebx, 4);
    std::memcpy(m_vendorId + 4, &leaf0.edx, 4);
    std::memcpy(m_vendorId + 8, &leaf0.ecx, 4);
    m_vendorId[12] = '\0';
    m_vendor = ParseCpuVendor(m_vendorId);

    bw::text::CopyInto(m_typeName, kTypeNameSize, CpuTypeName(m_signature));

    std::string_view model = CpuModelName(m_vendor, m_signature);
    const std::array<char, 49> brand = BrandString();
    if (model.empty()) {
        // The original left the buffer uninitialised here, which is every current Intel model the 2001 table lacks.
        model = brand[0] != '\0' ? std::string_view(brand.data()) : kUnknownProcessor;
    }
    bw::text::CopyInto(m_modelName, kModelNameSize, model);
}

void CPUCheck::DescribeDisplayDevices() noexcept
{
    bw::text::CopyInto(m_displayDevices, kDisplayDevicesCapacity, "\nDirectDraw says:\n");
    if (FAILED(imports::s_DirectDrawEnumerateA(&EnumerateDisplayDevice, m_displayDevices))) {
        bw::text::AppendInto(m_displayDevices, kDisplayDevicesCapacity,
                             "That we could not enumerate the devices (strange!).");
    }
    bw::text::AppendInto(m_displayDevices, kDisplayDevicesCapacity, "\n");
}

bool CPUCheck::IsEarlyPentium() noexcept
{
    return IsEarlyPentiumSignature(Cpuid(1).eax);
}

bool CPUCheck::HasCpuid() noexcept
{
    // CPUID exists when software can toggle EFLAGS.ID (bit 21).
    constexpr unsigned int kIdFlag = 1u << 21;
    const unsigned int original = __readeflags();
    __writeeflags(original ^ kIdFlag);
    const unsigned int toggled = __readeflags();
    __writeeflags(original);
    return ((original ^ toggled) & kIdFlag) != 0;
}

std::uint32_t CPUCheck::GetProcessorCount() noexcept
{
    SYSTEM_INFO info{};
    GetSystemInfo(&info);
    return info.dwNumberOfProcessors;
}

std::int32_t CPUCheck::MeasureSpeedMHz() noexcept
{
    // The original divided TSC ticks by GetTickCount milliseconds (±16 ms resolution) around Sleep(100); the
    // performance counter gives the same quantity precisely.
    LARGE_INTEGER frequency{};
    LARGE_INTEGER start{};
    LARGE_INTEGER end{};
    QueryPerformanceFrequency(&frequency);
    QueryPerformanceCounter(&start);
    const std::uint64_t tscStart = __rdtsc();
    Sleep(100);
    QueryPerformanceCounter(&end);
    const std::uint64_t tscEnd = __rdtsc();

    const double seconds = static_cast<double>(end.QuadPart - start.QuadPart) / static_cast<double>(frequency.QuadPart);
    const double megahertz = seconds > 0.0 ? static_cast<double>(tscEnd - tscStart) / seconds / 1.0e6 : 0.0;
    const auto speed = static_cast<std::int32_t>(
        std::clamp(megahertz, 0.0, static_cast<double>(std::numeric_limits<std::int32_t>::max())));
    bw::log::Info("CPU SPEED {}", speed);
    return speed;
}

BOOL WINAPI CPUCheck::EnumerateDisplayDevice(GUID* guid, char* description, char* name, void* context) noexcept
{
    auto* devices = static_cast<char*>(context);
    constexpr std::size_t capacity = kDisplayDevicesCapacity;

    IDirectDraw7* directDraw = nullptr;
    if (FAILED(imports::s_DirectDrawCreateEx(guid, reinterpret_cast<void**>(&directDraw), IID_IDirectDraw7,
                                             nullptr))) {
        bw::text::AppendInto(devices, capacity,
                             "\nCould not create DirectDraw device to get details from, but managed to get:\n");
        bw::text::AppendFormatInto(devices, capacity, "Driver desc:{}, Driver name:{}\n", description, name);
        return TRUE;
    }

    DDDEVICEIDENTIFIER2 identifier{};
    directDraw->GetDeviceIdentifier(&identifier, 0);
    directDraw->Release();

    // The labels are the original's: "desc" prints the driver file and "name" the adapter description, and the
    // "0x%u" fields print decimal numbers.
    const DWORD high = static_cast<DWORD>(identifier.liDriverVersion.HighPart);
    const DWORD low = identifier.liDriverVersion.LowPart;
    bw::text::AppendFormatInto(devices, capacity, "Driver desc:{}, Driver name:{}\n",
                               std::string_view(identifier.szDriver), std::string_view(identifier.szDescription));
    bw::text::AppendFormatInto(devices, capacity,
                               "Driver info:Product:0x{}, Version:0x{}, SubVersion:0x{}, Build:0x{}\n", HIWORD(high),
                               LOWORD(high), HIWORD(low), LOWORD(low));
    bw::text::AppendFormatInto(devices, capacity,
                               "Driver Vendor ID:0x{:X}, Driver device ID:0x{:X}\nDriver SubSysID:0x{:X} ,"
                               "Driver Revision:0x{:X}\n",
                               identifier.dwVendorId, identifier.dwDeviceId, identifier.dwSubSysId,
                               identifier.dwRevision);
    bw::text::AppendFormatInto(devices, capacity, "Driver Windows Hardware Quality Lab Level:{}\n",
                               identifier.dwWHQLLevel);
    return TRUE;
}

BW_HOOK_CONSTRUCTOR(0x0045c550, CPUCheck);
BW_HOOK_DELETING_DESTRUCTOR(0x0045c580, CPUCheck);
BW_HOOK_DESTRUCTOR(0x0045c5a0, CPUCheck);
BW_HOOK_FUNCTION(0x0045c5e0, CPUCheck::IsEarlyPentium);
BW_HOOK_METHOD(0x0045c610, CPUCheck, Run);
BW_HOOK_METHOD(0x0045c6e0, CPUCheck, Identify);
BW_HOOK_FUNCTION(0x0045d180, CPUCheck::HasCpuid);
BW_HOOK_FUNCTION(0x0045d1b0, CPUCheck::GetProcessorCount);
BW_HOOK_METHOD(0x0045d1d0, CPUCheck, DescribeDisplayDevices);
BW_HOOK_FUNCTION(0x0045d260, CPUCheck::EnumerateDisplayDevice);
BW_HOOK_FUNCTION(0x0045d4a0, CPUCheck::MeasureSpeedMHz);

}  // namespace black
