
#include <stdio.h>
#include "avi_reader.h"

#ifdef _MSC_VER
#define WINDOWS_DEMO 1
#endif

// Question about why to use Visual Studio 2022 to develop this library, at this point, is very clear.
// I use Windows, I can just create a window to show the AVI video directly.
// My demo can also help you find out how do you to implement an AVI player for your device.

#ifdef WINDOWS_DEMO
#include <Windows.h>
#include <olectl.h>
#include <ocidl.h>
int my_avi_player_create_window(void *my_avi_player, uint32_t target_width, uint32_t target_height);
void my_avi_player_destroy_window(void *my_avi_player);
void my_avi_player_show_video_frame(void *my_avi_player, fsize_t offset, fsize_t length);
void my_avi_player_play_audio_frame(void *my_avi_player, fsize_t offset, fsize_t length);
#ifdef _WIN64
#  define PRIsize_t PRIu64
#else
#  define PRIsize_t PRIu32
#endif
#endif

static uint64_t get_super_precise_time_in_ns();

typedef struct
{
    avi_reader r;
    avi_stream_reader s_video;
    avi_stream_reader s_audio;
    uint32_t video_width;
    uint32_t video_height;

    FILE *fp;

#if WINDOWS_DEMO
    HINSTANCE hInstance;
    LPCWSTR WindowClass;
    HWND Window;
    HDC hDC;
    int should_quit;
    int exit_code;
#endif
} my_avi_player;

static fssize_t my_avi_player_read(void *buffer, size_t len, void *userdata)
{
    my_avi_player *p = (my_avi_player *)userdata;
    return (fssize_t)fread(buffer, 1, len, p->fp);
}

static fssize_t my_avi_player_seek(fsize_t offset, void *userdata)
{
    my_avi_player *p = (my_avi_player *)userdata;
    return fseek(p->fp, offset, SEEK_SET) ? -1 : (fssize_t)offset;
}

static fssize_t my_avi_player_tell(void *userdata)
{
    my_avi_player *p = (my_avi_player *)userdata;
    return ftell(p->fp);
}

static void my_avi_player_poll_window_events(my_avi_player *p)
{
#if WINDOWS_DEMO
    MSG msg;
    while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE))
    {
        TranslateMessage(&msg);
        if (msg.message == WM_QUIT)
        {
            p->should_quit = 1;
            p->exit_code = (int)msg.wParam;
        }
        else
        {
            DispatchMessageW(&msg);
        }
    }
#endif
}

static void my_avi_player_on_video_cb(fsize_t offset, fsize_t length, void *userdata)
{
#if WINDOWS_DEMO
    my_avi_player_show_video_frame(userdata, offset, length);
#else
    printf("On video packet at %"PRIfsize_t" with length %"PRIfsize_t"\n", offset, length);
#endif
}

static void my_avi_player_on_audio_cb(fsize_t offset, fsize_t length, void *userdata)
{
#if WINDOWS_DEMO
    my_avi_player_play_audio_frame(userdata, offset, length);
#else
    printf("On audio packet at %"PRIfsize_t" with length %"PRIfsize_t"\n", offset, length);
#endif
}

static int my_avi_player_open(my_avi_player *p, const char *path)
{
    memset(p, 0, sizeof *p);
    p->fp = fopen(path, "rb");
    if (!p->fp) return 0;

    // Initialize the AVI reader
    if (!avi_reader_init
    (
        &p->r,
        p,
        my_avi_player_read,
        my_avi_player_seek,
        my_avi_player_tell,
        NULL,
        PRINT_DEBUG
    )) return 0;

    // I want to dedicate my stream reader 0 to the video stream,
    //   and my stream reader 1 to the audio stream.
    // Note: While AVI files can contain multiple streams (e.g. adult/child versions, multi-language audio),
    //   this demo intentionally only handles the first video and audio streams for simplicity.
    // You'd probably start with the same approach, right?
    for (size_t i = 0; i < p->r.num_streams; i++)
    {
        avi_stream_info* stream_info = &p->r.avi_stream_info[i];
        if (avi_stream_is_video(stream_info))
        {
            if (p->s_video.r == NULL)
            {
                printf("[INFO] Attaching our stream reader 0 to stream %" PRIsize_t " which is a video stream.\n", i);
                if (!avi_get_stream_reader(&p->r, (int)i, my_avi_player_on_video_cb, my_avi_player_on_video_cb, NULL, NULL, &p->s_video)) goto ErrRet;
            }
            else
            {
                printf("[INFO] The stream %" PRIsize_t " is also a video stream, since we already selected the first occured one, we don't use this one here.\n", i);
            }
        }
        else if (avi_stream_is_audio(stream_info))
        {
            if (p->s_audio.r == NULL)
            {
                printf("[INFO] Attaching our stream reader 1 to stream %" PRIsize_t " which is an audio stream.\n", i);
                if (!avi_get_stream_reader(&p->r, (int)i, NULL, NULL, NULL, my_avi_player_on_audio_cb, &p->s_audio)) goto ErrRet;
            }
            else
            {
                printf("[INFO] The stream %" PRIsize_t " is also an audio stream, since we already selected the first occured one, we don't use this one here.\n", i);
            }
        }
        else if (avi_stream_is_midi(stream_info))
        {
            printf("[INFO] The stream %" PRIsize_t " is a MIDI stream, we don't use it here.\n", i);
        }
        else if (avi_stream_is_text(stream_info))
        {
            printf("[INFO] The stream %" PRIsize_t " is a text stream, we don't use it here.\n", i);
        }
        else
        {
            char fourcc_buf[5] = { 0 };
            *(uint32_t*)fourcc_buf = stream_info->stream_header.fccType;
            printf("[WARN] The stream %" PRIsize_t " is neither video, audio, midi, nor text stream, it is \"%s\".\n", i, fourcc_buf);
        }
        // print the stream name. I'm just curious what would it be.
        if (strlen(stream_info->stream_name))
        {
            printf("[INFO] The name of the stream is \"%s\".\n", stream_info->stream_name);
        }
    }

    p->video_width = p->r.avih.dwWidth;
    p->video_height = p->r.avih.dwHeight;

#ifdef WINDOWS_DEMO
    if (!my_avi_player_create_window(p, p->video_width, p->video_height)) goto ErrRet;
#endif

    return 1;
ErrRet:
    return 0;
}

