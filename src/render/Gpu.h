#pragma once

// The Direct3D 9Ex side of the renderer. Neutral interface (no d3d9.h) so that the DirectDraw/Direct3D 7 objects, which
// need ddraw.h and d3d.h, can use it; Gpu.cpp is the only file that includes d3d9.h.
//
// Single device, created for the game window. All calls come from the game's rendering thread except where noted.

#include "core/Windows.h"
#include "render/Format.h"

#include <cstdint>
#include <memory>

namespace bw::render::gpu {

/// Settings for the device; from BWDecomp.ini [Display].
struct Options {
    bool vsync = true;
    std::uint32_t anisotropy = 0;  ///< 0 = as the game asks; otherwise forced for every linear minification.
    bool keepAspectRatio = true;
};

/// Creates the Direct3D 9Ex device for `window` (idempotent). Returns false when Direct3D 9Ex is unavailable.
bool Initialise(HWND window, const Options& options) noexcept;
[[nodiscard]] bool Ready() noexcept;

class Texture;
class RenderTarget;
class DepthBuffer;

struct LockedRect {
    void* bits = nullptr;
    std::int32_t pitch = 0;
};

/// A texture with its mipmap chain. The CPU writes into a system-memory copy; changes are uploaded to the GPU copy
/// before the texture is next drawn with (Direct3D 9Ex has no managed pool).
class Texture {
public:
    static std::unique_ptr<Texture> Create(std::uint32_t width, std::uint32_t height, std::uint32_t levels,
                                           Format format) noexcept;
    ~Texture();
    Texture(const Texture&) = delete;
    Texture& operator=(const Texture&) = delete;

    [[nodiscard]] bool Lock(std::uint32_t level, const RECT* rect, bool readOnly, LockedRect& out) noexcept;
    void Unlock(std::uint32_t level) noexcept;
    /// Uploads CPU changes to the GPU copy; done automatically before drawing.
    void Upload() noexcept;
    [[nodiscard]] std::uint32_t Levels() const noexcept { return m_levels; }

private:
    friend void SetTexture(std::uint32_t stage, Texture* texture) noexcept;
    Texture(void* system, void* gpu, std::uint32_t levels) noexcept
        : m_system(system), m_gpu(gpu), m_levels(levels)
    {
    }
    void* m_system;  // IDirect3DTexture9*, D3DPOOL_SYSTEMMEM
    void* m_gpu;     // IDirect3DTexture9*, D3DPOOL_DEFAULT
    std::uint32_t m_levels;
    std::uint32_t m_readOnlyLocks = 0;  // bit per level locked read-only
    bool m_dirty = true;
};

/// A colour buffer the game renders into (its back buffer, or a render-to-texture target).
class RenderTarget {
public:
    static std::unique_ptr<RenderTarget> Create(std::uint32_t width, std::uint32_t height, Format format) noexcept;
    ~RenderTarget();
    RenderTarget(const RenderTarget&) = delete;
    RenderTarget& operator=(const RenderTarget&) = delete;

    /// Copies the GPU contents into `bits` (CPU access to a render target; stalls the GPU).
    [[nodiscard]] bool Read(void* bits, std::int32_t pitch) noexcept;
    /// Replaces the GPU contents with `bits`.
    [[nodiscard]] bool Write(const void* bits, std::int32_t pitch) noexcept;
    void Fill(const RECT* rect, std::uint32_t argb) noexcept;
    /// Copies CPU pixels in `format` into `destination` (scaled with filtering when the sizes differ).
    void Draw(const void* bits, std::int32_t pitch, Format format, std::uint32_t width, std::uint32_t height,
              const RECT& source, const RECT& destination) noexcept;
    /// Copies part of another render target (scaled with filtering when the sizes differ).
    void CopyFrom(RenderTarget& source, const RECT& sourceRect, const RECT& destination) noexcept;
    [[nodiscard]] void* Native() const noexcept { return m_surface; }
    [[nodiscard]] std::uint32_t Width() const noexcept { return m_width; }
    [[nodiscard]] std::uint32_t Height() const noexcept { return m_height; }
    [[nodiscard]] Format GetFormat() const noexcept { return m_format; }

private:
    RenderTarget(void* surface, std::uint32_t width, std::uint32_t height, Format format) noexcept
        : m_surface(surface), m_width(width), m_height(height), m_format(format)
    {
    }
    void* m_surface;  // IDirect3DSurface9*
    std::uint32_t m_width;
    std::uint32_t m_height;
    Format m_format;
};

/// Depth buffer: 32-bit float depth that the CPU can read (the game reads depth to test whether the sun is
/// hidden), or 24-bit depth with 8-bit stencil where lockable depth is not available. Whatever format the game asked
/// for; reads are converted to it.
class DepthBuffer {
public:
    static std::unique_ptr<DepthBuffer> Create(std::uint32_t width, std::uint32_t height) noexcept;
    ~DepthBuffer();
    DepthBuffer(const DepthBuffer&) = delete;
    DepthBuffer& operator=(const DepthBuffer&) = delete;
    [[nodiscard]] void* Native() const noexcept { return m_surface; }

