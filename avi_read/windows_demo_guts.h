#ifndef _WINDOWS_DEMO_GUTS_H_
#define _WINDOWS_DEMO_GUTS_H_ 1

#include "avi_reader.h"

#define VIDEO_PLAY_BUFFERS 2
#define AUDIO_PLAY_BUFFERS 4
#define AUDIO_BUFFERS_PER_SEC 20

#include <Windows.h>
#include <olectl.h>
#include <ocidl.h>

#ifdef _WIN64
#  define PRIsize_t PRIu64
#else
#  define PRIsize_t PRIu32
#endif

typedef struct
{
    void *jpeg_data;
    size_t jpeg_data_len;
}VideoPlayBuffer;

typedef struct
{
    WAVEHDR whdr;
    void *buffer;
    size_t buffer_size;
    size_t data_size;
}AudioPlayBuffer;

typedef struct
{
    avi_reader *r;
    avi_stream_reader *s_video;
    avi_stream_reader *s_audio;
    HINSTANCE hInstance;
    LPCWSTR WindowClass;
    HWND Window;
    HDC hDC;
    uint32_t video_width;
    uint32_t video_height;
    HWAVEOUT AudioDev;
    VideoPlayBuffer v_play_buf;
    AudioPlayBuffer a_play_buf[AUDIO_PLAY_BUFFERS];
    size_t min_playable_size;
    int audio_buffer_is_playing;
    int should_quit;
    int exit_code;
} WindowsDemoGuts;

int apb_is_playing(AudioPlayBuffer *apb)
{
    return (apb->whdr.dwFlags & WHDR_INQUEUE) == WHDR_INQUEUE;
}

int apb_is_playable(AudioPlayBuffer *apb, WindowsDemoGuts *w)
{
    if (apb_is_playing(apb)) return 0;
    else if (apb->data_size >= w->min_playable_size) return 1;
    else return 0;
}

int apb_test_and_set_to_idle(AudioPlayBuffer *apb, WindowsDemoGuts *w)
{
    if (apb_is_playing(apb)) return 0;
    if ((apb->whdr.dwFlags & WHDR_PREPARED) == WHDR_PREPARED)
    {
        if ((apb->whdr.dwFlags & WHDR_DONE) != WHDR_DONE) return 0;
        MMRESULT mmr = waveOutUnprepareHeader(w->AudioDev, &apb->whdr, sizeof apb->whdr);
        switch (mmr)
        {
        case WAVERR_STILLPLAYING:
            return 0;
        case MMSYSERR_NOERROR:
            return 1;
        default:
            fprintf(stderr, "[ERROR] `waveOutUnprepareHeader()` returns %u.\n", mmr);
            return 0;
        }
    }
    return 1;
}

void vpb_decode_jpeg(VideoPlayBuffer *vpb, WindowsDemoGuts *w)
{
    // I'm using the very very old way to convert JPEG to BMP, because it supports C.
    // Older than WIC, and older than Gdiplus, older than C++ smart pointers.
    HGLOBAL my_jpeg_picture_memory = GlobalAlloc(GMEM_MOVEABLE, vpb->jpeg_data_len);
    if (!my_jpeg_picture_memory) goto Exit;

    void *ptr = GlobalLock(my_jpeg_picture_memory);
    if (!ptr) goto Exit;

    memcpy(ptr, vpb->jpeg_data, vpb->jpeg_data_len);

    GlobalUnlock(my_jpeg_picture_memory);

    // Here comes the COM part.
    HRESULT hr = S_OK;
    IStream *stream = NULL;
    hr = CreateStreamOnHGlobal(my_jpeg_picture_memory, TRUE, &stream);
    if (FAILED(hr)) goto Exit;

    IPicture *picture = NULL;
    hr = OleLoadPicture(stream, (LONG)vpb->jpeg_data_len, FALSE, &IID_IPicture, &picture);
    if (FAILED(hr)) goto Exit;

    int32_t src_w = 0, src_h = 0;
    hr = picture->lpVtbl->get_Width(picture, &src_w);
    if (FAILED(hr)) goto Exit;

    hr = picture->lpVtbl->get_Height(picture, &src_h);
    if (FAILED(hr)) goto Exit;

    RECT rc = { 0, 0, w->video_width, w->video_height };
    hr = picture->lpVtbl->Render(picture, w->hDC, 0, w->video_height - 1, (int32_t)w->video_width, -(int32_t)w->video_height, 0, 0, src_w, src_h, &rc);
    if (FAILED(hr)) goto Exit;

Exit:
    if (stream) stream->lpVtbl->Release(stream);
    if (picture) picture->lpVtbl->Release(picture);
    if (my_jpeg_picture_memory) GlobalFree(my_jpeg_picture_memory);
}

