/* This file is part of SpringMapConvNG (GPL v2 or later), see the LICENSE file */

#ifndef TILESTORAGE_H
#define TILESTORAGE_H
#include <list>
#include <stdint.h>
#include <stdio.h>
#include <unordered_map>
#include <vector>

class InvalidTileIndexException
{
};
class InvalidTileDataPointerException
{
};
enum CompressLevels {
	COMPRESS_SHITTY = 1,
	COMPRESS_REASONABLE = 2,
	COMPRESS_INSANE = 3,
	COMPRESS_REASONABLE_BESTQUALITY = 4


};
class TileStorage
{

public:
	TileStorage();
	virtual ~TileStorage();
	uint64_t AddTile(uint8_t* data); // 32x32 RGBA
	// Store a pre-compressed 680-byte tile (4 DXT1 mip levels) directly, bypassing
	// CompressTile and dedup. Takes ownership; freed in Reset(). Used for debug maps.
	uint64_t AddCompressedTile(uint8_t* compressed680);
	void WriteToFile(FILE* f, std::vector<uint64_t>& tile_order);
	uint64_t AddTileOrGetSimiliar(uint8_t* data, float th, int compresslevel);
	// Same as above but with a checksum the caller already computed (e.g. in
	// parallel). 32x32 RGBA.
	uint64_t AddTileOrGetSimiliar(uint8_t* data, uint64_t checksum, float th, int compresslevel);
	// Checksum of a 32x32 RGBA tile (the value used as its uid). Stateless.
	static uint64_t Checksum(const uint8_t* data);
	uint32_t GetTileCount();
	void SetDictSize(uint32_t s);
	void Reset();

private:
	uint64_t AddTile(uint8_t* data, uint64_t checksum); // checksum precomputed by caller
	void CompressAll();
	std::unordered_map<uint64_t, uint8_t*> m_tiles;
	std::unordered_map<uint64_t, uint8_t*> m_tiles_compressed;
	std::list<uint64_t> m_lasttiles;
	unsigned int m_dictcount;
};

#endif // TILESTORAGE_H
