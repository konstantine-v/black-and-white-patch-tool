#pragma once

#include "render/Format.h"
#include "render/Gpu.h"

#include <cstdint>
#include <vector>

namespace bw::render {

/// The CPU view of a depth buffer that Lock hands to the game. The game locks the whole buffer every frame to test
/// whether the sun is hidden but reads only a few values, so the view is filled lazily: all its pages start
/// inaccessible, and the first touch of a page faults into a handler that reads just those rows back from the GPU,
/// converts them to the game's depth format and lets the access continue.
class DepthView {
public:
    DepthView() noexcept = default;
    ~DepthView();
    DepthView(const DepthView&) = delete;
    DepthView& operator=(const DepthView&) = delete;

    /// Starts a lock of `depth` (`width` x `height`, rows of `pitch` bytes in `format`). Returns the view's first byte,
    /// or nullptr when it cannot be set up.
    [[nodiscard]] std::uint8_t* Begin(gpu::DepthBuffer& depth, Format format, std::uint32_t width, std::uint32_t height,
                                      std::int32_t pitch) noexcept;

private:
    static long __stdcall Handler(struct _EXCEPTION_POINTERS* exception) noexcept;
    bool Fill(std::uintptr_t address) noexcept;

    gpu::DepthBuffer* m_depth = nullptr;
    Format m_format = Format::D16;
    std::uint32_t m_width = 0;
    std::uint32_t m_height = 0;
    std::int32_t m_pitch = 0;
    std::uint8_t* m_memory = nullptr;
    std::size_t m_size = 0;
    std::vector<float> m_values;       // one page of rows read back
    std::vector<std::uint8_t> m_rows;  // the same rows converted
};

}  // namespace bw::render
