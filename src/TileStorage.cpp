/* This file is part of SpringMapConvNG (GPL v2 or later), see the LICENSE file */

#include "TileStorage.h"
#include "CRC.h"

#include <IL/il.h>
#include <IL/ilu.h>
#include <cstring>
#include <iostream>
#include <math.h>
#include <sstream>
#include <stdlib.h>

inline uint64_t tilechecksum(uint8_t* data)
{
	uint64_t r = 0;
	for (int x = 0; x < 32 * 32 * 4; x++) {
		r += data[x] * 63018038201L * x * x;
		r ^= 13091204281L;
		r *= 13091204281L * x;
		r *= 108086391056891903ULL * data[x];
	}
	Crc32 c;
	c.AddData(data, 32 * 32 * 4);
	r *= c.GetCrc32();
	return r;
}

inline float tilediff(uint8_t* t1, uint8_t* t2)
{
	// Integer reduction: branchless and auto-vectorizable. All addends are exact
	// integers and the max sum (4096*255) is well below 2^24, so this is
	// bit-identical to the original float accumulation regardless of SIMD ordering.
	int total = 0;
	for (int i = 0; i < 32 * 32 * 4; i++) {
		int d = int(t1[i]) - int(t2[i]);
		d = d < 0 ? -d : d;
		total += d < 30 ? d : 255; // A point that is VERY different must not be reused
	}
	return float(total) / (32.0f * 32.0f * 4.0f * 10.0f);
}

TileStorage::TileStorage()
{
	Reset();
	m_dictcount = 64;
}

TileStorage::~TileStorage()
{
	Reset();
}
void TileStorage::Reset()
{
	for (std::unordered_map<uint64_t, uint8_t*>::iterator it = m_tiles_compressed.begin(); it != m_tiles_compressed.end(); it++) {
		delete[] (*it).second;
	}
	for (std::unordered_map<uint64_t, uint8_t*>::iterator it = m_tiles.begin(); it != m_tiles.end(); it++) {
		delete[] (*it).second;
	}
	m_tiles.clear();
	m_lasttiles.clear();
	m_tiles_compressed.clear();
}

uint64_t TileStorage::AddTile(uint8_t* data)
{
	return AddTile(data, tilechecksum(data));
}
uint64_t TileStorage::AddTile(uint8_t* data, uint64_t checksum)
{
	if (m_tiles.find(checksum) != m_tiles.end()) {
		std::cerr << "Duplicate tile detected, dropping!" << std::endl;
		return checksum;
	}
	uint8_t* data_copy = new uint8_t[32 * 32 * 4];
	memcpy(data_copy, data, 32 * 32 * 4);
	m_tiles.insert(std::pair<uint64_t, uint8_t*>(checksum, data_copy));
	m_lasttiles.push_back(checksum);
	if (m_lasttiles.size() > m_dictcount)
		m_lasttiles.pop_front();
	return checksum;
}
uint64_t TileStorage::AddCompressedTile(uint8_t* compressed680)
{
	const uint64_t uid = 0;
	m_tiles_compressed[uid] = compressed680; // ownership transferred; freed in Reset()
	return uid;
}
void TileStorage::CompressAll()
{
	for (std::unordered_map<uint64_t, uint8_t*>::iterator it = m_tiles.begin(); it != m_tiles.end(); it++) {
		if (m_tiles_compressed.find((*it).first) == m_tiles_compressed.end()) {
			CompressTile((*it).first);
		}
	}
}
void TileStorage::SetDictSize(uint32_t s)
{
	m_dictcount = s;
}

