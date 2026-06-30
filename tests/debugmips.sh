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
