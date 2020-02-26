#include "raw2bmp.h"
#include <stdlib.h>

#define HIST_SIZE		65536
uint16_t histogram[HIST_SIZE];

void calc_hist(uint16_t* image, uint16_t start_row, uint16_t crop_rows, uint16_t rows, uint16_t width)
{
	uint16_t row, col;
	uint16_t cols = width - 2;

	// Clear out histogram bins
	memset(histogram, 0, sizeof(histogram));
	for (row = start_row; row < rows - crop_rows; ++row) {
		int index = row * width;
		for (col = 0; col < cols; ++col) {
			uint16_t pixel = image[index++];
			++histogram[pixel];
		}
	}
}

#if !defined(WIN32)

int32_t MulDiv(int32_t value, int32_t numer, int32_t denom)
{
	return (int32_t) ((int64_t) value * (int64_t) numer / denom);
}

#endif

int pixels2width(size_t pixels)
{
	int width = 320;
	switch (pixels) {
	case 640*480:
		width = 640;
		break;
	case 320*240:
		width = 320;
		break;
	case 208*156:
		width = 208;
		break;
	case 206*156:
		width = 206;
		break;
	case 200*150:
		width = 200;
		break;
	default:
		;
	}
	return width;
}

uint8_t* raw2bmp(uint16_t *rawbuf, size_t pixels, IMAGE_ORIENT orient, BMP_FILE* bmpFile)
{
    // Try to determine width * height from file size
	int width = pixels2width(pixels);
	uint16_t rows = pixels / width;
	uint16_t cols;
	uint16_t crop_rows, start_row;
	uint16_t image_rows;
	uint32_t sum;
	uint32_t p;
	uint16_t mean;
	uint16_t u;
	uint16_t min, max;
	uint32_t count;
	uint8_t* image;
	uint32_t hist_limit;
	int row, col;
	uint8_t* pout;

	if (orient == PORTRAIT) {
		// Swap width, rows if portrait mode
		uint16_t temp = width;
		width = rows;
		rows = temp;
	}
	cols = (width == 208) ? width - 2 : width;
	if (rows == 157) {
		crop_rows = 6;
		start_row = 3;
	} else {
		crop_rows = 0;
		start_row = 0;
	}
	image_rows = rows - crop_rows - start_row;

    sum = 0;
    for (p = 0; p < pixels; ++p) {
        uint16_t pixel = rawbuf[p];
        sum += pixel;
    }
    mean = (uint16_t) (sum / pixels);

	memset(bmpFile, 0, sizeof(BMP_FILE));

	bmpFile->bfHeader.bfType = 'B' |  ('M' << 8);
	bmpFile->bfHeader.bfOffBits = sizeof(bmpFile);
	bmpFile->bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
	bmpFile->bmiHeader.biWidth = width;
	bmpFile->bmiHeader.biHeight = image_rows;
	bmpFile->bmiHeader.biPlanes = 1;
	bmpFile->bmiHeader.biBitCount = 8;
	bmpFile->bmiHeader.biClrUsed = bmpFile->bmiHeader.biClrImportant = 256;

	// Create 256-color Gray Scale Palette
	for (u = 0; u < 256; ++u) {
		RGBQUAD* pRGB = bmpFile->bmiColors + u;
		pRGB->rgbRed = pRGB->rgbGreen = pRGB->rgbBlue = (uint8_t) u;
		pRGB->rgbReserved = 0;
	}

	// Allocate output image
	image = (uint8_t*) malloc(image_rows * width);
	if (image == NULL) {
		return NULL;
	}

	// Calculate Histogram of image
	calc_hist(rawbuf, start_row, crop_rows, rows, width);

	// Calculate 1%-99% min, max
	hist_limit = (image_rows * cols) / 100;
	count = 0;
	for (min = 0; ; ++min) {
		count += histogram[min];
		if (count >= hist_limit)
			break;
	}
	count = 0;
	for (max = HIST_SIZE-1; ; --max) {
		count += histogram[max];
		if (count >= hist_limit)
			break;
	}
//	fprintf(stdout, "min=%u, mean=%u max=%u\n", min, mean, max);

	// Linear AGC
	pout = image;
	for (row = rows - crop_rows - 1; row >= start_row; --row) {
		uint16_t* pinput = (uint16_t*) (rawbuf + row * width);
		for (col = 0; col < width; ++col) {
			uint16_t pixel = *pinput++;
			uint8_t out;
			if (pixel <= min) {
				out = 0;
			} else if (pixel >= max) {
				out = 255;
			} else {
				out = MulDiv(pixel - min, 255, max - min);
			}
			*pout++ = out;
		}
	}
	return image;
}

void ffc_offset(uint16_t* buf, const uint16_t* ffc, size_t pixels)
{
	// Calculate Flat Field mean average
	uint32_t sum = 0;
	size_t u;
	uint16_t mean;

	for (u = 0; u < pixels; ++u) {
		sum += ffc[u];
	}
	mean = (uint16_t) (sum / pixels);
	
	// Offset raw frame to create image
	for (u = 0; u < pixels; ++u) {
		int pixel = (mean - ffc[u]) + buf[u];
		// Limit to 14-bits
		buf[u] = (pixel < 0) ? 0 : (pixel > 0x3FFF) ? 0x3FFF : pixel;
	}
}
