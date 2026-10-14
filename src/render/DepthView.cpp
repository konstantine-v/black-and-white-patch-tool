#include "render/DepthView.h"

#include "core/Log.h"
#include "core/Windows.h"

#include <algorithm>
#include <atomic>
#include <cstring>

namespace bw::render {

namespace {

constexpr std::size_t kPage = 4096;

/// The view whose pages the exception handler fills (one depth buffer is read at a time).
std::atomic<DepthView*> g_active{nullptr};
std::atomic<bool> g_handlerInstalled{false};

}  // namespace

DepthView::~DepthView()
{
    DepthView* self = this;
    g_active.compare_exchange_strong(self, nullptr);
    if (m_memory != nullptr) {
        VirtualFree(m_memory, 0, MEM_RELEASE);
    }
}

std::uint8_t* DepthView::Begin(gpu::DepthBuffer& depth, Format format, std::uint32_t width, std::uint32_t height,
                               std::int32_t pitch) noexcept
{
    if (!g_handlerInstalled.exchange(true)) {
        AddVectoredExceptionHandler(1, &Handler);  // first: before the diagnostics' exception logger
    }
    const std::size_t size = (std::size_t(pitch) * height + kPage - 1) & ~(kPage - 1);
    if (m_memory == nullptr || m_size != size) {
        if (m_memory != nullptr) {
            VirtualFree(m_memory, 0, MEM_RELEASE);
        }
        m_memory = static_cast<std::uint8_t*>(VirtualAlloc(nullptr, size, MEM_RESERVE | MEM_COMMIT, PAGE_NOACCESS));
        m_size = m_memory != nullptr ? size : 0;
        try {
            const std::size_t rowsPerPage = kPage / std::max<std::size_t>(std::size_t(pitch), 1) + 2;
            m_values.resize(rowsPerPage * width);
            m_rows.resize(rowsPerPage * std::size_t(pitch));
        }
        catch (const std::bad_alloc&) {
            return nullptr;
        }
    }
    else {
        DWORD previous = 0;
        VirtualProtect(m_memory, m_size, PAGE_NOACCESS, &previous);
    }
    if (m_memory == nullptr) {
        return nullptr;
    }
    m_depth = &depth;
    m_format = format;
    m_width = width;
    m_height = height;
    m_pitch = pitch;
    g_active.store(this);
    return m_memory;
}

bool DepthView::Fill(std::uintptr_t address) noexcept
{
    const auto base = reinterpret_cast<std::uintptr_t>(m_memory);
    const std::uintptr_t page = address & ~std::uintptr_t{kPage - 1};
    const std::size_t pageOffset = page - base;
    const auto firstRow = static_cast<std::uint32_t>(pageOffset / m_pitch);
    const auto lastRow = std::min(static_cast<std::uint32_t>((pageOffset + kPage - 1) / m_pitch), m_height - 1);

    DWORD previous = 0;
    if (!VirtualProtect(reinterpret_cast<void*>(page), kPage, PAGE_READWRITE, &previous)) {
        return false;
    }
    if (firstRow < m_height) {
        const RECT rows{0, static_cast<LONG>(firstRow), static_cast<LONG>(m_width), static_cast<LONG>(lastRow + 1)};
        if (m_depth->Read(rows, m_values.data())) {
            const double scale = m_format == Format::D16 ? 65535.0 : (m_format == Format::D32 ? 4294967295.0 : 16777215.0);
            const int shift = m_format == Format::D24S8 || m_format == Format::D24X8 ? 8 : 0;
            const std::uint32_t step = BytesPerPixel(m_format);
            for (std::uint32_t row = 0; row <= lastRow - firstRow; ++row) {
                std::uint8_t* out = m_rows.data() + std::size_t(row) * m_pitch;
                for (std::uint32_t x = 0; x < m_width; ++x) {
                    const float depth = std::clamp(m_values[std::size_t(row) * m_width + x], 0.0f, 1.0f);
                    const auto value = static_cast<std::uint32_t>(depth * scale + 0.5) << shift;
                    std::memcpy(out + std::size_t(x) * step, &value, step);
                }
            }
            // Copy the part of these rows that lies in this page (rows may straddle the neighbouring pages).
            const std::size_t rowsBegin = std::size_t(firstRow) * m_pitch;
            const std::size_t from = pageOffset - rowsBegin;
            const std::size_t available = std::size_t(lastRow - firstRow + 1) * m_pitch - from;
            std::memcpy(reinterpret_cast<void*>(page), m_rows.data() + from, std::min(kPage, available));
        }
    }
    return true;
}

long __stdcall DepthView::Handler(EXCEPTION_POINTERS* exception) noexcept
{
    const EXCEPTION_RECORD& record = *exception->ExceptionRecord;
    if (record.ExceptionCode != EXCEPTION_ACCESS_VIOLATION || record.NumberParameters < 2) {
        return EXCEPTION_CONTINUE_SEARCH;
    }
    DepthView* view = g_active.load();
    const std::uintptr_t address = record.ExceptionInformation[1];
    if (view == nullptr || address < reinterpret_cast<std::uintptr_t>(view->m_memory) ||
        address >= reinterpret_cast<std::uintptr_t>(view->m_memory) + view->m_size) {
        return EXCEPTION_CONTINUE_SEARCH;
    }
    return view->Fill(address) ? EXCEPTION_CONTINUE_EXECUTION : EXCEPTION_CONTINUE_SEARCH;
}

}  // namespace bw::render