static int my_avi_player_play(my_avi_player *p)
{
    avi_stream_reader *s_video = &p->s_video;
    avi_stream_reader *s_audio = &p->s_audio;
    avi_stream_info *h_video = s_video->stream_info;
    avi_stream_info *h_audio = s_audio->stream_info;

    uint64_t start_time = get_super_precise_time_in_ns();
    while (1)
    {
        uint64_t cur_time = get_super_precise_time_in_ns();
        uint64_t delta_time = cur_time - start_time;

        uint64_t v_rate_ns = 0;
        uint64_t a_rate_ns = 0;

        if (h_video) v_rate_ns = (uint64_t)h_video->stream_header.dwRate * 1000000000 / h_video->stream_header.dwScale;
        if (h_audio) a_rate_ns = (uint64_t)h_audio->stream_header.dwRate * 1000000000 / h_audio->stream_header.dwScale;

        uint32_t target_v_frame_no = 0;
        uint32_t target_a_frame_no = 0;

        if (h_video) target_v_frame_no = (uint32_t)(delta_time / v_rate_ns);
        if (h_audio) target_a_frame_no = (uint32_t)(delta_time / a_rate_ns);

        int v_advanced = 0;
        int a_advanced = 0;
        int v_available = 0;
        int a_available = 0;
        if (h_video)
        {
            while (s_video->cur_stream_packet_index < target_v_frame_no)
            {
                v_advanced = 1;
                v_available = avi_stream_reader_move_to_next_packet(s_video, 0);
            }
        }
        if (h_audio)
        {
            while (s_audio->cur_stream_packet_index < target_a_frame_no)
            {
                a_advanced = 1;
                a_available = avi_stream_reader_move_to_next_packet(s_audio, 0);
            }
        }
        if (v_available && v_advanced) avi_stream_reader_call_callback_functions(s_video);
        if (a_available && a_advanced) avi_stream_reader_call_callback_functions(s_audio);

        if ((!v_available && v_advanced) && (!a_available && a_advanced)) break;

        my_avi_player_poll_window_events(p);
        if (p->should_quit) return p->exit_code;
    }

    return 0;
}

int main(int argc, char**argv)
{
    my_avi_player p;
    printf("Warning: this is just a test program.\n");
    if (!my_avi_player_open(&p, argv[1])) return 1;
    return my_avi_player_play(&p);
}

#ifdef WINDOWS_DEMO
LRESULT CALLBACK my_avi_player_window_proc_W(HWND hWnd, uint32_t msg, WPARAM wp, LPARAM lp)
{
    switch (msg)
    {
    case WM_CREATE:
        do
        {
            CREATESTRUCTW *cs = (CREATESTRUCTW *)lp;
            SetWindowLongPtrW(hWnd, 0, (LONG_PTR)cs->lpCreateParams);
        } while (0);
        break;
    case WM_DESTROY:
        PostQuitMessage(0);
        break;
    default:
        return DefWindowProcW(hWnd, msg, wp, lp);
    }
    return 0;
}

