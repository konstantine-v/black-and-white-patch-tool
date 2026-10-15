# Renderer

*Black & White* draws through DirectDraw 7 and Direct3D 7 (Lionhead's `LH3DLIB` on top of `LHScreen`). On current
Windows that API is a compatibility layer with hard limits: no render target wider than 2048 pixels
([FIXES.md](FIXES.md#crash-at-resolutions-wider-than-2048-pixels)), 16-bit texture and depth formats preferred by the
game's format selection, no anti-aliasing. The plan is to keep the game's rendering code and replace the API under it
with Direct3D 9Ex. That replacement exists since 0.7.0 (`[Display] Renderer=1`).

## What the game uses

Measured with `[Diagnostics] TraceDirectX=1` (`src/diagnostics/DirectXTrace.cpp`) over a session of front end, map
selection and two minutes of a skirmish game (22,427 frames), summarised by `tools/directx_report.py`. Callers are
original functions (`symbols/functions.csv`); "(BWDecomp/system)" is this project's code or other DLLs such as Bink.
`AddRef`/`Release` are omitted.

| Method | Calls | Main callers |
|---|---:|---|
| `IDirect3D7::CreateDevice` | 2 | FUN_00785fd0 (1), (BWDecomp/system) (1) |
| `IDirect3D7::CreateVertexBuffer` | 1 | (BWDecomp/system) (1) |
| `IDirect3D7::EnumZBufferFormats` | 1 | FUN_00785fd0 (1) |
| `IDirect3DDevice7::BeginScene` | 22,427 | FUN_007881a0 (22427) |
| `IDirect3DDevice7::Clear` | 7,573 | FUN_007880c0 (7565), FUN_005b3190 (8) |
| `IDirect3DDevice7::DeleteStateBlock` | 8 | FUN_00785890 (8) |
| `IDirect3DDevice7::DrawIndexedPrimitive` | 37,647 | FUN_007888d0 (19290), FUN_007838e0 (15216), FUN_00788520 (1987) |
| `IDirect3DDevice7::EndScene` | 22,427 | FUN_00788520 (22427) |
| `IDirect3DDevice7::EnumTextureFormats` | 1 | FUN_007b3040 (1) |
| `IDirect3DDevice7::GetTextureStageState` | 29,515 | FUN_00788f60 (29515) |
| `IDirect3DDevice7::SetLight` | 569,912 | FUN_00778be0 (569912) |
| `IDirect3DDevice7::SetRenderState` | 8,302,495 | FUN_004108c0 (2832593), FUN_00786640 (2108660), FUN_00410900 (980776) |
| `IDirect3DDevice7::SetTexture` | 3,986,283 | FUN_0078d7c0 (3927779), FUN_00786640 (58504) |
| `IDirect3DDevice7::SetTextureStageState` | 1,862,601 | FUN_00784b20 (602262), FUN_00788fc0 (492606), FUN_00789000 (492605) |
| `IDirect3DDevice7::SetTransform` | 19,295 | FUN_00774a40 (19295) |
| `IDirectDraw2::GetAvailableVidMem` | 2 | (BWDecomp/system) (2) |
| `IDirectDraw7::CreateClipper` | 4 | (BWDecomp/system) (3), FUN_00748500 (1) |
| `IDirectDraw7::CreatePalette` | 1 | (BWDecomp/system) (1) |
| `IDirectDraw7::CreateSurface` | 2,306 | FUN_007b3700 (2114), FUN_007c4250 (81), FUN_007cc4a0 (59) |
| `IDirectDraw7::GetDeviceIdentifier` | 2 | FUN_007485e0 (2) |
| `IDirectDraw7::QueryInterface` | 9 | (BWDecomp/system) (8), FUN_00785fd0 (1) |
| `IDirectDraw7::RestoreDisplayMode` | 2 | FUN_00748de0 (1), FUN_007489f0 (1) |
| `IDirectDraw7::SetCooperativeLevel` | 4 | FUN_00747490 (3), (BWDecomp/system) (1) |
| `IDirectDraw::GetCaps` | 1 | FUN_007485e0 (1) |
| `IDirectDraw::QueryInterface` | 3 | (BWDecomp/system) (2), FUN_007485e0 (1) |
| `IDirectDrawClipper::SetHWnd` | 1 | FUN_00749430 (1) |
| `IDirectDrawSurface7::AddAttachedSurface` | 1 | FUN_00785fd0 (1) |
| `IDirectDrawSurface7::Blt` | 30,441 | (BWDecomp/system) (22418), FUN_007b3450 (7957), FUN_0078d7c0 (37) |
| `IDirectDrawSurface7::GetSurfaceDesc` | 2,116 | FUN_007b3700 (2114), (BWDecomp/system) (2) |
| `IDirectDrawSurface7::IsLost` | 9,442 | FUN_007c37a0 (9431), FUN_007627b0 (11) |
| `IDirectDrawSurface7::Lock` | 46,906 | FUN_00748b30 (22418), FUN_007bed70 (14956), FUN_007c37a0 (9431) |
| `IDirectDrawSurface7::QueryInterface` | 2,131 | FUN_007b3700 (2114), (BWDecomp/system) (17) |
| `IDirectDrawSurface7::SetClipper` | 1 | FUN_00748de0 (1) |
| `IDirectDrawSurface7::Unlock` | 46,895 | FUN_00748c00 (22418), FUN_007bed70 (14956), FUN_007c2c10 (7857) |
| `IDirectDrawSurface::GetAttachedSurface` | 2 | (BWDecomp/system) (2) |
| `IDirectDrawSurface::GetCaps` | 3 | (BWDecomp/system) (3) |
| `IDirectDrawSurface::GetPalette` | 2 | (BWDecomp/system) (2) |
| `IDirectDrawSurface::GetPixelFormat` | 3 | (BWDecomp/system) (3) |
| `IDirectDrawSurface::GetSurfaceDesc` | 7 | (BWDecomp/system) (7) |
| `IDirectDrawSurface::QueryInterface` | 3 | (BWDecomp/system) (3) |

Notes:

- **Draw calls are undercounted.** Direct3D 7 builds a method table per device and rewrites its four draw entries
  (`DrawPrimitive`, `DrawIndexedPrimitive` and the vertex-buffer forms) whenever it switches internal fast paths. The
  trace re-applies its stubs once per frame, so draws made after a switch within a frame are not counted.
  `DrawIndexedPrimitive` from `0x007888d0`, `0x007838e0` and `0x00788520` is the main path.
- **The software rasteriser shares the back buffer.** `LHScreen::Flip` locks the back buffer every frame
  (`0x00748b30`) to give the self-modifying rasteriser in `SELFMOD` its address; `0x007bed70` and `0x007c37a0`
  lock surfaces for CPU drawing. A replacement must keep CPU-visible copies coherent with the GPU without a full
  read-back every frame.
- **Textures** are created by `0x007b3700` (`CreateSurface`, `GetSurfaceDesc`, `QueryInterface`), about 2,100 of them,
  filled through `Lock`/`Unlock` and copied with `Blt` (`0x007b3450`). Formats come from `EnumTextureFormats`
  (`0x007b3040`), where the game picks 16-bit 4444/1555/565 and 32-bit 8888.
- **Fixed function only:** render states, texture stage states, one transform (`0x00774a40`), lights
  (`0x00778be0`). Hardware T&L is not used ("Doesn't support hardware T'n'L" in the game's log); vertices arrive
  pre-transformed or are transformed by Direct3D's software pipeline.
- Older interface versions (`IDirectDraw`, `IDirectDraw2`, `IDirectDrawSurface`) are used only for set-up queries.

## Design

The replacement sits at the API boundary, where the game's code meets DirectDraw: `DirectDrawCreateEx` (imported at
`0x007eb028`) returns objects of this project that implement the interfaces above on Direct3D 9Ex.

| Original object | Replacement | Backed by |
|---|---|---|
| `IDirectDraw7` (+ `IDirectDraw`, `2`, `4` views) | `render::DirectDraw` | `IDirect3D9Ex`, display-mode bookkeeping (no real mode switch) |
| `IDirectDrawSurface7` (+ older views) | `render::Surface` | system-memory pixels plus, by role, a texture, the render target or the depth buffer |
| `IDirect3D7` | `render::DirectDraw` | device creation, format enumeration |
| `IDirect3DDevice7` | `render::Device` | `IDirect3DDevice9Ex`; render and texture-stage states mapped to D3D9 states and sampler states |
| primary surface + `Flip` | `render::Presenter` | swap chain the size of the window; the frame is scaled into it with filtering |

Why at the API boundary rather than inside `LH3DLIB`: the game's 2,300 surfaces and hundreds of state calls work
unchanged, every decompiled `LH3DLIB` function can later call the new device directly, and the original renderer stays
available for comparison (`[Display] Renderer`). Why Direct3D 9: it is the newest API with the same fixed-function
pipeline (texture stages, fog, lights, colour formats), so states translate one to one instead of through shaders,
and it has no 2048 limit. Its `Ex` variant never loses the device on Alt+Tab.

Things Direct3D 9 does not have, handled in the replacement:

- colour keys: converted to alpha when a texture is uploaded, with alpha testing enabled for colour-keyed textures;
- `D3DRENDERSTATE_TEXTUREPERSPECTIVE`, `ANTIALIAS`, `LINEPATTERN` and other retired states: ignored or mapped;
- DirectX 7 texture filter enums (`D3DTFG_*`/`D3DTFN_*`/`D3DTFP_*`): mapped to sampler states;
- CPU access to the render target: the back buffer is read back only when the game locks it after GPU drawing, and
  uploaded before the next GPU draw after a CPU write.

## Implementation (0.7.0, default since 0.8.0)

`[Display] Renderer=1` (the default) selects it; without Direct3D 9Ex the original renderer runs. Code in `src/render`:

| File | Role |
|---|---|
| `Renderer.cpp` | Replaces the `DirectDrawCreateEx` import; settings |
| `DirectDraw.cpp` | `IDirectDraw7` + `IDirect3D7` (+ `IDirectDraw`/`2`/`4` views), `IDirectDrawClipper`; a real DirectDraw object answers caps, device identifier and display-mode queries |
| `Surface.cpp` | `IDirectDrawSurface7` (+ versions 1-4, `IDirect3DTexture2`): roles primary, render target, depth, texture, system memory; locks, blits with format conversion, DXT decoding and encoding |
| `Device.cpp` | `IDirect3DDevice7`: caches every Direct3D 7 state (the game reads many back), translates changes, records state blocks as Direct3D 7 calls |
| `Gpu.cpp` | The only file that includes `d3d9.h`: device, textures (system-memory copy uploaded before drawing; 9Ex has no managed pool), render targets (multisampled with a single-sampled copy), depth buffers, depth probes (occlusion queries), draws, presentation through a separate swap chain sized to the window (resizing never resets the device) |
| `Translate.cpp`, `Pixels.cpp`, `Format.cpp` | Pure translation: states, transforms, vertex formats, pixel formats (unit-tested) |
| `DepthView.cpp` | Lazy CPU view of the depth buffer (below) |
| `TexturePack.cpp` | Texture dumping and replacement by content hash ([TEXTURES.md](TEXTURES.md)) |

What it took beyond a one-to-one translation:

- **Compressed surfaces report their size, not a pitch.** DirectDraw stores `dwLinearSize` where `lPitch` is; the game
  copies DXT data by that size. Reporting a pitch made it copy one row of blocks (black objects).
- **`ValidateDevice` always passes.** The game validates its blend set-ups at start-up with no texture bound; Direct3D
  9 drivers reject that, and the game then chose its multipass blending, which draws objects black.
- **DXT through blits.** The game decompresses DXT textures by blitting them into 16-bit textures and compresses others
  by blitting into DXT1; DirectDraw did both, so the replacement decodes and encodes on the CPU.
- **The sun test reads depth every frame.** `LH3DSky::SunVisibility` (`0x007bed70`, decompiled in 0.8.0) locks the
  whole z-buffer to read five values: whether the points around the sun are behind something. Any read waits for the
  GPU to finish the frame. With this renderer each point is an occlusion query instead (a one-pixel point at the
  sky's depth that passes the depth test where something nearer was drawn), answered a frame or two later without
  waiting; the game smooths the result over frames anyway. A depth lock still works otherwise: the depth buffer is
  32-bit float and lockable, and the lock returns a view whose pages are filled from the GPU only when touched
  (`DepthView.cpp`; not with multisampling, whose depth cannot be locked).
- **No back-buffer lock per frame.** `LHScreen::Flip` locked the back buffer every frame to refresh the software
  rasteriser's pointer; the rasteriser (`SELFMOD`) is dead code in this build, so the Direct3D 9 renderer skips it.
- **No display-mode changes.** "Full screen" is a borderless window; the game's resolution is the render target's,
  scaled to the window when they differ.

Quality options (0.8.0):

- **Anti-aliasing** (`AntiAliasing=4`, samples per pixel): the game draws into a multisampled render target. CPU
  access, copies and presentation use a single-sampled copy resolved from it when it is newer; pixels the CPU or a
  blit put into that copy are drawn back into the multisampled target before the next draw. Depth buffers are
  multisampled to match.
- **Full-precision textures** (`FullPrecisionTextures=1`): the game unpacks DXT textures by blitting them into
  16-bit textures, mostly A4R4G4B4 (4 bits per colour channel, visible banding). The decoded pixels are kept: such a
  texture's GPU copy becomes A8R8G8B8 with the decoded colours, until the game writes the level itself. Copies of a
  whole level (blits, `Load`) pass them on. The game's own art is 16-bit (`.555`, `.raw`) or DXT, so widening other
  textures would add nothing; the render target is 32-bit already (`ColorDepth32`).

Measured on an RTX 3080 Ti, front end: original renderer 142 fps at 1920x1080 (windowed, compositor-limited);
Direct3D 9 143 fps at 2560x1440 with VSync off, also with 4x anti-aliasing, in the front end and a skirmish game.

Debugging aids ([Log] Debug=1): surface kinds and blit paths, per-frame statistics (draws, texture uploads, depth
read-backs and probes, resolves, frame read-backs) with the frame rate. Texture dumps: `[Textures] Dump=1`
([TEXTURES.md](TEXTURES.md)).

## Steps

1. Inventory (this document, `TraceDirectX`). Done in 0.6.0.
2. DirectDraw and Direct3D 7 on Direct3D 9Ex: front end, videos, game, native resolutions above 2048. Done in 0.7.0.
3. Anti-aliasing, full-precision textures, the sun test through occlusion queries, Direct3D 9 as the default. Done in
   0.8.0.
4. Texture packs: dumping and replacing textures with images of any size in 32-bit colour. Done in 0.9.0
   ([TEXTURES.md](TEXTURES.md)).
