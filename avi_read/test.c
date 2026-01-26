
#include <stdio.h>
#include <assert.h>
#include "avi_reader.h"

#ifdef _MSC_VER
#define WINDOWS_DEMO 1
#undef BI_RGB
#undef BI_RLE8
#undef BI_RLE4
#undef BI_BITFIELDS
#undef BI_JPEG
#undef BI_PNG
#endif

// Question about why to use Visual Studio 2022 to develop this library, at this point, is very clear.
// I use Windows, I can just create a window to show the AVI video directly.
// My demo can also help you find out how do you to implement an AVI player for your device.

#ifdef WINDOWS_DEMO
#include "windows_demo_guts.h"
#endif

uint64_t get_super_precise_time_in_ms();

typedef struct my_avi_player_s
{
    avi_reader r;
    avi_stream_reader s_video;
    avi_stream_reader s_audio;
    uint32_t video_width;
    uint32_t video_height;

    FILE *fp;
    FILE *fp_video;
    FILE *fp_audio;

#if WINDOWS_DEMO
    int windows_guts_initialized;
    WindowsDemoGuts windows_guts;
#endif
    int should_quit;
    int exit_code;
} my_avi_player;

#if WINDOWS_DEMO
int my_avi_player_init_windows_guts(my_avi_player *p)
{
    int ret = windows_demo_create_window(&p->windows_guts, p->video_width, p->video_height, &p->s_video, &p->s_audio);
    return ret;
}
void my_avi_player_cleanup_windows_guts(my_avi_player *p)
{
    windows_demo_destroy_window(&p->windows_guts);
}
#endif

void my_avi_player_show_video_frame(void *userdata, fsize_t offset, fsize_t length)
{
#if WINDOWS_DEMO
    my_avi_player *p = userdata;
    windows_demo_show_video_frame(&p->windows_guts, offset, length);
#else
    printf("On play video frame at: %"PRIfsize_t", %"PRIfsize_t"\n", offset, length);
#endif
}

void my_avi_player_play_audio_packet(void *userdata, fsize_t offset, fsize_t length)
{
#if WINDOWS_DEMO
    my_avi_player *p = userdata;
    windows_demo_play_audio_packet(&p->windows_guts, offset, length, p->s_audio.is_no_more_packets);
#else
    printf("On play audio frame at: %"PRIfsize_t", %"PRIfsize_t"\n", offset, length);
#endif
}

static fssize_t my_avi_player_read(void *buffer, size_t len, void *userdata)
{
    my_avi_player *p = userdata;
    return (fssize_t)fread(buffer, 1, len, p->fp);
}

static fssize_t my_avi_player_seek(fsize_t offset, void *userdata)
{
    my_avi_player *p = userdata;
    fpos_t pos = offset;
    return fsetpos(p->fp, &pos) ? -1 : (fssize_t)offset;
}

static fssize_t my_avi_player_tell(void *userdata)
{
    my_avi_player *p = userdata;
    fpos_t pos = -1;
    fgetpos(p->fp, &pos);
    return (fssize_t)pos;
}

static fssize_t my_avi_video_read(void *buffer, size_t len, void *userdata)
{
    my_avi_player *p = userdata;
    return (fssize_t)fread(buffer, 1, len, p->fp_video);
}

static fssize_t my_avi_video_seek(fsize_t offset, void *userdata)
{
    my_avi_player *p = userdata;
    fpos_t pos = offset;
    return fsetpos(p->fp, &pos) ? -1 : (fssize_t)offset;
}

static fssize_t my_avi_video_tell(void *userdata)
{
    my_avi_player *p = userdata;
    fpos_t pos = -1;
    fgetpos(p->fp, &pos);
    return (fssize_t)pos;
}

static fssize_t my_avi_audio_read(void *buffer, size_t len, void *userdata)
{
    my_avi_player *p = userdata;
    return (fssize_t)fread(buffer, 1, len, p->fp_audio);
}

