/* SPDX-License-Identifier: GPL-3.0-only */

#include <stddef.h>

#include "flac_wave.h"
#include "osos.h"
#include "patch.h"
#include "patches/sites.h"

enum { AUDIO_ERROR = 3, AUDIO_WAVE = 3, READ_RAW = 2 };

struct audio_stream;
struct audio_stream_ops {
    void (*destroy)(struct audio_stream *);
    void (*delete)(struct audio_stream *);
    int (*open)(struct audio_stream *);
    int (*close)(struct audio_stream *);
    int (*read)(struct audio_stream *, void *, unsigned, unsigned);
    int (*seek)(struct audio_stream *, int64_t, unsigned);
    uint32_t (*size)(struct audio_stream *);
};

struct audio_stream {
    const struct audio_stream_ops *ops;
};

struct wave_reader {
    uint32_t base[4];
    struct audio_stream *stream;
    char path[1024];
};

struct flac_stream {
    struct audio_stream stream;
    struct audio_stream *source;
    struct flac_wave wave;
};

_Static_assert(offsetof(struct wave_reader, stream) == 0x10, "Wave stream offset");
_Static_assert(offsetof(struct wave_reader, path) == 0x14, "Wave path offset");
_Static_assert(offsetof(struct audio_stream_ops, seek) == 0x14, "Audio seek slot");

static int read_source(void *source, void *buffer, unsigned bytes)
{
    struct audio_stream *stream = source;
    return stream->ops->read(stream, buffer, bytes, READ_RAW);
}

static int seek_source(void *source, uint32_t offset)
{
    struct audio_stream *stream = source;
    return stream->ops->seek(stream, offset, 0);
}

static int close_flac(struct audio_stream *stream)
{
    struct flac_stream *flac = (struct flac_stream *)stream;
    if (flac->source) {
        struct audio_stream *source = flac->source;
        flac->source = NULL;
        source->ops->close(source);
        source->ops->delete(source);
    }
    return 0;
}

static void destroy_flac(struct audio_stream *stream)
{
    close_flac(stream);
}

static void delete_flac(struct audio_stream *stream)
{
    close_flac(stream);
    osos_import_free(stream);
}

static int read_flac(struct audio_stream *stream, void *buffer, unsigned bytes,
                     unsigned mode)
{
    struct flac_stream *flac = (struct flac_stream *)stream;
    if (!flac->source || mode != READ_RAW)
        return 0;
    int count = flac_wave_read(&flac->wave, buffer, bytes);
    return count < 0 ? 0 : count;
}

static int seek_flac(struct audio_stream *stream, int64_t offset, unsigned whence)
{
    struct flac_stream *flac = (struct flac_stream *)stream;
    return flac->source && !flac_wave_seek(&flac->wave, offset, whence) ? 0 : AUDIO_ERROR;
}

static uint32_t size_flac(struct audio_stream *stream)
{
    struct flac_stream *flac = (struct flac_stream *)stream;
    return flac->wave.size;
}

static const struct audio_stream_ops flac_ops = {
    .destroy = destroy_flac,
    .delete = delete_flac,
    .close = close_flac,
    .read = read_flac,
    .seek = seek_flac,
    .size = size_flac,
};

PATCH_ARM int cfw_classify_playback_path(const char *path, uint8_t *format)
{
    /* Send FLAC paths to the native WAV reader. */
    if (format && flac_wave_path(path)) {
        *format = AUDIO_WAVE;
        return 0;
    }
    return ((int (*)(const char *, uint8_t *))OSOS_AUDIO_CLASSIFY_PATH)(path, format);
}

PATCH_ARM int cfw_parse_wave_or_flac(struct wave_reader *reader)
{
    if (flac_wave_path(reader->path)) {
        struct audio_stream *source = reader->stream;
        if (!source || seek_source(source, 0))
            return AUDIO_ERROR;
        struct flac_stream *flac = osos_import_alloc(sizeof(*flac));
        if (!flac)
            return AUDIO_ERROR;
        if (flac_wave_open(&flac->wave, read_source, seek_source, source)) {
            osos_import_free(flac);
            return AUDIO_ERROR;
        }
        /* The WAV reader now owns the adapter, which closes the original stream. */
        flac->stream.ops = &flac_ops;
        flac->source = source;
        reader->stream = &flac->stream;
    }
    return ((int (*)(struct wave_reader *))OSOS_AUDIO_PARSE_WAVE)(reader);
}

PATCH_CALL(OSOS_AUDIO_CLASSIFY_PLAYBACK_CALL, OSOS_AUDIO_CLASSIFY_PATH,
           cfw_classify_playback_path);
PATCH_CALL(OSOS_AUDIO_PARSE_WAVE_CALL, OSOS_AUDIO_PARSE_WAVE, cfw_parse_wave_or_flac);
