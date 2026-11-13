#include "wav_stream.h"
#include <string.h>
enum { READ_RIFF_HEADER, READ_CHUNK_HEADER, READ_FORMAT, SKIP_CHUNK, SKIP_PADDING, READ_PCM_DATA };
static uint16_t read_le16(const uint8_t *bytes) {
    return (uint16_t)((uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8));
}

static uint32_t read_le32(const uint8_t *bytes) {
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8) | ((uint32_t)bytes[2] << 16) | ((uint32_t)bytes[3] << 24);
}

static int set_error(wav_stream_t *stream, uint8_t reason) {
    stream->error = reason;
    return -1;
}

void wav_stream_init(wav_stream_t *stream, uint32_t file_size) {
    memset(stream, 0, sizeof *stream);
    stream->file_size = file_size;
    if (file_size < 44) {
        stream->error = WAV_ERR_SMALL;
    }
}

static void next_chunk(wav_stream_t *stream) {
    stream->used = 0;
    stream->state = stream->pad ? SKIP_PADDING : READ_CHUNK_HEADER;
}

static int validate_format(wav_stream_t *stream) {
    static const uint8_t pcm_guid[16] = {1, 0, 0, 0, 0, 0, 0x10, 0, 0x80, 0, 0, 0xaa, 0, 0x38, 0x9b, 0x71};
    uint16_t encoding = read_le16(stream->scratch);
    uint32_t bytes_per_second;
    stream->channels = read_le16(stream->scratch + 2);
    stream->sample_rate = read_le32(stream->scratch + 4);
    stream->block_align = read_le16(stream->scratch + 12);
    stream->bits = read_le16(stream->scratch + 14);
    if (stream->channels < 1 || stream->channels > 2) {
        return set_error(stream, WAV_ERR_CHANNELS);
    }
    if (stream->sample_rate < 4000 || stream->sample_rate > 48000) {
        return set_error(stream, WAV_ERR_RATE);
    }
    if (stream->bits != 8 && stream->bits != 16 && stream->bits != 24) {
        return set_error(stream, WAV_ERR_BITS);
    }
    if (stream->block_align != stream->channels * (stream->bits / 8)) {
        return set_error(stream, WAV_ERR_ALIGN);
    }
    bytes_per_second = stream->sample_rate * stream->block_align;
    if (read_le32(stream->scratch + 8) != bytes_per_second) {
        return set_error(stream, WAV_ERR_BYTE_RATE);
    }
    if (encoding == 0xfffe) {
        uint16_t extension_size;
        if (stream->format_size < 40) {
            return set_error(stream, WAV_ERR_FMT_SIZE);
        }
        extension_size = read_le16(stream->scratch + 16);
        if (extension_size < 22 || (uint32_t)extension_size > stream->format_size - 18 || !read_le16(stream->scratch + 18) ||
            read_le16(stream->scratch + 18) > stream->bits || memcmp(stream->scratch + 24, pcm_guid, 16)) {
            return set_error(stream, WAV_ERR_EXTENSIBLE);
        }
    } else if (encoding != 1) {
        return set_error(stream, WAV_ERR_ENCODING);
    }
    stream->format_seen = 1;
    return 0;
}

static int32_t channel_sample(const uint8_t *bytes, uint16_t bits) {
    uint16_t value;
    if (bits == 8) {
        return ((int32_t)bytes[0] - 128) * 256;
    }
    /*
     * Retain the top16 bits of24-bit PCM, matching the working helper.
     */
    value = bits == 16 ? read_le16(bytes) : read_le16(bytes + 1);
    return value < 32768u ? (int32_t)value : (int32_t)value - 65536;
}

__attribute__((optimize("O2"))) uint16_t wav_stream_fast_frame(wav_stream_t *stream, const uint8_t *bytes,
                                                               uint16_t available, int16_t *sample) {
    uint16_t frame_size = stream->block_align;
    if (stream->state != READ_PCM_DATA || stream->frame_used || stream->error || stream->done || !frame_size || !bytes || !sample ||
        available < frame_size || stream->remaining < frame_size || stream->position > stream->file_size ||
        frame_size > stream->file_size - stream->position || stream->position > stream->riff_end ||
        frame_size > stream->riff_end - stream->position) {
        return 0;
    }
    int32_t mono_sample = channel_sample(bytes, stream->bits);
    if (stream->channels == 2) {
        mono_sample = (mono_sample + channel_sample(bytes + stream->bits / 8, stream->bits)) / 2;
    }
    *sample = (int16_t)mono_sample;
    stream->position += frame_size;
    stream->remaining -= frame_size;
    if (!stream->remaining) {
        stream->done = 1;
    }
    return frame_size;
}