static fssize_t my_avi_audio_seek(fsize_t offset, void *userdata)
{
    my_avi_player *p = userdata;
    fpos_t pos = offset;
    return fsetpos(p->fp, &pos) ? -1 : (fssize_t)offset;
}

static fssize_t my_avi_audio_tell(void *userdata)
{
    my_avi_player *p = userdata;
    fpos_t pos = -1;
    fgetpos(p->fp, &pos);
    return (fssize_t)pos;
}

static void my_avi_player_on_video_cb(fsize_t offset, fsize_t length, void *userdata)
{
    my_avi_player_show_video_frame(userdata, offset, length);
}

static void my_avi_player_on_audio_cb(fsize_t offset, fsize_t length, void *userdata)
{
    my_avi_player_play_audio_packet(userdata, offset, length);
}

static int my_avi_player_open(my_avi_player *p, const char *path)
{
    memset(p, 0, sizeof *p);
    p->fp = fopen(path, "rb");
    if (!p->fp) goto ErrRet;
    p->fp_video = fopen(path, "rb");
    if (!p->fp_video) goto ErrRet;
    p->fp_audio = fopen(path, "rb");
    if (!p->fp_audio) goto ErrRet;

    // Initialize the AVI reader
    if (!avi_reader_init
    (
        &p->r,
        p,
        my_avi_player_read,
        my_avi_player_seek,
        my_avi_player_tell,
        NULL,
        PRINT_INFO
    )) goto ErrRet;

    avi_stream_reader_set_read_seek_tell(&p->s_video, p, my_avi_video_read, my_avi_video_seek, my_avi_video_tell);
    avi_stream_reader_set_read_seek_tell(&p->s_audio, p, my_avi_audio_read, my_avi_audio_seek, my_avi_audio_tell);

    if (!avi_map_stream_readers(&p->r, p, p, my_avi_player_on_video_cb, my_avi_player_on_video_cb, NULL, my_avi_player_on_audio_cb, &p->s_video, &p->s_audio)) goto ErrRet;

    p->video_width = p->r.avih.dwWidth;
    p->video_height = p->r.avih.dwHeight;

#ifdef WINDOWS_DEMO
    if (!my_avi_player_init_windows_guts(p)) goto ErrRet;
#endif

    return 1;
ErrRet:
    return 0;
}

static void my_avi_player_close(my_avi_player *p)
{
    if (p->fp) fclose(p->fp);
    if (p->fp_video) fclose(p->fp_video);
    if (p->fp_audio) fclose(p->fp_audio);

#if WINDOWS_DEMO
    my_avi_player_cleanup_windows_guts(p);
#endif

    memset(p, 0, sizeof * p);
}

