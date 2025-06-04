#ifndef _AVI_READER_H_
#define _AVI_READER_H_ 1

#include "avi_guts.h"

#ifndef AVI_ENABLE_4GB_FILES
typedef uint32_t fsize_t;
typedef int32_t fssize_t;
#else
typedef uint64_t fsize_t;
typedef int64_t fssize_t;
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
}avi_stream_data;

typedef struct
{
	void* userdata;
	fssize_t(*f_read)(void *buffer, size_t len, void* userdata);
	fssize_t(*f_seek)(fsize_t offset, void* userdata);
	fssize_t(*f_tell)(void* userdata);
	void (*logprintf)(void* userdata, const char* fmt, ...);
	uint32_t riff_len;
	avi_main_header avih;
	uint32_t num_streams;
	avi_stream_data avi_stream_data[AVI_MAX_STREAMS];
	fsize_t stream_data_offset;
	int stream_data_is_lists;
	fsize_t idx_offset;
}avi_reader;

int avi_reader_init
(
	avi_reader* r,
	void* userdata,
	fssize_t(*f_read)(void* buffer, size_t len, void* userdata),
	fssize_t(*f_seek)(fsize_t offset, void* userdata),
	fssize_t(*f_tell)(void* userdata),
	void(*logprintf)(void* userdata, const char* fmt, ...)
);





#endif
