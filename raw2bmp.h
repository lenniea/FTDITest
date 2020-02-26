#ifndef __RAW2BMP_H__
#define __RAW2BMP_H__

#ifdef __cplusplus
	extern "C" {
#endif

#ifdef _WIN32
	#define WIN32_LEAN_AND_MEAN
	#include <windows.h>
	typedef unsigned char uint8_t;
	typedef unsigned short uint16_t;
	typedef unsigned long uint32_t;
#else
	#include <string.h>
	#include "bitmap.h"
	#include <stdint.h>
#endif

enum image_orient { LANDSCAPE, PORTRAIT };
typedef enum image_orient IMAGE_ORIENT;

#pragma pack(push)
#pragma pack(1)

typedef struct bmp_file
{
	BITMAPFILEHEADER bfHeader;
	BITMAPINFOHEADER bmiHeader;
	RGBQUAD          bmiColors[256];
} BMP_FILE;

#pragma pack(pop)

void ffc_offset(uint16_t* buf, const uint16_t* ffc, size_t pixels);
uint8_t* raw2bmp(uint16_t *rawbuf, size_t pixels, IMAGE_ORIENT orient, BMP_FILE* bmpFile);

#ifdef __cplusplus
	}
#endif

#endif /* __RAW2BMP_H__ */