static int my_avi_player_play(my_avi_player *p)
{
    avi_stream_reader *s_video = &p->s_video;
    avi_stream_reader *s_audio = &p->s_audio;
    avi_stream_info *h_video = s_video->stream_info;
    avi_stream_info *h_audio = s_audio->stream_info;
    wave_format_ex *af = &h_audio->audio_format;
    int have_video = (h_video != 0);
    int have_audio = (h_audio != 0);
    volatile uint64_t audio_byte_pos = 0;

#ifdef WINDOWS_DEMO
    if (have_audio)
    {
        while (!p->windows_guts.audio_buffer_is_playing)
        {
            if (!avi_stream_reader_move_to_next_packet(s_audio, 1)) return 0;
            audio_byte_pos += s_audio->cur_packet_len;
        }
    }
#endif

    uint64_t start_time = get_super_precise_time_in_ms();
#ifdef WINDOWS_DEMO
    uint64_t left_key_press_time = start_time;
    uint64_t right_key_press_time = start_time;
    int left_key_down = 0;
    int right_key_down = 0;
    uint64_t go_back;
    uint64_t go_forward;
#endif
    while (have_video || have_audio)
    {
        uint64_t cur_time = get_super_precise_time_in_ms();
        uint64_t relative_time_ms = (cur_time - start_time);

#if WINDOWS_DEMO
        if (GetAsyncKeyState(VK_LEFT))
        {
            if (!left_key_down)
            {
                left_key_press_time = cur_time;
                left_key_down = 1;
            }
            go_back = (cur_time - left_key_press_time) * 4;
            if (go_back > relative_time_ms) go_back = relative_time_ms;
            relative_time_ms -= go_back;
            printf("Go back: %llu      \r", go_back);
        }
        else
        {
            if (left_key_down)
            {
                go_back = (cur_time - left_key_press_time) * 4;
                start_time += go_back;
                if (start_time > cur_time) start_time = cur_time;
                left_key_down = 0;
                relative_time_ms -= go_back;
            }
        }
        if (GetAsyncKeyState(VK_RIGHT))
        {
            if (!right_key_down)
            {
                right_key_press_time = cur_time;
                right_key_down = 1;
            }
            go_forward = (cur_time - right_key_press_time) * 4;
            if (go_forward > relative_time_ms) go_forward = relative_time_ms;
            relative_time_ms += go_forward;
            printf("Go forward: %llu      \r", go_forward);
        }
        else
        {
            if (right_key_down)
            {
                go_forward = (cur_time - right_key_press_time) * 4;
                start_time -= go_forward;
                right_key_down = 0;
                relative_time_ms += go_forward;
            }
        }
#endif

        if (have_video)
        {
            fsize_t target_v_frame_no = avi_video_get_frame_number_by_time(s_video, relative_time_ms);

            avi_video_seek_to_frame_index(s_video, target_v_frame_no, 1);
        }

        if (have_audio)
        {
            int new_packet_got = 0;
            int num_playing = 0;
            int num_idle = 0;
            fsize_t target_a_byte_pos = avi_audio_get_target_byte_offset_by_time(s_audio, relative_time_ms);

#if WINDOWS_DEMO
            // Make sure all buffers are used for playing
            if (!s_audio->is_no_more_packets)
            {
                windows_demo_audio_buffers_get_status(&p->windows_guts, &num_idle, &num_playing);
                if (num_playing < AUDIO_PLAY_BUFFERS)
                {
                    if (audio_byte_pos >= target_a_byte_pos)
                    {
                        avi_stream_reader_move_to_next_packet(s_audio, 1);
                    }
                    else
                    {
                        avi_stream_reader_move_to_next_packet(s_audio, 0);
                    }
                    audio_byte_pos += s_audio->cur_packet_len;
                }
            }
#else
            printf("TODO: Process your audio stream.\n");
#endif
        }

        if ((have_video && s_video->is_no_more_packets) &&
            (have_audio && s_audio->is_no_more_packets) &&
#if WINDOWS_DEMO
            windows_demo_is_audio_buffers_all_idle(&p->windows_guts) &&
#endif
            1)
        {
            // Finished playback, return gracefully
            p->should_quit = 1;
            p->exit_code = 0;
        }

#if WINDOWS_DEMO
        windows_demo_poll_window_events(&p->windows_guts);
        if (p->windows_guts.should_quit)
        {
            p->should_quit = 1;
            p->exit_code = p->windows_guts.exit_code;
        }
#endif
        if (p->should_quit) return p->exit_code;
    }

    return 0;
}

int main(int argc, char**argv)
{
    my_avi_player p;
    fprintf(stderr, "[WARN]: this is just a test program.\n");
    if (argc < 2)
    {
        fprintf(stderr, "[ERROR]: Usage: *s <path_to_avi_file>\n");
        return 1;
    }
    if (!my_avi_player_open(&p, argv[1])) return 1;
    int exit_code = my_avi_player_play(&p);
    my_avi_player_close(&p);
    return exit_code;
}

#if __unix__
#include <time.h>
static uint64_t get_super_precise_time_in_ms()
{
    struct timespec tp;
    clock_gettime(CLOCK_MONOTONIC, &tp);
    return (uint64_t)tp.tv_sec * 1000 + tp.nsec / 1000000;
}
#endif

