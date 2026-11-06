/* Read-only FAT32 adapted from the group's helper. Directory traversal is
 * bounded, and LFN fragments are accepted only with a complete descending
 * ordinal chain and the checksum of the following short entry. */
#include <string.h>
#include "fat32.h"
#include "sd_card.h"
#define EOC 0x0ffffff8u
#define NAME_MAX 63u
#define DIRECTORY_CLUSTER_LIMIT 4096u
static uint32_t fat_start, data_start, root_cluster, cluster_count, fat_sectors;
static uint8_t sectors_per_cluster, mounted, last_error;
static uint8_t fat_buffer[512], folder_buffer[512];
static uint32_t fat_buffer_sector=0xffffffffu;
static uint16_t read16(const uint8_t *p) {return (uint16_t)(p[0]|((uint16_t)p[1]<<8));}
static uint32_t read32(const uint8_t *p) {return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);}
static uint8_t disk_read(uint32_t sector,uint8_t *p) {
 if (!sd_read_sector(sector,p)) {last_error=1;return 0;} return 1;
}
uint8_t fat32_last_error(void) {return last_error;}
static uint8_t valid_cluster(uint32_t c) {return c>=2 && c-2<cluster_count && c<0x0ffffff0u;}
static uint32_t cluster_sector(uint32_t c) {return data_start+(c-2)*sectors_per_cluster;}
static uint32_t next_cluster(uint32_t c) {
 uint32_t offset=c/128,sector=fat_start+offset;
 if (!valid_cluster(c)||offset>=fat_sectors) {last_error=2;return 0;}
 if (sector!=fat_buffer_sector) {
  fat_buffer_sector=0xffffffffu;
  if(!disk_read(sector,fat_buffer))return 0;
  fat_buffer_sector=sector;
 }
 return read32(fat_buffer+4*(c%128))&0x0fffffffu;
}
static uint8_t boot_signature(const uint8_t *p) {return p[510]==0x55&&p[511]==0xaa;}
/* Do not depend on the informational 'FAT32   ' label. Validate the BPB. */
static uint8_t looks_like_bpb(const uint8_t *p) {
 return boot_signature(p)&&read16(p+11)==512&&p[13]&&!(p[13]&(p[13]-1))&&p[13]<=128&&
  read16(p+14)&&p[16]&&p[16]<=2&&!read16(p+17)&&!read16(p+22)&&read32(p+36)&&!read16(p+42);
}
uint8_t fat32_mount(void) {
 uint8_t *p=fat_buffer;uint32_t start=0,total,metadata,clusters,spfat;uint16_t ext;
 mounted=0;last_error=0;fat_buffer_sector=0xffffffffu;
 if(!disk_read(0,p))return 0;
 if(!looks_like_bpb(p)) {
  uint8_t i,found=0;
  if(!boot_signature(p)){last_error=2;return 0;}
  for(i=0;i<4;i++) {
   const uint8_t *entry=p+446+16*i;
   if(entry[4]==0x0b||entry[4]==0x0c){start=read32(entry+8);found=1;break;}
  }
  if(!found||!start){last_error=2;return 0;}
  if(!disk_read(start,p))return 0;
  if(!looks_like_bpb(p)){last_error=2;return 0;}
 }
 total=read16(p+19);if(!total)total=read32(p+32);
 spfat=read32(p+36);
 if(spfat>(0xffffffffu-read16(p+14))/p[16]){last_error=2;return 0;}
 metadata=read16(p+14)+(uint32_t)p[16]*spfat;
 if(total<=metadata||start>0xffffffffu-total){last_error=2;return 0;}
 clusters=(total-metadata)/p[13];
 /* FAT32 requires >=65525 data clusters and enough FAT entries for them. */
 if(clusters<65525u||clusters>=0x0fffffeeu||spfat<(clusters+2u+127u)/128u){last_error=2;return 0;}
 root_cluster=read32(p+44)&0x0fffffffu;
 if(root_cluster<2||root_cluster-2>=clusters){last_error=2;return 0;}
 sectors_per_cluster=p[13];cluster_count=clusters;fat_sectors=spfat;
 fat_start=start+read16(p+14);data_start=start+metadata;
 ext=read16(p+40);
 if(ext&0x80u) {
  uint8_t active=(uint8_t)(ext&15u);
  if(active>=p[16]){last_error=2;return 0;}
  fat_start+=(uint32_t)active*spfat;
 }
 mounted=1;return 1;
}
static uint8_t lower(uint8_t c) {return c>='A'&&c<='Z'?(uint8_t)(c+32):c;}
static uint8_t same(const char *a,const char *b) {
 while(*a&&*b){if(lower((uint8_t)*a++)!=lower((uint8_t)*b++))return 0;}return *a==*b;
}
static void short_name(char *out,const uint8_t *e,uint8_t with_extension) {
 uint8_t i,n=8,k=0;while(n&&e[n-1]==' ')n--;
 for(i=0;i<n;i++)out[k++]=(char)e[i];
 if(with_extension&&e[8]!=' ') {
  out[k++]='.';for(i=8;i<11&&e[i]!=' ';i++)out[k++]=(char)e[i];
 }
 out[k]=0;
}
static uint8_t checksum(const uint8_t *e) {
 uint8_t sum=0,i;for(i=0;i<11;i++)sum=(uint8_t)(((sum&1u)?128u:0u)+(sum>>1)+e[i]);return sum;
}
typedef struct {char text[66];uint8_t expected,sum,active,slots;} long_name_t;
static void lfn_piece(long_name_t *l,const uint8_t *e) {
 static const uint8_t positions[13]={1,3,5,7,9,14,16,18,20,22,24,28,30};
 uint8_t ordinal=e[0]&31u,i;uint16_t offset;
 if(e[0]&0x40u) {
  memset(l,0,sizeof *l);
  if(!ordinal||ordinal>5u)return;
  memset(l->text,0xff,sizeof l->text);
  l->active=1;l->expected=ordinal;l->sum=e[13];l->slots=(uint8_t)(ordinal*13u);
 }
 if(!l->active||!ordinal||ordinal!=l->expected||e[13]!=l->sum||e[12]||read16(e+26)||(e[0]&0xa0u)) {l->active=0;return;}
 offset=(uint16_t)(ordinal-1)*13;
 for(i=0;i<13;i++) {
  uint16_t c=read16(e+positions[i]);uint16_t index=offset+i;
  if(c==0){l->text[index]=0;continue;}
  if(c==0xffffu)continue;
  if(index>=NAME_MAX||c<32||c>126||c=='/'||c=='\\'){l->active=0;return;}
  l->text[index]=(char)c;
 }
 l->expected--;
}
static uint8_t valid_long_name(long_name_t *l) {
 uint8_t i,end=0;
 for(i=0;i<l->slots;i++) {
  uint8_t c=(uint8_t)l->text[i];
  if(!end) {
   if(!c){end=1;continue;}
   if(c==255u||i>=NAME_MAX)return 0;
  } else if(c!=255u)return 0;
 }
 if(!end) {
  if(l->slots>NAME_MAX)return 0;
  l->text[l->slots]=0;
 }
 return 1;
}
static uint32_t entry_cluster(const uint8_t *e) {return (((uint32_t)read16(e+20)<<16)|read16(e+26))&0x0fffffffu;}
static void assign_file(fat32_file_t *f,const uint8_t *e) {
 short_name(f->name,e,0);f->first_cluster=entry_cluster(e);f->size=read32(e+28);
}
/* desired_directory separates a real folder component from the final file. */
static uint8_t directory_find(uint32_t cluster,const char *name,uint8_t desired_directory,fat32_file_t *file) {
 uint32_t budget=cluster_count<DIRECTORY_CLUSTER_LIMIT?cluster_count:DIRECTORY_CLUSTER_LIMIT;
 long_name_t l;memset(&l,0,sizeof l);
 while(valid_cluster(cluster)&&budget--) {
  uint8_t s;
  for(s=0;s<sectors_per_cluster;s++) {
   uint16_t off;
   if(!disk_read(cluster_sector(cluster)+s,folder_buffer))return 0;
   for(off=0;off<512;off+=32) {
    const uint8_t *e=folder_buffer+off;char alias[13];uint8_t match;
    if(!e[0])return 0;
    if(e[0]==0xe5){l.active=0;continue;}
    if(e[11]==0x0f){lfn_piece(&l,e);continue;}
    short_name(alias,e,1);
    match=same(alias,name)||(l.active&&!l.expected&&l.sum==checksum(e)&&valid_long_name(&l)&&same(l.text,name));
    l.active=0;
    if((e[11]&0x0eu)||!!(e[11]&0x10u)!=desired_directory||!match)continue;
    assign_file(file,e);
    if(!valid_cluster(file->first_cluster)&&file->size){last_error=2;return 0;}
    if(desired_directory&&!valid_cluster(file->first_cluster)){last_error=2;return 0;}
    return 1;
   }
  }
  cluster=next_cluster(cluster);
  if(last_error)return 0;
  if(cluster>=EOC)return 0;
 }
 last_error=2;return 0;
}
uint8_t fat32_find_path(const char *path,fat32_file_t *file) {
 uint32_t dir=root_cluster;uint8_t depth=0;fat32_file_t found;
 last_error=0;
 if(!mounted||!path||!file||!*path){last_error=2;return 0;}
 while(*path) {
  char name[NAME_MAX+1];uint8_t n=0,more;
  while(*path&&*path!='/') {
   uint8_t c=(uint8_t)*path++;
   if(n==NAME_MAX||c<32||c>126||c=='\\'){last_error=2;return 0;}name[n++]=(char)c;
  }
  name[n]=0;more=*path=='/';
  if(!n||same(name,".")||same(name,"..")||++depth>8){last_error=2;return 0;}
  if(more&&!*++path){last_error=2;return 0;}
  if(!directory_find(dir,name,more,&found))return 0;
  if(!more){*file=found;return 1;}dir=found.first_cluster;
 }
 return 0;
}
/* Compatibility root listing uses short aliases; new playback uses paths. */
uint8_t fat32_find_files(const char *extension,fat32_file_t files[],uint8_t max_files) {
 uint32_t c=root_cluster,budget=cluster_count<DIRECTORY_CLUSTER_LIMIT?cluster_count:DIRECTORY_CLUSTER_LIMIT;uint8_t count=0;
 last_error=0;if(!mounted||!extension||!files||!max_files)return 0;
 while(valid_cluster(c)&&budget--) {
  uint8_t s;for(s=0;s<sectors_per_cluster;s++) {
   uint16_t off;if(!disk_read(cluster_sector(c)+s,folder_buffer))return count;
   for(off=0;off<512;off+=32) {
    const uint8_t *e=folder_buffer+off;
    if(!e[0])return count;
    if(e[0]==0xe5||(e[11]&0x1eu)||e[11]==0x0f||memcmp(e+8,extension,3))continue;
    assign_file(files+count,e);if(++count==max_files)return count;
   }
  }
  c=next_cluster(c);if(last_error||c>=EOC)return count;
 }
 last_error=2;return count;
}
void fat32_open(fat32_reader_t *r,const fat32_file_t *f) {
 last_error=0;
 r->cluster=f->first_cluster;r->sector_in_cluster=0;r->bytes_left=f->size;r->error=!mounted;r->clusters_left=cluster_count;
}
uint8_t fat32_list_root_folder(const char *folder,void (*entry)(const char *,uint32_t)) {
 fat32_file_t dir;long_name_t l;uint8_t count=0;
 uint32_t cluster,budget=cluster_count<DIRECTORY_CLUSTER_LIMIT?cluster_count:DIRECTORY_CLUSTER_LIMIT;
 last_error=0;
 if(!mounted||!folder||!entry)return 0;
 if(!directory_find(root_cluster,folder,1,&dir))return 0;
 cluster=dir.first_cluster;memset(&l,0,sizeof l);
 while(valid_cluster(cluster)&&budget--) {
  uint8_t sector;
  for(sector=0;sector<sectors_per_cluster;sector++) {
   uint16_t off;if(!disk_read(cluster_sector(cluster)+sector,folder_buffer))return count;
   for(off=0;off<512;off+=32) {
    const uint8_t *e=folder_buffer+off;char alias[13];const char *name;
    if(!e[0])return count;
    if(e[0]==0xe5){l.active=0;continue;}
    if(e[11]==0x0f){lfn_piece(&l,e);continue;}
    short_name(alias,e,1);name=alias;
    if(l.active&&!l.expected&&l.sum==checksum(e)&&valid_long_name(&l))name=l.text;
    l.active=0;
    if(e[11]&0x1eu)continue;
    entry(name,read32(e+28));if(++count==64)return count;
   }
  }
  cluster=next_cluster(cluster);if(last_error||cluster>=EOC)return count;
 }
 last_error=2;return count;
}
uint16_t fat32_read_next_sector(fat32_reader_t *r,uint8_t *buffer) {
 uint16_t valid=512;
 last_error=0;
 if(r->error||!r->bytes_left)return 0;
 if(r->sector_in_cluster==sectors_per_cluster) {
  if(!r->clusters_left||!--r->clusters_left){r->error=1;last_error=2;return 0;}
  r->cluster=next_cluster(r->cluster);r->sector_in_cluster=0;
  if(last_error){r->error=1;return 0;}
 }
 if(!valid_cluster(r->cluster)){r->error=1;last_error=2;return 0;}
 if(!disk_read(cluster_sector(r->cluster)+r->sector_in_cluster,buffer)){r->error=1;return 0;}
 r->sector_in_cluster++;
 if(r->bytes_left<512)valid=(uint16_t)r->bytes_left;
 r->bytes_left-=valid;return valid;
}
