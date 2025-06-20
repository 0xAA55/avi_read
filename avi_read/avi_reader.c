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
#define MAKE2CC(c1, c2) ((c1) | ((c2) << 8))
#define MAKE4CC(c1, c2, c3, c4) ((c1) | ((c2) << 8) | ((c3) << 16) | ((c4) << 24))
#define MAKE2CC_(c2, c1) ((c1) | ((c2) << 8))
#define MAKE4CC_(c4, c3, c2, c1) ((c1) | ((c2) << 8) | ((c3) << 16) | ((c4) << 24))

#define FCC_JUNK MAKE4CC('J', 'U', 'N', 'K')
#define FCC_LIST MAKE4CC('L', 'I', 'S', 'T')
#define FCC_hdrl MAKE4CC('h', 'd', 'r', 'l')
#define FCC_avih MAKE4CC('a', 'v', 'i', 'h')
#define FCC_movi MAKE4CC('m', 'o', 'v', 'i')
#define FCC_idx1 MAKE4CC('i', 'd', 'x', '1')
#define FCC_strh MAKE4CC('s', 't', 'r', 'h')
#define FCC_strf MAKE4CC('s', 't', 'r', 'f')
#define FCC_strd MAKE4CC('s', 't', 'r', 'd')
#define FCC_strn MAKE4CC('s', 't', 'r', 'n')
#define FCC_JUNK_ MAKE4CC_('J', 'U', 'N', 'K')
#define FCC_LIST_ MAKE4CC_('L', 'I', 'S', 'T')
#define FCC_hdrl_ MAKE4CC_('h', 'd', 'r', 'l')
#define FCC_avih_ MAKE4CC_('a', 'v', 'i', 'h')
#define FCC_movi_ MAKE4CC_('m', 'o', 'v', 'i')
#define FCC_idx1_ MAKE4CC_('i', 'd', 'x', '1')
#define FCC_strh_ MAKE4CC_('s', 't', 'r', 'h')
#define FCC_strf_ MAKE4CC_('s', 't', 'r', 'f')
#define FCC_strd_ MAKE4CC_('s', 't', 'r', 'd')
#define FCC_strn_ MAKE4CC_('s', 't', 'r', 'n')
#define TCC_db MAKE2CC('d', 'b')
#define TCC_dc MAKE2CC('d', 'c')
#define TCC_pc MAKE2CC('p', 'c')
#define TCC_wb MAKE2CC('w', 'b')
#define TCC_db_ MAKE2CC_('d', 'b')
#define TCC_dc_ MAKE2CC_('d', 'c')
#define TCC_pc_ MAKE2CC_('p', 'c')
#define TCC_wb_ MAKE2CC_('w', 'b')

#define FATAL_PRINTF(r, fmt, ...)	{if (r->log_level >= PRINT_FATAL) r->f_logprintf(r->userdata, "[FATAL] " ## fmt, __VA_ARGS__);}
#define WARN_PRINTF(r, fmt, ...)	{if (r->log_level >= PRINT_WARN) r->f_logprintf(r->userdata, "[WARN] " ## fmt, __VA_ARGS__);}
#define INFO_PRINTF(r, fmt, ...)	{if (r->log_level >= PRINT_INFO) r->f_logprintf(r->userdata, "[INFO] " ## fmt, __VA_ARGS__);}
#define DEBUG_PRINTF(r, fmt, ...)	{if (r->log_level >= PRINT_DEBUG) r->f_logprintf(r->userdata, "[DEBUG] " ## fmt, __VA_ARGS__);}

#ifdef _MSC_VER
#  define NL "\n"
#else
#  define NL "\r\n"
#endif

int avi_stream_is_video(avi_stream_info *si)
{
	return !memcmp(&si->stream_header.fccType, "vids", 4);
}

int avi_stream_is_audio(avi_stream_info *si)
{
	return !memcmp(&si->stream_header.fccType, "auds", 4);
}

int avi_stream_is_text(avi_stream_info *si)
{
	return !memcmp(&si->stream_header.fccType, "txts", 4);
}

int avi_stream_is_midi(avi_stream_info *si)
{
	return !memcmp(&si->stream_header.fccType, "mids", 4);
}

static int must_match(avi_reader *r, const char *fourcc)
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

