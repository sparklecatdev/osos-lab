/* SPDX-License-Identifier: GPL-3.0-only */

#include "flac.h"

static int input_byte(struct flac_decoder *d)
{
    if (d->offset == d->length) {
        int length = d->read(d->source, d->buffer, sizeof(d->buffer));
        if (length < 0 || (unsigned)length > sizeof(d->buffer) ||
            (unsigned)length > UINT32_MAX - d->input_end) {
            d->error = 1;
            return -1;
        }
        if (!length)
            return -1;
        d->length = length;
        d->offset = 0;
        d->input_end += length;
    }
    return d->buffer[d->offset++];
}

static int metadata_byte(struct flac_decoder *d)
{
    int byte = input_byte(d);
    if (byte < 0) {
        d->error = 1;
        return 0;
    }
    return byte;
}

static uint32_t input_position(const struct flac_decoder *d)
{
    return d->input_end - d->length + d->offset;
}

int flac_open(struct flac_decoder *d, int (*read)(void *, void *, unsigned),
              void *source)
{
    static const uint8_t magic[] = {'f', 'L', 'a', 'C'};
    uint8_t info[34];
    if (!d || !read)
        return -1;

    d->read = read;
    d->source = source;
    d->offset = 0;
    d->length = 0;
    d->input_end = 0;
    d->seek_points = 0;
    d->seek_count = 0;
    d->bits_left = 0;
    d->samples_read = 0;
    d->frame_number = 0;
    d->error = 0;
    for (unsigned i = 0; i < sizeof(magic); i++) {
        if (input_byte(d) != magic[i])
            goto invalid;
    }

    int type = metadata_byte(d);
    unsigned length = 0;
    for (unsigned i = 0; i < 3; i++)
        length = (length << 8) | metadata_byte(d);
    if ((type & 127) != 0 || length != sizeof(info))
        goto invalid;
    for (unsigned i = 0; i < sizeof(info); i++)
        info[i] = metadata_byte(d);

    unsigned min_block = (info[0] << 8) | info[1];
    d->max_block = (info[2] << 8) | info[3];
    d->rate = (info[10] << 12) | (info[11] << 4) | (info[12] >> 4);
    d->channels = ((info[12] >> 1) & 7) + 1;
    d->bits = (((info[12] & 1) << 4) | (info[13] >> 4)) + 1;
    d->total_samples = info[13] & 15;
    for (unsigned i = 14; i < 18; i++)
        d->total_samples = (d->total_samples << 8) | info[i];

    if (min_block < 16 || min_block > d->max_block ||
        d->max_block > FLAC_MAX_BLOCK || d->rate < 8000 || d->rate > 48000 ||
        d->channels > 2 || d->bits < 4 || d->bits > 24)
        goto invalid;

    while (!(type & 128) && !d->error) {
        type = metadata_byte(d);
        if (!(type & 127) || (type & 127) == 127)
            goto invalid;
        length = 0;
        for (unsigned i = 0; i < 3; i++)
            length = (length << 8) | metadata_byte(d);
        if ((type & 127) == 3 && length % 18 == 0) {
            d->seek_table = input_position(d);
            d->seek_points = length / 18;
        }
        while (length-- && !d->error)
            metadata_byte(d);
    }
    d->audio_start = input_position(d);
    return d->error ? -1 : 0;

invalid:
    d->error = 1;
    return -1;
}

static void frame_byte(struct flac_decoder *d, unsigned byte)
{
    static const uint8_t crc8_table[16] = {
        0x00, 0x07, 0x0e, 0x09, 0x1c, 0x1b, 0x12, 0x15,
        0x38, 0x3f, 0x36, 0x31, 0x24, 0x23, 0x2a, 0x2d,
    };
    static const uint16_t crc16_table[16] = {
        0x0000, 0x8005, 0x800f, 0x000a, 0x801b, 0x001e, 0x0014, 0x8011,
        0x8033, 0x0036, 0x003c, 0x8039, 0x0028, 0x802d, 0x8027, 0x0022,
    };
    unsigned crc8 = d->crc8 ^ byte;
    unsigned crc16 = d->crc16 ^ (byte << 8);
    for (unsigned i = 0; i < 2; i++) {
        crc8 = ((crc8 << 4) & 0xff) ^ crc8_table[crc8 >> 4];
        crc16 = ((crc16 << 4) & 0xffff) ^ crc16_table[crc16 >> 12];
    }
    d->crc8 = crc8;
    d->crc16 = crc16;
    d->byte = byte;
    d->bits_left = 8;
}

