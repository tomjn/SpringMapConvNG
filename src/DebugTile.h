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
