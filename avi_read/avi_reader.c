#include"avi_reader.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#define MATCH4CC(str) (*(const uint32_t*)(str))
#define MAKE4CC(c1, c2, c3, c4) ((uint32_t)(c1) | (uint32_t)((c2) << 8) | (uint32_t)((c3) << 16) | (uint32_t)((c4) << 24))

#define AVIF_HASINDEX		0x00000010
#define AVIF_MUSTUSEINDEX	0x00000020
#define AVIF_ISINTERLEAVED	0x00000100
#define AVIF_TRUSTCKTYPE	0x00000800
#define AVIF_WASCAPTUREFILE	0x00010000
#define AVIF_COPYRIGHTED	0x00020000

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

static void default_logprintf(void *userdata, const char* format, ...)
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
	void(*logprintf)(void* userdata, const char* fmt, ...)
)
{
	memset(r, 0, sizeof *r);
	if (!logprintf) logprintf = default_logprintf;

	r->userdata = userdata;
	r->f_read = f_read;
	r->f_seek = f_seek;
	r->f_tell = f_tell;
	r->logprintf = logprintf;

	if (!must_match(r, "RIFF")) goto ErrRet;
	if (!must_read(r, &r->riff_len, 4)) goto ErrRet;

	fsize_t avi_start;
	if (!must_tell(r, &avi_start)) goto ErrRet;
	if (!must_match(r, "AVI ")) goto ErrRet;

	size_t file_end = (size_t)avi_start + r->riff_len;
	char fourcc_buf[5] = { 0 };
	uint32_t chunk_size;
	fsize_t end_of_chunk;
	int got_all_we_need = 0;
	int has_index = 0;

	// https://learn.microsoft.com/en-us/windows/win32/directshow/avi-riff-file-reference
	while (!got_all_we_need)
	{
		fsize_t cur_chunk_pos;
		if (!must_read(r, fourcc_buf, 4)) goto ErrRet;
		if (!must_read(r, &chunk_size, 4)) goto ErrRet;
		if (!must_tell(r, &cur_chunk_pos)) goto ErrRet;
		end_of_chunk = cur_chunk_pos + chunk_size;
		switch (MATCH4CC(fourcc_buf))
		{
		default:
		case MAKE4CC('J', 'U', 'N', 'K'):
			logprintf(userdata, "[INFO] Skipping chunk \"%s\"\r\n", fourcc_buf);
			break;
		case MAKE4CC('L', 'I', 'S', 'T'):
			if (!must_read(r, fourcc_buf, 4)) goto ErrRet;
			switch (MATCH4CC(fourcc_buf))
			{
			case MAKE4CC('h', 'd', 'r', 'l'):
				logprintf(userdata, "[INFO] Reading toplevel LIST chunk \"hdrl\"\r\n");
				do
				{
					fsize_t end_of_hdrl = end_of_chunk;
					fsize_t h_end_of_chunk;
					int avih_read = 0;
					int strl_read = 0;

					do
					{
						char h_fourcc_buf[5] = { 0 };
						uint32_t h_chunk_size;
						fsize_t h_chunk_pos;
						if (!must_read(r, h_fourcc_buf, 4)) goto ErrRet;
						if (!must_read(r, &h_chunk_size, 4)) goto ErrRet;
						if (!must_tell(r, &h_chunk_pos)) goto ErrRet;
						h_end_of_chunk = h_chunk_pos + h_chunk_size;
						switch (MATCH4CC(h_fourcc_buf))
						{
						case MAKE4CC('a', 'v', 'i', 'h'):
							if (avih_read)
							{
								logprintf(userdata, "[ERROR] AVI file format corrupted: duplicated main AVI header \"avih\"\r\n");
								goto ErrRet;
							}
							logprintf(userdata, "[INFO] Reading the main AVI header \"avih\"\r\n");
							r->avih.cb = h_chunk_size;
							if (!must_read(r, &(&(r->avih.cb))[1], r->avih.cb)) goto ErrRet;
							has_index = (r->avih.dwFlags & AVIF_HASINDEX) == AVIF_HASINDEX;
							avih_read = 1;
							break;
						case MAKE4CC('L', 'I', 'S', 'T'):
							logprintf(userdata, "[INFO] Reading the stream list\r\n");
							if (!must_match(r, "strl")) goto ErrRet;

							do
							{
								if (r->num_streams < AVI_MAX_STREAMS)
								{
									uint32_t sub_chunk_len;
									size_t string_len = 256;
									avi_stream_data* stream_data = &r->avi_stream_data[r->num_streams++];
									if (!must_match(r, "strh")) goto ErrRet;
									if (!must_read(r, &sub_chunk_len, 4)) goto ErrRet;
									if (!must_read(r, &stream_data->stream_header, sub_chunk_len)) goto ErrRet;
									if (!must_match(r, "strf")) goto ErrRet;
									if (!must_read(r, &sub_chunk_len, 4)) goto ErrRet;
									if (!must_tell(r, &stream_data->stream_format_offset)) goto ErrRet;
									stream_data->stream_format_len = sub_chunk_len;
									if (!rel_seek(r, sub_chunk_len)) goto ErrRet;
									if (!must_match(r, "strd")) break;
									if (!must_read(r, &sub_chunk_len, 4)) goto ErrRet;
									if (!must_tell(r, &stream_data->stream_additional_header_data_offset)) goto ErrRet;
									stream_data->stream_additional_header_data_len = sub_chunk_len;
									if (!rel_seek(r, sub_chunk_len)) goto ErrRet;
									if (!must_match(r, "strn")) break;
									if (!must_read(r, &sub_chunk_len, 4)) goto ErrRet;
									if (string_len > sub_chunk_len) string_len = sub_chunk_len;
									if (!must_read(r, &stream_data->stream_name, string_len)) goto ErrRet;
									if (!rel_seek(r, sub_chunk_len)) goto ErrRet;
								}
								else
								{
									logprintf(userdata, "[ERROR] Too many streams in the AVI file, must supported streams is %d\r\n", AVI_MAX_STREAMS);
									goto ErrRet;
								}
							} while (0);

							strl_read++;
							break;
						default:
						case MAKE4CC('J', 'U', 'N', 'K'):
							logprintf(userdata, "[INFO] Skipping chunk \"%s\"\r\n", h_fourcc_buf);
							break;
						}
						if (!must_seek(r, h_end_of_chunk)) goto ErrRet;
					} while (h_end_of_chunk < end_of_hdrl);
					if (!must_seek(r, end_of_hdrl)) goto ErrRet;
					if (!avih_read)
					{
						logprintf(userdata, "[ERROR] Missing main AVI header \"avih\"\r\n");
						goto ErrRet;
					}
					if (!strl_read)
					{
						logprintf(userdata, "[ERROR] No stream found in the AVI file.\r\n");
						goto ErrRet;
					}
				} while (0);

				break;
			case MAKE4CC('m', 'o', 'v', 'i'):
				logprintf(userdata, "[INFO] Reading toplevel LIST chunk \"movi\"\r\n");
				if (!must_tell(r, &r->stream_data_offset)) goto ErrRet;

				// Check if the AVI file uses LIST->rec pattern to store the packets
				if (!must_read(r, fourcc_buf, 4)) goto ErrRet;
				if (!memcmp(fourcc_buf, "LIST", 4))
				{
					r->stream_data_is_lists = 1;
				}
				break;
			case MAKE4CC('i', 'd', 'x', '1'):
				logprintf(userdata, "[INFO] Reading toplevel chunk \"idx1\"\r\n");
				if (!must_tell(r, &r->idx_offset)) goto ErrRet;
				break;
			}
		}
		if (!must_seek(r, end_of_chunk)) goto ErrRet;
		got_all_we_need = r->num_streams && r->stream_data_offset && ((has_index && r->idx_offset) || !has_index);
		if (end_of_chunk == file_end) break;
	}

	return 1;
ErrRet:
	logprintf(userdata, "[FATAL] Reading AVI file failed.\r\n");
	return 0;
}




