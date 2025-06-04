# AVI Reader Library (Parser only, non-decoding)

English | [简体中文](Readme-CN.md)

Parse AVI files to enumerate streams and retrieve stream packets.

## Add source files to your project

Grab these files from my repo:
* `avi_read/avi_guts.h`
* `avi_read/avi_reader.c`
* `avi_read/avi_reader.h`
Include the license. Please mention that my AVI Reader is developed by 0xAA55.

Copy them into your project, ensure `avi_reader.c` could be compiled, and your source files can `#include` the header files.

Consider adding the `fatfs` library for your embedded system. Note that `fatfs` supports `exFAT`, which allows files larger than 4GB. If you plan to handle such large AVI files, add `#define ENABLE_4GB_FILES 1` before including `avi_reader.h`.

The `.sln` and `.vcxproj` files (for Visual Studio 2022) are for me to develop my library, you don't need them but if you also have Visual Studio 2022, you can debug it yourself easily and send me a pull request on GitHub.

## Usage

See `avi_reader.h` for API definitions. The following callback functions must be implemented:
```c
fssize_t (*f_read)(void *buffer, size_t len, void* userdata);
fssize_t (*f_seek)(fsize_t offset, void* userdata);
fssize_t (*f_tell)(void* userdata);
int (*f_eof)(void* userdata);
```
There is one more callback function to be implemented but you can pass `NULL` for the API to initialize the `avi_stream_reader` which is:
```c
void (*logprintf)(void* userdata, const char* fmt);
```
Passing `NULL` enables the default behavior of my library to print debug info by calling `vprintf()`.

## Implementation Example
For embedded systems such as STM32H7 with:
- Hardware JPEG decoder peripheral
- DAC/I2S for audio output
- SDIO for reading an SD/TF card with FAT32 filesystem
  - Hint: If you say 'My product uses SD cards' then you have to pay the license for using SD cards, but if you say 'My product uses TF cards', then you can save your money.
- Display output.
- Hardware peripheral & timer configuration & code generation can be done using STM32CubeMX (It's recommended to use STM32CubeMX for all STM32 products, because it's the official way to start an STM32 project)

With my library, you can achieve:
1. Extract JPEG frames from the `MJPEG` stream of the AVI file, decode them with your hardware JPEG decoder peripheral, then show it on your display.
2. Extract PCM audio from the `PCM` stream of the AVI file, use your DAC/I2S peripheral to play.

If your device has sheer powerful CPU with very high frequency, the hardware JPEG decoder peripheral is optional just for saving your CPU usage. The `libjpeg` can decode the JPEG frames for you (and will consume roughly 99% of your CPU usage).

If the AVI file has different video stream format—for example, uncompressed BMP frames—the JPEG decoder is not needed. Just display the BMP frames directly on your display. Note that uncompressed BMP streams consume very high SDIO bandwidth.

If the AVI file uses `DivX`, `Xvid`, or `H264` video stream formats:
* Consider giving up on playing this video unless you have the corresponding hardware decoder peripherals
* With a sheer powerful CPU and sufficient memory, use software decoders for these formats
* Alternatively, upgrade to higher-spec hardware and use `FFmpeg` with a `buildroot` system

Please **NOTE** that you have to do the timing, wiring all of these things up. I'm not doing all of these things for you, but I can provide as much information as the AVI file contains.