size_t windows_demo_get_video_data(WindowsDemoGuts *w, void *buffer, fsize_t offset, fsize_t length)
{
    avi_stream_reader *r = w->s_video;
    r->f_seek(offset, r->userdata);
    return r->f_read(buffer, length, r->userdata);
}

size_t windows_demo_get_audio_data(WindowsDemoGuts *w, void *buffer, fsize_t offset, fsize_t length)
{
    avi_stream_reader *r = w->s_audio;
    r->f_seek(offset, r->userdata);
    return r->f_read(buffer, length, r->userdata);
}

void windows_demo_show_video_frame(WindowsDemoGuts *w, fsize_t offset, fsize_t length)
{
    VideoPlayBuffer *vpb = &w->v_play_buf;

    if (vpb->jpeg_data_len < length)
    {
        free(vpb->jpeg_data);
        vpb->jpeg_data_len = 0;
        vpb->jpeg_data = malloc(length);
        if (!vpb->jpeg_data) goto Exit;
        vpb->jpeg_data_len = length;
    }

    windows_demo_get_video_data(w, vpb->jpeg_data, offset, length);
    vpb_decode_jpeg(vpb, w);
    return;
Exit:
    fprintf(stderr, "[ERROR] Memory allocation error.\n");
}

AudioPlayBuffer *windows_demo_choose_idle_audio_buffer(WindowsDemoGuts *w)
{
    for (int i = 0; i < AUDIO_PLAY_BUFFERS; i++)
    {
        AudioPlayBuffer *ret = &w->a_play_buf[i];
        if (apb_test_and_set_to_idle(ret, w))
        {
            return ret;
        }
    }
    return NULL;
}