void TileStorage::CompressTile(uint64_t uid)
{
	uint8_t* m0;
	uint8_t* m1;
	uint8_t* m2;
	uint8_t* m3;
	uint8_t* dataptr = m_tiles[uid];
	uint8_t* compressedmipmaps = new uint8_t[680];
	if (!dataptr) {
		delete[] compressedmipmaps;
		throw InvalidTileDataPointerException();
	}
	uint32_t s;
	uint32_t s2 = 0;
	ILuint mip1 = ilGenImage();
	ilBindImage(mip1);
	ilTexImage(32, 32, 1, 4, IL_RGBA, IL_UNSIGNED_BYTE, dataptr);
	/*std::stringstream ss;
  ss << "Tile" << uid << ".png";
  ilSaveImage(ss.str().c_str());*/
	m0 = ilCompressDXT(ilGetData(), 32, 32, 1, IL_DXT1, &s);
	memcpy(&compressedmipmaps[s2], m0, s);
	s2 += s;
	iluScale(16, 16, 1);
	m1 = ilCompressDXT(ilGetData(), 16, 16, 1, IL_DXT1, &s);
	memcpy(&compressedmipmaps[s2], m1, s);
	s2 += s;
	iluScale(8, 8, 1);
	m2 = ilCompressDXT(ilGetData(), 8, 8, 1, IL_DXT1, &s);
	memcpy(&compressedmipmaps[s2], m2, s);
	s2 += s;
	iluScale(4, 4, 1);
	m3 = ilCompressDXT(ilGetData(), 4, 4, 1, IL_DXT1, &s);
	memcpy(&compressedmipmaps[s2], m3, s);
	s2 += s;
	ilDeleteImage(mip1);

	/*squish::CompressImage(dataptr,32,32,m0,squish::kDxt1);
  squish::CompressImage(dataptr,16,16,m1,squish::kDxt1);
  squish::CompressImage(dataptr,8,8,m2,squish::kDxt1);
  squish::CompressImage(dataptr,4,4,m3,squish::kDxt1);*/


	free(m0);
	free(m1);
	free(m2);
	free(m3);

	m_tiles_compressed[uid] = compressedmipmaps;
	// std::cout << "Tile " << uid << " compressed!" << std::endl;
}

void TileStorage::WriteToFile(FILE* f, std::vector<uint64_t>& tile_order)
{
	char magic[16];
	strcpy(magic, "spring tilefile");
	int version = 1;
	int numtiles = GetTileCount();
	int tileSize = 32;
	int compressionType = 1;
	fwrite(magic, 16, 1, f);
	fwrite(&version, 4, 1, f);
	fwrite(&numtiles, 4, 1, f);
	fwrite(&tileSize, 4, 1, f);
	fwrite(&compressionType, 4, 1, f);
	for (std::vector<uint64_t>::const_iterator it = tile_order.begin(); it != tile_order.end(); it++) {
		if (m_tiles_compressed.find(*it) == m_tiles_compressed.end()) {
			CompressAll();
		}
		if (m_tiles_compressed.find(*it) == m_tiles_compressed.end()) {
			throw InvalidTileIndexException();
		}
		fwrite(m_tiles_compressed[*it], 680, 1, f);
	}
	fflush(f);
}
uint64_t TileStorage::AddTileOrGetSimiliar(uint8_t* data, float th, int compresslevel)
{
	uint64_t checksum = tilechecksum(data);
	if (m_tiles.find(checksum) != m_tiles.end()) {
		// std::cout << "Debug(AddTileOrGetSimiliar): " << checksum << " already exists" << std::endl;
		return checksum;
	}
	if (compresslevel == COMPRESS_INSANE) {
		for (std::unordered_map<uint64_t, uint8_t*>::iterator it = m_tiles.begin(); it != m_tiles.end(); it++) {
			if (tilediff(data, (*it).second) < th) {
				return (*it).first;
			}
		}
	} else if (compresslevel == COMPRESS_REASONABLE) {
		for (std::list<uint64_t>::iterator it = m_lasttiles.begin(); it != m_lasttiles.end(); it++) {
			if (tilediff(data, m_tiles.at(*it)) < th) {
				return (*it);
			}
		}


	} else if (compresslevel == COMPRESS_SHITTY) {
		// do nothing...

	} else if (compresslevel == COMPRESS_REASONABLE_BESTQUALITY) {
		float mindiff = 9999999.0f;
		uint64_t besttile = 0;
		for (std::list<uint64_t>::iterator it = m_lasttiles.begin(); it != m_lasttiles.end(); it++) {
			float diff = tilediff(data, m_tiles.at(*it));
			if (diff < mindiff) {
				besttile = (*it);
				mindiff = diff;
			}
		}
		if (mindiff <= th)
			return besttile;
	}
	return AddTile(data, checksum);
}
uint32_t TileStorage::GetTileCount()
{
	return std::max(m_tiles_compressed.size(), m_tiles.size());
}
