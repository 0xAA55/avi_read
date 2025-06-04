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
	void* userdata;
	fssize_t(*f_read)(void *buffer, size_t len, void* userdata);
	fssize_t(*f_seek)(fsize_t offset, void* userdata);
	fssize_t(*f_tell)(void* userdata);
	int(*f_eof)(void* userdata);
	void (*logprintf)(void* userdata, const char* fmt);
	uint32_t riff_len;
	avi_main_header avih;
	uint32_t num_streams;
	fsize_t stream_header_offsets[AVI_MAX_STREAMS];
	fsize_t stream_data_offset;
	int stream_data_is_lists;
	fssize_t idx_offset;
}avi_reader;

int avi_reader_init
(
	avi_reader* r,
	void* userdata,
	fssize_t(*f_read)(void* buffer, size_t len, void* userdata),
	fssize_t(*f_seek)(fsize_t offset, void* userdata),
	fssize_t(*f_tell)(void* userdata),
	int(*f_eof)(void* userdata),
	void(*logprintf)(void* userdata, const char* fmt)
);





#endif
