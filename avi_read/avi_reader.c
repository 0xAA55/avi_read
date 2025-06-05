#include"avi_reader.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#define AVI_ROBUSTINESS 1
#if AVI_NO_ROBUSTINESS
#undef AVI_ROBUSTINESS
#endif

#define AVIF_HASINDEX		0x00000010
#define AVIF_MUSTUSEINDEX	0x00000020
#define AVIF_ISINTERLEAVED	0x00000100
#define AVIF_TRUSTCKTYPE	0x00000800
#define AVIF_WASCAPTUREFILE	0x00010000
#define AVIF_COPYRIGHTED	0x00020000

/* Flags for index */
#define AVIIF_LIST          0x00000001L // chunk is a 'LIST'
#define AVIIF_KEYFRAME      0x00000010L // this frame is a key frame.
#define AVIIF_FIRSTPART     0x00000020L // this frame is the start of a partial frame.
#define AVIIF_LASTPART      0x00000040L // this frame is the end of a partial frame.
#define AVIIF_MIDPART       (AVIIF_LASTPART|AVIIF_FIRSTPART)

#define AVIIF_NOTIME	    0x00000100L // this frame doesn't take any time
#define AVIIF_COMPUSE       0x0FFF0000L // these bits are for compressor use

#define MATCH4CC(str) (*(const uint32_t*)(str))
#define MATCH2CC(str) (*(const uint16_t*)(str))
#if   '\x01\x02\x03\x04' == 0x01020304
#  define MAKE2CC(c1, c2) ((c1) | ((c2) << 8))
#  define MAKE4CC(c1, c2, c3, c4) ((c1) | ((c2) << 8) | ((c3) << 16) | ((c4) << 24))
#elif '\x01\x02\x03\x04' == 0x04030201
#  define MAKE2CC(c2, c1) ((c1) | ((c2) << 8))
#  define MAKE4CC(c4, c3, c2, c1) ((c1) | ((c2) << 8) | ((c3) << 16) | ((c4) << 24))
#else
#  error What's wrong with your compiler?
#endif

#define FCC_JUNK MAKE4CC('J', 'U', 'N', 'K')
#define FCC_LIST MAKE4CC('L', 'I', 'S', 'T')
#define FCC_hdrl MAKE4CC('h', 'd', 'r', 'l')
#define FCC_avih MAKE4CC('a', 'v', 'i', 'h')
#define FCC_movi MAKE4CC('m', 'o', 'v', 'i')
#define FCC_idx1 MAKE4CC('i', 'd', 'x', '1')
#define TCC_db MAKE2CC('d', 'b')
#define TCC_dc MAKE2CC('d', 'c')
#define TCC_pc MAKE2CC('p', 'c')
#define TCC_wb MAKE2CC('w', 'b')

#define FATAL_PRINTF(r, fmt, ...)	if (r->log_level >= PRINT_FATAL) r->f_logprintf(r->userdata, "[FATAL] " ## fmt, __VA_ARGS__)
#define WARN_PRINTF(r, fmt, ...)	if (r->log_level >= PRINT_WARN) r->f_logprintf(r->userdata, "[WARN] " ## fmt, __VA_ARGS__)
#define INFO_PRINTF(r, fmt, ...)	if (r->log_level >= PRINT_INFO) r->f_logprintf(r->userdata, "[INFO] " ## fmt, __VA_ARGS__)
#define DEBUG_PRINTF(r, fmt, ...)	if (r->log_level >= PRINT_DEBUG) r->f_logprintf(r->userdata, "[DEBUG] " ## fmt, __VA_ARGS__)

#ifdef _MSC_VER
#  define NL "\n"
#else
#  define NL "\r\n"
#endif

static int must_match(avi_reader* r, const char* fourcc)
{
	char buf[5] = { 0 };
	if (r->f_read(buf, 4, r->userdata) != 4) return 0;
	if (memcmp(buf, fourcc, 4))
	{
		FATAL_PRINTF(r, "Matching FourCC failed: %s != %s" NL, buf, fourcc);
		return 0;
	}
	return 1;
}

