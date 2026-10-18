/* Audio decoder implementations (all public domain / CC0, see third_party). */
#pragma GCC diagnostic ignored "-Wpragmas"
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wunused-variable"
#pragma GCC diagnostic ignored "-Wunused-value"
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wtype-limits"
#pragma GCC diagnostic ignored "-Wimplicit-fallthrough"
#pragma GCC diagnostic ignored "-Wmisleading-indentation"
#pragma GCC diagnostic ignored "-Wshift-negative-value"
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#define DR_MP3_IMPLEMENTATION
#define DR_MP3_NO_STDIO_WIDE
#include "../third_party/dr_mp3.h"
#define DR_FLAC_IMPLEMENTATION
#include "../third_party/dr_flac.h"
#define DR_WAV_IMPLEMENTATION
#include "../third_party/dr_wav.h"
#include "../third_party/stb_vorbis.c"
