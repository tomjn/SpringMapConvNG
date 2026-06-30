# Debug Mipmap Map Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a `-debugmips` mode to `mapcompile` that emits a synthetic Spring/Recoil map whose four per-tile DXT1 mip levels are deliberately distinct (red/green/blue/yellow with white squares), so the engine's mip selection can be visually debugged by zooming.

**Architecture:** Build one debug tile's four mip levels in memory, DXT1-compress each into the standard 680-byte tile buffer, hand it to `TileStorage` pre-compressed (bypassing the downscaling mip deriver), and point every cell of the map's tile-index array at that single tile. `SMFMap::Compile()` gets minimal guards for the few spots that touch the (now absent) source texture. No input texture/heightmap required; map is sized by `-dw`/`-dh`.

**Tech Stack:** C++11, DevIL (`ilCompressDXT` for DXT1), CMake. Integration test is bash + Python stdlib (matching `tests/smt_numtiles.sh`).

---

## Background facts (verified, do not re-derive)

- A tile is 32x32 RGBA. Stored compressed as 4 concatenated DXT1 mip levels = **680 bytes**: mip0 32x32=512B, mip1 16x16=128B, mip2 8x8=32B, mip3 4x4=8B. Level offsets within the tile: `{0, 512, 640, 672}`.
- The normal pipeline (`TileStorage::CompressTile`) derives mips by downscaling mip0, so it cannot produce distinct per-level content. We bypass it with a pre-built buffer.
- `.smf` tile-index array stores the **position in the tile `order` vector**, not the tile uid. One tile => `order = [uid]` and every index = `0`.
- `TileStorage::Reset()` currently frees a compressed buffer only if a matching `m_tiles` (source) entry exists. Our debug tile has no source entry, so `Reset()` must be adjusted to free all compressed buffers independently (Task 3) or it leaks.
- `SMFHeader` byte offsets (packed ints/floats, total 80 bytes): `mapx`@24, `mapy`@28, `tilesize`@40. The tile-index array is the **last** `(mapx/4)*(mapy/4)` int32s written to the `.smf` (the header is rewritten at offset 0 afterwards).
- DevIL is initialised by `ilInit()` at the top of `main()` before any `SMFMap` is constructed.
- `-dw N` => `mapx = N*128`; `-dh N` => `mapy = N*128`. So `-dw 8 -dh 8` is a standard 8x8 map (`mapx=mapy=1024`, tile grid `256x256`, `65536` tiles all pointing at tile 0).

## File structure

- **Create** `src/DebugTile.h` - declares `DrawDebugLevel()` and `BuildDebugTile()`.
- **Create** `src/DebugTile.cpp` - pixel-fill + DXT1 compression for the debug tile. Depends on DevIL only.
- **Create** `tests/debugmips.sh` - end-to-end integration test (the test for this feature).
- **Modify** `CMakeLists.txt` - add `src/DebugTile.cpp` to `MAPCONV_FILES`.
- **Modify** `src/TileStorage.h` / `src/TileStorage.cpp` - add `AddCompressedTile()`; fix `Reset()`.
- **Modify** `src/SMFMap.h` / `src/SMFMap.cpp` - add debug constructor, `m_debug` member, `BuildDebugMinimap()` static helper, and `Compile()` branches.
- **Modify** `src/mapcompile.cpp` - parse `-debugmips`, `-dw`, `-dh`; construct in debug mode.

Note on testing style: this repo has **no C++ unit-test framework**; its sole test (`tests/smt_numtiles.sh`) is a bash script that runs the binary and inspects output bytes with Python. We follow that pattern: a single integration test, written first (Task 1). It stays red until the implementation is complete (end of Task 5). Implementation tasks 2-4 verify by compiling.

---

## Task 1: Write the failing integration test

**Files:**
- Create: `tests/debugmips.sh`

- [ ] **Step 1: Write the integration test**

Create `tests/debugmips.sh` with exactly this content:

