#include"avi_reader.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#define MATCH4CC(str) (*(const uint32_t*)(str))
#define MAKE4CC(c1, c2, c3, c4) ((uint32_t)(c1) | (uint32_t)((c2) << 8) | (uint32_t)((c3) << 16) | (uint32_t)((c4) << 24))

static int must_match(avi_reader* r, const char* fourcc)
{
	char buf[5] = { 0 };
	if (r->f_read(buf, 4, r->userdata) != 4) return 0;
	if (memcmp(buf, fourcc, 4))
	{
		r->logprintf(r->userdata, "[ERROR] Matching FourCC failed: %s != %s\r\n", buf, fourcc);
		return 0;
	}
	return 1;
}

static int must_read(avi_reader* r, void* buffer, size_t len)
{
	fssize_t rl = r->f_read(buffer, len, r->userdata);
	if (rl < 0)
	{
		r->logprintf(r->userdata, "[ERROR] Read %u bytes failed.\r\n", (unsigned int)len);
		return 0;
	}
	else if (rl != len)
	{
		r->logprintf(r->userdata, "[ERROR] Tried to read %u bytes, got %u bytes.\r\n", (unsigned int)len, (unsigned int)rl);
		return 0;
	}
	else
	{
		return 1;
	}
}

static int must_tell(avi_reader* r, fsize_t* cur_pos)
{
	fssize_t told = r->f_tell(r->userdata);
	if (told < 0)
	{
		r->logprintf(r->userdata, "[ERROR] `f_tell()` failed.\r\n");
		return 0;
	}
	else
	{
		*cur_pos = (fsize_t)told;
		return 1;
	}
}

static int must_seek(avi_reader* r, fsize_t target)
{
	fssize_t told = r->f_seek(target, r->userdata);
	if (told < 0)
	{
		r->logprintf(r->userdata, "[ERROR] `f_seek(%x)` failed.\r\n", target);
		return 0;
	}
	else
	{
		return 1;
	}
}

static int rel_seek(avi_reader* r, fssize_t offset)
{
	fssize_t cur_pos = r->f_tell(r->userdata);
	if (cur_pos < 0)
	{
		r->logprintf(r->userdata, "[ERROR] `f_tell()` failed.\r\n");
		return 0;
	}
	fsize_t target = (fsize_t)(cur_pos + offset);
	cur_pos = r->f_seek(target, r->userdata);
	if (cur_pos < 0)
	{
		r->logprintf(r->userdata, "[ERROR] `f_seek(%x)` failed.\r\n", target);
		return 0;
	}
	return 1;
}

static void default_logprintf(void *userdata, const char* format)
{
	va_list ap;
	va_start(ap, format);
	vprintf(format, ap);
	va_end(ap);
}

int avi_reader_init
(
	avi_reader* r,
	void* userdata,
	fssize_t(*f_read)(void* buffer, size_t len, void* userdata),
	fssize_t(*f_seek)(fsize_t offset, void* userdata),
	fssize_t(*f_tell)(void* userdata),
	int(*f_eof)(void* userdata),
	void(*logprintf)(void* userdata, const char* fmt)
)
{
	memset(r, 0, sizeof *r);
	if (!logprintf) logprintf = default_logprintf;

	r->userdata = userdata;
	r->f_read = f_read;
	r->f_seek = f_seek;
	r->f_tell = f_tell;
	r->f_eof = f_eof;
	r->logprintf = logprintf;

	if (!must_match(r, "RIFF")) return 0;
	if (!must_read(r, &r->riff_len, 4)) return 0;
	if (!must_match(r, "AVI ")) return 0;

	size_t file_end = (size_t)r->riff_len + 8;
	char fourcc_buf[5] = { 0 };
	uint32_t chunk_size;
	fsize_t end_of_chunk;
	int got_all_we_need = 0;

	// https://learn.microsoft.com/en-us/windows/win32/directshow/avi-riff-file-reference
	while (!got_all_we_need)
	{
		fsize_t cur_chunk_pos;
		if (!must_read(r, fourcc_buf, 4)) return 0;
		if (!must_read(r, &chunk_size, 4)) return 0;
		if (!must_tell(r, &cur_chunk_pos)) return 0;
		end_of_chunk = cur_chunk_pos + chunk_size;
		switch (MATCH4CC(fourcc_buf))
		{
		default:
		case MAKE4CC('J', 'U', 'N', 'K'):
			logprintf(userdata, "[INFO] Skipping chunk \"%s\"\r\n", fourcc_buf);
			if (!rel_seek(r, chunk_size)) return 0;
			break;
		case MAKE4CC('L', 'I', 'S', 'T'):
			if (!must_read(fourcc_buf, 4, userdata)) return 0;
			switch (MATCH4CC(fourcc_buf))
			{
			case MAKE4CC('h', 'd', 'r', 'l'):
			}
		}
		if (!must_seek(r, end_of_chunk)) return 0;
		if (end_of_chunk == file_end) break;
	}

	return 1;
ErrRet:
	return 0;
}