static uint32_t bits(struct flac_decoder *d, unsigned count)
{
    uint32_t value = 0;
    while (count && !d->error) {
        if (!d->bits_left) {
            int byte = input_byte(d);
            if (byte < 0) {
                d->error = 1;
                break;
            }
            frame_byte(d, byte);
        }
        unsigned take = count < d->bits_left ? count : d->bits_left;
        d->bits_left -= take;
        count -= take;
        value = (value << take) | ((d->byte >> d->bits_left) & ((1u << take) - 1));
    }
    return value;
}

static int32_t signed_bits(struct flac_decoder *d, unsigned count)
{
    if (!count)
        return 0;
    uint32_t sign = 1u << (count - 1);
    return (int32_t)((bits(d, count) ^ sign) - sign);
}

static uint32_t unary(struct flac_decoder *d, uint32_t limit)
{
    uint32_t value = 0;
    while (!bits(d, 1) && !d->error) {
        if (value == limit) {
            d->error = 1;
            break;
        }
        value++;
    }
    return value;
}

static uint64_t coded_number(struct flac_decoder *d)
{
    unsigned first = bits(d, 8);
    if (first < 128)
        return first;

    unsigned bytes = 0;
    for (unsigned mask = 128; first & mask; mask >>= 1)
        bytes++;
    if (bytes < 2 || bytes > 7) {
        d->error = 1;
        return 0;
    }
    uint64_t value = first & (127 >> bytes);
    for (unsigned i = 1; i < bytes; i++) {
        unsigned next = bits(d, 8);
        if ((next & 192) != 128)
            d->error = 1;
        value = (value << 6) | (next & 63);
    }
    if (value < (UINT64_C(1) << (bytes == 2 ? 7 : 5 * bytes - 4)))
        d->error = 1;
    return value;
}

static unsigned block_size(struct flac_decoder *d, unsigned code)
{
    if (code == 0)
        return 0;
    if (code == 1)
        return 192;
    if (code < 6)
        return 576u << (code - 2);
    if (code < 8)
        return bits(d, 8 * (code - 5)) + 1;
    return 256u << (code - 8);
}

static unsigned sample_rate(struct flac_decoder *d, unsigned code)
{
    static const uint32_t rates[] = {
        0, 88200, 176400, 192000, 8000, 16000, 22050, 24000, 32000, 44100, 48000, 96000
    };
    if (!code)
        return d->rate;
    if (code < sizeof(rates) / sizeof(*rates))
        return rates[code];
    if (code == 12)
        return bits(d, 8) * 1000;
    if (code == 13)
        return bits(d, 16);
    if (code == 14)
        return bits(d, 16) * 10;
    return 0;
}

static int fits_sample(int64_t value, unsigned width)
{
    int32_t limit = 1 << (width - 1);
    return value >= -limit && value < limit;
}