```bash
#!/usr/bin/env bash
# Integration test for the -debugmips synthetic debug map.
# Builds an 8x8 debug map, then checks:
#   - .smt has exactly 1 tile (header + 680 bytes)
#   - .smf header mapx/mapy/tilesize are correct
#   - every .smf tile index points at tile 0
#   - each of the 4 DXT1 mip levels has the expected background colour
#     (red / green / blue / yellow) and the square-bearing levels contain white
# Also decodes the 4 mip levels to upscaled PNGs (mip0..mip3.png in CWD) so the
# pattern can be eyeballed without the engine.
#
# Usage: tests/debugmips.sh [path-to-mapcompile]   (default: build/mapcompile)
set -euo pipefail

MAPCOMPILE="${1:-build/mapcompile}"
if [ ! -x "$MAPCOMPILE" ]; then
	echo "mapcompile not found/executable at: $MAPCOMPILE" >&2
	exit 2
fi
MAPCOMPILE="$(cd "$(dirname "$MAPCOMPILE")" && pwd)/$(basename "$MAPCOMPILE")"

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

"$MAPCOMPILE" -debugmips -dw 8 -dh 8 -o "$WORK/dbg" >/dev/null

python3 - "$WORK/dbg.smt" "$WORK/dbg.smf" <<'PY'
import sys, os, struct

smt, smf = sys.argv[1], sys.argv[2]

# --- .smt: exactly one 680-byte tile ---------------------------------------
hdr = open(smt, "rb").read(32)
num_tiles = struct.unpack("<i", hdr[20:24])[0]
size = os.path.getsize(smt)
assert num_tiles == 1, f"FAIL: .smt numTiles {num_tiles} != 1"
assert size == 32 + 680, f"FAIL: .smt size {size} != {32+680}"
tile = open(smt, "rb").read()[32:32+680]

# --- .smf header -----------------------------------------------------------
sh = open(smf, "rb").read()
mapx = struct.unpack("<i", sh[24:28])[0]
mapy = struct.unpack("<i", sh[28:32])[0]
tilesize = struct.unpack("<i", sh[40:44])[0]
assert mapx == 8*128, f"FAIL: mapx {mapx} != {8*128}"
assert mapy == 8*128, f"FAIL: mapy {mapy} != {8*128}"
assert tilesize == 32, f"FAIL: tilesize {tilesize} != 32"

# --- .smf tile index: trailing (mapx/4)*(mapy/4) int32s, all zero ----------
n = (mapx // 4) * (mapy // 4)
idx = sh[len(sh) - n*4:]
assert len(idx) == n*4, "FAIL: .smf shorter than tile index array"
assert not any(struct.unpack(f"<{n}i", idx)), "FAIL: a tile index is non-zero"

# --- DXT1 decode of the 4 mip levels --------------------------------------
def rgb565(v):
	r = (v >> 11) & 0x1f; g = (v >> 5) & 0x3f; b = v & 0x1f
	return ((r*255+15)//31, (g*255+31)//63, (b*255+15)//31)

def decode_dxt1(data, w, h):
	out = bytearray(w*h*3); i = 0
	for byo in range(0, h, 4):
		for bxo in range(0, w, 4):
			c0 = data[i] | (data[i+1] << 8)
			c1 = data[i+2] | (data[i+3] << 8)
			bits = data[i+4] | (data[i+5] << 8) | (data[i+6] << 16) | (data[i+7] << 24)
			i += 8
			col = [rgb565(c0), rgb565(c1)]
			if c0 > c1:
				col.append(tuple((2*col[0][k] + col[1][k])//3 for k in range(3)))
				col.append(tuple((col[0][k] + 2*col[1][k])//3 for k in range(3)))
			else:
				col.append(tuple((col[0][k] + col[1][k])//2 for k in range(3)))
				col.append((0, 0, 0))
			for py in range(4):
				for px in range(4):
					ci = (bits >> (2*(py*4 + px))) & 3
					o = ((byo+py)*w + (bxo+px))*3
					out[o], out[o+1], out[o+2] = col[ci]
	return out

LEVELS = [(0, 32, "red"), (512, 16, "green"), (640, 8, "blue"), (672, 4, "yellow")]
def is_red(p):    return p[0] > 200 and p[1] < 60  and p[2] < 60
def is_green(p):  return p[1] > 200 and p[0] < 60  and p[2] < 60
def is_blue(p):   return p[2] > 200 and p[0] < 60  and p[1] < 60
def is_yellow(p): return p[0] > 200 and p[1] > 200 and p[2] < 60
def is_white(p):  return p[0] > 200 and p[1] > 200 and p[2] > 200
CHECK = {"red": is_red, "green": is_green, "blue": is_blue, "yellow": is_yellow}

def chunk(tag, data):
	import zlib
	return struct.pack(">I", len(data)) + tag + data + struct.pack(">I", zlib.crc32(tag+data) & 0xffffffff)
def write_png(path, w, h, rgb, scale):
	import zlib
	W, H = w*scale, h*scale
	rows = b""
	for y in range(H):
		sy = y // scale; row = b"\x00"
		for x in range(W):
			sx = x // scale; o = (sy*w + sx)*3
			row += bytes(rgb[o:o+3])
		rows += row
	png = b"\x89PNG\r\n\x1a\n"
	png += chunk(b"IHDR", struct.pack(">IIBBBBB", W, H, 8, 2, 0, 0, 0))
	png += chunk(b"IDAT", zlib.compress(rows, 9))
	png += chunk(b"IEND", b"")
	open(path, "wb").write(png)

for off, dim, name in LEVELS:
	block = tile[off:off + max(8, (dim*dim)//2)]
	rgb = decode_dxt1(block, dim, dim)
	bg = rgb[0:3]  # top-left texel is always background by construction
	assert CHECK[name](bg), f"FAIL: mip {dim}x{dim} top-left {tuple(bg)} not {name}"
	pixels = [tuple(rgb[i:i+3]) for i in range(0, len(rgb), 3)]
	if dim >= 8:  # mip0/mip1/mip2 carry white squares; mip3 is solid
		assert any(is_white(p) for p in pixels), f"FAIL: mip {dim}x{dim} has no white square"
	write_png(f"mip{[32,16,8,4].index(dim)}.png", dim, dim, rgb, 16)

print("OK: debug map has 1 tile, all-zero indices, 4 distinct mip colours (red/green/blue/yellow)")
PY
```