int wav_stream_byte(wav_stream_t *stream, uint8_t byte, int16_t *sample) {
    if (stream->error) {
        return -1;
    }
    if (stream->done) {
        return 2;
    }
    if (!sample) {
        return set_error(stream, WAV_ERR_ARGUMENT);
    }
    if (stream->position >= stream->file_size) {
        return set_error(stream, WAV_ERR_FILE_END);
    }
    if (stream->state != READ_RIFF_HEADER && stream->position >= stream->riff_end) {
        return set_error(stream, WAV_ERR_RIFF_END);
    }
    stream->position++;
    switch (stream->state) {
    case READ_RIFF_HEADER:
        stream->scratch[stream->used++] = byte;
        if (stream->used == 12) {
            uint32_t length = read_le32(stream->scratch + 4);
            if (memcmp(stream->scratch, "RIFF", 4) || memcmp(stream->scratch + 8, "WAVE", 4)) {
                return set_error(stream, WAV_ERR_SIGNATURE);
            }
            if (length < 36 || length > stream->file_size - 8) {
                return set_error(stream, WAV_ERR_RIFF_SIZE);
            }
            stream->riff_end = length + 8;
            stream->used = 0;
            stream->state = READ_CHUNK_HEADER;
        }
        break;
    case READ_CHUNK_HEADER:
        stream->scratch[stream->used++] = byte;
        if (stream->used == 8) {
            uint32_t length = read_le32(stream->scratch + 4), riff_bytes_left = stream->riff_end - stream->position;
            stream->remaining = length;
            stream->pad = (uint8_t)(length & 1u);
            if (length > riff_bytes_left || (length == riff_bytes_left && stream->pad)) {
                return set_error(stream, WAV_ERR_CHUNK_SIZE);
            }
            if (!memcmp(stream->scratch, "fmt ", 4)) {
                if (stream->format_seen) {
                    return set_error(stream, WAV_ERR_DUP_FORMAT);
                }
                if (length < 16) {
                    return set_error(stream, WAV_ERR_FMT_SIZE);
                }
                stream->format_size = length;
                stream->used = 0;
                stream->state = READ_FORMAT;
            } else if (!memcmp(stream->scratch, "data", 4)) {
                if (!stream->format_seen) {
                    return set_error(stream, WAV_ERR_DATA_ORDER);
                }
                if (!length) {
                    return set_error(stream, WAV_ERR_DATA_EMPTY);
                }
                if (length % stream->block_align) {
                    return set_error(stream, WAV_ERR_PARTIAL_FRAME);
                }
                stream->data_size = length;
                stream->frame_used = 0;
                stream->state = READ_PCM_DATA;
            } else {
                stream->state = SKIP_CHUNK;
                if (!length) {
                    next_chunk(stream);
                }
            }
        }
        break;
    case READ_FORMAT:
        if (stream->used < sizeof stream->scratch) {
            stream->scratch[stream->used++] = byte;
        }
        if (!--stream->remaining) {
            if (validate_format(stream) < 0) {
                return -1;
            }
            next_chunk(stream);
        }
        break;
    case SKIP_CHUNK:
        if (!--stream->remaining) {
            next_chunk(stream);
        }
        break;
    case SKIP_PADDING:
        stream->pad = 0;
        stream->used = 0;
        stream->state = READ_CHUNK_HEADER;
        break;
    case READ_PCM_DATA:
        stream->remaining--;
        stream->frame[stream->frame_used++] = byte;
        if (stream->frame_used == stream->block_align) {
            int32_t mono_sample = channel_sample(stream->frame, stream->bits);
            if (stream->channels == 2) {
                mono_sample = (mono_sample + channel_sample(stream->frame + stream->bits / 8, stream->bits)) / 2;
            }
            *sample = (int16_t)mono_sample;
            stream->frame_used = 0;
            if (!stream->remaining) {
                stream->done = 1;
            }
            return 1;
        }
        break;
    default:
        return set_error(stream, WAV_ERR_STATE);
    }
    return 0;
}

int wav_stream_finish(wav_stream_t *stream) {
    if (stream->done && !stream->error) {
        return 0;
    }
    return set_error(stream, WAV_ERR_TRUNCATED);
}

const char *wav_stream_error_text(uint8_t error) {
    static const char *const messages[] = {"none",
                                           "file too small",
                                           "argument",
                                           "physical EOF",
                                           "RIFF ended",
                                           "not RIFF/WAVE",
                                           "RIFF size beyond file",
                                           "chunk size beyond RIFF",
                                           "duplicate fmt",
                                           "fmt size",
                                           "channels",
                                           "sample rate",
                                           "bit depth",
                                           "block alignment",
                                           "byte rate",
                                           "encoding tag",
                                           "extensible PCM metadata",
                                           "data before fmt",
                                           "empty data",
                                           "partial audio frame",
                                           "decoder state",
                                           "truncated"};
    return error < sizeof messages / sizeof messages[0] ? messages[error] : "unknown";
}
