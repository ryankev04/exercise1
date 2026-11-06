#include "wav_stream.h"
#include <string.h>
enum { HEADER,CHUNK,FORMAT,SKIP,PAD,DATA };
static uint16_t le16(const uint8_t *p) {
 return (uint16_t)((uint16_t)p[0]|((uint16_t)p[1]<<8));
}
static uint32_t le32(const uint8_t *p) {
 return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);
}
static int fail(wav_stream_t *s,uint8_t reason) {s->error=reason;return -1;}
void wav_stream_init(wav_stream_t *s,uint32_t file_size) {
 memset(s,0,sizeof *s);s->file_size=file_size;
 if(file_size<44)s->error=WAV_ERR_SMALL;
}
static void next_chunk(wav_stream_t *s) {s->used=0;s->state=s->pad?PAD:CHUNK;}
static int validate_format(wav_stream_t *s) {
 static const uint8_t pcm_guid[16]={1,0,0,0,0,0,0x10,0,0x80,0,0,0xaa,0,0x38,0x9b,0x71};
 uint16_t code=le16(s->scratch);
 uint32_t bytes_per_second;
 s->channels=le16(s->scratch+2);
 s->sample_rate=le32(s->scratch+4);
 s->block_align=le16(s->scratch+12);
 s->bits=le16(s->scratch+14);
 if(s->channels<1||s->channels>2)return fail(s,WAV_ERR_CHANNELS);
 if(s->sample_rate<4000||s->sample_rate>48000)return fail(s,WAV_ERR_RATE);
 if(s->bits!=8&&s->bits!=16&&s->bits!=24)return fail(s,WAV_ERR_BITS);
 if(s->block_align!=s->channels*(s->bits/8))return fail(s,WAV_ERR_ALIGN);
 bytes_per_second=s->sample_rate*s->block_align;
 if(le32(s->scratch+8)!=bytes_per_second)return fail(s,WAV_ERR_BYTE_RATE);
 if(code==0xfffe) {
  uint16_t extra;
  if(s->format_size<40)return fail(s,WAV_ERR_FMT_SIZE);
  extra=le16(s->scratch+16);
  if(extra<22 || (uint32_t)extra>s->format_size-18 ||
     !le16(s->scratch+18) || le16(s->scratch+18)>s->bits || memcmp(s->scratch+24,pcm_guid,16))return fail(s,WAV_ERR_EXTENSIBLE);
 } else if(code!=1)return fail(s,WAV_ERR_ENCODING);
 s->format_seen=1;return 0;
}
static int32_t channel_sample(const uint8_t *p,uint16_t bits) {
 uint16_t value;
 if(bits==8)return ((int32_t)p[0]-128)*256;
 /* Retain the top16 bits of24-bit PCM, matching the working helper. */
 value=bits==16?le16(p):le16(p+1);
 return value<32768u?(int32_t)value:(int32_t)value-65536;
}
__attribute__((optimize("O2"))) uint16_t wav_stream_fast_frame(wav_stream_t *s,const uint8_t *bytes,uint16_t available,int16_t *sample) {
 uint16_t size=s->block_align;
 if(s->state!=DATA || s->frame_used || s->error || s->done || !size ||
    !bytes || !sample || available<size || s->remaining<size ||
    s->position>s->file_size || size>s->file_size-s->position ||
    s->position>s->riff_end || size>s->riff_end-s->position)return 0;
 int32_t value=channel_sample(bytes,s->bits);
 if(s->channels==2)value=(value+channel_sample(bytes+s->bits/8,s->bits))/2;
 *sample=(int16_t)value;s->position+=size;s->remaining-=size;
 if(!s->remaining)s->done=1;
 return size;
}
int wav_stream_byte(wav_stream_t *s,uint8_t byte,int16_t *sample) {
 if(s->error)return -1;
 if(s->done)return 2;
 if(!sample)return fail(s,WAV_ERR_ARGUMENT);
 if(s->position>=s->file_size)return fail(s,WAV_ERR_FILE_END);
 if(s->state!=HEADER&&s->position>=s->riff_end)return fail(s,WAV_ERR_RIFF_END);
 s->position++;
 switch(s->state) {
 case HEADER:
  s->scratch[s->used++]=byte;
  if(s->used==12) {
   uint32_t length=le32(s->scratch+4);
   if(memcmp(s->scratch,"RIFF",4)||memcmp(s->scratch+8,"WAVE",4))return fail(s,WAV_ERR_SIGNATURE);
   if(length<36||length>s->file_size-8)return fail(s,WAV_ERR_RIFF_SIZE);
   s->riff_end=length+8;s->used=0;s->state=CHUNK;
  }
  break;
 case CHUNK:
  s->scratch[s->used++]=byte;
  if(s->used==8) {
   uint32_t length=le32(s->scratch+4),available=s->riff_end-s->position;
   s->remaining=length;s->pad=(uint8_t)(length&1u);
   if(length>available||(length==available&&s->pad))return fail(s,WAV_ERR_CHUNK_SIZE);
   if(!memcmp(s->scratch,"fmt ",4)) {
    if(s->format_seen)return fail(s,WAV_ERR_DUP_FORMAT);
    if(length<16)return fail(s,WAV_ERR_FMT_SIZE);
    s->format_size=length;s->used=0;s->state=FORMAT;
   } else if(!memcmp(s->scratch,"data",4)) {
    if(!s->format_seen)return fail(s,WAV_ERR_DATA_ORDER);
    if(!length)return fail(s,WAV_ERR_DATA_EMPTY);
    if(length%s->block_align)return fail(s,WAV_ERR_PARTIAL_FRAME);
    s->data_size=length;s->frame_used=0;s->state=DATA;
   } else {
    s->state=SKIP;if(!length)next_chunk(s);
   }
  }
  break;
 case FORMAT:
  if(s->used<sizeof s->scratch)s->scratch[s->used++]=byte;
  if(!--s->remaining) {
   if(validate_format(s)<0)return -1;
   next_chunk(s);
  }
  break;
 case SKIP:
  if(!--s->remaining)next_chunk(s);
  break;
 case PAD:
  s->pad=0;s->used=0;s->state=CHUNK;break;
 case DATA:
  s->remaining--;s->frame[s->frame_used++]=byte;
  if(s->frame_used==s->block_align) {
   int32_t value=channel_sample(s->frame,s->bits);
   if(s->channels==2)value=(value+channel_sample(s->frame+s->bits/8,s->bits))/2;
   *sample=(int16_t)value;s->frame_used=0;
   if(!s->remaining)s->done=1;
   return 1;
  }
  break;
 default:return fail(s,WAV_ERR_STATE);
 }
 return 0;
}
int wav_stream_finish(wav_stream_t *s) {
 if(s->done&&!s->error)return 0;
 return fail(s,WAV_ERR_TRUNCATED);
}

const char *wav_stream_error_text(uint8_t error) {
 static const char *const messages[]={"none","file too small","argument","physical EOF","RIFF ended",
  "not RIFF/WAVE","RIFF size beyond file","chunk size beyond RIFF","duplicate fmt","fmt size",
  "channels","sample rate","bit depth","block alignment","byte rate","encoding tag",
  "extensible PCM metadata","data before fmt","empty data","partial audio frame","decoder state","truncated"};
 return error<sizeof messages/sizeof messages[0]?messages[error]:"unknown";
}