static void residuals(struct flac_decoder *d, int32_t *plane, unsigned count,
                      unsigned order, unsigned width, const int16_t *coefficients,
                      unsigned shift)
{
    unsigned method = bits(d, 2);
    unsigned partitions = 1u << bits(d, 4);
    unsigned partition_size = count >> __builtin_ctz(partitions);
    if (method > 1 || (count & (partitions - 1)) || partition_size <= order) {
        d->error = 1;
        return;
    }

    unsigned rice_width = 4 + method;
    unsigned escape = (1u << rice_width) - 1;
    unsigned sample = order;
    for (unsigned part_end = partition_size; part_end <= count && !d->error;
         part_end += partition_size) {
        unsigned rice = bits(d, rice_width);
        unsigned raw = rice == escape ? bits(d, 5) : 0;
        for (; sample < part_end && !d->error; sample++) {
            int32_t residual;
            if (rice == escape) {
                residual = signed_bits(d, raw);
            } else {
                uint32_t high = unary(d, UINT32_MAX >> rice);
                uint32_t low = bits(d, rice);
                uint32_t folded = (high << rice) | low;
                residual = (int32_t)((folded >> 1) ^ (0u - (folded & 1)));
            }
            int64_t predicted = 0;
            for (unsigned i = 0; i < order; i++)
                predicted += (int64_t)coefficients[i] * plane[sample - i - 1];
            int64_t value = residual + (predicted >> shift);
            if (!fits_sample(value, width)) {
                d->error = 1;
                return;
            }
            plane[sample] = value;
        }
    }
}

static void subframe(struct flac_decoder *d, int32_t *plane, unsigned count,
                     unsigned width)
{
    static const int16_t fixed[5][4] = {
        {0}, {1}, {2, -1}, {3, -3, 1}, {4, -6, 4, -1}
    };
    unsigned header = bits(d, 8);
    unsigned type = (header >> 1) & 63;
    unsigned wasted = header & 1 ? unary(d, width - 1) + 1 : 0;

    if ((header & 128) || wasted >= width || d->error) {
        d->error = 1;
        return;
    }
    width -= wasted;
    if (!type) {
        int32_t value = signed_bits(d, width);
        for (unsigned i = 0; i < count; i++)
            plane[i] = value;
    } else if (type == 1) {
        for (unsigned i = 0; i < count; i++)
            plane[i] = signed_bits(d, width);
    } else {
        unsigned order;
        if (type >= 32)
            order = type - 31;
        else if (type >= 8 && type <= 12)
            order = type - 8;
        else
            goto invalid;
        if (order >= count)
            goto invalid;

        for (unsigned i = 0; i < order; i++)
            plane[i] = signed_bits(d, width);
        const int16_t *coefficients;
        int16_t lpc[32];
        int shift = 0;
        if (type >= 32) {
            unsigned precision = bits(d, 4) + 1;
            shift = signed_bits(d, 5);
            if (precision == 16 || shift < 0)
                goto invalid;
            for (unsigned i = 0; i < order; i++)
                lpc[i] = signed_bits(d, precision);
            coefficients = lpc;
        } else {
            coefficients = fixed[order];
        }
        residuals(d, plane, count, order, width, coefficients, shift);
    }

    if (wasted && !d->error) {
        for (unsigned i = 0; i < count; i++)
            plane[i] *= 1 << wasted;
    }
    return;

invalid:
    d->error = 1;
}

static int16_t pcm16(int32_t sample, unsigned width)
{
    if (width > 16)
        return sample >> (width - 16);
    return sample * (1 << (16 - width));
}

