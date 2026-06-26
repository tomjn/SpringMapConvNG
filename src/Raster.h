/* This file is part of SpringMapConvNG (GPL v2 or later), see the LICENSE file */

#ifndef RASTER_H
#define RASTER_H
#include <cstdint>
#include <string>
#include <vector>

// A DevIL-free pixel buffer used by the decompile path. Supported formats:
//   8-bit grayscale  (channels=1, bytesPerChannel=1)
//   16-bit grayscale (channels=1, bytesPerChannel=2, native-endian samples)
//   8-bit RGBA       (channels=4, bytesPerChannel=1)
// It owns its pixels, has no global state, and is therefore thread-safe.
class Raster
{
public:
	int w;
	int h;
	int channels;
	int bytesPerChannel;
	std::vector<uint8_t> data;

	Raster(int w, int h, int channels, int bytesPerChannel);

	uint8_t* ptr() { return data.data(); }
	const uint8_t* ptr() const { return data.data(); }

	// Flip rows top-to-bottom (matches the decompile path's previous use of
	// DevIL's iluFlipImage).
	void flipVertical();

	// Encode to PNG, always overwriting any existing file. 8-bit RGBA uses
	// fpng; grayscale uses lodepng. 16-bit samples (native-endian here) are
	// converted to PNG's big-endian byte order. Throws std::runtime_error on
	// failure.
	void savePng(const std::string& path) const;
};

#endif // RASTER_H
