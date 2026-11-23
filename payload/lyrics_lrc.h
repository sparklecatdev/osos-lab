/* SPDX-License-Identifier: GPL-3.0-only */
#pragma once

#include <stddef.h>
#include <stdint.h>

#define LRC_BYTES 32768

int lrc_format(char text[LRC_BYTES + 1], size_t size);
