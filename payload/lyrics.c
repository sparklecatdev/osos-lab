/* SPDX-License-Identifier: GPL-3.0-only */
#include "lyrics.h"
#include "lyrics_lrc.h"
#include "osos.h"
#include "patch.h"

enum {
    LYRICS_PROPERTY = 0x7f1b,
};

static struct {
    void *model;
    void *track;
    int active;
    char text[LRC_BYTES + 1];
} lyrics;

static int separator(char c)
{
    return c == '\\' || c == '/' || c == ':';
}

static int sidecar_path(const char *path, struct osos_path *sidecar)
{
    const char *folder = NULL, *file = path, *extension = NULL;
    for (const char *p = path; p && *p; p++) {
        if (separator(*p)) {
            folder = file;
            file = p + 1;
            extension = NULL;
        } else if (*p == '.') {
            extension = p;
        }
    }
    if (!folder || file - folder != 4 || (folder[0] != 'F' && folder[0] != 'f') ||
        folder[1] < '0' || folder[1] > '9' || folder[2] < '0' || folder[2] > '9' ||
        !extension || extension == file || extension - file > 32)
        return 0;
    for (const char *p = file; p < extension; p++) {
        char c = *p;
        if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
              (c >= '0' && c <= '9') || c == '_' || c == '-'))
            return 0;
    }

    static const char prefix[] = "iPod_Control\\Device\\Lyrics\\";
    static const char suffix[] = ".lrc";
    sidecar->volume = 0;
    char *out = sidecar->text;
    for (unsigned i = 0; i < sizeof(prefix) - 1; i++)
        *out++ = prefix[i];
    *out++ = 'F';
    *out++ = folder[1];
    *out++ = folder[2];
    *out++ = '\\';
    while (file < extension)
        *out++ = *file++;
    for (unsigned i = 0; i < sizeof(suffix); i++)
        *out++ = suffix[i];
    return 1;
}

static int load(void *model)
{
    struct osos_player *player = osos_player_get();
    void *track = player->current;
    if (lyrics.model == model && lyrics.track == track)
        return lyrics.text[0] != 0;

    lyrics.model = model;
    lyrics.track = track;
    lyrics.active = 0;
    lyrics.text[0] = 0;
    if (!track)
        return 0;

    struct osos_media_item item;
    osos_media_item_init(&item);
    osos_player_current_item(player, &item);
    if (!osos_media_item_valid(&item))
        return 0;
    struct osos_native_string path;
    struct osos_path sidecar;
    typedef void (*path_fn)(struct osos_native_string *, struct osos_media_item *);
    ((path_fn)osos_method(&item, 0x10))(&path, &item);
    int found = sidecar_path(osos_native_string_data(&path), &sidecar);
    osos_native_string_destroy(&path);
    if (!found)
        return 0;

    void *handle;
    if (osos_file_open(&sidecar, 1, &handle))
        return 0;
    uint32_t bytes = 0;
    int result = osos_file_read(handle, lyrics.text, sizeof(lyrics.text), &bytes);
    osos_file_close(handle);
    if (!result)
        return lrc_format(lyrics.text, bytes);
    lyrics.text[0] = 0;
    return 0;
}

void cfw_lrc_track_changed(void *model)
{
    if (lyrics.model == model) {
        lyrics.track = NULL;
        lyrics.active = 0;
        lyrics.text[0] = 0;
    }
}

const char *cfw_lrc_get_text(void *model)
{
    return lyrics.active && lyrics.model == model ? lyrics.text : NULL;
}

PATCH_ARM int cfw_lrc_available(void *model)
{
    if (load(model))
        return 1;
    return ((int (*)(void *))0x081ef374)(model);
}

PATCH_ARM int cfw_lrc_open(void *model)
{
    if (!load(model))
        return ((int (*)(void *))0x081ed8b8)(model);
    lyrics.active = 1;
    osos_model_notify(model, OSOS_PROPERTY_STRING, LYRICS_PROPERTY);
    return 1;
}

extern int cfw_lrc_event_original(void *model, void *event);

PATCH_ARM int cfw_lrc_event(void *model, void *event)
{
    uint32_t code = ((const uint32_t *)event)[1];
    if (code == 0x80009 || code == 0x8000a)
        cfw_lrc_track_changed(model);
    return cfw_lrc_event_original(model, event);
}

extern void cfw_lrc_property(void);
PATCH_JUMP(0x081efe74, 0xe92d41f0, 0xe1a04000, cfw_lrc_event);
PATCH_JUMP(0x081eecb0, 0xe594001c, 0xe3500000, cfw_lrc_property);
PATCH_CALL(0x08214220, 0x081ef374, cfw_lrc_available);
PATCH_CALL(0x08214e30, 0x081ef374, cfw_lrc_available);
PATCH_CALL(0x0821556c, 0x081ef374, cfw_lrc_available);
PATCH_CALL(0x08214624, 0x081ed8b8, cfw_lrc_open);
