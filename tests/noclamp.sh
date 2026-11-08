#!/usr/bin/env bash
# Regression test for -noclamp being ignored by mapcompile.
# SetHeightMap stretched the heightmap to 0..65535 before the command line had
# recorded -noclamp, so the compiled height words never matched the input.
#
# Compiles a 16-bit heightmap whose words span 1000..5000:
#   with -noclamp the .smf must hold the input words unchanged,
#   without it the .smf must be stretched to 0..65535 (clamping stays the default).
#
# Usage: tests/noclamp.sh [path-to-mapcompile]   (default: build/mapcompile)
set -euo pipefail

MAPCOMPILE="${1:-build/mapcompile}"
if [ ! -x "$MAPCOMPILE" ]; then
	echo "mapcompile not found/executable at: $MAPCOMPILE" >&2
	exit 2
fi
MAPCOMPILE="$(cd "$(dirname "$MAPCOMPILE")" && pwd)/$(basename "$MAPCOMPILE")"

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

# A 1024x1024 texture gives a 128x128 map, so the heightmap is 129x129.
python3 - "$WORK/tex.png" "$WORK/height.png" "$WORK/height.bin" <<'PY'
import sys, zlib, struct
def chunk(tag, data):
    return struct.pack(">I", len(data)) + tag + data + struct.pack(">I", zlib.crc32(tag + data) & 0xffffffff)
def png(path, w, h, bit_depth, color_type, raw_rows):
    raw = b"".join(b"\x00" + row for row in raw_rows)   # filter byte 0 per scanline
    data = b"\x89PNG\r\n\x1a\n"
    data += chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, bit_depth, color_type, 0, 0, 0))
    data += chunk(b"IDAT", zlib.compress(raw, 9))
    data += chunk(b"IEND", b"")
    open(path, "wb").write(data)

W = H = 1024
T = 32
patterns = [b"".join(bytes((tx, ty, (tx ^ ty) & 0xff)) * T for tx in range(W // T)) for ty in range(H // T)]
png(sys.argv[1], W, H, 8, 2, [patterns[y // T] for y in range(H)])

HW = HH = 129
words = [1000 + (4000 * (y * HW + x)) // (HW * HH - 1) for y in range(HH) for x in range(HW)]
assert min(words) == 1000 and max(words) == 5000
png(sys.argv[2], HW, HH, 16, 0, [struct.pack(">%dH" % HW, *words[y * HW:(y + 1) * HW]) for y in range(HH)])
open(sys.argv[3], "wb").write(struct.pack("<%dH" % len(words), *words))
PY

"$MAPCOMPILE" -t "$WORK/tex.png" -h "$WORK/height.png" -noclamp -ct 1 -o "$WORK/noclamp" >/dev/null
"$MAPCOMPILE" -t "$WORK/tex.png" -h "$WORK/height.png" -ct 1 -o "$WORK/clamp" >/dev/null

python3 - "$WORK/height.bin" "$WORK/noclamp.smf" "$WORK/clamp.smf" <<'PY'
import sys, struct
def height_words(path):
    d = open(path, "rb").read()
    hp = struct.unpack_from("<i", d, 52)[0]   # SMFHeader.heightmapPtr
    n = 129 * 129
    return struct.unpack_from("<%dH" % n, d, hp)
src = struct.unpack("<%dH" % (129 * 129), open(sys.argv[1], "rb").read())
noclamp = height_words(sys.argv[2])
clamp = height_words(sys.argv[3])
print(f"-noclamp: range {min(noclamp)}..{max(noclamp)}, {sum(a != b for a, b in zip(src, noclamp))} words differ from input")
print(f"default:  range {min(clamp)}..{max(clamp)}")
assert noclamp == src, "FAIL: -noclamp changed the height words (option applied after the heightmap was read)"
assert (min(clamp), max(clamp)) == (0, 65535), "FAIL: default compile no longer stretches the heightmap to 0..65535"
print("OK: -noclamp keeps the height words, default clamping still stretches")
PY
