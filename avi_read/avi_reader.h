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

typedef struct
{
	void* userdata;
	fssize_t(*f_read)(void *buffer, size_t len, void* userdata);
	fssize_t(*f_seek)(fsize_t offset, void* userdata);
	fssize_t(*f_tell)(void* userdata);
	int(*f_eof)(void* userdata);
	void (*logprintf)(void* userdata, const char* fmt);
	uint32_t riff_len;
	uint32_t num_streams;
}avi_stream_reader;

int avi_stream_reader_init
(
	avi_stream_reader* r,
	void* userdata,
	fssize_t(*f_read)(void* buffer, size_t len, void* userdata),
	fssize_t(*f_seek)(fsize_t offset, void* userdata),
	fssize_t(*f_tell)(void* userdata),
	int(*f_eof)(void* userdata),
	void(*logprintf)(void* userdata, const char* fmt)
);





#endif