static int must_read(avi_reader *r, void *buffer, size_t len)
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

static int must_tell(avi_reader *r, fsize_t *cur_pos)
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

static int must_seek(avi_reader *r, fsize_t target)
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

static int rel_seek(avi_reader *r, fssize_t offset)
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

static int must_match_s(avi_stream_reader *r, const char *fourcc)
{
	char buf[5] = { 0 };
	if (r->f_read(buf, 4, r->userdata) != 4) return 0;
	if (memcmp(buf, fourcc, 4))
	{
		FATAL_PRINTF(r->r, "Matching FourCC failed: %s != %s" NL, buf, fourcc);
		return 0;
	}
	return 1;
}

static int must_read_s(avi_stream_reader *r, void *buffer, size_t len)
{
	fssize_t rl = r->f_read(buffer, len, r->userdata);
	if (rl < 0)
	{
		FATAL_PRINTF(r->r, "Read %u bytes failed." NL, (unsigned int)len);
		return 0;
	}
	else if (rl != len)
	{
		FATAL_PRINTF(r->r, "Tried to read %u bytes, got %u bytes." NL, (unsigned int)len, (unsigned int)rl);
		return 0;
	}
	else
	{
		return 1;
	}
}

static int must_tell_s(avi_stream_reader *r, fsize_t *cur_pos)
{
	fssize_t told = r->f_tell(r->userdata);
	if (told < 0)
	{
		FATAL_PRINTF(r->r, "`f_tell()` failed." NL);
		return 0;
	}
	else
	{
		*cur_pos = (fsize_t)told;
		return 1;
	}
}

static int must_seek_s(avi_stream_reader *r, fsize_t target)
{
	fssize_t told = r->f_seek(target, r->userdata);
	if (told < 0)
	{
		FATAL_PRINTF(r->r, "`f_seek(%x)` failed." NL, target);
		return 0;
	}
	else
	{
		return 1;
	}
}

static int rel_seek_s(avi_stream_reader *r, fssize_t offset)
{
	fssize_t cur_pos = r->f_tell(r->userdata);
	if (cur_pos < 0)
	{
		FATAL_PRINTF(r->r, "`f_tell()` failed." NL);
		return 0;
	}
	fsize_t target = (fsize_t)(cur_pos + offset);
	cur_pos = r->f_seek(target, r->userdata);
	if (cur_pos < 0)
	{
		FATAL_PRINTF(r->r, "`f_seek(%x)` failed." NL, target);
		return 0;
	}
	return 1;
}

static void default_logprintf(void *userdata, const char *format, ...)
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
	avi_reader *r,
	void *userdata,
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
		FATAL_PRINTF(r, "Invalid parameter: `avi_reader *r` must not be NULL." NL);
		r = NULL;
		goto ErrRet;
	}
