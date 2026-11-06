/**
 * @file minimp3.cpp
 * @brief The vendored minimp3 decoder (third_party/minimp3, CC0), built once for the sound card's MP3 codec.
 *
 * Layer III only, and the scalar code on every target, so native and WebAssembly decode alike.
 */

#define MINIMP3_IMPLEMENTATION
#define MINIMP3_ONLY_MP3
#define MINIMP3_NO_SIMD
#include "minimp3.h"
