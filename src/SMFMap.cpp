/* This file is part of SpringMapConvNG (GPL v2 or later), see the LICENSE file */

#include "SMFMap.h"
#include "Dxt1.h"
#include "Dxt1Encode.h"
#include "Image.h"
#include "Raster.h"
#include "TileStorage.h"
#include <atomic>
#include <cstring>
#include <iostream>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <thread>
#include <unordered_map>


#ifndef bzero
#define bzero(ptr, len) memset(ptr, 0, len)

#endif

// fread that fails loudly on a short read instead of silently leaving the
// destination buffer with uninitialised/garbage data. Truncated or corrupt
// input is unrecoverable for this tool, so we report and abort.
static void checkedFread(void* ptr, size_t size, size_t count, FILE* f)
{
	if (fread(ptr, size, count, f) != count) {
		std::cerr << "Error: unexpected end of file or read error (input truncated or corrupt)" << std::endl;
		throw InvalidSmfFileException();
	}
}
SMFMap::SMFMap(std::string name, std::string texturepath)
{

	m_tiles = new TileStorage();
	metalmap = NULL;
	heightmap = NULL;
	typemap = NULL;
	minimap = NULL;
	vegetationmap = NULL;
	r_metalmap = NULL;
	r_heightmap = NULL;
	r_typemap = NULL;
	r_texture = NULL;
	r_minimap = NULL;
	texture = new Image(texturepath.c_str());

	if (texture->w < 1)
		throw CannotLoadTextureException();
	if (texture->w % 1024 != 0 || texture->h % 1024 != 0) {
		throw InvalidMapSizeException();
	}
	mapx = (texture->w / 1024) * 128;
	mapy = (texture->h / 1024) * 128;
	m_minh = 0.0;
	m_maxh = 1.0;
	m_name = name;
	m_doclamp = true;
	m_th = 0;
	m_comptype = COMPRESS_REASONABLE;
	m_smooth = false;
	texpath = texturepath;
}
SMFMap::SMFMap(std::string smfname)
{
	std::vector<std::string> tile_files;
	std::vector<std::vector<uint8_t>> tiles_rgba;
	metalmap = NULL;
	heightmap = NULL;
	typemap = NULL;
	minimap = NULL;
	vegetationmap = NULL;
	texture = NULL;
	r_metalmap = NULL;
	r_heightmap = NULL;
	r_typemap = NULL;
	r_texture = NULL;
	r_minimap = NULL;
	m_tiles = NULL;
	FILE* smffile = fopen(smfname.c_str(), "rb");
	if (!smffile) {
		throw CannotLoadSmfFileException();
	}
	SMFHeader hdr;
	checkedFread(&hdr, sizeof(hdr), 1, smffile);
	if (strncmp(hdr.magic, "spring map file", 15) != 0) {
		fclose(smffile);
		throw InvalidSmfFileException();
	}
	mapx = hdr.mapx;
	mapy = hdr.mapy;
	m_minh = hdr.minHeight;
	m_maxh = hdr.maxHeight;
	m_smfname = smfname;
	m_doclamp = true;
	m_th = 0;
	m_comptype = COMPRESS_REASONABLE;
	m_smooth = false;
	r_texture = new Raster((mapx / 128) * 1024, (mapy / 128) * 1024, 4, 1);
	std::cout << "Loading metal map..." << std::endl;
	// Orientation note: DevIL's ilSaveImage emitted PNGs with one implicit
	// vertical flip (its images default to a lower-left origin). To reproduce
	// the original on-disk output without DevIL, the explicit flips below are
	// the inverse of what the old DevIL code did per map.
	r_metalmap = new Raster(mapx / 2, mapy / 2, 1, 1);
	fseek(smffile, hdr.metalmapPtr, SEEK_SET);
	checkedFread(r_metalmap->ptr(), mapx / 2 * mapy / 2, 1, smffile);
	r_metalmap->flipVertical();


	std::cout << "Loading heightmap..." << std::endl;
	r_heightmap = new Raster(mapx + 1, mapy + 1, 1, 2);
	fseek(smffile, hdr.heightmapPtr, SEEK_SET);
	checkedFread(r_heightmap->ptr(), (mapx + 1) * (mapy + 1) * 2, 1, smffile);

	std::cout << "Loading type map..." << std::endl;
	r_typemap = new Raster(mapx / 2, mapy / 2, 1, 1);
	fseek(smffile, hdr.typeMapPtr, SEEK_SET);
	checkedFread(r_typemap->ptr(), mapx / 2 * mapy / 2, 1, smffile);

	std::cout << "Loading minimap..." << std::endl;
	r_minimap = new Raster(1024, 1024, 4, 1);
	uint8_t* dxt1data = new uint8_t[699064];
	fseek(smffile, hdr.minimapPtr, SEEK_SET);
	checkedFread(dxt1data, 699064, 1, smffile);
	decodeBC1(dxt1data, 1024, 1024, r_minimap->ptr());
	std::cout << "Extracting main texture..." << std::endl;
	int* tilematrix = new int[mapx / 4 * mapy / 4];

	fseek(smffile, hdr.tilesPtr, SEEK_SET);
	MapTileHeader thdr;
	checkedFread(&thdr, sizeof(thdr), 1, smffile);
	while (tile_files.size() < thdr.numTileFiles) {
		tile_files.push_back("");
		char byte;
		int numtiles;
		checkedFread(&numtiles, 4, 1, smffile);
		checkedFread(&byte, 1, 1, smffile);
		while (byte != 0) {
			tile_files[tile_files.size() - 1].append(1, byte);
			checkedFread(&byte, 1, 1, smffile);
		}
	}
	for (std::vector<std::string>::iterator it = tile_files.begin(); it != tile_files.end(); it++) {
		std::cout << "Opening " << *it << std::endl;
		FILE* smtfile = fopen((*it).c_str(), "rb");
		if (!smtfile) {
			fclose(smffile);
			delete[] tilematrix;
			throw CannotOpenSmtFileException();
		}
		TileFileHeader smthdr;
		checkedFread(&smthdr, sizeof(smthdr), 1, smtfile);
		if (strncmp(smthdr.magic, "spring tilefile", 14)) {
			fclose(smffile);
			fclose(smtfile);
			delete[] tilematrix;
			throw InvalidSmtFileException();
		}
		for (int i = 0; i < smthdr.numTiles; i++) {
			checkedFread(dxt1data, 680, 1, smtfile);
			tiles_rgba.emplace_back(32 * 32 * 4);
			decodeBC1(dxt1data, 32, 32, tiles_rgba.back().data());
		}
		fclose(smtfile);
	}
	std::cout << "Tiles @ " << ftell(smffile) << std::endl;
	checkedFread(tilematrix, mapx / 4 * mapy / 4 * 4, 1, smffile);
	unsigned int* texdata = (unsigned int*)r_texture->ptr();
	std::cout << "Blitting tiles..." << std::endl;
	for (int y = 0; y < mapy / 4; y++) {
		std::cout << "Row " << y << " of " << mapy / 4 << std::endl;
		for (int x = 0; x < mapx / 4; x++) {
			if (tilematrix[y * (mapx / 4) + x] >= (int)tiles_rgba.size()) {
				std::cerr << "Warning: tile " << tilematrix[y * (mapx / 4) + x] << " out of range" << std::endl;
				continue;
			}
			unsigned int* data = (unsigned int*)tiles_rgba[tilematrix[y * (mapx / 4) + x]].data();
			int r2 = 0;
			for (int y2 = y * 32; y2 < y * 32 + 32; y2++) // FAST blitting
			{
				memcpy(&texdata[y2 * r_texture->w + x * 32], &data[r2 * 32], 32 * 4);
				r2++;
			}
		}
	}


	std::cout << "Loading features..." << std::endl;

	fseek(smffile, hdr.featurePtr, SEEK_SET);
	MapFeatureHeader mfhdr;
	checkedFread(&mfhdr, sizeof(mfhdr), 1, smffile);
	//-32767.0f+f->rotation/65535.0f*360

	std::vector<std::string> feature_types;
	while (feature_types.size() < mfhdr.numFeatureType) {
		feature_types.push_back("");
		char byte;
		checkedFread(&byte, 1, 1, smffile);
		while (byte != 0) {
			feature_types[feature_types.size() - 1].append(1, byte);
			checkedFread(&byte, 1, 1, smffile);
		}
	}
	for (int i = 0; i < mfhdr.numFeatures; i++) {
		MapFeatureStruct f;
		checkedFread(&f, sizeof(f), 1, smffile);
		if (f.featureType >= feature_types.size()) {
			std::cerr << "Warning: invalid feature type " << f.featureType << std::endl;
			continue;
		}
		AddFeature(feature_types[f.featureType], f.xpos, f.ypos, f.zpos, -32767.0f + f.rotation / 65535.0f * 360);
	}
	fclose(smffile);
	delete[] dxt1data;
	delete[] tilematrix;
}