#endif

	memset(r, 0, sizeof  *r);
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
		FATAL_PRINTF(r, "Invalid parameter: must provide your `seek_cb` implementation." NL);
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
		case FCC_JUNK_:
			INFO_PRINTF(r, "Skipping chunk \"%s\"" NL, fourcc_buf);
			break;
		case FCC_LIST:
		case FCC_LIST_:
			if (!must_read(r, fourcc_buf, 4)) goto ErrRet;
			switch (MATCH4CC(fourcc_buf))
			{
			case FCC_hdrl:
			case FCC_hdrl_:
				INFO_PRINTF(r, "Reading toplevel LIST chunk \"hdrl\"" NL);
				do
				{
					fsize_t end_of_hdrl = end_of_chunk;
					int avih_read = 0;
					int strl_read = 0;

					char hdrl_fourcc_buf[5] = { 0 };
					uint32_t hdrl_chunk_size;
					fsize_t hdrl_chunk_pos;
					fsize_t hdrl_end_of_chunk;
					do
					{
						if (!must_read(r, hdrl_fourcc_buf, 4)) goto ErrRet;
						if (!must_read(r, &hdrl_chunk_size, 4)) goto ErrRet;
						if (!must_tell(r, &hdrl_chunk_pos)) goto ErrRet;
						hdrl_end_of_chunk = hdrl_chunk_pos + hdrl_chunk_size;
						switch (MATCH4CC(hdrl_fourcc_buf))
						{
						case FCC_avih:
						case FCC_avih_:
							if (avih_read)
							{
								FATAL_PRINTF(r, "AVI file format corrupted: duplicated main AVI header \"avih\"" NL);
								goto ErrRet;
							}
							INFO_PRINTF(r, "Reading the main AVI header \"avih\"" NL);
							r->avih.cb = hdrl_chunk_size;
							if (!must_read(r, &(&(r->avih.cb))[1], r->avih.cb)) goto ErrRet;
							if (r->avih.dwStreams > AVI_MAX_STREAMS)
							{
								FATAL_PRINTF(r, "The AVI file contains too many streams (%u) exceeded the limit %d" NL, r->avih.dwStreams, AVI_MAX_STREAMS);
								goto ErrRet;
							}
							has_index = (r->avih.dwFlags & AVIF_HASINDEX) == AVIF_HASINDEX;
							avih_read = 1;
							break;
						case FCC_LIST:
						case FCC_LIST_:
							INFO_PRINTF(r, "Reading the stream list" NL);
							if (!must_match(r, "strl")) goto ErrRet;

							fsize_t string_len = 0;
							uint32_t stream_id = r->num_streams;
							if (stream_id >= AVI_MAX_STREAMS)
							{
								FATAL_PRINTF(r, "Too many streams in the AVI file, max supported streams is %d" NL, AVI_MAX_STREAMS);
								goto ErrRet;
							}
							avi_stream_info *stream_data = &r->avi_stream_info[r->num_streams++];
							const fsize_t max_string_len = AVI_MAX_STREAM_NAME - 1;

							char strl_fourcc[5] = { 0 };
							uint32_t strl_chunk_size = 0;
							fsize_t strl_chunk_pos;
							fsize_t strl_end_of_chunk;
							do
							{
								if (!must_read(r, strl_fourcc, 4)) goto ErrRet;
								if (!must_read(r, &strl_chunk_size, 4)) goto ErrRet;
								if (!must_tell(r, &strl_chunk_pos)) goto ErrRet;
								strl_end_of_chunk = strl_chunk_pos + strl_chunk_size;
								switch (MATCH4CC(strl_fourcc))
								{
								default:
								case FCC_JUNK:
								case FCC_JUNK_:
									INFO_PRINTF(r, "Skipping chunk \"%s\"" NL, strl_fourcc);
									break;
								case FCC_strh:
								case FCC_strh_:
									INFO_PRINTF(r, "Reading the stream header for stream id %u" NL, stream_id);
									if (!must_read(r, &stream_data->stream_header, strl_chunk_size)) goto ErrRet;
									string_len = (sizeof stream_data->stream_name) - 1;
									break;
								case FCC_strf:
								case FCC_strf_:
									INFO_PRINTF(r, "Reading the stream format for stream id %u" NL, stream_id);
									if (!must_tell(r, &stream_data->stream_format_offset)) goto ErrRet;
									stream_data->stream_format_len = strl_chunk_size;
									break;
								case FCC_strd:
								case FCC_strd_:
									INFO_PRINTF(r, "Reading the stream additional header data for stream id %u" NL, stream_id);
									if (!must_tell(r, &stream_data->stream_additional_header_data_offset)) goto ErrRet;
									stream_data->stream_additional_header_data_len = strl_chunk_size;
									break;
								case FCC_strn:
								case FCC_strn_:
									INFO_PRINTF(r, "Reading the stream name for stream id %u" NL, stream_id);
									string_len = strl_chunk_size;
									if (string_len > max_string_len) string_len = max_string_len;
									if (!must_read(r, &stream_data->stream_name, string_len)) goto ErrRet;
									break;
								}
								if (!must_seek(r, strl_end_of_chunk)) goto ErrRet;
							} while (strl_end_of_chunk < hdrl_end_of_chunk);

							if (avi_stream_is_audio(stream_data))
							{
								size_t max_read = sizeof stream_data->audio_format;
								size_t min_read = max_read - 2;
								if (stream_data->stream_format_len >= min_read)
								{
									if (!must_seek(r, stream_data->stream_format_offset)) goto ErrRet;
									if (!must_read(r, &stream_data->audio_format, max_read)) goto ErrRet;
									switch (stream_data->audio_format.wFormatTag)
									{
									case 2:
										break;
									default:
										stream_data->audio_format.cbSize = 0;
										break;
									}
								}
							}

							char fourcc_type[5] = { 0 };
							char fourcc_handler[5] = { 0 };
							*(uint32_t *)fourcc_type = stream_data->stream_header.fccType;
							*(uint32_t *)fourcc_handler = stream_data->stream_header.fccHandler;
							if (!string_len)
							{
								INFO_PRINTF(r, "Stream %u: Type: \"%s\", Handler: \"%s\"" NL, stream_id, fourcc_type, fourcc_handler);
							}
							else
							{
								INFO_PRINTF(r, "Stream %u: Type: \"%s\", Handler: \"%s\", Name: %s" NL, stream_id, fourcc_type, fourcc_handler, stream_data->stream_name);
							}

							strl_read++;
							break;
						default:
						case FCC_JUNK:
						case FCC_JUNK_:
							INFO_PRINTF(r, "Skipping chunk \"%s\"" NL, hdrl_fourcc_buf);
							break;
						}
						if (!must_seek(r, hdrl_end_of_chunk)) goto ErrRet;
					} while (hdrl_end_of_chunk < end_of_hdrl);
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
			case FCC_movi_:
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
			}
			break;
		case FCC_idx1:
		case FCC_idx1_:
			INFO_PRINTF(r, "Reading toplevel chunk \"idx1\"" NL);
			if (!must_tell(r, &r->idx_offset)) goto ErrRet;
			r->num_indices = chunk_size / sizeof(avi_index_entry);
			break;
		}
		// Skip the current chunk
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
#if AVI_ROBUSTINESS
	if (!r)
	{
		avi_reader fake_r = create_only_for_printf(default_logprintf, PRINT_FATAL, NULL);
		r = &fake_r;
		FATAL_PRINTF(r, "Param `avi_reader *r` must not be NULL. You get your stream from what?" NL);
		r = NULL;
		goto ErrRet;
	}
	if (stream_id >= (int)r->num_streams)
	{
		FATAL_PRINTF(r, "Bad stream id `%d` (Max: `%u`)" NL, stream_id, r->num_streams);
		goto ErrRet;
	}
	if (!s_out)
	{
		FATAL_PRINTF(r, "Param `avi_stream_reader *s_out` must not be NULL. You asked for a stream but passed a NULL pointer, what's wrong with you?" NL);
		goto ErrRet;
	}
