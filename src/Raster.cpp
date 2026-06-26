/* This file is part of SpringMapConvNG (GPL v2 or later), see the LICENSE file */

#include "Raster.h"
#include "third_party/fpng.h"
#include "third_party/lodepng.h"
#include <cstring>
#include <stdexcept>

Raster::Raster(int w, int h, int channels, int bytesPerChannel)
    : w(w)
    , h(h)
    , channels(channels)
    , bytesPerChannel(bytesPerChannel)
    , data((size_t)w * h * channels * bytesPerChannel, 0)
{
}

void Raster::flipVertical()
{
	const size_t stride = (size_t)w * channels * bytesPerChannel;
	std::vector<uint8_t> tmp(stride);
	for (int y = 0; y < h / 2; y++) {
		uint8_t* top = data.data() + (size_t)y * stride;
		uint8_t* bot = data.data() + (size_t)(h - 1 - y) * stride;
		std::memcpy(tmp.data(), top, stride);
		std::memcpy(top, bot, stride);
		std::memcpy(bot, tmp.data(), stride);
	}
}

void Raster::savePng(const std::string& path) const
{
	if (channels == 4 && bytesPerChannel == 1) {
		static bool inited = false;
		if (!inited) {
			fpng::fpng_init();
			inited = true;
		}
		if (!fpng::fpng_encode_image_to_file(path.c_str(), data.data(), (uint32_t)w, (uint32_t)h, 4)) {
			throw std::runtime_error("fpng failed to write " + path);
		}
		return;
	}
	if (channels == 1 && bytesPerChannel == 1) {
		unsigned err = lodepng::encode(path, data, (unsigned)w, (unsigned)h, LCT_GREY, 8);
		if (err)
			throw std::runtime_error("lodepng failed to write " + path + ": " + lodepng_error_text(err));
		return;
	}
	if (channels == 1 && bytesPerChannel == 2) {
		// PNG stores 16-bit samples big-endian; our buffer is native-endian.
		std::vector<uint8_t> be(data.size());
		for (size_t i = 0; i + 1 < data.size(); i += 2) {
			be[i] = data[i + 1];
			be[i + 1] = data[i];
		}
		unsigned err = lodepng::encode(path, be, (unsigned)w, (unsigned)h, LCT_GREY, 16);
		if (err)
			throw std::runtime_error("lodepng failed to write " + path + ": " + lodepng_error_text(err));
		return;
	}
	throw std::runtime_error("Raster::savePng: unsupported pixel format for " + path);
}
