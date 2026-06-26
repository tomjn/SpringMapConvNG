/* This file is part of SpringMapConvNG (GPL v2 or later), see the LICENSE file */

#ifndef DXT1ENCODE_H
#define DXT1ENCODE_H
#include <cstddef>
#include <cstdint>

// DevIL-free, thread-safe BC1/DXT1 encoding (backed by stb_dxt). None of these
// functions touch shared or global state, so they are safe to call from many
// threads at once.

// Compress an 8-bit RGBA image to DXT1. w and h must be multiples of 4. dst
// receives (w/4)*(h/4)*8 bytes, row-major 4x4 blocks.
void compressDXT1(const uint8_t* rgba, int w, int h, uint8_t* dst);

// Box-downscale an 8-bit RGBA image by 2x (averaging each 2x2 group). w and h
// must be even. dst receives (w/2)*(h/2)*4 bytes.
void downscaleHalfBox(const uint8_t* src, int w, int h, uint8_t* dst);

// Encode one 32x32 RGBA tile to the 680-byte mipmapped DXT1 layout the SMT
// format uses: the 32x32 level (512 B) followed by box-downscaled 16x16 (128 B),
// 8x8 (32 B) and 4x4 (8 B) levels.
void encodeTile680(const uint8_t* rgba32x32, uint8_t out[680]);

#endif // DXT1ENCODE_H