#endif

	if (!on_video_compressed) on_video_compressed = default_on_stream_data_cb;
	if (!on_video) on_video = default_on_stream_data_cb;
	if (!on_palette_change) on_palette_change = default_on_stream_data_cb;
	if (!on_audio) on_audio = default_on_stream_data_cb;

	memset(s_out, 0, sizeof *s_out);
	s_out->r = r;
	s_out->stream_id = stream_id;
	s_out->stream_info = &r->avi_stream_info[stream_id];
	s_out->cur_stream_packet_index = 0;
	s_out->userdata = r->userdata;
	s_out->f_read = r->f_read;
	s_out->f_seek = r->f_seek;
	s_out->f_tell = r->f_tell;
	s_out->on_video_compressed = on_video_compressed;
	s_out->on_video = on_video;
	s_out->on_palette_change = on_palette_change;
	s_out->on_audio = on_audio;

	return 1;
ErrRet:
	if (r) FATAL_PRINTF(r, "Reading AVI file failed." NL);
	if (s_out) memset(s_out, 0, sizeof  *s_out);
	return 0;
}

int avi_stream_reader_set_read_seek_tell
(
	avi_stream_reader *s,
	void *userdata,
	read_cb f_read,
	seek_cb f_seek,
	tell_cb f_tell
)
{
	avi_reader *r;
#if AVI_ROBUSTINESS
	if (!s)
	{
		avi_reader fake_r = create_only_for_printf(default_logprintf, PRINT_FATAL, NULL);
		r = &fake_r;
		FATAL_PRINTF(r, "Param `avi_stream_reader *s` must not be NULL. Otherwise you are setting the callback functions for the void?" NL);
		r = NULL;
		goto ErrRet;
	}
#endif
	r = s->r;

	s->userdata = userdata;
	if (f_read) s->f_read = f_read;
	if (f_seek) s->f_seek = f_seek;
	if (f_tell) s->f_tell = f_tell;
	return 1;
ErrRet:
	if (r) FATAL_PRINTF(r, "`avi_stream_reader_set_read_seek_tell()` failed." NL);
	return 0;
}

