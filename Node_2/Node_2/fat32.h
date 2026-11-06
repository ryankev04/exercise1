#ifndef FAT32_H_
#define FAT32_H_
#include <stdint.h>
/* Read-only FAT32, 512-byte sectors. Path components support ASCII long
 * filenames up to 63 characters and short aliases; matching ignores ASCII case.
 * No allocation, writes, Unicode conversion or recursive directory listing. */
typedef struct {
 char name[9]; /* short base-name for legacy root listing */
 uint32_t first_cluster;
 uint32_t size;
} fat32_file_t;
typedef struct {
 uint32_t cluster;
 uint8_t sector_in_cluster;
 uint32_t bytes_left;
 uint8_t error;
 uint32_t clusters_left; /* bounds corrupt/cyclic file chains */
} fat32_reader_t;
uint8_t fat32_mount(void);
/* 1 found, 0 missing/error. Examples: MUSIC/kubbi_firelight.wav,
 * SFX/q3_enter.wav. No '.', '..', repeated/trailing slashes or absolute paths. */
uint8_t fat32_find_path(const char *path, fat32_file_t *file);
/* 0 = none/not found, 1 = SD read error, 2 = invalid metadata/path/limit. */
uint8_t fat32_last_error(void);
uint8_t fat32_find_files(const char *extension, fat32_file_t files[], uint8_t max_files);
/* Diagnostic listing of a single folder below root, at most64 regular files.
 * Callback name is temporary; no filesystem re-entry from callback. */
uint8_t fat32_list_root_folder(const char *folder,void (*entry)(const char *,uint32_t));
void fat32_open(fat32_reader_t *reader, const fat32_file_t *file);
uint16_t fat32_read_next_sector(fat32_reader_t *reader, uint8_t *buffer);
#endif
