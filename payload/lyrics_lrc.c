/* SPDX-License-Identifier: GPL-3.0-only */
#include "lyrics_lrc.h"

static int digit(char c)
{
    return c >= '0' && c <= '9';
}

static const char *timestamp(const char *p, const char *end, char close)
{
    const char *start = p;
    while (p < end && digit(*p) && p - start < 4)
        p++;
    if (p == start || p == end || *p++ != ':' || end - p < 3 ||
        p[0] < '0' || p[0] > '5' || !digit(p[1]))
        return NULL;
    p += 2;
    if (*p == '.') {
        start = ++p;
        while (p < end && digit(*p) && p - start < 3)
            p++;
        if (p == start)
            return NULL;
    }
    return p < end && *p == close ? p + 1 : NULL;
}

static int metadata(const char *p, const char *end)
{
    static const char *const keys[] = {
        "al", "ar", "au", "by", "id", "la", "length", "offset", "re", "ti", "tool", "ve",
    };
    if (end - p < 4 || *p++ != '[' || end[-1] != ']')
        return 0;
    for (size_t i = 0; i < sizeof(keys) / sizeof(keys[0]); i++) {
        const char *key = keys[i], *name = p;
        while (*key && name < end && *key == *name) {
            key++;
            name++;
        }
        if (!*key && name < end && *name == ':')
            return 1;
    }
    return 0;
}

static uint32_t read_code_point(const char **cursor, const char *end)
{
    const char *p = *cursor;
    uint32_t c = (uint8_t)*p++;
    *cursor = p;
    if (c < 0x80)
        return c ? c : 0xfffd;
    unsigned extra;
    uint32_t minimum;
    if (c >= 0xc2 && c <= 0xdf) {
        extra = 1;
        minimum = 0x80;
        c &= 0x1f;
    } else if (c >= 0xe0 && c <= 0xef) {
        extra = 2;
        minimum = 0x800;
        c &= 0x0f;
    } else if (c >= 0xf0 && c <= 0xf4) {
        extra = 3;
        minimum = 0x10000;
        c &= 0x07;
    } else {
        return 0xfffd;
    }
    if ((size_t)(end - p) < extra)
        return 0xfffd;
    for (unsigned i = 0; i < extra; i++) {
        uint8_t byte = *p++;
        if ((byte & 0xc0) != 0x80)
            return 0xfffd;
        c = (c << 6) | (byte & 0x3f);
    }
    if (c < minimum || (c >= 0xd800 && c <= 0xdfff) || c > 0x10ffff)
        return 0xfffd;
    *cursor = p;
    return c;
}

int lrc_format(char text[LRC_BYTES + 1], size_t size)
{
    if (size > LRC_BYTES) {
        text[0] = 0;
        return 0;
    }
    const char *p = text, *end = text + size;
    char *out = text;
    if (size >= 3 && (uint8_t)p[0] == 0xef && (uint8_t)p[1] == 0xbb &&
        (uint8_t)p[2] == 0xbf)
        p += 3;

    while (p < end) {
        const char *line_end = p;
        while (line_end < end && *line_end != '\n' && *line_end != '\r')
            line_end++;
        const char *next = line_end;
        if (next < end && *next++ == '\r' && next < end && *next == '\n')
            next++;
        if (metadata(p, line_end)) {
            p = next;
            continue;
        }
        while (p < line_end && *p == '[') {
            const char *after = timestamp(p + 1, line_end, ']');
            if (!after)
                break;
            p = after;
        }
        while (p < line_end) {
            if (*p == '<') {
                const char *after = timestamp(p + 1, line_end, '>');
                if (after) {
                    p = after;
                    continue;
                }
            }
            const char *bytes = p;
            uint32_t c = read_code_point(&p, line_end);
            if ((c == 0xfffd && p - bytes == 1) || (c < 0x20 && c != '\t')) {
                *out++ = '?';
            } else {
                while (bytes < p)
                    *out++ = *bytes++;
            }
        }
        if (out != text && line_end < end)
            *out++ = '\n';
        p = next;
    }
    while (out > text && out[-1] == '\n')
        out--;
    *out = 0;
    return out != text;
}
