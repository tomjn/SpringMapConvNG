/* This file is part of SpringMapConvNG (GPL v2 or later), see the LICENSE file */

#include "Dxt1.h"

// Expand an RGB565 value to 8-bit per channel, replicating the high bits into
// the low bits the way GPUs and the BC1 reference decoder do.
static inline void rgb565(uint16_t c, int& r, int& g, int& b)
{
	int r5 = (c >> 11) & 0x1F;
	int g6 = (c >> 5) & 0x3F;
	int b5 = c & 0x1F;
	r = (r5 << 3) | (r5 >> 2);
	g = (g6 << 2) | (g6 >> 4);
	b = (b5 << 3) | (b5 >> 2);
}

void decodeBC1(const uint8_t* src, int w, int h, uint8_t* dst)
{
	const int bw = w / 4;
	const int bh = h / 4;
	for (int by = 0; by < bh; by++) {
		for (int bx = 0; bx < bw; bx++) {
			const uint8_t* blk = src + ((size_t)by * bw + bx) * 8;
			uint16_t c0 = (uint16_t)(blk[0] | (blk[1] << 8));
			uint16_t c1 = (uint16_t)(blk[2] | (blk[3] << 8));
			uint32_t bits = (uint32_t)blk[4] | ((uint32_t)blk[5] << 8) |
					((uint32_t)blk[6] << 16) | ((uint32_t)blk[7] << 24);

			int r[4], g[4], b[4], a[4];
			rgb565(c0, r[0], g[0], b[0]);
			rgb565(c1, r[1], g[1], b[1]);
			a[0] = a[1] = a[2] = a[3] = 255;
			if (c0 > c1) {
				// Four-colour (opaque) block.
				r[2] = (2 * r[0] + r[1]) / 3;
				g[2] = (2 * g[0] + g[1]) / 3;
				b[2] = (2 * b[0] + b[1]) / 3;
				r[3] = (r[0] + 2 * r[1]) / 3;
				g[3] = (g[0] + 2 * g[1]) / 3;
				b[3] = (b[0] + 2 * b[1]) / 3;
			} else {
				// Three-colour block: index 3 is transparent black.
				r[2] = (r[0] + r[1]) / 2;
				g[2] = (g[0] + g[1]) / 2;
				b[2] = (b[0] + b[1]) / 2;
				r[3] = g[3] = b[3] = 0;
				a[3] = 0;
			}

			for (int py = 0; py < 4; py++) {
				for (int px = 0; px < 4; px++) {
					int idx = (bits >> (2 * (py * 4 + px))) & 0x3;
					int x = bx * 4 + px;
					int y = by * 4 + py;
					uint8_t* p = dst + ((size_t)y * w + x) * 4;
					p[0] = (uint8_t)r[idx];
					p[1] = (uint8_t)g[idx];
					p[2] = (uint8_t)b[idx];
					p[3] = (uint8_t)a[idx];
				}
			}
		}
	}
}