- [ ] **Step 2: Make it executable**

Run: `chmod +x tests/debugmips.sh`

- [ ] **Step 3: Build the current binary and run the test to verify it FAILS**

Run:
```bash
cmake -S . -B build
cmake --build build -j
tests/debugmips.sh build/mapcompile
```
Expected: FAIL. `mapcompile` does not recognise `-debugmips` (no `-t` given), prints help and exits 1, so the script aborts at the `mapcompile` invocation before producing any `.smt`/`.smf`. (Acceptable failure modes: non-zero exit from `mapcompile`, or "smt not found".)

- [ ] **Step 4: Commit the failing test**

```bash
git add tests/debugmips.sh
git commit -m "test: add failing integration test for -debugmips debug map"
```

---

## Task 2: Debug tile builder (`DebugTile.h` / `DebugTile.cpp`)

**Files:**
- Create: `src/DebugTile.h`
- Create: `src/DebugTile.cpp`
- Modify: `CMakeLists.txt:17-26` (add source to `MAPCONV_FILES`)

- [ ] **Step 1: Create `src/DebugTile.h`**

```cpp
/* This file is part of SpringMapConvNG (GPL v2 or later), see the LICENSE file */

#ifndef DEBUGTILE_H
#define DEBUGTILE_H
#include <stdint.h>

// Fill a size x size RGBA buffer with a solid background colour plus a regular
// grid of solid white 4x4 squares (squaresPerRow across and down). Each square
// is exactly one 4x4 DXT1 block, placed in the bottom-right block of its cell so
// the top-left 4x4 block of the image is always pure background. Pass
// squaresPerRow <= 0 for a solid fill (no squares). bg is RGB (3 bytes); alpha
// is set to 255.
void DrawDebugLevel(uint8_t* rgba, int size, const uint8_t bg[3], int squaresPerRow);

// Build the four DXT1 mip levels of the debug tile and return a new[]-allocated
// 680-byte buffer (mip0 32x32 red, mip1 16x16 green, mip2 8x8 blue, mip3 4x4
// yellow). Caller owns the buffer. Requires ilInit() to have been called.
uint8_t* BuildDebugTile();

#endif // DEBUGTILE_H
```