static int must_read(avi_reader* r, void* buffer, size_t len)
{
	fssize_t rl = r->f_read(buffer, len, r->userdata);
	if (rl < 0)
	{
		FATAL_PRINTF(r, "Read %u bytes failed." NL, (unsigned int)len);
		return 0;
	}
	else if (rl != len)
	{
		FATAL_PRINTF(r, "Tried to read %u bytes, got %u bytes." NL, (unsigned int)len, (unsigned int)rl);
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
		FATAL_PRINTF(r, "`f_tell()` failed." NL);
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
		FATAL_PRINTF(r, "`f_seek(%x)` failed." NL, target);
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
		FATAL_PRINTF(r, "`f_tell()` failed." NL);
		return 0;
	}
	fsize_t target = (fsize_t)(cur_pos + offset);
	cur_pos = r->f_seek(target, r->userdata);
	if (cur_pos < 0)
	{
		FATAL_PRINTF(r, "`f_seek(%x)` failed." NL, target);
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
	(void)userdata;
}

static avi_reader create_only_for_printf(logprintf_cb f_logprintf, avi_logprintf_level log_level, void *userdata)
{
	avi_reader fake_r;
	memset(&fake_r, 0, sizeof fake_r);
	fake_r.f_logprintf = f_logprintf;
	fake_r.userdata = userdata;
	fake_r.log_level = log_level;
	return fake_r;
}

int avi_reader_init
(
	avi_reader* r,
	void* userdata,
	read_cb f_read,
	seek_cb f_seek,
	tell_cb f_tell,
	logprintf_cb f_logprintf,
	avi_logprintf_level log_level
)
{
	if (!f_logprintf) f_logprintf = default_logprintf;
#if AVI_ROBUSTINESS
	if (!r)
	{
		avi_reader fake_r = create_only_for_printf(f_logprintf, log_level, userdata);
		r = &fake_r;
		FATAL_PRINTF(r, "Invalid parameter : `avi_reader* r` must not be NULL." NL);
		r = NULL;
		goto ErrRet;
	}
#endif

	memset(r, 0, sizeof * r);
	r->userdata = userdata;
	r->f_read = f_read;
	r->f_seek = f_seek;
	r->f_tell = f_tell;
	r->f_logprintf = f_logprintf;
	r->log_level = log_level;

#if AVI_ROBUSTINESS
	if (!f_read)
	{
		FATAL_PRINTF(r, "Invalid parameter: must provide your `read_cb` implementation." NL);
		goto ErrRet;
	}
	if (!f_seek)
	{
		FATAL_PRINTF(r, "Invalid parameter : must provide your `seek_cb` implementation." NL);
		goto ErrRet;
	}
	if (!f_tell)
	{
		FATAL_PRINTF(r, "Invalid parameter: must provide your `tell_cb` implementation." NL);
		goto ErrRet;
	}
#endif
	uint32_t riff_len;
	if (!must_match(r, "RIFF")) goto ErrRet;
	if (!must_read(r, &riff_len, 4)) goto ErrRet;

	fsize_t avi_start;
	if (!must_tell(r, &avi_start)) goto ErrRet;
	if (!must_match(r, "AVI ")) goto ErrRet;

	r->end_of_file = (size_t)avi_start + riff_len;
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
		case FCC_JUNK:
			INFO_PRINTF(r, "Skipping chunk \"%s\"" NL, fourcc_buf);
			break;
		case FCC_LIST:
			if (!must_read(r, fourcc_buf, 4)) goto ErrRet;
			switch (MATCH4CC(fourcc_buf))
			{
			case FCC_hdrl:
				INFO_PRINTF(r, "Reading toplevel LIST chunk \"hdrl\"" NL);
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
						case FCC_avih:
							if (avih_read)
							{
								FATAL_PRINTF(r, "AVI file format corrupted: duplicated main AVI header \"avih\"" NL);
								goto ErrRet;
							}
							INFO_PRINTF(r, "Reading the main AVI header \"avih\"" NL);
							r->avih.cb = h_chunk_size;
							if (!must_read(r, &(&(r->avih.cb))[1], r->avih.cb)) goto ErrRet;
							has_index = (r->avih.dwFlags & AVIF_HASINDEX) == AVIF_HASINDEX;
							avih_read = 1;
							break;
						case FCC_LIST:
							INFO_PRINTF(r, "Reading the stream list" NL);
							if (!must_match(r, "strl")) goto ErrRet;

							do
							{
								if (r->num_streams < AVI_MAX_STREAMS)
								{
									uint32_t sub_chunk_len;
									uint32_t stream_id = r->num_streams;
									avi_stream_info *stream_data = &r->avi_stream_info[r->num_streams++];
									memset(stream_data, 0, sizeof *stream_data);
									size_t string_len = (sizeof stream_data->stream_name) - 1;
									if (!must_match(r, "strh")) goto ErrRet;
									if (!must_read(r, &sub_chunk_len, 4)) goto ErrRet;
									if (!must_read(r, &stream_data->stream_header, sub_chunk_len)) goto ErrRet;
									if (!must_match(r, "strf")) goto ErrRet;
									if (!must_read(r, &sub_chunk_len, 4)) goto ErrRet;
									if (!must_tell(r, &stream_data->stream_format_offset)) goto ErrRet;
									stream_data->stream_format_len = sub_chunk_len;
									if (!rel_seek(r, sub_chunk_len)) goto ErrRet;
									if (must_match(r, "strd"))
									{
										if (!must_read(r, &sub_chunk_len, 4)) goto ErrRet;
										if (!must_tell(r, &stream_data->stream_additional_header_data_offset)) goto ErrRet;
										stream_data->stream_additional_header_data_len = sub_chunk_len;
										if (!rel_seek(r, sub_chunk_len)) goto ErrRet;
									}
									if (must_match(r, "strn"))
									{
										if (!must_read(r, &sub_chunk_len, 4)) goto ErrRet;
										if (string_len > sub_chunk_len) string_len = sub_chunk_len;
										if (!must_read(r, &stream_data->stream_name, string_len)) goto ErrRet;
										if (!rel_seek(r, sub_chunk_len)) goto ErrRet;
									}
									else
									{
										string_len = 0;
									}

									char fourcc_type[5] = { 0 };
									char fourcc_handler[5] = { 0 };
									*(uint32_t*)fourcc_type = stream_data->stream_header.fccType;
									*(uint32_t*)fourcc_handler = stream_data->stream_header.fccHandler;
									if (!string_len)
										INFO_PRINTF(r, "Stream %u: Type: \"%s\", Handler: \"%s\"" NL, stream_id, fourcc_type, fourcc_handler);
									else
										INFO_PRINTF(r, "Stream %u: Type: \"%s\", Handler: \"%s\", Name: %s" NL, stream_id, fourcc_type, fourcc_handler, stream_data->stream_name);
								}
								else
								{
									FATAL_PRINTF(r, "Too many streams in the AVI file, max supported streams is %d" NL, AVI_MAX_STREAMS);
									goto ErrRet;
								}
							} while (0);

							strl_read++;
							break;
						default:
						case FCC_JUNK:
							INFO_PRINTF(r, "Skipping chunk \"%s\"" NL, h_fourcc_buf);
							break;
						}
						if (!must_seek(r, h_end_of_chunk)) goto ErrRet;
					} while (h_end_of_chunk < end_of_hdrl);
					if (!must_seek(r, end_of_hdrl)) goto ErrRet;
					if (!avih_read)
					{
						FATAL_PRINTF(r, "Missing main AVI header \"avih\"" NL);
						goto ErrRet;
					}
					if (!strl_read)
					{
						FATAL_PRINTF(r, "No stream found in the AVI file." NL);
						goto ErrRet;
					}
				} while (0);

				break;
			case FCC_movi:
				INFO_PRINTF(r, "Reading toplevel LIST chunk \"movi\"" NL);
				if (!must_tell(r, &r->stream_data_offset)) goto ErrRet;

				// Check if the AVI file uses LIST(rec) pattern to store the packets
				if (!must_read(r, fourcc_buf, 4)) goto ErrRet;
				if (!memcmp(fourcc_buf, "LIST", 4))
				{
					if (!must_read(r, fourcc_buf, 4)) goto ErrRet;
					if (!memcmp(fourcc_buf, "rec ", 4))
					{
						INFO_PRINTF(r, "This AVI file uses `LIST(rec)` structure to store packets." NL);
					}
					else
					{
						FATAL_PRINTF(r, "Inside LIST(movi): expected LIST(rec), got LIST(%s)." NL, fourcc_buf);
						goto ErrRet;
					}
				}
				break;
			case FCC_idx1:
				INFO_PRINTF(r, "Reading toplevel chunk \"idx1\"" NL);
				if (!must_tell(r, &r->idx_offset)) goto ErrRet;
				r->num_indices = chunk_size / sizeof(avi_index_entry);
				break;
			}
		}
		if (!must_seek(r, end_of_chunk)) goto ErrRet;
		got_all_we_need = r->num_streams && r->stream_data_offset && ((has_index && r->idx_offset) || !has_index);
		if (end_of_chunk == r->end_of_file) break;
	}

	if (!r->idx_offset)
	{
		WARN_PRINTF(r, "No AVI index: per-stream seeking requires per-packet file traversal." NL);
	}

	return 1;
