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
    std::uint32_t multisample = 0;      ///< Samples per pixel of render targets (anti-aliasing); 0 or 1 = off.
    bool fullPrecisionTextures = true;  ///< see Texture::SetFullPrecision
    bool keepAspectRatio = true;
};

/// True when Direct3D 9Ex can be created on this system (checked before the renderer replaces DirectDraw).
[[nodiscard]] bool Available() noexcept;

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
    /// Gives `level` contents more precise than its 16-bit format holds: `argb` is the whole level in A8R8G8B8, rows
    /// `pitch` bytes apart. The game decodes compressed (DXT) textures into 16-bit ones with 4 bits per colour; with
    /// Options::fullPrecisionTextures the GPU copy becomes A8R8G8B8 and shows the decoded colours until the CPU next
    /// writes the level. Returns false when not applicable (option off, not a 16-bit colour format).
    bool SetFullPrecision(std::uint32_t level, const std::uint32_t* argb, std::int32_t pitch) noexcept;
    /// Passes `level`'s full-precision contents, if it has any, on to `destinationLevel` of `destination` (a copy of
    /// the same size).
    void CopyFullPrecision(std::uint32_t level, Texture& destination, std::uint32_t destinationLevel) noexcept;
    /// Uploads CPU changes to the GPU copy; done automatically before drawing.
    void Upload() noexcept;
    [[nodiscard]] std::uint32_t Levels() const noexcept { return m_levels; }
    /// The IDirect3DTexture9 to draw with.
    [[nodiscard]] void* Native() const noexcept { return m_full != nullptr ? m_full : m_gpu; }

private:
    Texture(void* system, void* gpu, std::uint32_t width, std::uint32_t height, std::uint32_t levels,
            Format format) noexcept
        : m_system(system), m_gpu(gpu), m_width(width), m_height(height), m_levels(levels), m_format(format)
    {
    }
    bool MakeFullPrecision() noexcept;
    void ConvertToFullPrecision(std::uint32_t level) noexcept;

    void* m_system;                // IDirect3DTexture9*, D3DPOOL_SYSTEMMEM, the game's format
    void* m_gpu;                   // IDirect3DTexture9*, D3DPOOL_DEFAULT, the game's format; released at full precision
    void* m_fullSystem = nullptr;  // IDirect3DTexture9*, D3DPOOL_SYSTEMMEM, A8R8G8B8 (full precision only)
    void* m_full = nullptr;        // IDirect3DTexture9*, D3DPOOL_DEFAULT, A8R8G8B8 (full precision only)
    std::uint32_t m_width;
    std::uint32_t m_height;
    std::uint32_t m_levels;
    Format m_format;
    std::uint32_t m_readOnlyLocks = 0;  // bit per level locked read-only
    std::uint32_t m_dirtyLevels = ~0u;  // bit per level changed since the last upload
    std::uint32_t m_preciseLevels = 0;  // bit per level whose full-precision pixels are current
};

/// A colour buffer the game renders into (its back buffer, or a render-to-texture target). With multisampling the
/// game draws into a multisampled surface; CPU access, copies and presentation use a single-sampled copy, resolved
/// from the multisampled one when that is newer (and drawn back into it when the copy is newer).
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
    [[nodiscard]] std::uint32_t Width() const noexcept { return m_width; }
    [[nodiscard]] std::uint32_t Height() const noexcept { return m_height; }
    [[nodiscard]] Format GetFormat() const noexcept { return m_format; }
    [[nodiscard]] bool Multisampled() const noexcept { return m_multisampled != nullptr; }

    /// The single-sampled surface with the latest pixels (IDirect3DSurface9*), for reading and copying from.
    [[nodiscard]] void* Resolved() noexcept;
    /// The single-sampled surface, about to be changed by a copy or the CPU (IDirect3DSurface9*).
    [[nodiscard]] void* ForWriting() noexcept;
    /// The surface draws go to (IDirect3DSurface9*), up to date; marks it as about to be drawn into.
    [[nodiscard]] void* ForDrawing() noexcept;

private:
    /// Which copy holds the latest pixels (multisampled targets).
    enum class Newest : std::uint8_t { Both, Single, Multisampled };

    RenderTarget(void* surface, void* texture, void* multisampled, std::uint32_t width, std::uint32_t height,
                 Format format) noexcept
        : m_surface(surface), m_texture(texture), m_multisampled(multisampled), m_width(width), m_height(height),
          m_format(format)
    {
    }
    void* m_surface;       // IDirect3DSurface9*, single-sampled
    void* m_texture;       // IDirect3DTexture9* owning m_surface when multisampled (to draw it back), else null
    void* m_multisampled;  // IDirect3DSurface9*, multisampled, or null
    std::uint32_t m_width;
    std::uint32_t m_height;
    Format m_format;
    Newest m_newest = Newest::Both;
};

/// Depth buffer: 32-bit float depth that the CPU can read, or 24-bit depth with 8-bit stencil when multisampled or
/// where lockable depth is not available. Whatever format the game asked for; reads are converted to it. (The game
/// reads depth to test whether the sun is hidden; with this renderer that test uses ProbeDepth instead.)
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
    [[nodiscard]] bool Readable() const noexcept { return m_readable; }

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

/// Probe slots for ProbeDepth.
inline constexpr std::uint32_t kDepthProbes = 8;
/// Whether the depth buffer bound now is nearer than `depth` (0..1) at pixel (`x`, `y`) of the render target, asked
/// without waiting for the GPU: each call starts an occlusion query (unless the previous one of this `probe` slot is
/// still running) and returns the latest answer of that slot, which lags a frame or two behind. False until the
/// first answer arrives, and when no depth buffer is bound. The pixel is clamped to the render target. `probe` <
/// kDepthProbes.
[[nodiscard]] bool ProbeDepth(std::uint32_t probe, std::int32_t x, std::int32_t y, float depth) noexcept;

/// Shows `frame` in `window`'s client area: scaled to `destination` (client coordinates, letterbox rectangle) with
/// filtering, the rest cleared to black. Resizes the swap chain to the client area when it changed.
void Present(RenderTarget& frame, const RECT& source, HWND window, const RECT& destination) noexcept;

/// Work counted since the last call (for the debug frame-rate log).
struct Statistics {
    std::uint32_t draws = 0;
    std::uint32_t textureUploads = 0;
    std::uint32_t depthReads = 0;
    std::uint32_t depthProbes = 0;
    std::uint32_t resolves = 0;
    std::uint32_t targetReads = 0;
    std::uint32_t targetWrites = 0;
};
[[nodiscard]] Statistics TakeStatistics() noexcept;

/// Releases every resource (process exit).
void Shutdown() noexcept;

}  // namespace bw::render::gpu
