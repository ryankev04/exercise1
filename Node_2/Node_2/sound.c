/* AcceptedV4 audio integration: two independently decoded/resampled voices.
 * Legacy CAN settings/status IDs retained; automatic policy is audio.c.
 * SD access is foreground only. Solenoid/control timing remains separate. */
#include <stdio.h>
#include <string.h>
#include "sam.h"
#include "sound.h"
#include "audio.h"
#include "fat32.h"
#include "sd_card.h"
#include "can.h"
#include "wav_stream.h"
#include "pcm_resample.h"
#include "sound_mix.h"
#define RING_SIZE 8192u
#define RING_MASK (RING_SIZE-1u)
#define PREFILL 1024u
#define MUSIC_COUNT 8u
typedef struct {const char *path;const char *alias;fat32_file_t file;uint8_t found;} asset_t;
static asset_t assets[]={
	{.path="MUSIC/kubbi_firelight.wav"}, 
	{.path="MUSIC/kubbi_something.wav"},
	{.path="MUSIC/pablo_stage1.wav"}, 
	{.path="MUSIC/pablo_stage2.wav"}, 
	{.path="MUSIC/pablo_stage3.wav"},
	{.path="MUSIC/pablo_hills.wav",.alias="MUSIC/hills.wav"},
	{.path="MUSIC/pablo_city.wav",.alias="MUSIC/city.wav"}, 
	{.path="MUSIC/mario_theme.wav"},
	{.path="SFX/q3_navigate.wav",.alias="SFX/q3_menu1_navigate.wav"},
	{.path="SFX/q3_enter.wav",.alias="SFX/q3_menu1_enter.wav"},
	{.path="SFX/q3_back.wav",.alias="SFX/q3_menu1_back.wav"},
	{.path="SFX/q3_change.wav",.alias="SFX/q3_menu1_change.wav"},
	{.path="SFX/q3_error.wav",.alias="SFX/q3_menu1_error.wav"},
	{.path="SFX/ball_hit.wav"}, 
	{.path="SFX/rocketrace_low_rise.wav",.alias="SFX/rockettrace_low_rise.wav"},
	{.path="SFX/rocketrace_high_rise.wav",.alias="SFX/rockettrace_high_rise.wav"}, 
	{.path="SFX/rocketrace_lost.wav",.alias="SFX/rockettrace_lost.wav"},
	{.path = "MUSIC/pablo_lounge.wav"}
};
typedef struct {
 volatile int16_t ring[RING_SIZE];
 volatile uint32_t write,read,underruns;
 volatile uint8_t active,started,eof;
 uint8_t loop,priority,id;
 fat32_file_t file;
 fat32_reader_t reader;
 wav_stream_t wav;
 pcm_resample_t resample;
} voice_t;
static voice_t voices[2];
static uint8_t sector[512],ready,music_count,card_state=SOUND_STATE_NO_CARD;
static volatile uint16_t gain=32;
static uint8_t music_enabled=1,music_pending,effect_pending,effect_priority,current_music;
static const char *desired_music,*desired_effect;
static volatile uint32_t dac_busy;
static volatile uint32_t sample_interrupts;
static const uint16_t volume_gain[]={0,4,8,13,20,32,48,72,108,162,256};