void SMFMap::SetClamping(bool b)
{
	m_doclamp = b;
}
void SMFMap::SaveSourceFiles()
{
	if (r_metalmap) {
		r_metalmap->savePng("metalmap.png");
	}
	if (r_typemap) {
		r_typemap->savePng("typemap.png");
	}
	if (r_heightmap) {
		r_heightmap->savePng("heightmap.png");
	}
	if (r_texture) {
		r_texture->savePng("texture.png");
	}
	if (r_minimap) {
		r_minimap->savePng("minimap.png");
	}
	FILE* featurefile = fopen("features.txt", "w");
	for (std::map<std::string, std::list<MapFeatureStruct*>*>::iterator it = features.begin(); it != features.end(); it++) {
		for (std::list<MapFeatureStruct*>::iterator it2 = (*it).second->begin(); it2 != (*it).second->end(); it2++) {
			MapFeatureStruct* f = (*it2);
			float degrot = -32767.0f + f->rotation / 65535.0f * 360; // 32767.0f-((orientation/360.0)*65535.0f);

			fprintf(featurefile, "%s %f %f %f %f\n", (*it).first.c_str(), f->xpos, f->ypos, f->zpos, degrot);
		}
	}
	fclose(featurefile);

	const char* compileCmd = "springMapConvNG -t texture.png -h heightmap.png -z typemap.png -m metalmap.png -maxh %f -minh %f -th 0.8 -ct 4 -features features.txt -minimap minimap.png -o \"%s\"";
	FILE* makefile = fopen("Makefile", "w");
	fprintf(makefile, "%s: texture.png heightmap.png typemap.png metalmap.png minimap.png features.txt\n", m_smfname.c_str());
	std::string smfbasename = m_smfname.substr(0, m_smfname.find("."));
	fprintf(makefile, "\t");
	fprintf(makefile, compileCmd, m_maxh, m_minh, smfbasename.c_str());
	fprintf(makefile, "\n");
	fclose(makefile);

	FILE* batchfile = fopen("make.bat", "w");
	fprintf(batchfile, compileCmd, m_maxh, m_minh, smfbasename.c_str());
	fprintf(batchfile, "\r\npause\r\n");
	fclose(batchfile);
}

