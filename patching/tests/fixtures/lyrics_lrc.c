/* SPDX-License-Identifier: GPL-3.0-only */
#include <assert.h>
#include <string.h>

#include "../../../payload/lyrics_lrc.c"

int main(void)
{
    static char text[LRC_BYTES + 1];
    static const char input[] =
        "\xef\xbb\xbf[ar:Example]\r\n"
        "[00:03.456][00:01.20]<00:01.20>Caf\xc3\xa9\r\n"
        "[00:02.5]\xf0\x9f\x8e\xb5\n"
        "Translation\r"
        "[00:05]\n"
        "[Verse: 2]\n"
        "[00:06]A\xff" "B\0C\n"
        "[00:60.00]Literal <not a timestamp>\n"
        "[offset:+100]";
    memcpy(text, input, sizeof(input));
    assert(lrc_format(text, sizeof(input) - 1));
    assert(strcmp(text, "Caf\xc3\xa9\n\xf0\x9f\x8e\xb5\nTranslation\n\n"
                        "[Verse: 2]\nA?B?C\n[00:60.00]Literal <not a timestamp>") == 0);

    static const char empty[] = "[ti:Title]\n[00:01.00]<00:01.50>\n";
    memcpy(text, empty, sizeof(empty));
    assert(!lrc_format(text, sizeof(empty) - 1));
    assert(text[0] == 0);

    memset(text, 'x', LRC_BYTES);
    text[600] = '\n';
    assert(lrc_format(text, LRC_BYTES));
    assert(strlen(text) == LRC_BYTES && text[600] == '\n' && text[LRC_BYTES - 1] == 'x');
    assert(!lrc_format(text, LRC_BYTES + 1));
    assert(text[0] == 0);
}
