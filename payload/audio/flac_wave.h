/* SPDX-License-Identifier: GPL-3.0-only */

#ifndef CFW_FLAC_WAVE_H
#define CFW_FLAC_WAVE_H

#include "flac.h"

struct flac_wave {
    struct flac_decoder decoder;
    int (*seek)(void *source, uint32_t offset);
    uint32_t position;
    uint32_t size;
    uint32_t frame_start;
    uint32_t frame_end;
    uint8_t header[44];
    int16_t pcm[2 * FLAC_MAX_BLOCK];
};

int flac_wave_path(const char *path);
int flac_wave_open(struct flac_wave *wave, int (*read)(void *, void *, unsigned),
                   int (*seek)(void *, uint32_t), void *source);
int flac_wave_seek(struct flac_wave *wave, int64_t offset, unsigned whence);
int flac_wave_read(struct flac_wave *wave, void *buffer, unsigned bytes);

#endif