void SMFMap::SetVegetationMap(std::string path)
{
	Image* img = new Image(path.c_str());
	if (img->w > 0) {
		if (vegetationmap)
			delete vegetationmap;
		vegetationmap = img;
		vegetationmap->ConvertToLUM();
		if (img->w != mapx / 4 || img->h != mapy / 4) {
			std::cerr << "Warning: Vegetation map has wrong size , rescaling!" << std::endl;
			vegetationmap->Rescale(mapx / 4, mapy / 4);
		}
	}
}
void SMFMap::AddFeature(std::string name, float x, float y, float z, float orientation)
{
	if (features.find(name) == features.end()) // Allocate new vector
	{
		features[name] = new std::list<MapFeatureStruct*>();
	}
	MapFeatureStruct* feat = (MapFeatureStruct*)malloc(sizeof(MapFeatureStruct));
	feat->xpos = x;
	feat->ypos = y;
	feat->zpos = z;
	feat->rotation = 32767.0f - ((orientation / 360.0) * 65535.0f);
	feat->relativeSize = 1;
	features[name]->push_back(feat);
}

void SMFMap::SetHeightRange(float minh, float maxh)
{
	m_minh = minh;
	m_maxh = maxh;
}

SMFMap::~SMFMap()
{
	delete m_tiles;
	if (metalmap)
		delete metalmap;
	if (heightmap)
		delete heightmap;
	if (typemap)
		delete typemap;
	if (minimap)
		delete minimap;
	if (texture)
		delete texture;
	if (vegetationmap)
		delete vegetationmap;
	delete r_metalmap;
	delete r_heightmap;
	delete r_typemap;
	delete r_texture;
	delete r_minimap;
}
void SMFMap::SetMiniMap(std::string path)
{
	delete texture;
	std::cout << "Loading minimap " << path << std::endl;
	Image* img = new Image(path.c_str());
	if (img->w > 0) {
		if (minimap)
			delete minimap;
		minimap = img;
		minimap->ConvertToRGBA();
		minimap->FlipVertical();
		if (img->w != 1024 || img->h != 1024) {
			std::cerr << "Warning: Minimap has wrong size , rescaling!" << std::endl;
			minimap->Rescale(1024, 1024);
		}
		texture = new Image(texpath.c_str());
		return;
	}
	std::cout << "Failed " << path << std::endl;
	texture = new Image(texpath.c_str());
}

