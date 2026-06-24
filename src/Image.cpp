/* This file is part of SpringMapConvNG (GPL v2 or later), see the LICENSE file */

#include "Image.h"

CannotLoadImageException::CannotLoadImageException(std::string path)
    : runtime_error(path)
{
}

Image::Image()
{
	ilGenImages(1, &image);
	w = 0;
	h = 0;
	d = 0;
	datapointer = NULL;
}
void Image::GetRect(int x, int y, int w, int h, ILenum format, void* dest)
{
	ilBindImage(image);
	ilCopyPixels(x, y, 0, w, h, 1, format, IL_UNSIGNED_BYTE, dest);
}
void Image::GetRect(int x, int y, int w, int h, ILenum format, ILenum type, void* dest)
{
	ilBindImage(image);
	ilCopyPixels(x, y, 0, w, h, 1, format, type, dest);
}
void Image::FlipVertical()
{
	ilBindImage(image);
	iluFlipImage();
}

Image::Image(const char* filename, bool hdrlum)
{

	ilGenImages(1, &image);
	ilBindImage(image);
	if (!ilLoadImage(filename)) {
		throw CannotLoadImageException(std::string(filename));
	}
	if (!hdrlum) {
		ConvertToRGBA();
	} else if (ilGetInteger(IL_IMAGE_BYTES_PER_PIXEL) != 2 || ilGetInteger(IL_IMAGE_FORMAT) != IL_LUMINANCE) {
		ConvertToLUMHDR();
	}
	w = ilGetInteger(IL_IMAGE_WIDTH);
	h = ilGetInteger(IL_IMAGE_HEIGHT);
	d = ilGetInteger(IL_IMAGE_DEPTH);
	datapointer = ilGetData();
}
void Image::Save(const char* filename)
{
	ilBindImage(image);
	ilSaveImage(filename);
}
void Image::AllocateLUM(int x, int y, char* data)
{
	ilBindImage(image);
	ilTexImage(x, y, 1, 1, IL_LUMINANCE, IL_UNSIGNED_BYTE, data);
	datapointer = ilGetData();
	w = ilGetInteger(IL_IMAGE_WIDTH);
	h = ilGetInteger(IL_IMAGE_HEIGHT);
	d = ilGetInteger(IL_IMAGE_DEPTH);
}

void Image::AllocateRGBA(int x, int y, char* data)
{
	ilBindImage(image);
	ilTexImage(x, y, 1, 4, IL_RGBA, IL_UNSIGNED_BYTE, data);
	datapointer = ilGetData();
	w = ilGetInteger(IL_IMAGE_WIDTH);
	h = ilGetInteger(IL_IMAGE_HEIGHT);
	d = ilGetInteger(IL_IMAGE_DEPTH);
}
void Image::Rescale(int x, int y)
{
	ilBindImage(image);
	iluScale(x, y, 1);
	datapointer = ilGetData();
	w = ilGetInteger(IL_IMAGE_WIDTH);
	h = ilGetInteger(IL_IMAGE_HEIGHT);
	d = ilGetInteger(IL_IMAGE_DEPTH);
}

void Image::ConvertToLUM()
{
	ilBindImage(image);
	ilConvertImage(IL_LUMINANCE, IL_UNSIGNED_BYTE);
	datapointer = ilGetData();
}
void Image::ConvertToLUMHDR()
{
	ilBindImage(image);
	ilConvertImage(IL_LUMINANCE, IL_UNSIGNED_SHORT);
	datapointer = ilGetData();
}

void Image::ConvertToRGBA()
{
	ilBindImage(image);
	ilConvertImage(IL_RGBA, IL_UNSIGNED_BYTE);
	datapointer = ilGetData();
}
Image::~Image()
{
	ilDeleteImages(1, &image);
}