int flac_decode(struct flac_decoder *d, int16_t *output)
{
    static const uint8_t widths[] = {0, 8, 12, 0, 16, 20, 24, 32};
    if (!d || !output || d->error)
        return -1;
    int first = input_byte(d);
    if (first < 0) {
        if (d->error || (d->total_samples && d->samples_read != d->total_samples))
            goto invalid;
        return 0;
    }
    d->crc8 = 0;
    d->crc16 = 0;
    frame_byte(d, first);
    unsigned sync = bits(d, 16);
    unsigned sizes = bits(d, 8);
    unsigned format = bits(d, 8);
    unsigned assignment = format >> 4;
    unsigned width_code = (format >> 1) & 7;
    unsigned width = width_code ? widths[width_code] : d->bits;
    if ((sync & 0xfffe) != 0xfff8 || (format & 1) || width != d->bits ||
        (assignment != d->channels - 1u &&
         (d->channels != 2 || assignment < 8 || assignment > 10)))
        goto invalid;
    uint64_t number = coded_number(d);
    unsigned count = block_size(d, sizes >> 4);
    unsigned rate = sample_rate(d, sizes & 15);
    bits(d, 8);
    if (d->error || d->crc8 || !count || count > d->max_block || rate != d->rate ||
        number != ((sync & 1) ? d->samples_read : d->frame_number) ||
        (d->seek_count && (count != d->seek_count ||
                          (!(sync & 1) && (uint32_t)d->samples_read % d->max_block))) ||
        (d->total_samples && d->samples_read + count > d->total_samples))
        goto invalid;

    subframe(d, d->plane[0], count, width + (assignment == 9));
    if (d->channels == 2)
        subframe(d, d->plane[1], count, width + (assignment == 8 || assignment == 10));
    if (bits(d, d->bits_left) != 0)
        goto invalid;
    bits(d, 16);
    if (d->error || d->crc16)
        goto invalid;

    for (unsigned i = 0; i < count; i++) {
        int32_t left = d->plane[0][i];
        if (d->channels == 2) {
            int32_t right = d->plane[1][i];
            if (assignment == 8) {
                right = left - right;
            } else if (assignment == 9) {
                left += right;
            } else if (assignment == 10) {
                int32_t mid = 2 * left + (right & 1);
                left = (mid + right) >> 1;
                right = (mid - right) >> 1;
            }
            if (!fits_sample(left, width) || !fits_sample(right, width))
                goto invalid;
            *output++ = pcm16(left, width);
            *output++ = pcm16(right, width);
        } else {
            *output++ = pcm16(left, width);
        }
    }
    d->samples_read += count;
    d->frame_number++;
    d->seek_count = 0;
    return count;

invalid:
    d->error = 1;
    return -1;
}

static int move_input(struct flac_decoder *d, int (*seek)(void *, uint32_t),
                      uint32_t position)
{
    d->offset = 0;
    d->length = 0;
    d->bits_left = 0;
    d->input_end = position;
    d->error = seek(d->source, position) != 0;
    return d->error;
}

static uint64_t metadata_number(struct flac_decoder *d, unsigned bytes)
{
    uint64_t value = 0;
    while (bytes--)
        value = (value << 8) | metadata_byte(d);
    return value;
}

int flac_seek(struct flac_decoder *d, int (*seek)(void *, uint32_t),
              uint32_t sample, int16_t *output)
{
    int backwards = sample < d->samples_read;
    if (!backwards && (!d->seek_points || d->error))
        return d->error ? -1 : 0;

    uint32_t next = input_position(d);
    uint32_t best_sample = 0;
    uint32_t best_offset = 0;
    unsigned best_count = 0;
    unsigned lo = 0, hi = d->seek_points;
    while (lo < hi) {
        unsigned mid = lo + (hi - lo) / 2;
        if (move_input(d, seek, d->seek_table + 18 * mid))
            break;
        uint64_t point = metadata_number(d, 8);
        if (point > sample) {
            hi = mid;
            continue;
        }
        lo = mid + 1;
        uint64_t offset = metadata_number(d, 8);
        unsigned count = metadata_number(d, 2);
        if (!d->error && (backwards || point > d->samples_read) &&
            offset <= UINT32_MAX - d->audio_start &&
            count && count <= d->max_block && point + count <= d->total_samples) {
            best_sample = point;
            best_offset = offset;
            best_count = count;
        }
    }

    if (best_count) {
        if (!move_input(d, seek, d->audio_start + best_offset)) {
            d->samples_read = best_sample;
            d->frame_number = best_sample / d->max_block;
            d->seek_count = best_count;
            int count = flac_decode(d, output);
            if (count > 0)
                return count;
        }
        d->seek_points = 0;
    } else if (!backwards) {
        return move_input(d, seek, next) ? -1 : 0;
    }

    d->samples_read = 0;
    d->frame_number = 0;
    d->seek_count = 0;
    return move_input(d, seek, d->audio_start) ? -1 : 0;
}
