/* Read-only FAT32 adapted from the group's helper. Directory traversal is
 * bounded, and LFN fragments are accepted only with a complete descending
 * ordinal chain and the checksum of the following short entry. */
#include <string.h>
#include "fat32.h"
#include "sd_card.h"
#define FAT_END_OF_CHAIN 0x0ffffff8u
#define NAME_MAX 63u
#define DIRECTORY_CLUSTER_LIMIT 4096u
static uint32_t fat_start, data_start, root_cluster, cluster_count, fat_sectors;
static uint8_t sectors_per_cluster, mounted, last_error;
static uint8_t fat_buffer[512], folder_buffer[512];
static uint32_t fat_buffer_sector = 0xffffffffu;
static uint16_t read_le16(const uint8_t *p) {
    return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}

static uint32_t read_le32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint8_t disk_read(uint32_t sector, uint8_t *p) {
    if (!sd_read_sector(sector, p)) {
        last_error = 1;
        return 0;
    }
    return 1;
}

uint8_t fat32_last_error(void) {
    return last_error;
}

static uint8_t valid_cluster(uint32_t c) {
    return c >= 2 && c - 2 < cluster_count && c < 0x0ffffff0u;
}

static uint32_t cluster_sector(uint32_t c) {
    return data_start + (c - 2) * sectors_per_cluster;
}

static uint32_t next_cluster(uint32_t c) {
    uint32_t offset = c / 128, sector = fat_start + offset;
    if (!valid_cluster(c) || offset >= fat_sectors) {
        last_error = 2;
        return 0;
    }
    if (sector != fat_buffer_sector) {
        fat_buffer_sector = 0xffffffffu;
        if (!disk_read(sector, fat_buffer)) {
            return 0;
        }
        fat_buffer_sector = sector;
    }
    return read_le32(fat_buffer + 4 * (c % 128)) & 0x0fffffffu;
}

static uint8_t boot_signature(const uint8_t *p) {
    return p[510] == 0x55 && p[511] == 0xaa;
}

static uint8_t looks_like_bpb(const uint8_t *p) {
    return boot_signature(p) && read_le16(p + 11) == 512 && p[13] && !(p[13] & (p[13] - 1)) && p[13] <= 128 &&
           read_le16(p + 14) && p[16] && p[16] <= 2 && !read_le16(p + 17) && !read_le16(p + 22) && read_le32(p + 36) &&
           !read_le16(p + 42);
}
/************************************************************************/
/*
 * Do not depend on the informational 'FAT32   ' label. Validate the BPB.
 */
/************************************************************************/

uint8_t fat32_mount(void) {
    uint8_t *boot_sector = fat_buffer;
    uint32_t partition_start = 0, total_sectors, metadata_sectors, clusters, sectors_per_fat;
    uint16_t fat_flags;
    mounted = 0;
    last_error = 0;
    fat_buffer_sector = 0xffffffffu;
    if (!disk_read(0, boot_sector)) {
        return 0;
    }
    if (!looks_like_bpb(boot_sector)) {
        uint8_t i, found = 0;
        if (!boot_signature(boot_sector)) {
            last_error = 2;
            return 0;
        }
        for (i = 0; i < 4; i++) {
            const uint8_t *entry = boot_sector + 446 + 16 * i;
            if (entry[4] == 0x0b || entry[4] == 0x0c) {
                partition_start = read_le32(entry + 8);
                found = 1;
                break;
            }
        }
        if (!found || !partition_start) {
            last_error = 2;
            return 0;
        }
        if (!disk_read(partition_start, boot_sector)) {
            return 0;
        }
        if (!looks_like_bpb(boot_sector)) {
            last_error = 2;
            return 0;
        }
    }
    total_sectors = read_le16(boot_sector + 19);
    if (!total_sectors) {
        total_sectors = read_le32(boot_sector + 32);
    }
    sectors_per_fat = read_le32(boot_sector + 36);
    if (sectors_per_fat > (0xffffffffu - read_le16(boot_sector + 14)) / boot_sector[16]) {
        last_error = 2;
        return 0;
    }
    metadata_sectors = read_le16(boot_sector + 14) + (uint32_t)boot_sector[16] * sectors_per_fat;
    if (total_sectors <= metadata_sectors || partition_start > 0xffffffffu - total_sectors) {
        last_error = 2;
        return 0;
    }
    clusters = (total_sectors - metadata_sectors) / boot_sector[13];
    /*
     * FAT32 requires >=65525 data clusters and enough FAT entries for them.
     */
    if (clusters < 65525u || clusters >= 0x0fffffeeu || sectors_per_fat < (clusters + 2u + 127u) / 128u) {
        last_error = 2;
        return 0;
    }
    root_cluster = read_le32(boot_sector + 44) & 0x0fffffffu;
    if (root_cluster < 2 || root_cluster - 2 >= clusters) {
        last_error = 2;
        return 0;
    }
    sectors_per_cluster = boot_sector[13];
    cluster_count = clusters;
    fat_sectors = sectors_per_fat;
    fat_start = partition_start + read_le16(boot_sector + 14);
    data_start = partition_start + metadata_sectors;
    fat_flags = read_le16(boot_sector + 40);
    if (fat_flags & 0x80u) {
        uint8_t active = (uint8_t)(fat_flags & 15u);
        if (active >= boot_sector[16]) {
            last_error = 2;
            return 0;
        }
        fat_start += (uint32_t)active * sectors_per_fat;
    }
    mounted = 1;
    return 1;
}

