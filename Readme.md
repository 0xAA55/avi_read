# AVI Reader Library (Parser only, non-decoding)

English|[简体中文](Readme-CN.md)

Read AVI files to enumerate streams and retrieve stream packets.

## Add source files to your project

Grab these files from the repo:
* `avi_read/avi_guts.h`
* `avi_read/avi_reader.c`
* `avi_read/avi_reader.h`
And the license. Please mention it that the AVI Reader is developed by 0xAA55.

Copy them into your project, make sure `avi_reader.c` could be compiled, and make sure you can include the header files.

I suggest you add `fatfs` library for your embedded system. Note that `fatfs` supports `exFAT`, which allows file bigger than 4GB, if you plan to manipulate such big AVI files, add `#define ENABLE_4GB_FILES 1` before including `avi_reader.h`.

I'm using Visual Studio 2022 as my development tool, so you can see there's some `.sln`, `.vcxproj` files. Just ignore them.

## Usage

See `avi_reader.h`, this file defines the API. If you know C, you know how to use the API.

You have to provide these callback functions for my library to work:
```
	fssize_t(*f_read)(void *buffer, size_t len, void* userdata);
	fssize_t(*f_seek)(fsize_t offset, void* userdata);
	fssize_t(*f_tell)(void* userdata);
	int(*f_eof)(void* userdata);
	void (*logprintf)(void* userdata, const char* fmt);
```
**NOTE** `logprintf` is not needed, if you provide `NULL`, my library will use `vprintf()` to print debug info.

## Best practice

For embedded systems like STM32H7 with hardware JPEG decoder periph, DAC or IIS for sound, and SDIO for reading a SD card with FAT32 filesystem, and a display, my library can help you to play AVI file with `MJPEG` stream and `PCM` stream. The JPEG frames will be extracted out from the `MJPEG` stream, use your hardware JPEG decoder periph to decode the frame to RGB888, then show it on the display. Also `PCM` waveforms will be extracted out, use your DAC or IIS periph to play it. Please **NOTE** that you have to do the timing. I'm not doing all of these things for you, but I can provide as many information as the AVI file contains.
