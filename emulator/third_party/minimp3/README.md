# minimp3

`minimp3.h` is [lieff/minimp3](https://github.com/lieff/minimp3)'s decoder, unchanged, at commit
`ea99364f61c14656440e8d77e9c233ccf3124633` (2026-07-27), CC0 (`../../licenses/minimp3_LICENSE`).
sha256 `57e437c5c1f0e8b243885d3929c8973b5e6c778451e0100ab4251d19915cb3ad`.

The sound card's MP3 codec (`components/devices/minimp3.cpp`) builds it with `MINIMP3_ONLY_MP3` (layer III only)
and `MINIMP3_NO_SIMD` (the same scalar code on every target).