static uint8_t lower(uint8_t c) {
    return c >= 'A' && c <= 'Z' ? (uint8_t)(c + 32) : c;
}

static uint8_t names_match(const char *a, const char *b) {
    while (*a && *b) {
        if (lower((uint8_t)*a++) != lower((uint8_t)*b++)) {
            return 0;
        }
    }
    return *a == *b;
}

static void read_short_name(char *out, const uint8_t *e, uint8_t with_extension) {
    uint8_t i, n = 8, k = 0;
    while (n && e[n - 1] == ' ') {
        n--;
    }
    for (i = 0; i < n; i++) {
        out[k++] = (char)e[i];
    }
    if (with_extension && e[8] != ' ') {
        out[k++] = '.';
        for (i = 8; i < 11 && e[i] != ' '; i++) {
            out[k++] = (char)e[i];
        }
    }
    out[k] = 0;
}

static uint8_t short_name_checksum(const uint8_t *e) {
    uint8_t sum = 0, i;
    for (i = 0; i < 11; i++) {
        sum = (uint8_t)(((sum & 1u) ? 128u : 0u) + (sum >> 1) + e[i]);
    }
    return sum;
}
typedef struct {
    char text[66];
    uint8_t expected, sum, active, slots;
} long_name_t;
static void read_long_name_fragment(long_name_t *long_name, const uint8_t *entry) {
    static const uint8_t positions[13] = {1, 3, 5, 7, 9, 14, 16, 18, 20, 22, 24, 28, 30};
    uint8_t ordinal = entry[0] & 31u, i;
    uint16_t offset;
    if (entry[0] & 0x40u) {
        memset(long_name, 0, sizeof *long_name);
        if (!ordinal || ordinal > 5u) {
            return;
        }
        memset(long_name->text, 0xff, sizeof long_name->text);
        long_name->active = 1;
        long_name->expected = ordinal;
        long_name->sum = entry[13];
        long_name->slots = (uint8_t)(ordinal * 13u);
    }
    if (!long_name->active || !ordinal || ordinal != long_name->expected || entry[13] != long_name->sum || entry[12] || read_le16(entry + 26) ||
        (entry[0] & 0xa0u)) {
        long_name->active = 0;
        return;
    }
    offset = (uint16_t)(ordinal - 1) * 13;
    for (i = 0; i < 13; i++) {
        uint16_t character = read_le16(entry + positions[i]);
        uint16_t index = offset + i;
        if (character == 0) {
            long_name->text[index] = 0;
            continue;
        }
        if (character == 0xffffu) {
            continue;
        }
        if (index >= NAME_MAX || character < 32 || character > 126 || character == '/' || character == '\\') {
            long_name->active = 0;
            return;
        }
        long_name->text[index] = (char)character;
    }
    long_name->expected--;
}

static uint8_t valid_long_name(long_name_t *l) {
    uint8_t i, end = 0;
    for (i = 0; i < l->slots; i++) {
        uint8_t c = (uint8_t)l->text[i];
        if (!end) {
            if (!c) {
                end = 1;
                continue;
            }
            if (c == 255u || i >= NAME_MAX) {
                return 0;
            }
        } else if (c != 255u) {
            return 0;
        }
    }
    if (!end) {
        if (l->slots > NAME_MAX) {
            return 0;
        }
        l->text[l->slots] = 0;
    }
    return 1;
}