/*void SMFMap::SetFeatureMap(std::string path)
{
  Image * img = new Image(path.c_str());
  if ( img->w > 0 )
  {
    if ( featuremap )
      delete featuremap;
    featuremap = img;
    featuremap->ConvertToLUM();
  }
  if ( img->w != mapx/2 || img->h != mapy/2 )
  {
    std::cerr << "Warning: Feature map has wrong size , rescaling!" << std::endl;
    heightmap->Rescale(mapx+1,mapy+1);

  }
}*/
void SMFMap::SetHeightMap(std::string path)
{
	Image* img = new Image(path.c_str(), true);
	if (img->w > 0) {
		if (heightmap)
			delete heightmap;
		heightmap = img;
		// heightmap->ConvertToLUMHDR();
		if (img->w != mapx + 1 || img->h != mapy + 1) {
			std::cerr << "Warning: Height map has wrong size , rescaling! (" << img->w << "," << img->h << ") instead of (" << mapx + 1 << "," << mapy + 1 << ")" << std::endl;
			heightmap->Rescale(mapx + 1, mapy + 1);
		}
		// Clamp heightmap before blurring
		if (m_doclamp) {
			float _min = 65537.0f;
			float _max = -65337.0f;
			unsigned short* pixels = (unsigned short*)heightmap->datapointer;
			for (int i = 0; i < heightmap->w * heightmap->h; i++) {
				if (_min > pixels[i])
					_min = pixels[i];
				if (_max < pixels[i])
					_max = pixels[i];
			}
			std::cout << "Range : " << _min << " -> " << _max << std::endl;
			float range = _max - _min;
			for (int i = 0; i < heightmap->w * heightmap->h; i++) {
				pixels[i] = (unsigned short)((((pixels[i] - _min) / range) * 65535.0f));
			}
		}
		if (m_smooth) {
			std::cout << "Blurring heightmap..." << std::endl;
			/*ilBindImage(heightmap->image); // Seems broken with 16 bit image
	  iluBlurAvg(5);
	  heightmap->datapointer = ilGetData();*/
			unsigned short* tempdata = new unsigned short[img->h * img->w];
			for (int pass = 0; pass < 3; pass++) {
				std::cout << "Blurring heightmap pass " << pass + 1 << "..." << std::endl;
				memcpy(tempdata, img->datapointer, img->h * img->w * 2);
				for (int y = 1; y < img->h - 1; y++) {
					for (int x = 1; x < img->w - 1; x++) {
						float sum = 0.0f;
						sum += ((unsigned short*)img->datapointer)[y * img->w + x];
						sum += ((unsigned short*)img->datapointer)[(y - 1) * img->w + (x - 1)];
						sum += ((unsigned short*)img->datapointer)[(y - 1) * img->w + (x - 0)];
						sum += ((unsigned short*)img->datapointer)[(y - 1) * img->w + (x + 1)];
						sum += ((unsigned short*)img->datapointer)[(y - 0) * img->w + (x - 1)];
						sum += ((unsigned short*)img->datapointer)[(y - 0) * img->w + (x + 1)];
						sum += ((unsigned short*)img->datapointer)[(y + 1) * img->w + (x - 1)];
						sum += ((unsigned short*)img->datapointer)[(y + 1) * img->w + (x - 0)];
						sum += ((unsigned short*)img->datapointer)[(y + 1) * img->w + (x + 1)];
						sum /= 9.0f;
						tempdata[y * img->w + x] = (unsigned short)(sum);
					}
				}
				memcpy(img->datapointer, tempdata, img->h * img->w * 2);
			}
			delete[] tempdata;
		}
	}
}
void SMFMap::SetBlur(bool b)
{
	m_smooth = b;
}
void SMFMap::SetCompareTileCount(uint32_t count)
{
	m_tiles->SetDictSize(count);
}

