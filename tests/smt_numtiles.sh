#!/usr/bin/env bash
# Regression test for the .smt tile-file header tile count (issue #4).
# Compiles a synthetic 1024x1024 texture with no tile dedup (-ct 1), then checks
# that the .smt header's numTiles field matches the number of tiles actually
# written (file size) and is non-zero.
#
# Usage: tests/smt_numtiles.sh [path-to-mapcompile]   (default: build/mapcompile)
set -euo pipefail

MAPCOMPILE="${1:-build/mapcompile}"
if [ ! -x "$MAPCOMPILE" ]; then
	echo "mapcompile not found/executable at: $MAPCOMPILE" >&2
	exit 2
fi
MAPCOMPILE="$(cd "$(dirname "$MAPCOMPILE")" && pwd)/$(basename "$MAPCOMPILE")"

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

# Write a valid 1024x1024 8-bit RGB PNG using only the Python standard library.
# Every 32x32 tile gets a distinct colour so exact-duplicate dedup keeps all of
# them (a solid image would collapse to a single tile).
python3 - "$WORK/tex.png" <<'PY'
import sys, zlib, struct
W = H = 1024
T = 32  # tile size in pixels
patterns = [b"".join(bytes((tx, ty, (tx ^ ty) & 0xff)) * T for tx in range(W // T)) for ty in range(H // T)]
raw = b"".join(b"\x00" + patterns[y // T] for y in range(H))   # filter byte 0 per scanline
def chunk(tag, data):
    return struct.pack(">I", len(data)) + tag + data + struct.pack(">I", zlib.crc32(tag + data) & 0xffffffff)
png = b"\x89PNG\r\n\x1a\n"
png += chunk(b"IHDR", struct.pack(">IIBBBBB", W, H, 8, 2, 0, 0, 0))
png += chunk(b"IDAT", zlib.compress(raw, 9))
png += chunk(b"IEND", b"")
open(sys.argv[1], "wb").write(png)
PY

# -ct 1 = no dedup, so a 1024x1024 texture yields exactly (128/4)*(128/4) = 1024 tiles.
"$MAPCOMPILE" -t "$WORK/tex.png" -o "$WORK/smttest" -ct 1 >/dev/null

python3 - "$WORK/smttest.smt" <<'PY'
import sys, os, struct
p = sys.argv[1]
hdr = open(p, "rb").read(32)
num_tiles = struct.unpack("<i", hdr[20:24])[0]
size = os.path.getsize(p)
implied = (size - 32) // 680           # 32-byte header + 680 bytes per tile
print(f".smt numTiles={num_tiles} implied-from-size={implied} (size={size})")
assert num_tiles > 0, "FAIL: .smt header numTiles is 0 (issue #4 regression)"
assert num_tiles == implied, f"FAIL: header numTiles {num_tiles} != tiles in file {implied}"
assert num_tiles == 1024, f"FAIL: expected 1024 tiles for -ct 1, got {num_tiles}"
print("OK: .smt header numTiles matches the tiles written")
PY