- [ ] **Step 2: Create `src/DebugTile.cpp`**

```cpp
/* This file is part of SpringMapConvNG (GPL v2 or later), see the LICENSE file */

#include "DebugTile.h"
#include <IL/il.h>
#include <cstring>

void DrawDebugLevel(uint8_t* rgba, int size, const uint8_t bg[3], int squaresPerRow)
{
	for (int i = 0; i < size * size; i++) {
		rgba[i * 4 + 0] = bg[0];
		rgba[i * 4 + 1] = bg[1];
		rgba[i * 4 + 2] = bg[2];
		rgba[i * 4 + 3] = 255;
	}
	if (squaresPerRow <= 0)
		return; // solid level, no squares (mip3)

	const int cell = size / squaresPerRow; // cell size in texels (a multiple of 4)
	for (int cy = 0; cy < squaresPerRow; cy++) {
		for (int cx = 0; cx < squaresPerRow; cx++) {
			const int ox = cx * cell + (cell - 4); // bottom-right 4x4 block of the cell
			const int oy = cy * cell + (cell - 4);
			for (int y = 0; y < 4; y++) {
				for (int x = 0; x < 4; x++) {
					const int p = ((oy + y) * size + (ox + x)) * 4;
					rgba[p + 0] = 255;
					rgba[p + 1] = 255;
					rgba[p + 2] = 255;
					rgba[p + 3] = 255;
				}
			}
		}
	}
}

uint8_t* BuildDebugTile()
{
	static const uint8_t RED[3] = { 255, 0, 0 };
	static const uint8_t GREEN[3] = { 0, 255, 0 };
	static const uint8_t BLUE[3] = { 0, 0, 255 };
	static const uint8_t YELLOW[3] = { 255, 255, 0 };
	struct Level {
		int size;
		const uint8_t* bg;
		int squares;
	};
	const Level levels[4] = {
		{ 32, RED, 4 },    // mip0: 16 squares (4x4 grid of 8x8 cells)
		{ 16, GREEN, 2 },  // mip1: 4 squares (2x2)
		{ 8, BLUE, 1 },    // mip2: 1 square
		{ 4, YELLOW, 0 },  // mip3: solid
	};

	uint8_t* out = new uint8_t[680];
	uint32_t off = 0;
	ILuint img = ilGenImage();
	ilBindImage(img);
	for (int i = 0; i < 4; i++) {
		const int s = levels[i].size;
		uint8_t* rgba = new uint8_t[s * s * 4];
		DrawDebugLevel(rgba, s, levels[i].bg, levels[i].squares);
		ilTexImage(s, s, 1, 4, IL_RGBA, IL_UNSIGNED_BYTE, rgba);
		ILuint csize;
		ILubyte* dxt = ilCompressDXT(ilGetData(), s, s, 1, IL_DXT1, &csize);
		memcpy(out + off, dxt, csize);
		off += csize;
		free(dxt);
		delete[] rgba;
	}
	ilDeleteImage(img);
	return out; // 512 + 128 + 32 + 8 = 680
}
```

- [ ] **Step 3: Register the source in CMake**

In `CMakeLists.txt`, modify the `MAPCONV_FILES` list (currently lines 17-26) to add `src/DebugTile.cpp`. Result:

```cmake
set(MAPCONV_FILES
	src/CRC.cpp
	src/DebugTile.cpp
	src/Dxt1.cpp
	src/Image.cpp
	src/Raster.cpp
	src/SMFMap.cpp
	src/TileStorage.cpp
	src/third_party/fpng.cpp
	src/third_party/lodepng.cpp
)
```

- [ ] **Step 4: Verify it compiles**

Run:
```bash
cmake -S . -B build
cmake --build build -j
```
Expected: build succeeds (the new TU compiles and links; nothing calls it yet).

- [ ] **Step 5: Commit**

```bash
git add src/DebugTile.h src/DebugTile.cpp CMakeLists.txt
git commit -m "feat: add debug mipmap tile builder"
```

---

## Task 3: `TileStorage::AddCompressedTile` + `Reset()` fix

**Files:**
- Modify: `src/TileStorage.h:31-37` (add public declaration)
- Modify: `src/TileStorage.cpp:53-64` (`Reset`), and add `AddCompressedTile` implementation

- [ ] **Step 1: Declare `AddCompressedTile` in the header**

In `src/TileStorage.h`, add the declaration to the `public:` section, right after the existing `AddTile(uint8_t* data)` line (line 31):

```cpp
	uint64_t AddTile(uint8_t* data); // 32x32 RGBA
	// Store a pre-compressed 680-byte tile (4 DXT1 mip levels) directly, bypassing
	// CompressTile and dedup. Takes ownership; freed in Reset(). Used for debug maps.
	uint64_t AddCompressedTile(uint8_t* compressed680);
```

- [ ] **Step 2: Fix `Reset()` to free all compressed buffers independently**

The current `Reset()` (lines 53-64) frees a compressed buffer only when a matching `m_tiles` source entry exists. A debug tile added via `AddCompressedTile` has no source entry and would leak. Source buffers (`m_tiles`, 4096 bytes) and compressed buffers (`m_tiles_compressed`, 680 bytes) are always distinct allocations, so freeing each map independently is correct and cannot double-free. Replace the body of `Reset()` with:

```cpp
void TileStorage::Reset()
{
	for (std::unordered_map<uint64_t, uint8_t*>::iterator it = m_tiles_compressed.begin(); it != m_tiles_compressed.end(); it++) {
		delete[] (*it).second;
	}
	for (std::unordered_map<uint64_t, uint8_t*>::iterator it = m_tiles.begin(); it != m_tiles.end(); it++) {
		delete[] (*it).second;
	}
	m_tiles.clear();
	m_lasttiles.clear();
	m_tiles_compressed.clear();
}
```

- [ ] **Step 3: Implement `AddCompressedTile`**

Add this implementation to `src/TileStorage.cpp` (e.g. immediately after `AddTile(uint8_t*, uint64_t)`, before `CompressAll`):

```cpp
uint64_t TileStorage::AddCompressedTile(uint8_t* compressed680)
{
	const uint64_t uid = 0;
	m_tiles_compressed[uid] = compressed680; // ownership transferred; freed in Reset()
	return uid;
}
```

- [ ] **Step 4: Verify it compiles**

Run: `cmake --build build -j`
Expected: build succeeds (nothing calls `AddCompressedTile` yet).

- [ ] **Step 5: Commit**

```bash
git add src/TileStorage.h src/TileStorage.cpp
git commit -m "feat: add AddCompressedTile and free all compressed tiles in Reset"
```

---

## Task 4: `SMFMap` debug constructor, minimap, and `Compile()` branches

**Files:**
- Modify: `src/SMFMap.h:88-140` (constructor declaration + `m_debug` member)
- Modify: `src/SMFMap.cpp` (includes, texture-constructor sets `m_debug=false`, new debug constructor, `BuildDebugMinimap` static helper, three `Compile()` edits)

- [ ] **Step 1: Declare the debug constructor and `m_debug` member**