void SMFMap::SetMetalMap(std::string path)
{
	Image* img = new Image(path.c_str());
	if (img->w > 0) {
		if (metalmap)
			delete metalmap;
		metalmap = img;
		metalmap->ConvertToLUM();

		if (img->w != mapx / 2 || img->h != mapy / 2) {
			std::cerr << "Warning: Metal map has wrong size , rescaling! (" << img->w << "," << img->h << ") instead of (" << mapx / 2 << "," << mapy / 2 << ")" << std::endl;
			metalmap->Rescale(mapx / 2, mapy / 2);
		}
	}
}
void SMFMap::SetTypeMap(std::string path)
{
	Image* img = new Image(path.c_str());
	if (img->w > 0) {
		if (typemap)
			delete typemap;
		typemap = img;
		typemap->ConvertToLUM();
		if (img->w != mapx / 2 || img->h != mapy / 2) {
			std::cerr << "Warning: Type map has wrong size , rescaling! (" << img->w << "," << img->h << ") instead of (" << mapx / 2 << "," << mapy / 2 << ")" << std::endl;
			typemap->Rescale(mapx / 2, mapy / 2);
		}
	}
}

void SMFMap::Compile()
{
	SMFHeader hdr;
	strcpy(hdr.magic, "spring map file");
	hdr.version = 1;
	hdr.mapid = rand();
	hdr.mapx = (texture->w / 1024) * 128;
	hdr.mapy = (texture->h / 1024) * 128;
	hdr.squareSize = 8;
	hdr.texelPerSquare = 8;
	hdr.tilesize = 32;
	hdr.minHeight = m_minh;
	hdr.maxHeight = m_maxh;


	short int* hmap = new short int[(mapy + 1) * (mapx + 1)];
	bzero(hmap, ((mapy + 1) * (mapx + 1)) * 2);
	if (heightmap) {
		// heightmap->GetRect(0,0,heightmap->w,heightmap->h,IL_LUMINANCE,IL_SHORT,hmap); : IL seems to fail to convert from unsigned short to signed
		/*for ( int k = 0; k < (mapy+1)*(mapx+1); k++ )
	{
	  int pix = ((unsigned short*)heightmap->datapointer)[k];
	  hmap[k] = short(int(pix)-int(32767));

	  */
		memcpy(hmap, heightmap->datapointer, ((mapy + 1) * (mapx + 1)) * 2);
	}
	unsigned char* typedata = new unsigned char[mapy / 2 * mapx / 2];
	bzero(typedata, (mapy / 2 * mapx / 2));
	if (typemap) {
		typemap->GetRect(0, 0, typemap->w, typemap->h, IL_LUMINANCE, IL_UNSIGNED_BYTE, typedata);
	}
	uint8_t* minimap_data = new uint8_t[699064];
	bzero(minimap_data, 699064);
	if (minimap) {
		int p = 0;
		int s = 1024;
		// Box-downscale the 1024x1024 minimap through 9 DXT1 mip levels
		// (1024..4), the same layout the old DevIL path produced.
		std::vector<uint8_t> level(minimap->datapointer, minimap->datapointer + (size_t)1024 * 1024 * 4);
		for (int i = 0; i < 9; i++) {
			compressDXT1(level.data(), s, s, &minimap_data[p]);
			p += s / 4 * s / 4 * 8;
			if (s > 4) {
				std::vector<uint8_t> next((size_t)(s / 2) * (s / 2) * 4);
				downscaleHalfBox(level.data(), s, s, next.data());
				level.swap(next);
				s >>= 1;
			}
		}
	}
	unsigned char* metalmap_data = new unsigned char[mapx / 2 * mapy / 2];
	bzero(metalmap_data, (mapy / 2 * mapx / 2));
	if (metalmap) {
		metalmap->GetRect(0, 0, metalmap->w, metalmap->h, IL_LUMINANCE, IL_UNSIGNED_BYTE, metalmap_data);
	}
	/*hdr.heightmapPtr = sizeof(hdr);
    hdr.typeMapPtr = hdr.heightmapPtr + ((mapy+1)*(mapx+1))*2;
    hdr.minimapPtr = hdr.typeMapPtr + (mapy/2 * mapx/2);
    hdr.metalmapPtr = hdr.minimapPtr + 699048;
    hdr.featurePtr = hdr.metalmapPtr + (mapy/2 * mapx/2);*/
	MapFeatureHeader mfhdr;

	hdr.numExtraHeaders = 1;
	ExtraHeader grassHeader;
	grassHeader.size = 4;
	grassHeader.type = 1;
	MapTileHeader mthdr;
	mthdr.numTileFiles = 1;
	unsigned char* grass_data = new unsigned char[mapx / 4 * mapy / 4];
	bzero(grass_data, mapx / 4 * mapy / 4);
	if (vegetationmap) {
		vegetationmap->GetRect(0, 0, vegetationmap->w, vegetationmap->h, IL_LUMINANCE, IL_UNSIGNED_BYTE, grass_data);
	}
	int* tiles = new int[mapx / 4 * mapy / 4];
	std::vector<uint64_t> order;
	DoCompress(tiles, order);
	/* for ( int y = 0; y < mapy/4; y++ )
    {
    for ( int x = 0; x < mapx/4; x++ )
      {
	printf("%5d,",tiles[(mapx/4)*y+x]);
      }
      printf("\n");
    }*/

	FILE* tilefile = fopen((m_name + std::string(".smt")).c_str(), "wb");
	delete texture; // Free texture; not used again in Compile() (the destructor handles the rest)
	texture = NULL;
	m_tiles->WriteToFile(tilefile, order);

	fclose(tilefile);
	FILE* smffile = fopen((m_name + std::string(".smf")).c_str(), "wb");
	fwrite(&hdr, sizeof(hdr), 1, smffile);
	fwrite(&grassHeader, sizeof(grassHeader), 1, smffile);
	int _ofs = ftell(smffile) + 4;
	fwrite(&_ofs, 4, 1, smffile);
	fwrite(grass_data, mapx / 4 * mapy / 4, 1, smffile);

	hdr.minimapPtr = ftell(smffile);
	fwrite(minimap_data, 699064, 1, smffile);
	hdr.heightmapPtr = ftell(smffile);
	fwrite(hmap, ((mapy + 1) * (mapx + 1)) * 2, 1, smffile);
	hdr.typeMapPtr = ftell(smffile);
	fwrite(typedata, mapy / 2 * mapx / 2, 1, smffile);

	hdr.metalmapPtr = ftell(smffile);
	fwrite(metalmap_data, mapy / 2 * mapx / 2, 1, smffile);
	hdr.featurePtr = ftell(smffile);


	mfhdr.numFeatures = 0;
	for (std::map<std::string, std::list<MapFeatureStruct*>*>::iterator it = features.begin(); it != features.end(); it++) // Enumerate features
		mfhdr.numFeatures += (*it).second->size();
	mfhdr.numFeatureType = features.size();
	fwrite(&mfhdr, sizeof(mfhdr), 1, smffile);
	{
		std::map<std::string, unsigned int> featureTypes;
		unsigned int z = 0;
		for (std::map<std::string, std::list<MapFeatureStruct*>*>::iterator it = features.begin(); it != features.end(); it++) // Write feature types
		{
			fwrite((*it).first.c_str(), (*it).first.size() + 1, 1, smffile);
			featureTypes[(*it).first] = z++;
		}
		for (std::map<std::string, std::list<MapFeatureStruct*>*>::iterator it = features.begin(); it != features.end(); it++) // Write feature types
		{
			for (std::list<MapFeatureStruct*>::iterator it2 = (*it).second->begin(); it2 != (*it).second->end(); it2++) {
				(*it2)->featureType = featureTypes[(*it).first];
				if (heightmap && (*it2)->ypos < -490000.0f) // Align on terrain
				{
					unsigned int hmapx = ((*it2)->xpos / float((mapx / 128) * 1024)) * heightmap->w;
					unsigned int hmapy = ((*it2)->zpos / float((mapy / 128) * 1024)) * heightmap->h;
					(*it2)->ypos = hdr.minHeight + (float((unsigned short)hmap[hmapy * (mapx + 1) + hmapx]) / 65535.0f) * (hdr.maxHeight - hdr.minHeight);
					std::cout << "Feature " << (*it).first << " Instance " << (*it2) << " Terrain height: " << (*it2)->ypos << std::endl;
				}

				fwrite((*it2), sizeof(MapFeatureStruct), 1, smffile);
			}
		}
	}

	hdr.tilesPtr = ftell(smffile);
	uint32_t tc = m_tiles->GetTileCount();
	mthdr.numTiles = tc;
	fwrite(&mthdr, sizeof(mthdr), 1, smffile);
	fwrite(&tc, 4, 1, smffile);
	fwrite((m_name + std::string(".smt")).c_str(), (m_name + std::string(".smt")).length() + 1, 1, smffile);
	fwrite(tiles, (mapx / 4 * mapy / 4) * 4, 1, smffile);
	fseek(smffile, 0, SEEK_SET);
	fwrite(&hdr, sizeof(hdr), 1, smffile);
	fclose(smffile);
	delete[] metalmap_data;
	delete[] hmap;
	delete[] typedata;
	delete[] tiles;
	delete[] minimap_data;
	delete[] grass_data;
}