void windows_demo_play_audio_packet(WindowsDemoGuts *w, fsize_t offset, fsize_t length, int is_last_audio_packet)
{
    avi_stream_info *audio_stream_info = w->s_audio->stream_info;

    if (is_last_audio_packet) return;

    // If the audio device was not opened yet
    if (!w->AudioDev)
    {
        // Extract the full audio format data with extension data to feed the `waveOutOpen()`
        size_t format_len = audio_stream_info->stream_format_len;
        void *format_ptr = malloc(format_len);
        if (!format_ptr) goto Exit;
        windows_demo_get_audio_data(w, format_ptr, audio_stream_info->stream_format_offset, (fsize_t)format_len);
        MMRESULT mr = waveOutOpen(&w->AudioDev, WAVE_MAPPER, format_ptr, 0, 0, CALLBACK_NULL | WAVE_ALLOWSYNC);
        switch (mr)
        {
        case MMSYSERR_NOERROR:
            free(format_ptr);
            break;
        case WAVERR_BADFORMAT:
            fprintf(stderr, "[FATAL] waveOutOpen(): WAVERR_BADFORMAT.\n");
        default:
            free(format_ptr);
            goto Exit;
        }
        printf("[INFO] Audio device opended\n");
    }
    AudioPlayBuffer *playbuf = &w->a_play_buf[0];

    while (1)
    {
        // Randomly pick an audio play buffer that's not playing currently.
        playbuf = windows_demo_choose_idle_audio_buffer(w);
        if (playbuf)
            break;
        else // Saturated
            Sleep(1);
    }
    // Let's set a value for minimum playable size
    if (!w->min_playable_size)
    {
        w->min_playable_size = (size_t)audio_stream_info->audio_format.nSamplesPerSec *
            audio_stream_info->audio_format.nBlockAlign /
            AUDIO_BUFFERS_PER_SEC; // Every second play this amount of buffers.
    }
    size_t min_playable_size = w->min_playable_size;

    if (playbuf->data_size + length > playbuf->buffer_size)
    {
        // Don't want to split the data to the next buffer, just grow the current buffer is fine.
        size_t new_size = playbuf->data_size + length;
        if (new_size < min_playable_size) new_size = min_playable_size;
        void *new_buffer = realloc(playbuf->buffer, new_size);
        if (!new_buffer)
        {
            free(playbuf->buffer);
            playbuf->buffer_size = 0;
            playbuf->data_size = 0;
            goto Exit;
        }
        playbuf->buffer = new_buffer;
        playbuf->buffer_size = new_size;
    }

    void *audio_data_write_to = (uint8_t *)playbuf->buffer + playbuf->data_size;

    // Write the extracted audio data to the buffer
    windows_demo_get_audio_data(w, audio_data_write_to, offset, length);
    playbuf->data_size += length;

    if (playbuf->data_size >= min_playable_size || is_last_audio_packet)
    {
        MMRESULT mr;
        // Link the WAVEHDR to the buffer
        WAVEHDR *pwhdr = &playbuf->whdr;
        pwhdr->lpData = playbuf->buffer;
        pwhdr->dwBufferLength = (DWORD)playbuf->data_size;

        // Prepare and play
        mr = waveOutPrepareHeader(w->AudioDev, pwhdr, sizeof * pwhdr);
        assert(mr == MMSYSERR_NOERROR);
        mr = waveOutWrite(w->AudioDev, pwhdr, sizeof * pwhdr);
        assert(mr == MMSYSERR_NOERROR);

        // Set to playing state
        w->audio_buffer_is_playing = 1;

        // Reset the buffer write ptr
        playbuf->data_size = 0;
    }

    return;
Exit:
    fprintf(stderr, "[ERROR] Could not play the audio buffer %d.\n", (int)(playbuf - w->a_play_buf));
}

int windows_demo_is_audio_buffers_all_idle(WindowsDemoGuts *w)
{
    for (int i = 0; i < AUDIO_PLAY_BUFFERS; i++)
    {
        AudioPlayBuffer *ret = &w->a_play_buf[i];
        if (!apb_test_and_set_to_idle(ret, w))
        {
            return 0;
        }
    }
    return 1;
}

void windows_demo_audio_buffers_get_status(WindowsDemoGuts *w, int *num_idle, int *num_playing)
{
    for (int i = 0; i < AUDIO_PLAY_BUFFERS; i++)
    {
        AudioPlayBuffer *ret = &w->a_play_buf[i];
        if (!apb_test_and_set_to_idle(ret, w))
            (*num_playing)++;
        else
            (*num_idle)++;
    }
}

void windows_demo_poll_window_events(WindowsDemoGuts *w)
{
    MSG msg;
    while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE))
    {
        TranslateMessage(&msg);
        if (msg.message == WM_QUIT)
        {
            w->should_quit = 1;
            w->exit_code = (int)msg.wParam;
        }
        else
        {
            DispatchMessageW(&msg);
        }
    }
}

LRESULT CALLBACK windows_demo_window_proc_W(HWND hWnd, uint32_t msg, WPARAM wp, LPARAM lp)
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

void windows_demo_destroy_window(WindowsDemoGuts *w)
{
    if (w->Window && w->hDC) ReleaseDC(w->Window, w->hDC);
    if (w->Window) DestroyWindow(w->Window);
    if (w->WindowClass) UnregisterClassW(w->WindowClass, w->hInstance);
    CoUninitialize();
    if (w->AudioDev)
    {
        waveOutReset(w->AudioDev);
        while (!windows_demo_is_audio_buffers_all_idle(w)) Sleep(10);
        for (size_t i = 0; i < AUDIO_PLAY_BUFFERS; i++)
        {
            AudioPlayBuffer *playbuf = &w->a_play_buf[i];
            WAVEHDR *pwhdr = &playbuf->whdr;
            playbuf->data_size = 0;
            playbuf->buffer_size = 0;
            free(playbuf->buffer);
            playbuf->buffer = NULL;
            memset(pwhdr, 0, sizeof * pwhdr);
        }
        waveOutClose(w->AudioDev);
    }
    w->hDC = 0;
    w->Window = 0;
    w->WindowClass = 0;
    CoUninitialize();
}

