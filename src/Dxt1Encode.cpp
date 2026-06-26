/* This file is part of SpringMapConvNG (GPL v2 or later), see the LICENSE file */

#include "Dxt1Encode.h"
#include <cstring>
#include <string.h>
#define STB_DXT_IMPLEMENTATION
#define STB_DXT_STATIC
#include "third_party/stb_dxt.h"

void compressDXT1(const uint8_t* rgba, int w, int h, uint8_t* dst)
{
	// DevIL's ilCompressDXT treated images as having a lower-left origin, so it
	// effectively compressed them vertically flipped. The rest of the compile
	// pipeline (and every existing map) depends on that orientation, so read the
	// source rows bottom-to-top here to reproduce it exactly.
	const int bw = w / 4;
	const int bh = h / 4;
	uint8_t block[64]; // 4x4 RGBA
	for (int by = 0; by < bh; by++) {
		for (int bx = 0; bx < bw; bx++) {
			for (int py = 0; py < 4; py++) {
				int srcrow = h - 1 - (by * 4 + py);
				const uint8_t* row = rgba + (size_t)(srcrow * w + bx * 4) * 4;
				std::memcpy(&block[py * 16], row, 16);
			}
			uint8_t* out = dst + ((size_t)by * bw + bx) * 8;
			stb_compress_dxt_block(out, block, 0, STB_DXT_HIGHQUAL);
		}
	}
}

void downscaleHalfBox(const uint8_t* src, int w, int h, uint8_t* dst)
{
	const int dw = w / 2;
	const int dh = h / 2;
	for (int y = 0; y < dh; y++) {
		for (int x = 0; x < dw; x++) {
			const uint8_t* a = src + (size_t)((y * 2) * w + x * 2) * 4;
			const uint8_t* b = a + 4;
			const uint8_t* c = src + (size_t)((y * 2 + 1) * w + x * 2) * 4;
			const uint8_t* d = c + 4;
			uint8_t* o = dst + (size_t)(y * dw + x) * 4;
			for (int ch = 0; ch < 4; ch++)
				o[ch] = (uint8_t)((a[ch] + b[ch] + c[ch] + d[ch] + 2) / 4);
		}
	}
}

void encodeTile680(const uint8_t* rgba32x32, uint8_t out[680])
{
	uint8_t mip16[16 * 16 * 4];
	uint8_t mip8[8 * 8 * 4];
	uint8_t mip4[4 * 4 * 4];

	int p = 0;
	compressDXT1(rgba32x32, 32, 32, &out[p]);
	p += 32 / 4 * 32 / 4 * 8; // 512

	downscaleHalfBox(rgba32x32, 32, 32, mip16);
	compressDXT1(mip16, 16, 16, &out[p]);
	p += 16 / 4 * 16 / 4 * 8; // 128

	downscaleHalfBox(mip16, 16, 16, mip8);
	compressDXT1(mip8, 8, 8, &out[p]);
	p += 8 / 4 * 8 / 4 * 8; // 32

	downscaleHalfBox(mip8, 8, 8, mip4);
	compressDXT1(mip4, 4, 4, &out[p]);
	p += 8; // 4x4 -> 1 block
}