static uint32_t entry_cluster(const uint8_t *e) {
    return (((uint32_t)read_le16(e + 20) << 16) | read_le16(e + 26)) & 0x0fffffffu;
}

static void assign_file(fat32_file_t *f, const uint8_t *e) {
    read_short_name(f->name, e, 0);
    f->first_cluster = entry_cluster(e);
    f->size = read_le32(e + 28);
}

static uint8_t directory_find(uint32_t cluster, const char *name, uint8_t desired_directory,
                              fat32_file_t *file) {
    uint32_t budget = cluster_count < DIRECTORY_CLUSTER_LIMIT ? cluster_count : DIRECTORY_CLUSTER_LIMIT;
    long_name_t long_name;
    memset(&long_name, 0, sizeof long_name);
    while (valid_cluster(cluster) && budget--) {
        uint8_t sector_index;
        for (sector_index = 0; sector_index < sectors_per_cluster; sector_index++) {
            uint16_t entry_offset;
            if (!disk_read(cluster_sector(cluster) + sector_index, folder_buffer)) {
                return 0;
            }
            for (entry_offset = 0; entry_offset < 512; entry_offset += 32) {
                const uint8_t *entry = folder_buffer + entry_offset;
                char alias[13];
                uint8_t match;
                if (!entry[0]) {
                    return 0;
                }
                if (entry[0] == 0xe5) {
                    long_name.active = 0;
                    continue;
                }
                if (entry[11] == 0x0f) {
                    read_long_name_fragment(&long_name, entry);
                    continue;
                }
                read_short_name(alias, entry, 1);
                match = names_match(alias, name) || (long_name.active && !long_name.expected && long_name.sum == short_name_checksum(entry) &&
                                              valid_long_name(&long_name) && names_match(long_name.text, name));
                long_name.active = 0;
                if ((entry[11] & 0x0eu) || !!(entry[11] & 0x10u) != desired_directory || !match) {
                    continue;
                }
                assign_file(file, entry);
                if (!valid_cluster(file->first_cluster) && file->size) {
                    last_error = 2;
                    return 0;
                }
                if (desired_directory && !valid_cluster(file->first_cluster)) {
                    last_error = 2;
                    return 0;
                }
                return 1;
            }
        }
        cluster = next_cluster(cluster);
        if (last_error) {
            return 0;
        }
        if (cluster >= FAT_END_OF_CHAIN) {
            return 0;
        }
    }
    last_error = 2;
    return 0;
}
/************************************************************************/
/*
 * desired_directory separates a real folder component from the final file.
 */
/************************************************************************/

uint8_t fat32_find_path(const char *path, fat32_file_t *file) {
    uint32_t dir = root_cluster;
    uint8_t depth = 0;
    fat32_file_t found;
    last_error = 0;
    if (!mounted || !path || !file || !*path) {
        last_error = 2;
        return 0;
    }
    while (*path) {
        char name[NAME_MAX + 1];
        uint8_t n = 0, more;
        while (*path && *path != '/') {
            uint8_t c = (uint8_t)*path++;
            if (n == NAME_MAX || c < 32 || c > 126 || c == '\\') {
                last_error = 2;
                return 0;
            }
            name[n++] = (char)c;
        }
        name[n] = 0;
        more = *path == '/';
        if (!n || names_match(name, ".") || names_match(name, "..") || ++depth > 8) {
            last_error = 2;
            return 0;
        }
        if (more && !*++path) {
            last_error = 2;
            return 0;
        }
        if (!directory_find(dir, name, more, &found)) {
            return 0;
        }
        if (!more) {
            *file = found;
            return 1;
        }
        dir = found.first_cluster;
    }
    return 0;
}

