/* SPDX-License-Identifier: GPL-3.0-only */

#ifndef CFW_FLAC_H
#define CFW_FLAC_H

#include <stdint.h>

#define FLAC_MAX_BLOCK 4608

struct flac_decoder {
    int (*read)(void *source, void *buffer, unsigned bytes);
    void *source;
    uint64_t total_samples;
    uint64_t samples_read;
    uint32_t rate;
    uint32_t frame_number;
    uint16_t max_block;
    uint8_t channels;
    uint8_t bits;

    unsigned offset;
    unsigned length;
    unsigned bits_left;
    uint8_t buffer[4096];
    uint8_t byte;
    uint8_t crc8;
    uint16_t crc16;
    int error;
    int32_t plane[2][FLAC_MAX_BLOCK];
};

/* Read callback returns bytes read, zero at EOF, or a negative I/O error. */
int flac_open(struct flac_decoder *decoder,
              int (*read)(void *, void *, unsigned), void *source);

/* Decode one frame to interleaved PCM. Returns samples per channel, 0 at EOF,
 * or -1 on invalid data. Output holds at least max_block * channels samples.
 * The 16-bit output truncates source precision above 16 bits.
 */
int flac_decode(struct flac_decoder *decoder, int16_t *output);

#endif
