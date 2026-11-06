#ifndef WAV_STREAM_H
#define WAV_STREAM_H
#include <stdint.h>
enum {
 WAV_ERR_SMALL=1,WAV_ERR_ARGUMENT,WAV_ERR_FILE_END,WAV_ERR_RIFF_END,
 WAV_ERR_SIGNATURE,WAV_ERR_RIFF_SIZE,WAV_ERR_CHUNK_SIZE,WAV_ERR_DUP_FORMAT,
 WAV_ERR_FMT_SIZE,WAV_ERR_CHANNELS,WAV_ERR_RATE,WAV_ERR_BITS,
 WAV_ERR_ALIGN,WAV_ERR_BYTE_RATE,WAV_ERR_ENCODING,WAV_ERR_EXTENSIBLE,
 WAV_ERR_DATA_ORDER,WAV_ERR_DATA_EMPTY,WAV_ERR_PARTIAL_FRAME,WAV_ERR_STATE,
 WAV_ERR_TRUNCATED
};
const char *wav_stream_error_text(uint8_t error);
/* Allocation-free incremental WAV decoder. PCM 8/16/24-bit mono/stereo,
 * 4000..48000 Hz. Each output is one native-rate signed16 mono frame.
 * Extensible accepts PCM GUID and 1..container valid bits, left-aligned. */
typedef struct {
 uint32_t file_size,position,riff_end,remaining,data_size,sample_rate,format_size;
 uint16_t channels,bits,block_align;
 uint8_t scratch[40],frame[6];
 uint8_t state,used,pad,format_seen,frame_used,done,error;
} wav_stream_t;
void wav_stream_init(wav_stream_t *s,uint32_t file_size);
/* 0 header/partial frame, 1 frame emitted, -1 malformed/unsupported,
 * 2 already complete. The final frame returns1 and sets done. */
int wav_stream_byte(wav_stream_t *s,uint8_t byte,int16_t *sample);
int wav_stream_finish(wav_stream_t *s);
/* Decode one complete validated PCM frame in bulk. Returns bytes consumed;
 * zero means use the byte parser (metadata, partial frame, EOF or error). */
uint16_t wav_stream_fast_frame(wav_stream_t *s,const uint8_t *bytes,uint16_t available,int16_t *sample);
#endif