void windows_demo_set_source(WindowsDemoGuts *w, avi_stream_reader *s_video, avi_stream_reader *s_audio)
{
    avi_reader *r = NULL;
    if (!r && s_video) r = s_video->r;
    if (!r && s_audio) r = s_audio->r;
    w->r = r;
    w->s_video = s_video;
    w->s_audio = s_audio;
}

int windows_demo_create_window(WindowsDemoGuts *w, uint32_t target_width, uint32_t target_height, avi_stream_reader *s_video, avi_stream_reader *s_audio)
{
    memset(w, 0, sizeof * w);
    w->video_width = target_width;
    w->video_height = target_height;

    // https://devblogs.microsoft.com/oldnewthing/20041025-00/?p=37483
    extern HINSTANCE __ImageBase;
    w->hInstance = __ImageBase;

    windows_demo_set_source(w, s_video, s_audio);

    WNDCLASSEXW WCExW = {
        sizeof(WNDCLASSEXW),
        0,
        windows_demo_window_proc_W,
        0,
        sizeof(w),
        w->hInstance,
        LoadCursorW(NULL, IDC_ARROW),
        LoadIconW(NULL, IDI_APPLICATION),
        (HBRUSH)(COLOR_WINDOW + 1),
        NULL,
        L"AVI_Player_Class",
        LoadIconW(NULL, IDI_APPLICATION),
    };

    w->WindowClass = (LPCWSTR)RegisterClassExW(&WCExW);
    if (!w->WindowClass)
    {
        fprintf(stderr, "[FATAL] RegisterClassExW() failed.\n");
        goto ErrRet;
    }

    w->Window = CreateWindowExW(0, w->WindowClass, L"AVI(MJPEG + PCM only) player", WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, target_width, target_height, NULL, NULL, w->hInstance, w);
    if (!w->Window)
    {
        fprintf(stderr, "[FATAL] CreateWindowExW() failed.\n");
        goto ErrRet;
    }
    RECT rc_w, rc_c;
    GetWindowRect(w->Window, &rc_w);
    GetClientRect(w->Window, &rc_c);
    uint32_t border_width = (rc_w.right - rc_w.left) - (rc_c.right - rc_c.left);
    uint32_t border_height = (rc_w.bottom - rc_w.top) - (rc_c.bottom - rc_c.top);
    if (!MoveWindow(w->Window, rc_w.left, rc_w.top, target_width + border_width, target_height + border_height, FALSE))
    {
        // If the window can't be resized to the target size, it'll show a cropped AVI video.
        // I don't want this, I'd rather quit. Showing cropped video isn't an option.
        fprintf(stderr, "[FATAL] MoveWindow() failed.\n");
        goto ErrRet;
    }
    ShowWindow(w->Window, SW_SHOW);

    // Use this to draw picture to data via GDI
    w->hDC = GetDC(w->Window);
    if (!w->hDC)
    {
        fprintf(stderr, "[FATAL] GetDC() failed.\n");
        goto ErrRet;
    }

    // Initialize COM. I want to use the retro one `OleLoadPicture()`
    if (FAILED(CoInitializeEx(NULL, COINIT_MULTITHREADED)))
    {
        fprintf(stderr, "[FATAL] CoInitializeEx() failed.\n");
        goto ErrRet;
    }

    return 1;
ErrRet:
    windows_demo_destroy_window(w);
    return 0;
}

uint64_t get_super_precise_time_in_ns()
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
#pragma comment(lib, "winmm.lib")

#endif
