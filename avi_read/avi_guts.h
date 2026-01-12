#ifndef _AVI_GUTS_H_
#define _AVI_GUTS_H_ 1

#include <stddef.h>
#include <inttypes.h>

#pragma pack(push, 1)

typedef struct
{
	int16_t x;
	int16_t y;
	int16_t r;
	int16_t b;
}rect;

typedef struct
{
	uint32_t cb;
	uint32_t dwMicroSecPerFrame;
	uint32_t dwMaxBytesPerSec;
	uint32_t dwPaddingGranularity;
	uint32_t dwFlags;
	uint32_t dwTotalFrames;
	uint32_t dwInitialFrames;
	uint32_t dwStreams;
	uint32_t dwSuggestedBufferSize;
	uint32_t dwWidth;
	uint32_t dwHeight;
	uint32_t dwReserved[4];
} avi_main_header;

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
	uint32_t dwChunkId;
	uint32_t dwFlags;
	uint32_t dwOffset;
	uint32_t dwSize;
} avi_index_entry;

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
} bitmap_info_header;

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

typedef struct
{
	uint8_t R;
	uint8_t G;
	uint8_t B;
	uint8_t A;
} palette_entry;

typedef struct
{
	bitmap_info_header BMIF;
	union
	{
		uint32_t bitfields[4];
		palette_entry palette[256];
	};
} bitmap_header_max_size;

typedef struct
{
	uint8_t first_entry;
	uint8_t num_entries;
	uint16_t flags;
	palette_entry palette[256];
} avi_palette_change_max_size;

#pragma pack(pop)

#define BI_RGB 0
#define BI_RLE8 1
#define BI_RLE4 2
#define BI_BITFIELDS 3
#define BI_JPEG 4
#define BI_PNG 5

#endif