uint8_t fat32_find_files(const char *extension, fat32_file_t files[], uint8_t max_files) {
    uint32_t c = root_cluster,
             budget = cluster_count < DIRECTORY_CLUSTER_LIMIT ? cluster_count : DIRECTORY_CLUSTER_LIMIT;
    uint8_t count = 0;
    last_error = 0;
    if (!mounted || !extension || !files || !max_files) {
        return 0;
    }
    while (valid_cluster(c) && budget--) {
        uint8_t s;
        for (s = 0; s < sectors_per_cluster; s++) {
            uint16_t off;
            if (!disk_read(cluster_sector(c) + s, folder_buffer)) {
                return count;
            }
            for (off = 0; off < 512; off += 32) {
                const uint8_t *e = folder_buffer + off;
                if (!e[0]) {
                    return count;
                }
                if (e[0] == 0xe5 || (e[11] & 0x1eu) || e[11] == 0x0f || memcmp(e + 8, extension, 3)) {
                    continue;
                }
                assign_file(files + count, e);
                if (++count == max_files) {
                    return count;
                }
            }
        }
        c = next_cluster(c);
        if (last_error || c >= FAT_END_OF_CHAIN) {
            return count;
        }
    }
    last_error = 2;
    return count;
}
/************************************************************************/
/*
 * Compatibility root listing uses short aliases; new playback uses paths.
 */
/************************************************************************/

void fat32_open(fat32_reader_t *reader, const fat32_file_t *file) {
    last_error = 0;
    reader->cluster = file->first_cluster;
    reader->sector_in_cluster = 0;
    reader->bytes_left = file->size;
    reader->error = !mounted;
    reader->clusters_left = cluster_count;
}

uint8_t fat32_list_root_folder(const char *folder, void (*entry)(const char *, uint32_t)) {
    fat32_file_t dir;
    long_name_t long_name;
    uint8_t count = 0;
    uint32_t cluster,
        budget = cluster_count < DIRECTORY_CLUSTER_LIMIT ? cluster_count : DIRECTORY_CLUSTER_LIMIT;
    last_error = 0;
    if (!mounted || !folder || !entry) {
        return 0;
    }
    if (!directory_find(root_cluster, folder, 1, &dir)) {
        return 0;
    }
    cluster = dir.first_cluster;
    memset(&long_name, 0, sizeof long_name);
    while (valid_cluster(cluster) && budget--) {
        uint8_t sector;
        for (sector = 0; sector < sectors_per_cluster; sector++) {
            uint16_t entry_offset;
            if (!disk_read(cluster_sector(cluster) + sector, folder_buffer)) {
                return count;
            }
            for (entry_offset = 0; entry_offset < 512; entry_offset += 32) {
                const uint8_t *directory_entry = folder_buffer + entry_offset;
                char alias[13];
                const char *name;
                if (!directory_entry[0]) {
                    return count;
                }
                if (directory_entry[0] == 0xe5) {
                    long_name.active = 0;
                    continue;
                }
                if (directory_entry[11] == 0x0f) {
                    read_long_name_fragment(&long_name, directory_entry);
                    continue;
                }
                read_short_name(alias, directory_entry, 1);
                name = alias;
                if (long_name.active && !long_name.expected && long_name.sum == short_name_checksum(directory_entry) && valid_long_name(&long_name)) {
                    name = long_name.text;
                }
                long_name.active = 0;
                if (directory_entry[11] & 0x1eu) {
                    continue;
                }
                entry(name, read_le32(directory_entry + 28));
                if (++count == 64) {
                    return count;
                }
            }
        }
        cluster = next_cluster(cluster);
        if (last_error || cluster >= FAT_END_OF_CHAIN) {
            return count;
        }
    }
    last_error = 2;
    return count;
}

uint16_t fat32_read_next_sector(fat32_reader_t *reader, uint8_t *buffer) {
    uint16_t valid_bytes = 512;
    last_error = 0;
    if (reader->error || !reader->bytes_left) {
        return 0;
    }
    if (reader->sector_in_cluster == sectors_per_cluster) {
        if (!reader->clusters_left || !--reader->clusters_left) {
            reader->error = 1;
            last_error = 2;
            return 0;
        }
        reader->cluster = next_cluster(reader->cluster);
        reader->sector_in_cluster = 0;
        if (last_error) {
            reader->error = 1;
            return 0;
        }
    }
    if (!valid_cluster(reader->cluster)) {
        reader->error = 1;
        last_error = 2;
        return 0;
    }
    if (!disk_read(cluster_sector(reader->cluster) + reader->sector_in_cluster, buffer)) {
        reader->error = 1;
        return 0;
    }
    reader->sector_in_cluster++;
    if (reader->bytes_left < 512) {
        valid_bytes = (uint16_t)reader->bytes_left;
    }
    reader->bytes_left -= valid_bytes;
    return valid_bytes;
}
