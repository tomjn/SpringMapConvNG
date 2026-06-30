# Debug mipmap map - design

Date: 2026-06-30
Status: Approved (pending implementation plan)

## Goal

Add a mode to `mapcompile` that generates a synthetic Spring/Recoil map whose
per-tile mipmap levels are deliberately *different* from one another, so the map
can be used to visually debug which mip level the engine samples at a given
camera zoom. As you zoom out the terrain should visibly change colour
(red -> green -> blue -> yellow), one colour per mip level.

This is the standard "coloured mip levels" GPU debugging technique, applied to
Spring's per-tile DXT1 mip chain.

## Background (verified facts)

- Spring's ground diffuse texture is a mosaic of **32x32 tiles**. The mip chain
  (32 -> 16 -> 8 -> 4) lives *inside each tile*; tiles tessellate across the map.
  A per-tile pattern therefore repeats across the whole map; there is no map
  small enough to show a single map-wide marker (smallest map is ~32x32 tiles).
- Each tile is stored as 4 concatenated DXT1 mip levels = **680 bytes**
  (512 + 128 + 32 + 8), at offsets `{0, 512, 640, 672}`.
- The normal pipeline (`TileStorage::CompressTile`) *derives* mips by
  downscaling mip0 with `iluScale`, so it cannot produce distinct per-level
  content. The debug path must bypass it.
- The engine (Recoil, `rts/Map/SMF/SMFGroundTextures.cpp`) in its **default**
  configuration (`SMFTextureStreaming = false`) uploads all 4 stored mip levels
  verbatim (`LoadSquareTexturePersistent`, `glCompressedTexImage2D` for
  `level = 0..3`), sets `GL_LINEAR_MIPMAP_LINEAR` (trilinear) + anisotropic
  filtering, and never calls `glGenerateMipmap`. So custom mip content IS
  sampled at render time. Confirmed.
- Because filtering is **trilinear**, the engine cross-fades smoothly between
  levels (fractional LOD) rather than switching instantly. The debug map will
  show colours *morphing* through intermediate blends as you zoom, not hard
  steps. This is acceptable and is itself the LOD-ramp visualization.

## Decisions

- **Per-level appearance:** distinct background colour per level, plus white
  square markers whose count decreases with level ("dots on coloured bg",
  refined to solid block-aligned squares).
- **Invocation:** a flag on the existing `mapcompile` tool, fully synthetic,
  sized by arguments. No input texture required.
- **Terrain:** flat (whole map at one height -> one mip level visible at a time
  -> cleanest "this zoom = this colour" readout).
- **Markers:** solid white axis-aligned squares, each exactly one 4x4 DXT1
  block (or a multiple), positioned on block boundaries. Block-aligned squares
  compress losslessly under DXT1 (each block uniform) - no edge fringing at any
  level, unlike circular dots.
- **Colours/counts:** hardcoded constants. No configurability (YAGNI).

## Approach (chosen: A)

Pre-build one debug tile's four mip levels in memory, DXT1-compress each, hand
the finished 680-byte buffer to `TileStorage`, and point every cell of the tile
index array at that single tile. `Compile()` gets minimal guards for the few
spots that touch `texture`.

Rejected alternatives:
- **B - generalize `CompressTile` to accept 4 source images.** Over-built;
  threads a second source through `AddTile`/dedup/checksum. That is exactly the
  complexity a uniform debug map lets us avoid. (This would be the design if we
  ever want the general per-location custom-mip feature.)
- **C - synthesize a real texture and run the normal pipeline.** Does not work:
  mips 1-3 would be downscaled blurs of mip0, not distinct colours.

## Components

### 1. CLI (`src/mapcompile.cpp`)

New flags:
- `-debugmips` (bool) - enable debug-map mode.
- `-dw N` - map width in Spring units; `mapx = N * 128`.
- `-dh N` - map height in Spring units; `mapy = N * 128`.

Behaviour:
- When `-debugmips` is set, `-t` (texture) is not required; `-o` still is.
- Validate `dw`, `dh` are positive integers; error + help otherwise.
- Construct `SMFMap` in debug mode (see below) instead of the texture
  constructor. Compression-related flags (`-ct`, `-th`, `-ccount`) are ignored
  in debug mode (single authored tile, no dedup).

### 2. Debug tile builder

A helper (in `TileStorage` or a small free function) that produces the 680-byte
buffer:

For each level, allocate an RGBA buffer, fill the background colour, stamp the
white square markers, `ilCompressDXT(..., IL_DXT1)`, and copy into the buffer at
the level offset.

| Level | Size  | Bg colour | White markers (4x4 block-aligned squares) |
|-------|-------|-----------|-------------------------------------------|
| mip0  | 32x32 | red       | 16 squares, one per 8x8 cell (4x4 grid)   |
| mip1  | 16x16 | green     | 4 squares (2x2 grid)                      |
| mip2  | 8x8   | blue      | 1 square (one of the four blocks)         |
| mip3  | 4x4   | yellow    | solid (the single block is the tile)      |