// Copy one 32x32 RGBA tile straight out of the texture buffer, vertically
// flipped to match what the old DevIL GetRect()-plus-row-swap path produced.
static inline void extractTileFlipped(const uint8_t* texdata, int texw, int x, int y, uint8_t* out)
{
	for (int r = 0; r < 32; r++) {
		const uint8_t* src = texdata + (size_t)((y * 32 + 31 - r) * texw + x * 32) * 4;
		memcpy(&out[r * 32 * 4], src, 32 * 4);
	}
}

void SMFMap::DoCompress(int* indices, std::vector<uint64_t>& order)
{
	order.clear();

	const int tw = mapx / 4;
	const int tht = mapy / 4;
	const int ntiles = tw * tht;
	const int texw = texture->w;
	const uint8_t* texdata = texture->datapointer;

	// Phase 1 (parallel): extract every tile from the texture buffer and compute
	// its checksum. tilechecksum is the dominant per-tile cost and each tile is
	// independent, so fan the work out across cores. No DevIL, no shared state.
	std::vector<uint64_t> checksums(ntiles);
	{
		unsigned hw = std::thread::hardware_concurrency();
		if (hw == 0)
			hw = 1;
		size_t nthreads = ntiles > 0 ? std::min<size_t>(hw, (size_t)ntiles) : 0;
		std::atomic<int> next(0);
		auto worker = [&]() {
			uint8_t tile[32 * 32 * 4];
			for (;;) {
				int i = next.fetch_add(1);
				if (i >= ntiles)
					break;
				extractTileFlipped(texdata, texw, i % tw, i / tw, tile);
				checksums[i] = TileStorage::Checksum(tile);
			}
		};
		std::vector<std::thread> pool;
		for (size_t t = 0; t < nthreads; t++)
			pool.push_back(std::thread(worker));
		for (size_t t = 0; t < pool.size(); t++)
			pool[t].join();
	}

	// Phase 2 (sequential): dedup in processing order, which keeps the result
	// byte-identical to the original single-threaded behaviour.
	std::unordered_map<uint64_t, uint32_t> existingtiles;
	uint8_t tiledata[32 * 32 * 4];
	for (int i = 0; i < ntiles; i++) {
		if (i % 50 == 0)
			printf("\rCompressing %8d/%8d      - %6d tiles                    ", i, ntiles, m_tiles->GetTileCount());
		int x = i % tw;
		int y = i / tw;
		extractTileFlipped(texdata, texw, x, y, tiledata);
		uint64_t uid = m_tiles->AddTileOrGetSimiliar(tiledata, checksums[i], m_th, m_comptype);
		if (existingtiles.find(uid) == existingtiles.end()) {
			indices[tw * y + x] = order.size();
			existingtiles[uid] = order.size();
			order.push_back(uid);
		} else {
			indices[tw * y + x] = existingtiles[uid];
		}
	}
	printf("\n");
	std::cout << "Compress done , ratio: " << float(existingtiles.size()) / float(ntiles) * 100.0 << std::endl;
}
void SMFMap::SetCompressionTol(float th)
{
	m_th = th;
}
void SMFMap::SetCompressionType(int c)
{
	m_comptype = c;
}