ErrRet:
	if (r) FATAL_PRINTF(r, "Reading AVI file failed." NL);
	return 0;
}

static void default_on_stream_data_cb(fsize_t offset, fsize_t length, void *userdata)
{
	(void)offset;
	(void)length;
	(void)userdata;
}

int avi_get_stream_reader
(
	avi_reader *r,
	int stream_id,
	on_stream_data_cb on_video_compressed,
	on_stream_data_cb on_video,
	on_stream_data_cb on_palette_change,
	on_stream_data_cb on_audio,
	avi_stream_reader *s_out
)
{
	void* userdata = NULL;
	logprintf_cb logprintf = default_logprintf;
#if AVI_ROBUSTINESS
	if (!r)
	{
		avi_reader fake_r = create_only_for_printf(logprintf, PRINT_FATAL, userdata);
		r = &fake_r;
		FATAL_PRINTF(r, "Param `avi_reader* r` must not be NULL. You get your stream from what?" NL);
		r = NULL;
		goto ErrRet;
	}
	userdata = r->userdata;
	logprintf = r->f_logprintf;
	if (stream_id >= (int)r->num_streams)
	{
		FATAL_PRINTF(r, "Bad stream id `%d` (Max: `%u`)" NL, stream_id, r->num_streams);
		goto ErrRet;
	}
	if (!s_out)
	{
		FATAL_PRINTF(r, "Param `avi_stream_reader* s_out` must not be NULL. You asked for a stream but passed a NULL pointer, what's wrong with you?" NL);
		goto ErrRet;
	}
#endif

	if (!on_video_compressed) on_video_compressed = default_on_stream_data_cb;
	if (!on_video) on_video = default_on_stream_data_cb;
	if (!on_palette_change) on_palette_change = default_on_stream_data_cb;
	if (!on_audio) on_audio = default_on_stream_data_cb;

	memset(s_out, 0, sizeof  *s_out);
	s_out->r = r;
	s_out->stream_id = stream_id;
	s_out->stream_info = &r->avi_stream_info[stream_id];
	s_out->cur_stream_packet_index = 0;
	s_out->on_video_compressed = on_video_compressed;
	s_out->on_video = on_video;
	s_out->on_palette_change = on_palette_change;
	s_out->on_audio = on_audio;
	if (r->idx_offset && r->num_indices)
	{
		INFO_PRINTF(r, "Seeking the first packet of the stream %d using the indices from the AVI file." NL, stream_id);
		if (!must_seek(r, r->idx_offset)) goto ErrRet;
		avi_index_entry index;
		for (fsize_t i = 0; i < r->num_indices; i++)
		{
			int stream_no;
			char fourcc_buf[5] = { 0 };
			if (!must_read(r, &index, sizeof index)) goto ErrRet;
			*(uint32_t*)fourcc_buf = index.dwChunkId;
			if (sscanf(fourcc_buf, "%d", &stream_no) != 1) continue;
			if (stream_no == stream_id)
			{
				INFO_PRINTF(r, "Successfully found the first packet of the stream %d: Offset = 0x%"PRIx32", Length = 0x%"PRIx32"." NL, stream_id, index.dwOffset, index.dwSize);
				s_out->cur_4cc = index.dwChunkId;
				s_out->cur_packet_offset = index.dwOffset;
				s_out->cur_packet_len = index.dwSize;
				break;
			}
		}
		if (!s_out->cur_packet_offset || !s_out->cur_packet_len)
		{
			FATAL_PRINTF(r, "Could not find the first packet for the stream id %d." NL, stream_id);
			goto ErrRet;
		}
	}
	else
	{
		INFO_PRINTF(r, "Seeking the first packet of the stream %d via file traversal." NL, stream_id);
		if (!must_seek(r, r->stream_data_offset)) goto ErrRet;

		char fourcc_buf[5] = { 0 };
		uint32_t chunk_size;
		fsize_t chunk_start = 0;
		fsize_t chunk_end = 0;
		do
		{
			if (!must_read(r, fourcc_buf, 4)) goto ErrRet;
			if (!must_read(r, &chunk_size, 4)) goto ErrRet;
			if (!must_tell(r, &chunk_start)) goto ErrRet;
			chunk_end = chunk_start + chunk_size;

			if (!memcmp(fourcc_buf, "LIST", 4))
			{
				if (!must_match(r, "rec ")) goto ErrRet;
				DEBUG_PRINTF(r, "Seeking into a LIST(rec) chunk." NL);
				s_out->cur_rec_list_offset = chunk_start + 4;
				s_out->cur_rec_list_len = chunk_size - 4;
				// Move inside the LIST chunk to find the packet.
				continue; // Not to skip the chunk.
			}
			else
			{
				int stream_no;
				if (sscanf(fourcc_buf, "%d", &stream_no) != 1)
				{
					WARN_PRINTF(r, "Encountering unknown FourCC \"%s\" while seeking for a packet, skipping." NL, fourcc_buf);
				}
				else if (stream_no == stream_id)
				{
					INFO_PRINTF(r, "Successfully found the first packet of the stream %d: Offset = 0x%"PRIxfsize_t", Length = 0x%"PRIx32"." NL, stream_id, chunk_start, chunk_size);
					s_out->cur_4cc = *(uint32_t*)fourcc_buf;
					s_out->cur_packet_offset = chunk_start;
					s_out->cur_packet_len = chunk_size;
					break;
				}
			}
			if (!must_seek(r, chunk_end)) goto ErrRet;
		} while (chunk_end < r->end_of_file);
		if (!s_out->cur_packet_offset || !s_out->cur_packet_len)
		{
			FATAL_PRINTF(r, "No first packet found for stream id %d after full file traversal." NL, stream_id);
			goto ErrRet;
		}
	}

	return 1;
ErrRet:
	if (r) FATAL_PRINTF(r, "Reading AVI file failed." NL);
	return 0;
}
