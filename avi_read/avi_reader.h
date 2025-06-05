#ifndef _AVI_READER_H_
#define _AVI_READER_H_ 1

#include "avi_guts.h"

#ifndef AVI_ENABLE_4GB_FILES
typedef uint32_t fsize_t;
typedef int32_t fssize_t;
#define PRIfsize_t PRIu32
#define PRIxfsize_t PRIx32
#define PRIfssize_t PRId32
#define PRIxfssize_t PRIx32
#else
typedef uint64_t fsize_t;
typedef int64_t fssize_t;
#define PRIfsize_t PRIu64
#define PRIxfsize_t PRIx64
#define PRIfssize_t PRId64
#define PRIxfssize_t PRIx64
#endif

#ifndef AVI_MAX_STREAMS
#define AVI_MAX_STREAMS 8
#endif

typedef struct
{
	avi_stream_header stream_header;
	fsize_t stream_format_offset;
	fsize_t stream_format_len;
	fsize_t stream_additional_header_data_offset;
	fsize_t stream_additional_header_data_len;
	char stream_name[256];
}avi_stream_info;

typedef fssize_t(*read_cb)(void* buffer, size_t len, void* userdata);
typedef fssize_t(*seek_cb)(fsize_t offset, void* userdata);
typedef fssize_t(*tell_cb)(void* userdata);
typedef void (*logprintf_cb)(void* userdata, const char* fmt, ...);

typedef void(*on_stream_data_cb)(fsize_t offset, fsize_t length, void *userdata);

typedef enum
{
	PRINT_NOTHING = 0,
	PRINT_FATAL = 1,
	PRINT_WARN = 2,
	PRINT_INFO = 3,
	PRINT_DEBUG = 4,
}avi_logprintf_level;

/// <summary>
/// The core struct of this library, stores the critical informations about the AVI file.
/// With this struct initialized by calling `avi_reader_init()`, you can then extract packets from each stream of the AVI file.
/// </summary>
typedef struct
{
	void *userdata; /// The data to pass to your callback functions.
	read_cb f_read; /// Your `read()` callback function pointer.
	seek_cb f_seek; /// Your `seek()` callback function pointer.
	tell_cb f_tell; /// Your `tell()` callback function pointer.

	/// Your `printf()` callback function pointer.
	logprintf_cb f_logprintf;

	/// The log level, see `avi_logprintf_level`
	avi_logprintf_level log_level;

	/// The position of the end of the AVI file.
	fsize_t end_of_file;

	/// The AVI main header.
	avi_main_header avih;

	/// Number of streams inside the AVI file.
	uint32_t num_streams;

	/// AVI stream header data.
	avi_stream_info avi_stream_info[AVI_MAX_STREAMS];

	/// The offset to the AVI file's "body".
	fsize_t stream_data_offset;

	/// The `idx1` chunk offset. If the AVI file has an `idx1` chunk, seeking in this AVI file would be very fast and cheap.
	fsize_t idx_offset;

	/// Number of entries in the `idx1` chunk.
	fsize_t num_indices;
}avi_reader;

typedef struct
{
	avi_reader *r;
	int stream_id;
	avi_stream_info *stream_info;
	uint32_t cur_4cc;
	fsize_t cur_packet_offset;
	fsize_t cur_packet_len;
	on_stream_data_cb on_video_compressed;	/// Compressed video frame got
	on_stream_data_cb on_video;				/// Uncompressed video frame (probably BMP) got
	on_stream_data_cb on_palette_change;	/// Palette change for your video (If the AVI file is using color index as pixel data, the actual color in RGB form comes from the palette)
	on_stream_data_cb on_audio;				/// Audio, it can be either compressed or uncompressed, depends on its format.
}avi_stream_reader;

int avi_reader_init
(
	avi_reader* r,
	void* userdata,
	read_cb f_read,
	seek_cb f_seek,
	tell_cb f_tell,
	logprintf_cb f_logprintf,
	avi_logprintf_level log_level
);

int avi_get_stream_reader
(
	avi_reader *r,
	int stream_id,
	on_stream_data_cb on_video_compressed,
	on_stream_data_cb on_video,
	on_stream_data_cb on_palette_change,
	on_stream_data_cb on_audio,
	avi_stream_reader *s_out
);

#endif
