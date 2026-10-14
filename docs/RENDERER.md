# Renderer

*Black & White* draws through DirectDraw 7 and Direct3D 7 (Lionhead's `LH3DLIB` on top of `LHScreen`). On current
Windows that API is a compatibility layer with hard limits: no render target wider than 2048 pixels
([FIXES.md](FIXES.md#crash-at-resolutions-wider-than-2048-pixels)), 16-bit texture and depth formats preferred by the
game's format selection, no anti-aliasing. The plan is to keep the game's rendering code and replace the API under it
with Direct3D 9Ex.

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

## Steps

1. Inventory (this document, `TraceDirectX`). Done in 0.6.0.
2. DirectDraw objects and presentation on Direct3D 9Ex: surfaces, `Lock`/`Unlock`, `Blt`, `Flip`; the front end's 2D
   parts and the videos.
3. The Direct3D 7 device: states, textures, draws; the front end and the game.
4. Higher resolutions than 2048, 32-bit textures and depth, anti-aliasing and anisotropic filtering as options.
