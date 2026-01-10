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

#ifndef AVI_MAX_STREAM_NAME
#define AVI_MAX_STREAM_NAME 64
#endif

typedef struct
{
	avi_stream_header stream_header;
	fsize_t stream_format_offset;
	fsize_t stream_format_len;
	fsize_t stream_additional_header_data_offset;
	fsize_t stream_additional_header_data_len;
	char stream_name[AVI_MAX_STREAM_NAME];
	int format_data_is_valid;
	union
	{
		bitmap_header_max_size bitmap_format;
		wave_format_ex audio_format;
	};
}avi_stream_info;

int avi_stream_is_video(avi_stream_info* si);
int avi_stream_is_audio(avi_stream_info* si);
int avi_stream_is_text(avi_stream_info* si);
int avi_stream_is_midi(avi_stream_info* si);

typedef fssize_t(*read_cb)(void *buffer, size_t len, void *userdata);
typedef fssize_t(*seek_cb)(fsize_t offset, void *userdata);
typedef fssize_t(*tell_cb)(void *userdata);
typedef void (*logprintf_cb)(void *userdata, const char *fmt, ...);

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
	/// The `avi_reader` struct pointer, we borrow its callback functions to call read()/seek()/tell()/printf()
	avi_reader *r;

	/// The stream index start from zero.
	int stream_id;

	/// The short path to `r->avi_stream_info[self->stream_id]`
	avi_stream_info *stream_info;

	/// Is this stream ended?
	int is_no_more_packets;

	/// The current packet FourCC value. Used to determine the type of the packet.
	uint32_t cur_4cc;

	/// The current packet index
	fsize_t cur_packet_index;

	/// The current stream packet index
	fsize_t cur_stream_packet_index;

	/// The current packet position in the file.
	fsize_t cur_packet_offset;

	/// The current packet length
	fsize_t cur_packet_len;

	/// When `r->log_level` is `PRINT_DEBUG`, normally the functions associated to the stream will print debug messages.
	/// Set this to 1 to mute the debug messages from this specific stream.
	int mute_cur_stream_debug_print;

	/// Your callback functions, when the packet is going to be processed, the callback functions will be called.
	void *userdata; /// The data to pass to your callback functions.
	read_cb f_read; /// Your `read()` callback function pointer.
	seek_cb f_seek; /// Your `seek()` callback function pointer.
	tell_cb f_tell; /// Your `tell()` callback function pointer.
	on_stream_data_cb on_video_compressed;	/// Compressed video frame got
	on_stream_data_cb on_video;				/// Uncompressed video frame (probably BMP) got
	on_stream_data_cb on_palette_change;	/// Palette change for your video (If the AVI file is using color index as pixel data, the actual color in RGB form comes from the palette)
	on_stream_data_cb on_audio;				/// Audio, it can be either compressed or uncompressed, depends on its format.
}avi_stream_reader;

/// <summary>
/// Initialize the `avi_reader`. The callback functions will be used to parse the AVI file header.
/// After parsed the AVI header, the struct `avi_reader` stores the information if the AVI file.
/// e.g. How many streams inside it, what is the format of each stream, does this AVI file indexed.
/// The next step to handle the AVI file is to call `avi_get_stream_reader()`, which creates the `avi_stream_reader` struct
///   for you to handle each stream of the AVI file.
/// </summary>
/// <param name="r">Your `avi_reader` to be initialized.</param>
/// <param name="userdata">Your data to pass to your callback functions.</param>
/// <param name="f_read">Your `read()` function for me to read the AVI file.</param>
/// <param name="f_seek">Your `seek()` function for me to change the absolute read position.</param>
/// <param name="f_tell">Your `tell()` function for me to retrieve the current read position.</param>
/// <param name="f_logprintf">Your `printf()` function for me to log. You can pass `NULL` and I will call `vprintf()` as the default behavior.</param>
/// <param name="log_level">The log level, see `avi_logprintf_level`</param>
/// <returns>0 for fail, nonzero for success.</returns>
int avi_reader_init
(
	avi_reader *r,
	void *userdata,
	read_cb f_read,
	seek_cb f_seek,
	tell_cb f_tell,
	logprintf_cb f_logprintf,
	avi_logprintf_level log_level
);

/// <summary>
/// Get the specified stream reader to read the packets of the specified stream.
/// </summary>
/// <param name="r">A pointer to the `avi_reader` struct you had it initialized before.</param>
/// <param name="stream_id">The stream index you want to </param>
/// <param name="on_video_compressed">Your function to receive a compressed video packet. Passing NULL is allowed.</param>
/// <param name="on_video">Your function to receive an uncompressed video packet. Passing NULL is allowed.</param>
/// <param name="on_palette_change">Your function to receive a palette change event packet. Passing NULL is allowed.</param>
/// <param name="on_audio">Your function to receive an audio packet. Passing NULL is allowed.</param>
/// <param name="s_out">Your `avi_stream_reader` to be initialized.</param>
/// <returns>0 for fail, nonzero for success.</returns>
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

/// <summary>
/// Set read()/seek()/tell() and userdata specificly for the stream reader.
/// This function allows you to use a different fd/file handle to read the stream.
/// Using different fd/file handle will increase the IO performance of the `avi_stream_reader`.
/// Passing NULL to the callback functions will not change the previous callback function.
/// </summary>
/// <param name="s">Your stream reader</param>
/// <param name="userdata">An object pass to your callback functions.</param>
/// <param name="f_read">Your `read()` function for me to read the AVI file. Passing NULL is allowed.</param>
/// <param name="f_seek">Your `seek()` function for me to change the absolute read position. Passing NULL is allowed.</param>
/// <param name="f_tell">Your `tell()` function for me to retrieve the current read position. Passing NULL is allowed.</param>
/// <returns></returns>
void avi_stream_reader_set_read_seek_tell
(
	avi_stream_reader *s,
	void *userdata,
	read_cb f_read,
	seek_cb f_seek,
	tell_cb f_tell
);

/// <summary>
/// Call the callback functions of an `avi_stream_reader` struct for the current packet.
/// After calling `avi_get_stream_reader()`, you have a freshly created stream reader that has the first packet of your stream.
/// But the callback functions were not called at that time.
/// After you have prepared to play the AVI file, the first thing is to call this function to process your first packet.
/// Then your callback functions are called to process the first packet.
/// </summary>
/// <param name="s">Your stream reader</param>
/// <returns>0 for fail, nonzero for success.</returns>
int avi_stream_reader_call_callback_functions(avi_stream_reader *s);

/// <summary>
/// Move to the next packet, then call the callback functions for you to receive the packet.
/// If you set `cur_packet_offset` to zero, then it will move to the first packet of the stream.
/// </summary>
/// <param name="s">Your stream reader</param>
/// <returns>0 for fail, nonzero for success.</returns>
int avi_stream_reader_move_to_next_packet(avi_stream_reader *s, int call_receive_functions);

/// <summary>
/// Check if the stream reader is no more packets to read.
/// </summary>
/// <param name="s">The stream reader</param>
/// <returns>1 for yes, 0 for no. If yes, then there's no more packets to read. -1 for bad parameters.</returns>
int avi_stream_reader_is_end_of_stream(avi_stream_reader *s);

#endif