    /// Reads depth values (0..1) of `rect` into `values` (row by row, rect width per row). False when this depth
    /// buffer cannot be read.
    [[nodiscard]] bool Read(const RECT& rect, float* values) noexcept;

private:
    DepthBuffer(void* surface, bool readable) noexcept : m_surface(surface), m_readable(readable) {}
    void* m_surface;  // IDirect3DSurface9*
    bool m_readable;
};

// Rendering state. Values are Direct3D 9 enums (see render/Translate.h).
void SetRenderTarget(RenderTarget* target, DepthBuffer* depth) noexcept;
void SetRenderState(std::uint32_t state, std::uint32_t value) noexcept;
void SetSamplerState(std::uint32_t stage, std::uint32_t state, std::uint32_t value) noexcept;
void SetTextureStageState(std::uint32_t stage, std::uint32_t state, std::uint32_t value) noexcept;
void SetTexture(std::uint32_t stage, Texture* texture) noexcept;
/// `matrix`: 16 floats, row-major as D3DMATRIX.
void SetTransform(std::uint32_t state, const float* matrix) noexcept;
/// `viewport`: D3DVIEWPORT7/9 (identical layout).
void SetViewport(const void* viewport) noexcept;
/// `material`: D3DMATERIAL7/9 (identical layout).
void SetMaterial(const void* material) noexcept;
/// `light`: D3DLIGHT7/9 (identical layout). Returns false when Direct3D 9 rejects the light.
bool SetLight(std::uint32_t index, const void* light) noexcept;
void LightEnable(std::uint32_t index, bool enable) noexcept;
/// `rects`: D3DRECT array (identical layout).
void Clear(std::uint32_t count, const void* rects, std::uint32_t flags, std::uint32_t color, float z,
           std::uint32_t stencil) noexcept;
void BeginScene() noexcept;
void EndScene() noexcept;
void Draw(std::uint32_t type, std::uint32_t fvf, const void* vertices, std::uint32_t vertexCount) noexcept;
void DrawIndexed(std::uint32_t type, std::uint32_t fvf, const void* vertices, std::uint32_t vertexCount,
                 const std::uint16_t* indices, std::uint32_t indexCount) noexcept;
[[nodiscard]] std::uint32_t ValidateDevice() noexcept;  ///< passes, or 0 when the state cannot be rendered

/// Shows `frame` in `window`'s client area: scaled to `destination` (client coordinates, letterbox rectangle) with
/// filtering, the rest cleared to black. Resizes the swap chain to the client area when it changed.
void Present(RenderTarget& frame, const RECT& source, HWND window, const RECT& destination) noexcept;

/// Work counted since the last call (for the debug frame-rate log).
struct Statistics {
    std::uint32_t draws = 0;
    std::uint32_t textureUploads = 0;
    std::uint32_t depthReads = 0;
    std::uint32_t targetReads = 0;
    std::uint32_t targetWrites = 0;
};
[[nodiscard]] Statistics TakeStatistics() noexcept;

/// Releases every resource (process exit).
void Shutdown() noexcept;

}  // namespace bw::render::gpu
