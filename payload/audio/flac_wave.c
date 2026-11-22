/* SPDX-License-Identifier: GPL-3.0-only */

#include "flac_wave.h"

int flac_wave_path(const char *path)
{
    if (!path)
        return 0;
    const char *suffix = path;
    for (const char *p = path; *p; p++) {
        if (*p == '.')
            suffix = p;
    }
    return suffix[0] == '.' && (suffix[1] | 32) == 'f' &&
           (suffix[2] | 32) == 'l' && (suffix[3] | 32) == 'a' &&
           (suffix[4] | 32) == 'c' && suffix[5] == '\0';
}

static void put_le(uint8_t *out, uint32_t value, unsigned size)
{
    for (unsigned i = 0; i < size; i++) {
        out[i] = value;
        value >>= 8;
    }
}

int flac_wave_open(struct flac_wave *wave, int (*read)(void *, void *, unsigned),
                   int (*seek)(void *, uint32_t), void *source)
{
    static const uint8_t header[44] = {
        'R', 'I', 'F', 'F', 0, 0, 0, 0, 'W', 'A', 'V', 'E',
        'f', 'm', 't', ' ', 16, 0, 0, 0, 1, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 16, 0,
        'd', 'a', 't', 'a', 0, 0, 0, 0,
    };
    if (!wave || !seek || flac_open(&wave->decoder, read, source))
        return -1;

    uint32_t stride = 2 * wave->decoder.channels;
    uint64_t bytes = wave->decoder.total_samples * stride;
    if (!bytes || bytes > INT32_MAX - sizeof(header))
        return -1;
    for (unsigned i = 0; i < sizeof(header); i++)
        wave->header[i] = header[i];
    put_le(wave->header + 4, bytes + sizeof(header) - 8, 4);
    put_le(wave->header + 22, wave->decoder.channels, 2);
    put_le(wave->header + 24, wave->decoder.rate, 4);
    put_le(wave->header + 28, wave->decoder.rate * stride, 4);
    put_le(wave->header + 32, stride, 2);
    put_le(wave->header + 40, bytes, 4);
    wave->seek = seek;
    wave->size = sizeof(header) + bytes;
    wave->position = 0;
    wave->frame_start = sizeof(header);
    wave->frame_end = sizeof(header);
    return 0;
}

int flac_wave_seek(struct flac_wave *wave, int64_t offset, unsigned whence)
{
    uint32_t base;
    switch (whence) {
    case 0: base = 0; break;
    case 1: base = wave->position; break;
    case 2: base = wave->size; break;
    default: return -1;
    }
    if (offset < -(int64_t)base || offset > wave->size - base)
        return -1;
    wave->position = base + offset;
    return 0;
}

static int decode_until(struct flac_wave *wave)
{
    struct flac_decoder *decoder = &wave->decoder;
    uint32_t stride = 2 * decoder->channels;
    if (wave->position < wave->frame_start ||
        (decoder->seek_points && wave->position >= wave->frame_end &&
         wave->position - wave->frame_end >= decoder->max_block * stride)) {
        uint32_t sample = (wave->position - sizeof(wave->header)) / stride;
        int count = flac_seek(decoder, wave->seek, sample, wave->pcm);
        if (count < 0)
            return -1;
        wave->frame_end = sizeof(wave->header) + (uint32_t)decoder->samples_read * stride;
        wave->frame_start = wave->frame_end - count * stride;
    }
    while (wave->position >= wave->frame_end) {
        int count = flac_decode(decoder, wave->pcm);
        if (count <= 0)
            return -1;
        wave->frame_start = wave->frame_end;
        wave->frame_end += count * stride;
    }
    return 0;
}

int flac_wave_read(struct flac_wave *wave, void *buffer, unsigned bytes)
{
    uint8_t *out = buffer;
    unsigned copied = 0;
    if (!out && bytes)
        return -1;
    while (copied < bytes && wave->position < wave->size) {
        const uint8_t *in;
        unsigned available;
        if (wave->position < sizeof(wave->header)) {
            in = wave->header + wave->position;
            available = sizeof(wave->header) - wave->position;
        } else {
            if (decode_until(wave))
                return copied ? (int)copied : -1;
            /* OSOS and the supported host targets are little endian. */
            in = (const uint8_t *)wave->pcm + (wave->position - wave->frame_start);
            available = wave->frame_end - wave->position;
        }
        unsigned count = bytes - copied;
        if (count > available)
            count = available;
        for (unsigned i = 0; i < count; i++)
            *out++ = in[i];
        wave->position += count;
        copied += count;
    }
    return copied;
}
