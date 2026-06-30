/* This file is part of SpringMapConvNG (GPL v2 or later), see the LICENSE file */

#include "DebugTile.h"
#include <IL/il.h>
#include <cstdlib>
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