int avi_stream_reader_call_callback_functions(avi_stream_reader *s)
{
	avi_reader *r = NULL;
#if AVI_ROBUSTINESS
	if (!s)
	{
		avi_reader fake_r = create_only_for_printf(default_logprintf, PRINT_FATAL, NULL);
		r = &fake_r;
		FATAL_PRINTF(r, "Invalid parameter: `avi_stream_reader *s` must not be NULL, because all of the callback functions needed are in the structure." NL);
		r = NULL;
		goto ErrRet;
	}
#endif
	r = s->r;
	char fourcc_buf[5] = { 0 };
	*(uint32_t *)fourcc_buf = s->cur_4cc;
	switch (MATCH2CC(&fourcc_buf[2])) // Avoid endianess handling
	{
	case TCC_db:
	case TCC_db_:
		s->on_video(s->cur_packet_offset, s->cur_packet_len, s->r->userdata);
		break;
	case TCC_dc:
	case TCC_dc_:
		s->on_video_compressed(s->cur_packet_offset, s->cur_packet_len, s->r->userdata);
		break;
	case TCC_pc:
	case TCC_pc_:
		s->on_palette_change(s->cur_packet_offset, s->cur_packet_len, s->r->userdata);
		break;
	case TCC_wb:
	case TCC_wb_:
		s->on_audio(s->cur_packet_offset, s->cur_packet_len, s->r->userdata);
		break;
	default:
		FATAL_PRINTF(r, "Unknown stream type: \"%s\"." NL, fourcc_buf);
		goto ErrRet;
	}
	return 1;
ErrRet:
	if (r) FATAL_PRINTF(r, "`avi_stream_call_callback_functions()` failed." NL);
	return 0;
}