Markers carry the "getting coarser" signal at mip0/mip1; background colour
carries the signal at all levels (and is the only signal at mip2/mip3). Manual
RGBA fill (tiny buffers); DevIL is already linked for `ilCompressDXT`.

### 3. `TileStorage` change

Add:
```cpp
uint64_t AddCompressedTile(uint8_t* compressed680);
```
Inserts a pre-built 680-byte buffer directly into `m_tiles_compressed` under a
fixed uid (e.g. `0`); bypasses `m_tiles`, `CompressTile`, and dedup. Buffer is
`new[]`-allocated and freed in `Reset()` like every other compressed tile.
`GetTileCount()` already returns `max(m_tiles_compressed.size(),
m_tiles.size())`, so it reports `1`.

### 4. `SMFMap` debug path

- New debug entry point: a debug constructor `SMFMap(name)` + `SetDebug(dw, dh)`
  setter (or equivalent) that sets `m_debug = true` and `mapx`/`mapy` directly,
  leaving `texture` null.
- In `Compile()`, three guarded spots:
  1. Header `mapx`/`mapy` taken from members, not `texture->w/h`.
  2. Replace the `DoCompress(tiles, order)` call with a debug branch: build the
     one debug tile, `AddCompressedTile`, push its uid to `order`, and `memset`
     the `tiles[]` index array to 0 (every cell -> tile 0).
  3. Guard `delete texture` behind `if (texture)`.
- **Heightmap:** flat falls out for free - `hmap` is already `bzero`'d when no
  heightmap is set. No code needed.
- **Minimap:** synthesize a 1024x1024 image of the mip0 pattern (red + squares)
  and run it through the **existing** minimap compression loop so the in-engine
  minimap is not black. Reuses existing code; just feeds a generated image
  instead of a file.
- typemap / metalmap / grass / features: already default to zeroed/empty.

## Data flow

```
mapcompile -debugmips -dw 8 -dh 8 -o out
  -> SMFMap(out) + SetDebug(8, 8)         // mapx = mapy = 1024
  -> Compile()  [m_debug branch]
       build debug tile (4 DXT1 levels -> 680 bytes)
       TileStorage::AddCompressedTile(buf)
       tiles[] := all 0
       synthesize debug minimap -> existing 9-level minimap compressor
       write out.smt (1 tile) + out.smf (flat height, all-zero tile index)
```

## Testing

**Constraint:** the engine cannot be run on the development machine. Final
in-engine confirmation is a hand-off to a third party. The automated byte-level
checks are therefore the **primary acceptance gate**, not a smoke test, and must
be strong enough to stand on their own. Every link in the chain has already been
verified independently (engine reads stored mips - confirmed by code reading;
DXT1 layout - confirmed against `SMFFormat.h`), so the only thing the hand-off
adds is end-to-end visual confirmation, not correctness of any individual link.

New `tests/debugmips.sh` (shell + embedded Python, matching
`tests/smt_numtiles.sh`). Runs `mapcompile -debugmips -dw 8 -dh 8 -o out`, then
asserts:

1. **`.smt` structure** - header `numTiles == 1`; file size `== 32 + 680`.
2. **`.smf` tile index** - every entry of the `tiles[]` array
   (`mapx/4 * mapy/4` ints) is `0`.
3. **`.smf` header** - `mapx == dw*128`, `mapy == dh*128`, `tilesize == 32`.
4. **Per-level colour** - for each of the 4 DXT1 mip blocks in the `.smt`, read
   the two RGB565 endpoint colours directly (no full decode needed - we only
   need the dominant/background colour) and assert they match
   red / green / blue / yellow respectively. This is the real regression guard:
   it proves the four levels carry *distinct* content (the whole point), and
   would fail if the levels were ever accidentally made identical.

**Local eyeball aid (no engine needed):** the test decodes the four `.smt` mip
blocks and writes them as upscaled PNGs (`mip0.png`..`mip3.png`). Done in the
test's Python, so no production code is added. This lets the source pattern be
verified by eye on this machine and lets the PNGs accompany the hand-off so the
tester knows exactly what "correct" looks like before loading anything.

**Hand-off verification (delegated, not runnable here):** the spec/PR includes a
short tester note - "load in Recoil with default settings (texture streaming
off); the terrain should colour-morph red -> green -> blue -> yellow as you zoom
out; if it stays one colour, streaming is on." The automated test cannot observe
the GPU, and we cannot run the engine locally, so this is the one step performed
by someone else.

## Out of scope

- Configurable colours, marker counts, or marker shapes.
- The general per-location custom-mip feature (Approach B).
- Sloped/ramped debug terrain (flat only).
- Behaviour under `SMFTextureStreaming = true` (only mip0 is uploaded there, so
  the debug effect is absent by design; this doubles as a streaming-on detector).
```
