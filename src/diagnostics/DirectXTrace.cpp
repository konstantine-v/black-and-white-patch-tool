#include "diagnostics/DirectXTrace.h"

#include "core/Image.h"
#include "core/Log.h"
#include "core/Patch.h"

#include <ddraw.h>
#include <d3d.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <new>
#include <format>
#include <fstream>
#include <mutex>
#include <vector>

namespace bw::diagnostics {

namespace {

#include "diagnostics/DirectXMethods.inc"

struct InterfaceInfo {
    const char* name;
    std::span<const char* const> methods;
};

enum class Interface : std::uint8_t {
    DirectDraw,
    DirectDraw2,
    DirectDraw4,
    DirectDraw7,
    Surface,
    Surface2,
    Surface3,
    Surface4,
    Surface7,
    Clipper,
    Palette,
    GammaControl,
    Direct3D7,
    Device7,
    VertexBuffer7,
};

constexpr InterfaceInfo kInterfaces[] = {
    {"IDirectDraw", kIDirectDrawMethods},
    {"IDirectDraw2", kIDirectDraw2Methods},
    {"IDirectDraw4", kIDirectDraw4Methods},
    {"IDirectDraw7", kIDirectDraw7Methods},
    {"IDirectDrawSurface", kIDirectDrawSurfaceMethods},
    {"IDirectDrawSurface2", kIDirectDrawSurface2Methods},
    {"IDirectDrawSurface3", kIDirectDrawSurface3Methods},
    {"IDirectDrawSurface4", kIDirectDrawSurface4Methods},
    {"IDirectDrawSurface7", kIDirectDrawSurface7Methods},
    {"IDirectDrawClipper", kIDirectDrawClipperMethods},
    {"IDirectDrawPalette", kIDirectDrawPaletteMethods},
    {"IDirectDrawGammaControl", kIDirectDrawGammaControlMethods},
    {"IDirect3D7", kIDirect3D7Methods},
    {"IDirect3DDevice7", kIDirect3DDevice7Methods},
    {"IDirect3DVertexBuffer7", kIDirect3DVertexBuffer7Methods},
};

constexpr std::uint32_t MethodId(Interface kind, std::size_t slot) noexcept
{
    return (static_cast<std::uint32_t>(kind) << 8) | static_cast<std::uint32_t>(slot);
}

/// Bytes per generated stub:
///   push <method id>; push dword ptr [esp+4] (the caller's return address); call RecordCall; add esp, 8;
///   jmp dword ptr [original]
/// The stub clobbers only EAX/ECX/EDX, which a COM (stdcall) method may not expect to receive anything in.
constexpr std::size_t kStubSize = 32;

CallCounter* g_counter = nullptr;
std::filesystem::path g_output;
decltype(&::DirectDrawCreateEx) g_directDrawCreateEx = nullptr;
std::once_flag g_instrumentOnce;
std::mutex g_instrumentedMutex;
/// An instrumented method table: where it is, the stubs written into it and the targets they continue to.
struct InstrumentedTable {
    void** table;
    std::uint8_t* stubs;
    void** originals;
    std::size_t size;
};
std::vector<InstrumentedTable> g_instrumentedTables;
/// Original method address -> method id, for finding method pointers the game copied out of a table.
std::vector<std::pair<Address, std::uint32_t>> g_originalMethods;
HMODULE g_game = nullptr;
image::Section g_gameText{};

extern "C" void __cdecl RecordCall(std::uint32_t caller, std::uint32_t method) noexcept
{
    g_counter->Add(method, caller);
}

void Write32(std::uint8_t* at, std::uint32_t value) noexcept
{
    std::memcpy(at, &value, sizeof value);
}

/// Points every method of `object`'s vtable at a counting stub (once per vtable).
void Instrument(void* object, Interface kind) noexcept
{
    if (object == nullptr) {
        return;
    }
    void** table = *static_cast<void***>(object);
    const std::span<const char* const> methods = kInterfaces[static_cast<std::size_t>(kind)].methods;
    std::lock_guard lock(g_instrumentedMutex);
    if (std::ranges::find(g_instrumentedTables, table, &InstrumentedTable::table) != g_instrumentedTables.end()) {
        return;
    }
    auto* stubs = static_cast<std::uint8_t*>(
        VirtualAlloc(nullptr, methods.size() * kStubSize, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
    auto* originals = new (std::nothrow) void*[methods.size()];
    if (stubs == nullptr || originals == nullptr) {
        log::Warning("TraceDirectX: out of memory instrumenting {}", kInterfaces[static_cast<std::size_t>(kind)].name);
        return;
    }
    for (std::size_t slot = 0; slot < methods.size(); ++slot) {
        originals[slot] = table[slot];
        g_originalMethods.emplace_back(reinterpret_cast<Address>(table[slot]), MethodId(kind, slot));
        std::uint8_t* stub = stubs + slot * kStubSize;
        stub[0] = 0x68;  // push imm32
        Write32(stub + 1, MethodId(kind, slot));
        stub[5] = 0xff;  // push dword ptr [esp+4]
        stub[6] = 0x74;
        stub[7] = 0x24;
        stub[8] = 0x04;
        stub[9] = 0xe8;  // call rel32
        Write32(stub + 10, static_cast<std::uint32_t>(reinterpret_cast<Address>(&RecordCall) -
                                                      reinterpret_cast<Address>(stub + 14)));
        stub[14] = 0x83;  // add esp, 8
        stub[15] = 0xc4;
        stub[16] = 0x08;
        stub[17] = 0xff;  // jmp dword ptr [m32]
        stub[18] = 0x25;
        Write32(stub + 19, static_cast<std::uint32_t>(reinterpret_cast<Address>(&originals[slot])));
    }
    patch::FlushCode(reinterpret_cast<Address>(stubs), methods.size() * kStubSize);

    DWORD previous = 0;
    if (!VirtualProtect(table, methods.size() * sizeof(void*), PAGE_READWRITE, &previous)) {
        log::Warning("TraceDirectX: cannot patch the {} method table", kInterfaces[static_cast<std::size_t>(kind)].name);
        return;
    }
    for (std::size_t slot = 0; slot < methods.size(); ++slot) {
        table[slot] = stubs + slot * kStubSize;
    }
    VirtualProtect(table, methods.size() * sizeof(void*), previous, &previous);
    g_instrumentedTables.push_back({table, stubs, originals, methods.size()});
}

template <typename T>
void Release(T*& object) noexcept
{
    if (object != nullptr) {
        object->Release();
        object = nullptr;
    }
}

/// Instruments every DirectDraw/Direct3D 7 interface by creating a throw-away object of each kind: ddraw.dll uses
/// one method table per interface, shared by all objects, so the game's objects use the same tables.
void InstrumentAll() noexcept
{
    IDirectDraw7* directDraw = nullptr;
    if (FAILED(g_directDrawCreateEx(nullptr, reinterpret_cast<void**>(&directDraw), IID_IDirectDraw7, nullptr))) {
        log::Warning("TraceDirectX: cannot create a DirectDraw object");
        return;
    }
    Instrument(directDraw, Interface::DirectDraw7);
    auto instrumentAs = [](IUnknown* object, const IID& iid, Interface kind) {
        IUnknown* other = nullptr;
        if (object != nullptr && SUCCEEDED(object->QueryInterface(iid, reinterpret_cast<void**>(&other)))) {
            Instrument(other, kind);
            other->Release();
        }
    };
    instrumentAs(directDraw, IID_IDirectDraw, Interface::DirectDraw);
    instrumentAs(directDraw, IID_IDirectDraw2, Interface::DirectDraw2);
    instrumentAs(directDraw, IID_IDirectDraw4, Interface::DirectDraw4);
    directDraw->SetCooperativeLevel(nullptr, DDSCL_NORMAL);

    IDirectDrawClipper* clipper = nullptr;
    if (SUCCEEDED(directDraw->CreateClipper(0, &clipper, nullptr))) {
        Instrument(clipper, Interface::Clipper);
    }
    PALETTEENTRY entries[256]{};
    IDirectDrawPalette* palette = nullptr;
    if (SUCCEEDED(directDraw->CreatePalette(DDPCAPS_8BIT | DDPCAPS_ALLOW256, entries, &palette, nullptr))) {
        Instrument(palette, Interface::Palette);
    }

    DDSURFACEDESC2 description{};
    description.dwSize = sizeof description;
    description.dwFlags = DDSD_CAPS;
    description.ddsCaps.dwCaps = DDSCAPS_PRIMARYSURFACE;
    IDirectDrawSurface7* primary = nullptr;
    if (SUCCEEDED(directDraw->CreateSurface(&description, &primary, nullptr))) {
        Instrument(primary, Interface::Surface7);
        instrumentAs(primary, IID_IDirectDrawGammaControl, Interface::GammaControl);
    }

    description.dwFlags = DDSD_CAPS | DDSD_WIDTH | DDSD_HEIGHT;
    description.dwWidth = 64;
    description.dwHeight = 64;
    description.ddsCaps.dwCaps = DDSCAPS_OFFSCREENPLAIN | DDSCAPS_3DDEVICE | DDSCAPS_VIDEOMEMORY;
    IDirectDrawSurface7* target = nullptr;
    if (SUCCEEDED(directDraw->CreateSurface(&description, &target, nullptr))) {
        Instrument(target, Interface::Surface7);
        instrumentAs(target, IID_IDirectDrawSurface, Interface::Surface);
        instrumentAs(target, IID_IDirectDrawSurface2, Interface::Surface2);
        instrumentAs(target, IID_IDirectDrawSurface3, Interface::Surface3);
        instrumentAs(target, IID_IDirectDrawSurface4, Interface::Surface4);
    }

    IDirect3D7* direct3D = nullptr;
    IDirect3DDevice7* device = nullptr;
    IDirect3DVertexBuffer7* vertexBuffer = nullptr;
    if (SUCCEEDED(directDraw->QueryInterface(IID_IDirect3D7, reinterpret_cast<void**>(&direct3D)))) {
        Instrument(direct3D, Interface::Direct3D7);
        if (target != nullptr && SUCCEEDED(direct3D->CreateDevice(IID_IDirect3DHALDevice, target, &device))) {
            Instrument(device, Interface::Device7);
        }
        D3DVERTEXBUFFERDESC buffer{};
        buffer.dwSize = sizeof buffer;
        buffer.dwFVF = D3DFVF_XYZ;
        buffer.dwNumVertices = 16;
        if (SUCCEEDED(direct3D->CreateVertexBuffer(&buffer, &vertexBuffer, 0))) {
            Instrument(vertexBuffer, Interface::VertexBuffer7);
        }
    }
    log::Info("TraceDirectX: {} method tables instrumented", g_instrumentedTables.size());

    Release(vertexBuffer);
    Release(device);
    Release(direct3D);
    Release(target);
    Release(primary);
    Release(palette);
    Release(clipper);
    Release(directDraw);
}

HRESULT WINAPI DirectDrawCreateExTraced(GUID* guid, void** directDraw, REFIID iid, IUnknown* outer)
{
    std::call_once(g_instrumentOnce, InstrumentAll);
    return g_directDrawCreateEx(guid, directDraw, iid, outer);
}

/// Logs every global of the game that holds a DirectX method address: calls through such a copy bypass the
/// instrumented tables and are missing from the counts.
void LogCopiedMethods()
{
    for (const char* name : {".data", ".rdata"}) {
        const std::optional<image::Section> section = image::FindSection(g_game, name);
        if (!section) {
            continue;
        }
        MEMORY_BASIC_INFORMATION memory{};
        for (Address address = section->begin; address + 4 <= section->end; address += 4) {
            if ((address & 0xfff) == 0) {
                if (VirtualQuery(reinterpret_cast<void*>(address), &memory, sizeof memory) == 0 ||
                    memory.State != MEM_COMMIT || (memory.Protect & (PAGE_NOACCESS | PAGE_GUARD)) != 0) {
                    address += 0xffc;
                    continue;
                }
            }
            const Address value = *reinterpret_cast<const Address*>(address);
            for (const auto& [method, id] : g_originalMethods) {
                if (value == method) {
                    const InterfaceInfo& info = kInterfaces[id >> 8];
                    log::Warning("TraceDirectX: 0x{:08x} holds a copy of {}::{} (calls through it are not counted)",
                                 address, info.name, info.methods[id & 0xff]);
                }
            }
        }
    }
}

}  // namespace

void CallCounter::Add(std::uint32_t method, std::uint32_t caller) noexcept
{
    const std::uint64_t key = (std::uint64_t{method} << 32) | caller;
    std::size_t index = static_cast<std::size_t>((key * 0x9e3779b97f4a7c15ull) >> 49) & (kCapacity - 1);
    for (std::size_t probe = 0; probe < kCapacity; ++probe, index = (index + 1) & (kCapacity - 1)) {
        Slot& slot = m_slots[index];
        std::uint64_t current = slot.key.load(std::memory_order_acquire);
        if (current == 0) {
            if (slot.key.compare_exchange_strong(current, key, std::memory_order_acq_rel)) {
                current = key;
            }
        }
        if (current == key) {
            slot.count.fetch_add(1, std::memory_order_relaxed);
            return;
        }
    }
    m_dropped.fetch_add(1, std::memory_order_relaxed);
}

void TraceDirect3DDevice(void* device) noexcept
{
    if (g_counter != nullptr && g_directDrawCreateEx != nullptr) {
        Instrument(device, Interface::Device7);
    }
}

void RefreshDirectXTrace() noexcept
{
    if (g_counter == nullptr) {
        return;
    }
    std::lock_guard lock(g_instrumentedMutex);
    for (const InstrumentedTable& entry : g_instrumentedTables) {
        for (std::size_t slot = 0; slot < entry.size; ++slot) {
            void* const stub = entry.stubs + slot * kStubSize;
            void* const current = entry.table[slot];
            if (current == stub) {
                continue;
            }
            DWORD previous = 0;
            if (VirtualProtect(&entry.table[slot], sizeof(void*), PAGE_READWRITE, &previous)) {
                entry.originals[slot] = current;
                entry.table[slot] = stub;
                VirtualProtect(&entry.table[slot], sizeof(void*), previous, &previous);
            }
        }
    }
}

void InstallDirectXTrace(HMODULE game, const std::filesystem::path& output) noexcept
{
    g_counter = new (std::nothrow) CallCounter();
    if (g_counter == nullptr) {
        return;
    }
    if (const std::optional<image::Section> text = image::FindSection(game, ".text")) {
        g_gameText = *text;
    }
    g_output = output;
    g_game = game;
    g_directDrawCreateEx = patch::ReplaceImport(game, "DDRAW.dll", "DirectDrawCreateEx", &DirectDrawCreateExTraced);
    if (g_directDrawCreateEx == nullptr) {
        log::Warning("TraceDirectX: DirectDrawCreateEx is not imported");
        return;
    }
    log::Info("TraceDirectX: recording DirectDraw/Direct3D calls to {}", output.string());
}

void WriteDirectXTrace() noexcept
{
    if (g_counter == nullptr || g_output.empty()) {
        return;
    }
    try {
        std::vector<CallCounter::Entry> entries;
        g_counter->ForEach([&](const CallCounter::Entry& entry) { entries.push_back(entry); });
        std::ranges::sort(entries, [](const auto& a, const auto& b) {
            return a.method != b.method ? a.method < b.method : a.count > b.count;
        });
        std::ofstream out(g_output);
        out << "interface,method,caller,in_game,count\n";
        for (const CallCounter::Entry& entry : entries) {
            const InterfaceInfo& info = kInterfaces[entry.method >> 8];
            const std::size_t slot = entry.method & 0xff;
            const bool inGame = entry.caller >= g_gameText.begin && entry.caller < g_gameText.end;
            out << std::format("{},{},0x{:08x},{},{}\n", info.name, info.methods[slot], entry.caller, inGame ? 1 : 0,
                               entry.count);
        }
        log::Info("TraceDirectX: {} call sites written ({} dropped)", entries.size(), g_counter->Dropped());
        LogCopiedMethods();
    }
    catch (...) {
        log::Warning("TraceDirectX: writing the report failed");
    }
}

}  // namespace bw::diagnostics