In `src/SMFMap.h`, add the constructor declaration after the existing decompile constructor (line 93):

```cpp
	SMFMap(std::string name, std::string texturepath);
	SMFMap(std::string smfname); // Decompile
	SMFMap(std::string name, int dw, int dh); // Debug mipmap map (synthetic, no texture)
```

In the `private:` members, add `m_debug` next to the other flags (after `bool m_smooth;`, line 135):

```cpp
	bool m_smooth;
	bool m_debug;
```

- [ ] **Step 2: Include the debug tile header in `SMFMap.cpp`**

In `src/SMFMap.cpp`, add the include alongside the existing project includes (after `#include "Dxt1.h"`, line 4):

```cpp
#include "DebugTile.h"
```

- [ ] **Step 3: Set `m_debug = false` in the texture constructor**

In the existing `SMFMap(std::string name, std::string texturepath)` constructor, set the new flag next to `m_smooth = false;` (line 60):

```cpp
	m_smooth = false;
	m_debug = false;
	texpath = texturepath;
```

- [ ] **Step 4: Add the `BuildDebugMinimap` static helper and the debug constructor**

Add the following to `src/SMFMap.cpp`, immediately after the texture constructor (after its closing brace at line 62). `BuildDebugMinimap` tiles the 32x32 mip0 pattern across a 1024x1024 RGBA image so the in-engine minimap is not black:

```cpp
// Build a 1024x1024 RGBA debug minimap: the mip0 tile pattern (red + white
// squares) tiled across the whole image. Caller owns the returned Image.
static Image* BuildDebugMinimap()
{
	static const uint8_t RED[3] = { 255, 0, 0 };
	uint8_t tile[32 * 32 * 4];
	DrawDebugLevel(tile, 32, RED, 4);

	Image* mm = new Image();
	mm->AllocateRGBA(1024, 1024);
	for (int y = 0; y < 1024; y++) {
		for (int x = 0; x < 1024; x++) {
			const int s = ((y & 31) * 32 + (x & 31)) * 4;
			const int d = (y * 1024 + x) * 4;
			mm->datapointer[d + 0] = tile[s + 0];
			mm->datapointer[d + 1] = tile[s + 1];
			mm->datapointer[d + 2] = tile[s + 2];
			mm->datapointer[d + 3] = tile[s + 3];
		}
	}
	return mm;
}

SMFMap::SMFMap(std::string name, int dw, int dh)
{
	m_tiles = new TileStorage();
	metalmap = NULL;
	heightmap = NULL;
	typemap = NULL;
	minimap = NULL;
	vegetationmap = NULL;
	r_metalmap = NULL;
	r_heightmap = NULL;
	r_typemap = NULL;
	r_texture = NULL;
	r_minimap = NULL;
	texture = NULL;
	mapx = dw * 128;
	mapy = dh * 128;
	m_minh = 0.0;
	m_maxh = 1.0;
	m_name = name;
	m_doclamp = true;
	m_th = 0;
	m_comptype = COMPRESS_REASONABLE;
	m_smooth = false;
	m_debug = true;
	minimap = BuildDebugMinimap();
}
```

- [ ] **Step 5: Use `mapx`/`mapy` members for the header (not `texture->w/h`)**

In `Compile()`, replace the texture-derived header size (lines 466-467):

```cpp
	hdr.mapx = (texture->w / 1024) * 128;
	hdr.mapy = (texture->h / 1024) * 128;
```

with the already-set members (identical value on the normal path; required because `texture` is null in debug mode):

```cpp
	hdr.mapx = mapx;
	hdr.mapy = mapy;
```

- [ ] **Step 6: Branch the tile generation in `Compile()`**

In `Compile()`, the current code (lines 538-540) is:

```cpp
	int* tiles = new int[mapx / 4 * mapy / 4];
	std::vector<uint64_t> order;
	DoCompress(tiles, order);
```

Replace the `DoCompress(tiles, order);` call with a debug branch:

```cpp
	int* tiles = new int[mapx / 4 * mapy / 4];
	std::vector<uint64_t> order;
	if (m_debug) {
		uint8_t* dbgtile = BuildDebugTile(); // 680-byte 4-level DXT1 tile
		uint64_t uid = m_tiles->AddCompressedTile(dbgtile);
		order.push_back(uid);
		for (int i = 0; i < mapx / 4 * mapy / 4; i++)
			tiles[i] = 0; // every cell -> order index 0 (the one debug tile)
	} else {
		DoCompress(tiles, order);
	}
```

- [ ] **Step 7: Guard the texture free in `Compile()`**

The code (lines 551-552) unconditionally frees `texture`:

```cpp
	delete texture; // Free texture; not used again in Compile() (the destructor handles the rest)
	texture = NULL;
```

Guard it for the debug path (where `texture` is already null):

```cpp
	if (texture) {
		delete texture; // Free texture; not used again in Compile() (the destructor handles the rest)
		texture = NULL;
	}
```

- [ ] **Step 8: Verify it compiles**

Run: `cmake --build build -j`
Expected: build succeeds (nothing constructs the debug `SMFMap` yet; CLI wiring is Task 5).

- [ ] **Step 9: Commit**

```bash
git add src/SMFMap.h src/SMFMap.cpp
git commit -m "feat: add SMFMap debug-mipmap synthetic compile path"
```

---

## Task 5: CLI wiring in `mapcompile.cpp` + go green

**Files:**
- Modify: `src/mapcompile.cpp` (help text, flag parsing, success gate, construction)

- [ ] **Step 1: Add help text for the new flags**

In `help()`, after the existing usage lines (around line 15), add a line describing the debug mode:

```cpp
	std::cout << argv[0] << " -debugmips -dw [width] -dh [height] -o [outputsuffix]   (synthetic mip-debug map; width/height in Spring units, mapx=width*128)" << std::endl;
```

- [ ] **Step 2: Add the flag variables**

In `main()`, alongside the other locals (near line 45-46), add:

```cpp
	bool debugmips = false;
	int dw = 0;
	int dh = 0;
```

- [ ] **Step 3: Parse the new flags**

In the argument-parsing if/else chain, add three branches. Add them right before the final `} else if (strncmp(&argv[i][1], "h", 1) == 0)` catch-all (line 165). Note `-dh` must be matched here (the catch-all only triggers on a leading `h`, so `-dh`/`-dw` would otherwise be silently ignored):

```cpp
					} else if (strcmp(&argv[i][1], "debugmips") == 0) // Debug mipmap map
					{
						debugmips = true;
					} else if (strcmp(&argv[i][1], "dw") == 0) // Debug map width (Spring units)
					{
						if (i + 1 < argc) {
							dw = atoi(argv[++i]);
						} else {
							goto error;
						}
					} else if (strcmp(&argv[i][1], "dh") == 0) // Debug map height (Spring units)
					{
						if (i + 1 < argc) {
							dh = atoi(argv[++i]);
						} else {
							goto error;
						}
```

- [ ] **Step 4: Accept debug mode at the success gate and construct accordingly**

The current success gate (line 173-174) requires `valid1` (a `-t` texture) and `valid2` (a `-o` output):

```cpp
		if (valid1 && valid2)
			goto success;
```

Replace it so debug mode is accepted without a texture (it needs `-o` plus positive `-dw`/`-dh`):

```cpp
		if (debugmips) {
			if (valid2 && dw > 0 && dh > 0)
				goto success;
			goto error;
		}
		if (valid1 && valid2)
			goto success;
```

Then at the `success:` label, change the construction (line 182):

```cpp
	success:
		SMFMap* m = new SMFMap(outputname, texture);
```

to branch on debug mode:

```cpp
	success:
		SMFMap* m;
		if (debugmips)
			m = new SMFMap(outputname, dw, dh);
		else
			m = new SMFMap(outputname, texture);
```

