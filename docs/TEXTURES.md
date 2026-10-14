# Texture packs

The Direct3D 9 renderer can save the textures the game draws and draw your own images in their place: higher
resolution, 32-bit colour with alpha. Settings are in `[Textures]` in `BWDecomp.ini`; code in
`src/render/TexturePack.cpp` and `Texture::Identify` in `src/render/Gpu.cpp`.

## Making a replacement

1. Set `Dump=1` and play through the scenes whose textures you want. Every distinct texture is saved once to
   `Textures\Dump\` in the game folder as `bw_<hash>_<width>x<height>_<format>.png`, with its alpha channel. The hash
   identifies the texture's contents.
2. Edit or upscale the image. Any size works: the game addresses textures in 0..1 coordinates, so a 1024x1024 image
   replaces a 256x256 one without changes to the game. Keep the layout (the game packs several images into one
   texture, and the models' coordinates point into it). Keep the aspect ratio.
3. Save it in `Textures\Replace\` at the same relative path as the dump: a named texture as e.g.
   `Replace\data\textures\sky.png` or `Replace\segments\2b.png`; a hash-named one anywhere below `Replace\` with a
   name starting with `bw_<hash>`, e.g. `bw_32ab16257c935dbb_creature_tiger.png`. PNG, DDS, BMP, JPEG and TIFF are read through Windows Imaging Component.
4. Set `Dump=0` again and start the game. `BWDecomp.log` lists how many replacements were found and logs each one when
   it is first drawn (`Textures: replaced bw_... with a 1024x1024 image`).

Details:

- Images are used at 8 bits per channel (32-bit A8R8G8B8). Files with 16 bits per channel are read and converted:
  the game draws into a 32-bit frame, so more precision would not show.
- Mipmaps are generated from the image (2x2 box filter); smaller levels in the file are not read.
- A texture whose original format has no alpha (X1R5G5B5, R5G6B5) ignores the image's alpha, so blending stays as the
  game set it up.
- The game keeps working with its own copy of the texture: it reads, edits and saves what it did before. Only the GPU
  draws the replacement.
- Replacements are loaded when a texture is first drawn and kept for the session (GPU memory, not the game's 32-bit
  address space). Check the GPU's limit for very large images (16384 on current GPUs; logged when exceeded).
- With `Dump=0` and no files in `Textures\Replace\` the renderer does not hash textures at all.

## Names (0.10.0)

The game's texture loaders are decompiled (`src/game/lh3dlib/LH3DTexture.cpp`) and tell the renderer what they load,
so these textures are dumped and replaced by name, whatever their contents' hash:

| Name | Source | Notes |
|---|---|---|
| `data\textures\<file>` | `Data\Textures\*.raw`: 24-bit colour plus an 8-bit `<file>a.raw` alpha | Drawn at full 24-bit colour (the game converts them to 16-bit); dumps are full colour too |
| `segments\<hex>` | DXT textures of the game's texture resource file (models, buildings, banners) | |
| the file's path | other DDS files (citadel, save-game room) | |

Textures the game builds in memory (creature skins, the landscape, the logo, clouds) keep hash names. Textures that
change again within a quarter of a second (object shadows, redrawn every frame) are not dumped or replaced.

## What can be replaced

The game keeps its textures in 256x256 pages and packs smaller images into them (`LH3DRender`'s texture cache). A page
can be replaced when its contents are the same every time:

| Works | Does not work (yet) |
|---|---|
| Named textures (above); creature skins, logos, clouds by hash | Fonts: glyphs are added to a page in the order text first appears, so the page differs between runs |
| The landscape by hash: each map (`.lnd`) holds its landscape pages as DXT3 textures; the hashes stay the same for a map | Object shadows (redrawn every frame) |

Tested: 0.9.0 the creature skin page by hash; 0.10.0 `data\textures\sky` (the sea) and `segments\2b` (the village banners) by name, 4x larger, in a skirmish game.

Next: names and full colour for the landscape (`0x007627b0` loads a map's DXT3 pages into in-memory textures; naming
them `landscape\<map>\<n>` needs the map's name), and names for the other in-memory textures (creature skins).