int my_avi_player_create_window(void *player, uint32_t target_width, uint32_t target_height)
{
    my_avi_player *p = (my_avi_player *)player;

    // https://devblogs.microsoft.com/oldnewthing/20041025-00/?p=37483
    extern HINSTANCE __ImageBase;
    p->hInstance = __ImageBase;

    WNDCLASSEXW WCExW = {
        sizeof(WNDCLASSEXW),
        0,
        my_avi_player_window_proc_W,
        0,
        sizeof (p),
        p->hInstance,
        LoadCursorW(NULL, IDC_ARROW),
        LoadIconW(NULL, IDI_APPLICATION),
        (HBRUSH)(COLOR_WINDOW + 1),
        NULL,
        L"AVI_Player_Class",
        LoadIconW(NULL, IDI_APPLICATION),
    };

    p->WindowClass = (LPCWSTR)RegisterClassExW(&WCExW);
    if (!p->WindowClass)
    {
        fprintf(stderr, "[FATAL] RegisterClassExW() failed.\n");
        goto ErrRet;
    }

    p->Window = CreateWindowExW(0, p->WindowClass, L"AVI(MJPEG + PCM only) player", WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, target_width, target_height, NULL, NULL, p->hInstance, player);
    if (!p->Window)
    {
        fprintf(stderr, "[FATAL] CreateWindowExW() failed.\n");
        goto ErrRet;
    }
    RECT rc_w, rc_c;
    GetWindowRect(p->Window, &rc_w);
    GetClientRect(p->Window, &rc_c);
    uint32_t border_width = (rc_w.right - rc_w.left) - (rc_c.right - rc_c.left);
    uint32_t border_height = (rc_w.bottom - rc_w.top) - (rc_c.bottom - rc_c.top);
    if (!MoveWindow(p->Window, rc_w.left, rc_w.top, target_width + border_width, target_height + border_height, FALSE))
    {
        // If the window can't be resized to the target size, it'll show a cropped AVI video.
        // I don't want this, I'd rather quit. Showing cropped video isn't an option.
        fprintf(stderr, "[FATAL] MoveWindow() failed.\n");
        goto ErrRet;
    }

    p->hDC = GetDC(p->Window);
    if (!p->hDC)
    {
        fprintf(stderr, "[FATAL] GetDC() failed.\n");
        goto ErrRet;
    }

    // Initialize COM. I want to use `OleLoadPicture()`
    if (FAILED(CoInitialize(NULL)))
    {
        fprintf(stderr, "[FATAL] CoInitialize() failed.\n");
        goto ErrRet;
    }

    return 1;
ErrRet:
    my_avi_player_destroy_window(player);
    return 0;
}

void my_avi_player_destroy_window(void* player)
{
    my_avi_player *p = (my_avi_player *)player;
    if (p->Window && p->hDC) ReleaseDC(p->Window, p->hDC);
    if (p->Window) DestroyWindow(p->Window);
    if (p->WindowClass) UnregisterClassW(p->WindowClass, p->hInstance);
    CoUninitialize();
    p->hDC = 0;
    p->Window = 0;
    p->WindowClass = 0;
}

void my_avi_player_show_video_frame(void *player, fsize_t offset, fsize_t length)
{
    my_avi_player *p = (my_avi_player *)player;

    // I'm using the very very old way to convert JPEG to BMP, because it supports C.
    // Older than WIC, and older than Gdiplus, older than C++ smart pointers.
    HGLOBAL my_jpeg_picture_memory = GlobalAlloc(GMEM_MOVEABLE, length);
    if (!my_jpeg_picture_memory) goto Exit;

    void* ptr = GlobalLock(my_jpeg_picture_memory);
    if (!ptr) goto Exit;

    my_avi_player_seek(offset, player);
    my_avi_player_read(ptr, length, player);
    GlobalUnlock(my_jpeg_picture_memory);

    // Here comes the COM part.
    HRESULT hr = S_OK;
    IStream *stream = NULL;
    hr = CreateStreamOnHGlobal(my_jpeg_picture_memory, TRUE, &stream);
    if (FAILED(hr)) goto Exit;

    IPicture *picture = NULL;
    hr = OleLoadPicture(stream, length, FALSE, &IID_IPicture, &picture);
    if (FAILED(hr)) goto Exit;

    HDC hPictureDC = NULL;
    hr = picture->lpVtbl->get_CurDC(picture, &hPictureDC);
    if (FAILED(hr)) goto Exit;

    BitBlt(p->hDC, 0, 0, p->video_width, p->video_height, hPictureDC, 0, 0, SRCCOPY);
Exit:
    if (stream) stream->lpVtbl->Release(stream);
    if (picture) picture->lpVtbl->Release(picture);
    if (my_jpeg_picture_memory) GlobalFree(my_jpeg_picture_memory);
}

void my_avi_player_play_audio_frame(void *player, fsize_t offset, fsize_t length)
{
    my_avi_player *p = (my_avi_player *)player;
    void *audio_data = malloc(length);
    if (!audio_data) return;

    memset(audio_data, 0, length);
    my_avi_player_seek(offset, player);
    my_avi_player_read(audio_data, length, player);



    free(audio_data);
}

static uint64_t get_super_precise_time_in_ns()
{
    uint64_t counter = 0, freq = 0;
    QueryPerformanceFrequency((LARGE_INTEGER *)&freq);
    QueryPerformanceCounter((LARGE_INTEGER *)&counter);
    return counter * 1000000000 / freq;
}

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")
#endif

#if __unix__
#include <time.h>
static uint64_t get_super_precise_time_in_ns()
{
    struct timespec tp;
    clock_gettime(CLOCK_MONOTONIC, &tp);
    return (uint64_t)tp.tv_sec * 1000000000 + tp.nsec;
}
#endif