int avi_stream_reader_move_to_next_packet(avi_stream_reader *s, int call_receive_functions)
{
	avi_reader *r = NULL;
#if AVI_ROBUSTINESS
	if (!s)
	{
		avi_reader fake_r = create_only_for_printf(default_logprintf, PRINT_FATAL, NULL);
		r = &fake_r;
		FATAL_PRINTF(r, "Param `avi_stream_reader *r` must not be NULL. You are reading packets from the void." NL);
		r = NULL;
		goto ErrRet;
	}
#endif
	r = s->r;
	fsize_t packet_no = s->cur_stream_packet_index + 1;
	fsize_t packet_no_avi = s->cur_packet_index + 1;
	int stream_id = s->stream_id;

	// The kickstart of the packet seeking
	if (!s->cur_packet_offset)
	{
		packet_no = 0;
		packet_no_avi = 0;
		s->cur_packet_offset = r->stream_data_offset;
		s->cur_packet_len = 0;
	}

	int packet_found = 0;
	if (r->idx_offset && r->num_indices)
	{
		if (!s->mute_cur_stream_debug_print && packet_no == 0)
		{
			DEBUG_PRINTF(r, "Seeking packet %"PRIfsize_t" of the stream %d using the indices from the AVI file." NL, packet_no, stream_id);
		}
		fsize_t start_of_movi = r->stream_data_offset - 4;
		avi_index_entry index;
		for (fsize_t i = packet_no_avi; i < r->num_indices; i++)
		{
			int stream_no;
			char fourcc_buf[5] = { 0 };
			if (!must_seek_s(s, r->idx_offset + i * (sizeof index))) goto ErrRet;
			if (!must_read_s(s, &index, sizeof index)) goto ErrRet;
			*(uint32_t *)fourcc_buf = index.dwChunkId;
			if (sscanf(fourcc_buf, "%d", &stream_no) != 1) continue;
			if (stream_no == stream_id)
			{
				fsize_t offset = index.dwOffset + start_of_movi + 8;
				if (!s->mute_cur_stream_debug_print)
				{
					DEBUG_PRINTF(r, "Successfully found packet %"PRIfsize_t"(%"PRIfsize_t") of the stream %d: Offset = 0x%"PRIxfsize_t", Length = 0x%"PRIx32"." NL, packet_no, packet_no_avi, stream_id, offset, index.dwSize);
				}
				s->cur_4cc = index.dwChunkId;
				s->cur_packet_index = i;
				s->cur_packet_offset = offset;
				s->cur_packet_len = index.dwSize;
				s->cur_stream_packet_index = packet_no;
				s->is_no_more_packets = 0;
				packet_found = 1;
				if (call_receive_functions)
				{
					if (!avi_stream_reader_call_callback_functions(s)) goto ErrRet;
				}
				break;
			}
		}
		if (!packet_found)
		{
			s->is_no_more_packets = 1;
			WARN_PRINTF(r, "Could not find packet %"PRIfsize_t" for the stream id %d." NL, packet_no, stream_id);
		}
	}
	else
	{
		if (!s->mute_cur_stream_debug_print && packet_no == 0)
		{
			DEBUG_PRINTF(r, "Seeking packet %"PRIfsize_t" of the stream %d via file traversal." NL, packet_no, stream_id);
		}
		if (!must_seek(r, s->cur_packet_offset + s->cur_packet_len)) goto ErrRet;

		char fourcc_buf[5] = { 0 };
		uint32_t chunk_size;
		fsize_t chunk_start = 0;
		fsize_t chunk_end = 0;
		fsize_t packet_index = 0;
		do
		{
			if (!must_read_s(s, fourcc_buf, 4)) goto ErrRet;
			if (!must_read_s(s, &chunk_size, 4)) goto ErrRet;
			if (!must_tell_s(s, &chunk_start)) goto ErrRet;
			chunk_end = chunk_start + chunk_size;

			if (!memcmp(fourcc_buf, "LIST", 4))
			{
				if (!must_match_s(s, "rec ")) goto ErrRet;
				if (!s->mute_cur_stream_debug_print)
				{
					DEBUG_PRINTF(r, "Seeking into a LIST(rec) chunk." NL);
				}
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
					if (!s->mute_cur_stream_debug_print)
					{
						DEBUG_PRINTF(r, "Successfully found packet %"PRIfsize_t"(%"PRIfsize_t") of the stream %d: Offset = 0x%"PRIxfsize_t", Length = 0x%"PRIx32"." NL, packet_no, packet_no_avi, stream_id, chunk_start, packet_index);
					}
					s->cur_4cc = *(uint32_t *)fourcc_buf;
					s->cur_packet_index = packet_index;
					s->cur_packet_offset = chunk_start;
					s->cur_packet_len = chunk_size;
					s->cur_stream_packet_index = packet_no;
					s->is_no_more_packets = 0;
					packet_found = 1;
					if (call_receive_functions)
					{
						if (!avi_stream_reader_call_callback_functions(s)) goto ErrRet;
					}
					break;
				}
				else if (!(
					memcmp(&fourcc_buf[2], "db", 2) &
					memcmp(&fourcc_buf[2], "dc", 2) &
					memcmp(&fourcc_buf[2], "pc", 2) &
					memcmp(&fourcc_buf[2], "wb", 2)))
				{
					packet_index++;
				}
			}
			// Skip the current chunk
			if (!must_seek_s(s, chunk_end)) goto ErrRet;
		} while (chunk_end < r->end_of_file);
		if (!packet_found)
		{
			s->is_no_more_packets = 1;
			WARN_PRINTF(r, "No packet found for stream id %d after full file traversal." NL, stream_id);
		}
	}

	return 1;
ErrRet:
	if (r) FATAL_PRINTF(r, "`avi_stream_reader_move_to_next_packet()` failed." NL);
	return 0;
}

int avi_stream_reader_is_end_of_stream(avi_stream_reader *s)
{
	avi_reader *r = NULL;
#if AVI_ROBUSTINESS
	if (!s)
	{
		avi_reader fake_r = create_only_for_printf(default_logprintf, PRINT_FATAL, NULL);
		r = &fake_r;
		FATAL_PRINTF(r, "Param `avi_stream_reader *r` must not be NULL. You don't want to check if the void is end of stream." NL);
		r = NULL;
		return 1; // Yes, the void don't have any packets for you to read.
	}
#endif
	r = s->r;
	return s->is_no_more_packets;
}

