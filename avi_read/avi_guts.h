#ifndef _AVI_GUTS_H_
#define _AVI_GUTS_H_ 1

#include <stddef.h>
#include <inttypes.h>

typedef struct
{
	int32_t x;
	int32_t y;
	int32_t r;
	int32_t b;
}rect;

typedef struct
{
	uint32_t fccType;
	uint32_t fccHandler;
	uint32_t dwFlags;
	uint16_t wPriority;
	uint16_t wLanguage;
	uint32_t dwInitialFrames;
	uint32_t dwScale;
	uint32_t dwRate;
	uint32_t dwStart;
	uint32_t dwLength;
	uint32_t dwSuggestedBufferSize;
	uint32_t dwQuality;
	uint32_t dwSampleSize;
	rect     rcFrame;
} avi_stream_header;

typedef struct
{
	uint32_t biSize;
	int32_t  biWidth;
	int32_t  biHeight;
	uint16_t biPlanes;
	uint16_t biBitCount;
	uint32_t biCompression;
	uint32_t biSizeImage;
	int32_t  biXPelsPerMeter;
	int32_t  biYPelsPerMeter;
	uint32_t biClrUsed;
	uint32_t biClrImportant;
}bitmap_info_header;

typedef struct
{
	uint16_t wFormatTag;
	uint16_t nChannels;
	uint32_t nSamplesPerSec;
	uint32_t nAvgBytesPerSec;
	uint16_t nBlockAlign;
	uint16_t wBitsPerSample;
	uint16_t cbSize;
} wave_format_ex;

#endif
