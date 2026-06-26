/* This file is part of SpringMapConvNG (GPL v2 or later), see the LICENSE file */

#ifndef DXT1_H
#define DXT1_H
#include <cstddef>
#include <cstdint>

// Decode the top mip level of BC1/DXT1 compressed data to 8-bit RGBA.
// w and h must be multiples of 4. src must hold at least (w/4)*(h/4)*8 bytes
// (any trailing mipmap data is ignored). dst must hold w*h*4 bytes, written
// top-down, row-major.
void decodeBC1(const uint8_t* src, int w, int h, uint8_t* dst);

#endif // DXT1_H