void audio_backend_music(const char *path) {desired_music=path;music_pending=1;}
void audio_backend_effect(const char *path,uint8_t priority) {
 if(!effect_pending || priority>=effect_priority){desired_effect=path;effect_priority=priority;effect_pending=1;}
}
static void amp_update(void) {
 if(gain && (voices[0].active || voices[1].active)) PIOC->PIO_CODR=PIO_PC19;
 else PIOC->PIO_SODR=PIO_PC19;
}
static void stop_voice(voice_t *v) {
 NVIC_DisableIRQ(TC3_IRQn);
 v->active=0;v->started=0;v->eof=0;v->read=v->write;
 __DMB();NVIC_EnableIRQ(TC3_IRQn);
}
static void rewind_voice(voice_t *v) {
 fat32_open(&v->reader,&v->file);
 wav_stream_init(&v->wav,v->file.size);
 memset(&v->resample,0,sizeof v->resample);
}
static uint8_t asset_id(const char *path) {
 unsigned i;if(!path)return 0;
 for(i=0;i<sizeof assets/sizeof assets[0];i++)if(!strcmp(path,assets[i].path))return (uint8_t)(i+1);
 return 0;
}
static void start_voice(voice_t *v,const char *path,uint8_t loop,uint8_t priority) {
 uint8_t id=asset_id(path);
 if(!loop && v->active && priority<v->priority)return;
 if(!loop && (!id||!assets[id-1].found||!ready))return;
 stop_voice(v);
 if(loop)current_music=0;
 if(loop && !path && ready)card_state=SOUND_STATE_OK;
 if(!id || !assets[id-1].found || !ready) {
  if(path){printf("Sound: missing/unavailable %s\r\n",path);if(ready)card_state=SOUND_STATE_NO_SONGS;}
  return;
 }
 v->file=assets[id-1].file;v->id=id;v->loop=loop;v->priority=priority;
 rewind_voice(v);__DMB();v->active=1;
 if(loop)current_music=id;
 if(loop)card_state=SOUND_STATE_OK;
 /* No blocking UART output when an effect starts: refill both voices first. */
}
static int16_t take_sample(voice_t *v) {
 int16_t sample=0;
 if(!v->active||!v->started)return 0;
 if(v->read!=v->write){sample=v->ring[v->read&RING_MASK];__DMB();v->read++;}
 else if(v->eof)v->active=0;
 else v->underruns++;
 return sample;
}
__attribute__((optimize("O2"))) void TC3_Handler(void) {
 (void)TC1->TC_CHANNEL[0].TC_SR;
 sample_interrupts++;
 uint8_t fx_active=voices[1].active && voices[1].started;
 int16_t music=take_sample(&voices[0]),effect=take_sample(&voices[1]);
 /* These three recordings peak below -21 dBFS. Lift them by 18 dB;
  * ball_hit already approaches full scale and must not receive this boost. */
 if(voices[1].id>=15u && voices[1].id<=17u) {
  int32_t boosted=(int32_t)effect*8;
  if(boosted>32767)boosted=32767;
  if(boosted< -32768)boosted= -32768;
  effect=(int16_t)boosted;
 }
 uint16_t value=sound_mix_to_dac(music,effect,fx_active,gain);
 if(DACC->DACC_ISR&DACC_ISR_TXRDY)DACC->DACC_CDR=value;
 else dac_busy++;
}
static void hardware_init(void) {
 /* Same pin routing/amp polarity as AcceptedV4. D44 has no physical effect
  * if the optional shutdown transistor is not connected. */
 PMC->PMC_PCER0=(1u<<ID_PIOC);
 PIOC->PIO_SODR=PIO_PC19;PIOC->PIO_PER=PIO_PC19;PIOC->PIO_OER=PIO_PC19;
 PMC->PMC_PCER1=(1u<<(ID_DACC-32));DACC->DACC_CR=DACC_CR_SWRST;
 DACC->DACC_MR=DACC_MR_TRGEN_DIS|DACC_MR_WORD_HALF|DACC_MR_USER_SEL_CHANNEL0|DACC_MR_REFRESH(8)|DACC_MR_STARTUP_1024;
 DACC->DACC_ACR=DACC_ACR_IBCTLCH0(2)|DACC_ACR_IBCTLCH1(2)|DACC_ACR_IBCTLDACCORE(1);
 DACC->DACC_CHER=DACC_CHER_CH0;DACC->DACC_CDR=2048;
 PMC->PMC_PCER0=(1u<<ID_TC3);
 TC1->TC_CHANNEL[0].TC_CCR=TC_CCR_CLKDIS;TC1->TC_CHANNEL[0].TC_IDR=0xffffffffu;
 (void)TC1->TC_CHANNEL[0].TC_SR;
 TC1->TC_CHANNEL[0].TC_CMR=TC_CMR_TCCLKS_TIMER_CLOCK1|TC_CMR_WAVE|TC_CMR_WAVSEL_UP_RC;
 SystemCoreClockUpdate();
 TC1->TC_CHANNEL[0].TC_RC=(SystemCoreClock/2u+PCM_OUTPUT_RATE/2u)/PCM_OUTPUT_RATE;
 NVIC_SetPriority(TC0_IRQn,1);NVIC_SetPriority(TC3_IRQn,3);
 NVIC_ClearPendingIRQ(TC3_IRQn);TC1->TC_CHANNEL[0].TC_IER=TC_IER_CPCS;
 NVIC_EnableIRQ(TC3_IRQn);TC1->TC_CHANNEL[0].TC_CCR=TC_CCR_CLKEN|TC_CCR_SWTRG;
}
static void list_entry(const char *name,uint32_t size) {
 printf("Sound: file %s (%lu bytes)\r\n",name,(unsigned long)size);
}
void sound_init(void) {
 unsigned i,found=0;hardware_init();
 printf("Sound: speed check; MCK=%lu TC3_RC=%lu target=%lu Hz\r\n",
  (unsigned long)SystemCoreClock,(unsigned long)TC1->TC_CHANNEL[0].TC_RC,(unsigned long)PCM_OUTPUT_RATE);
 sd_result_t result=sd_init();
 if(result!=SD_OK){printf("Sound: SD init error %u\r\n",(unsigned)result);return;}
 if(!fat32_mount()){card_state=SOUND_STATE_NOT_FAT32;printf("Sound: FAT32 mount/read error\r\n");return;}
 ready=1;card_state=SOUND_STATE_OK;
 printf("Sound: directory MUSIC\r\n");fat32_list_root_folder("MUSIC",list_entry);
 printf("Sound: directory SFX\r\n");fat32_list_root_folder("SFX",list_entry);
 for(i=0;i<sizeof assets/sizeof assets[0];i++) {
  assets[i].found=fat32_find_path(assets[i].path,&assets[i].file);
  if(!assets[i].found && assets[i].alias)assets[i].found=fat32_find_path(assets[i].alias,&assets[i].file);
  if(assets[i].found){found++;if(i<MUSIC_COUNT)music_count++;}
  else printf("Sound: missing %s (lookup %u)\r\n",assets[i].path,(unsigned)fat32_last_error());
 }
 if(!music_count)card_state=SOUND_STATE_NO_SONGS;
 printf("Sound: %u/17 paths found; WAV validation occurs on playback\r\n",found);
}
void sound_set(uint8_t enabled,uint8_t volume) {
 uint8_t next=!!enabled;
 if(volume>SOUND_VOLUME_MAX)volume=SOUND_VOLUME_MAX;
 gain=volume_gain[volume];
 if(next!=music_enabled){music_enabled=next;music_pending=1;}
 amp_update();
}
/* Supported input4k..48k -> output48k: <=12 output samples/input frame. */
static uint32_t refill_capacity(const voice_t *v) {
 uint32_t rate=v->wav.sample_rate;
 uint32_t bytes=(uint32_t)v->wav.channels*v->wav.bits/8u;
 if(rate<4000||rate>PCM_OUTPUT_RATE||!bytes)return 6157u;
 return (((512u/bytes+1u)*PCM_OUTPUT_RATE+rate-1u)/rate)+1u;
}
__attribute__((optimize("O2"))) static void service_voice(voice_t *v) {
 uint16_t i,valid;
 if(!v->active||v->eof)return;
 if(RING_SIZE-(uint32_t)(v->write-v->read)<refill_capacity(v))return;
 if(v->wav.done){if(!v->loop){v->eof=1;return;}rewind_voice(v);}
 valid=fat32_read_next_sector(&v->reader,sector);
 if(!valid){v->eof=1;if(v==voices){card_state=SOUND_STATE_READ_ERR;current_music=0;}printf("Sound: read/truncation error voice%u\r\n",(unsigned)(v-voices));}
 else for(i=0;i<valid;i++) {
  int16_t sample,output[12];
  uint16_t used=wav_stream_fast_frame(&v->wav,sector+i,(uint16_t)(valid-i),&sample);
  int result;
  if(used){i=(uint16_t)(i+used-1u);result=1;}
  else result=wav_stream_byte(&v->wav,sector[i],&sample);
  if(result<0){v->eof=1;if(v==voices){card_state=SOUND_STATE_BAD_FILE;current_music=0;}printf("Sound: WAV FAIL %s code=%u (%s) pos=%lu file=%lu riff=%lu rate=%lu bits=%u ch=%u align=%u\r\n",
    assets[v->id-1].path,(unsigned)v->wav.error,wav_stream_error_text(v->wav.error),
    (unsigned long)v->wav.position,(unsigned long)v->wav.file_size,(unsigned long)v->wav.riff_end,
    (unsigned long)v->wav.sample_rate,(unsigned)v->wav.bits,(unsigned)v->wav.channels,(unsigned)v->wav.block_align);
   printf("Sound: sector prefix:");for(uint8_t h=0;h<64&&h<valid;h++)printf(" %02X",sector[h]);printf("\r\n");break;}
  if(result==1) {
   uint8_t count=pcm_resample_frame(&v->resample,sample,v->wav.sample_rate,output),j;
   for(j=0;j<count;j++){v->ring[v->write&RING_MASK]=output[j];__DMB();v->write++;}
  }
  if(v->wav.done){if(!v->loop)v->eof=1;break;}
 }
 if((uint32_t)(v->write-v->read)>=PREFILL || v->eof || v->wav.done){__DMB();v->started=1;}
}
void sound_update(void) {
 if(music_pending){music_pending=0;start_voice(&voices[0],music_enabled?desired_music:NULL,1,0);}
 if(effect_pending){effect_pending=0;start_voice(&voices[1],desired_effect,0,effect_priority);}
 /* Restore V6's eight-sector budget. Feed the voice nearest starvation,
  * rather than giving an already full voice half the refill opportunities.
  * All SD access stays in foreground, with a fixed bound per main-loop pass. */
 for(uint8_t sector_count=0;sector_count<8u;sector_count++) {
  voice_t *chosen=NULL;
  for(uint8_t i=0;i<2u;i++) {
   voice_t *v=&voices[i];
   uint32_t queued=v->write-v->read;
   if(!v->active || v->eof || RING_SIZE-queued<refill_capacity(v))continue;
   if(!chosen || queued<(uint32_t)(chosen->write-chosen->read))chosen=v;
  }
  if(!chosen)break;
  service_voice(chosen);
 }
 amp_update();
}
void sound_can_update(uint32_t now_ms) {
 static uint32_t report_ms,report_irq;
 if((uint32_t)(now_ms-report_ms)>=2000u) {
  uint32_t irq=sample_interrupts,elapsed=now_ms-report_ms;
  printf("Sound: auto IRQ/s=%lu queued=%lu/%lu underruns=%lu/%lu DACbusy=%lu native=%lu\r\n",
   (unsigned long)(((irq-report_irq)*1000u)/elapsed),
   (unsigned long)(voices[0].write-voices[0].read),(unsigned long)(voices[1].write-voices[1].read),
   (unsigned long)voices[0].underruns,(unsigned long)voices[1].underruns,
   (unsigned long)dac_busy,(unsigned long)voices[0].wav.sample_rate);
  report_irq=irq;report_ms=now_ms;
 }
 static uint32_t last;static uint8_t next,advertised=255;CanMsg m={0};
 if((uint32_t)(now_ms-last)<50u)return;
 if(current_music!=advertised)next=0; /* publish authoritative ID before name parts */
 if(next && current_music) {
  const char *name=strrchr(assets[current_music-1].path,'/');
  uint8_t part=next-1,i;size_t length;
  name=name?name+1:assets[current_music-1].path;length=strlen(name);
  if(length>=4)length-=4; /* omit .wav */
  m.id=CAN_ID_SOUND_NAME;m.length=8;m.byte[0]=current_music;m.byte[1]=part;
  for(i=0;i<6;i++){uint8_t pos=part*6+i;if(pos<length && pos<20)m.byte[2+i]=(uint8_t)name[pos];}
 } else {
  m.id=CAN_ID_SOUND_INFO;m.length=3;m.byte[0]=music_count;m.byte[1]=current_music;m.byte[2]=card_state;
 }
 if(can_tx_try(m)){last=now_ms;if(!next)advertised=current_music;if(++next>SOUND_NAME_PARTS)next=0;}
}
void sound_report(void) {
 printf("Sound: mounted=%u music=%u fx=%u gain=%u underruns=%lu/%lu DACbusy=%lu\r\n",
  (unsigned)ready,(unsigned)current_music,(unsigned)voices[1].active,(unsigned)gain,
  (unsigned long)voices[0].underruns,(unsigned long)voices[1].underruns,(unsigned long)dac_busy);
 printf("Sound: clock MCK=%lu RC=%lu IRQ=%lu rate=%lu music_native=%lu queued=%lu/%lu\r\n",
  (unsigned long)SystemCoreClock,(unsigned long)TC1->TC_CHANNEL[0].TC_RC,
  (unsigned long)sample_interrupts,(unsigned long)((SystemCoreClock/2u)/TC1->TC_CHANNEL[0].TC_RC),
  (unsigned long)voices[0].wav.sample_rate,(unsigned long)(voices[0].write-voices[0].read),
  (unsigned long)(voices[1].write-voices[1].read));
}
