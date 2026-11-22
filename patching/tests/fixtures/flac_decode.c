/* SPDX-License-Identifier: GPL-3.0-only */

#include "flac.h"
#include "flac_wave.h"
#include <stdio.h>
#include <string.h>

static int read_source(void *source, void *buffer, unsigned bytes)
{
    FILE *file = source;
    /* Exercise partial reads, including across headers and frame boundaries. */
    size_t length = fread(buffer, 1, bytes > 67 ? 67 : bytes, file);
    return ferror(file) ? -1 : (int)length;
}

static int rewind_source(void *source)
{
    return fseek(source, 0, SEEK_SET);
}

static int read_wave(void)
{
    static struct flac_wave wave;
    unsigned char bytes[511];
    if (!flac_wave_path("F00/song.FlAc") || flac_wave_path(".f") ||
        flac_wave_path("song.flac.wav") || flac_wave_path(NULL) ||
        flac_wave_open(&wave, read_source, rewind_source, stdin))
        return 1;

    int count;
    while ((count = flac_wave_read(&wave, bytes, sizeof(bytes))) > 0) {
        if (fwrite(bytes, 1, count, stdout) != (size_t)count)
            return 1;
    }
    if (count < 0 || !flac_wave_seek(&wave, 1, 2) || !flac_wave_seek(&wave, -1, 0) ||
        !flac_wave_seek(&wave, 0, 3))
        return 1;

    if (flac_wave_seek(&wave, 43, 0) || flac_wave_read(&wave, bytes, 3) != 3 ||
        fwrite(bytes, 1, 3, stdout) != 3 ||
        flac_wave_seek(&wave, 9000, 1) || flac_wave_read(&wave, bytes, 11) != 11 ||
        fwrite(bytes, 1, 11, stdout) != 11 ||
        flac_wave_seek(&wave, -7, 2) || flac_wave_read(&wave, bytes, sizeof(bytes)) != 7 ||
        fwrite(bytes, 1, 7, stdout) != 7)
        return 1;
    return 0;
}

int main(int argc, char **argv)
{
    if (argc == 2 && !strcmp(argv[1], "wave"))
        return read_wave();

    static struct flac_decoder decoder;
    static int16_t pcm[FLAC_MAX_BLOCK * 2];
    if (flac_open(&decoder, read_source, stdin))
        return 1;

    int count;
    while ((count = flac_decode(&decoder, pcm)) > 0) {
        unsigned samples = count * decoder.channels;
        if (fwrite(pcm, sizeof(*pcm), samples, stdout) != samples)
            return 1;
    }
    return count < 0;
}
