/* This file is part of SpringMapConvNG (GPL v2 or later), see the LICENSE file */

#ifndef IMAGE_H
#define IMAGE_H
#include <IL/il.h>
#include <IL/ilu.h>
#include <stdexcept>
#include <string>

class CannotLoadImageException : public std::runtime_error
{
public:
	CannotLoadImageException(std::string path);
};
class Image
{
public:
	ILuint image;
	int w;
	int h;
	int d;
	unsigned char* datapointer;
	Image();

	Image(const char* filename, bool hdrlum = false);

	void Save(const char* filename);

	void AllocateLUM(int x, int y, char* data = NULL);

	void AllocateRGBA(int x, int y, char* data = NULL);

	void Rescale(int x, int y);

	void ConvertToLUM();

	void ConvertToLUMHDR();

	void ConvertToRGBA();

	void FlipVertical();
	void GetRect(int x, int y, int w, int h, ILenum format, void* dest);
	void GetRect(int x, int y, int w, int h, ILenum format, ILenum type, void* dest);
	~Image();
};

#endif // IMAGE_H
