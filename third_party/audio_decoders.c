/* The implementations of the two music decoders, compiled once as C: dr_mp3 (MP3) and stb_vorbis (Ogg Vorbis).
   src/core/music.cpp includes their headers. Both read from memory only. */
#define DR_MP3_IMPLEMENTATION
#define DR_MP3_NO_STDIO
#include "dr_mp3/dr_mp3.h"

#define STB_VORBIS_NO_STDIO
#define STB_VORBIS_NO_PUSHDATA_API
/* Floating-point output only: the integer conversion is the one part that depends on the machine's byte order. */
#define STB_VORBIS_NO_INTEGER_CONVERSION
#include "stb_vorbis/stb_vorbis.c"