Note: `goto` jumps may not cross a variable initialisation. `SMFMap* m;` is declared then assigned in both branches (not initialised in the declaration), which keeps the existing `goto success` jumps valid. The subsequent `m->Set...` calls are all guarded by `length() > 0` checks on empty strings in debug mode, so they are no-ops; leave them unchanged.

- [ ] **Step 5: Build and run the test to verify it PASSES**

Run:
```bash
cmake --build build -j
tests/debugmips.sh build/mapcompile
```
Expected: PASS - prints `OK: debug map has 1 tile, all-zero indices, 4 distinct mip colours (red/green/blue/yellow)`. Four PNG previews (`mip0.png`..`mip3.png`) are written to the current directory.

- [ ] **Step 6: Sanity-check the PNG previews by eye (optional, no engine needed)**

Open `mip0.png`..`mip3.png`. Expected: mip0 red with a 4x4 grid of white squares; mip1 green with 2x2 squares; mip2 blue with one square; mip3 solid yellow.

- [ ] **Step 7: Commit**

```bash
git add src/mapcompile.cpp
git commit -m "feat: wire -debugmips/-dw/-dh CLI flags for debug map"
```

---

## Task 6: Verify the normal compile path is unbroken + finalise

**Files:** none (regression check)

- [ ] **Step 1: Run the pre-existing tile-count test to confirm no regression**

Run: `tests/smt_numtiles.sh build/mapcompile`
Expected: PASS (`OK: .smt header numTiles matches the tiles written`). This proves the `Reset()` change and the `Compile()` header/texture edits did not break the normal texture-driven path.

- [ ] **Step 2: Run the debug test once more**

Run: `tests/debugmips.sh build/mapcompile`
Expected: PASS.

- [ ] **Step 3: Write the tester hand-off note**

Because the engine cannot run on this machine, add a short note for whoever loads the map. Append to the PR description (or commit it as `docs/debug-mipmap-map.md` if preferred):

> Build a map: `mapcompile -debugmips -dw 8 -dh 8 -o debugmap` (produces `debugmap.smf` + `debugmap.smt`). Load in Recoil with **default** settings (texture streaming OFF). As you zoom out the terrain should colour-morph red -> green -> blue -> yellow (one mip level per colour; transitions cross-fade because filtering is trilinear). If the whole map stays one colour at all zooms, texture streaming is enabled (only mip0 is uploaded in that mode).

- [ ] **Step 4: Final commit (if a hand-off doc file was added)**

```bash
git add docs/debug-mipmap-map.md
git commit -m "docs: add debug mipmap map tester hand-off note"
```

---

## Self-review notes (already applied)

- **Spec coverage:** per-level appearance (Task 2 `DrawDebugLevel`/`BuildDebugTile`), `-debugmips -dw -dh` invocation (Task 5), `mapx=dw*128` sizing (Task 4 ctor), flat terrain (free - `hmap` is zeroed when no heightmap; no code), synthesized minimap (Task 4 `BuildDebugMinimap`), pre-built tile bypass (Task 3), all-zero tile index (Task 4 branch), 4 automated assertions + per-level PNG eyeball aid (Task 1), hand-off verification note (Task 6). All covered.
- **Type/name consistency:** `DrawDebugLevel(uint8_t*, int, const uint8_t[3], int)`, `BuildDebugTile() -> uint8_t*`, `TileStorage::AddCompressedTile(uint8_t*) -> uint64_t`, `SMFMap(std::string, int, int)`, member `m_debug` - used identically across tasks.
- **No placeholders:** every code step contains the full code; every command lists expected output.
- **`-dw`/`-dh` parse order:** added before the `strncmp(..., "h", 1)` catch-all so they are not swallowed (noted in Task 5 Step 3).
- **`goto` + variable init:** `SMFMap* m;` declared uninitialised then assigned, so the existing `goto success` jumps remain legal (noted in Task 5 Step 4).
```
