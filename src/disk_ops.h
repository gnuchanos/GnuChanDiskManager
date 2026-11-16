#ifndef GNUCHAN_DISK_OPS_H
#define GNUCHAN_DISK_OPS_H

#include <stddef.h>

#define DM_FIELD_SIZE 256
#define DM_MAX_DEVICES 256

typedef struct {
    char path[DM_FIELD_SIZE];
    char name[DM_FIELD_SIZE];
    char size[DM_FIELD_SIZE];
    char type[DM_FIELD_SIZE];
    char filesystem[DM_FIELD_SIZE];
    char label[DM_FIELD_SIZE];
    char mountpoint[DM_FIELD_SIZE];
    char model[DM_FIELD_SIZE];
    char parent[DM_FIELD_SIZE];
    char partition_number[DM_FIELD_SIZE];
    char uuid[DM_FIELD_SIZE];
    int removable;
    int readonly;
} DmDevice;

typedef struct {
    DmDevice *devices;
    size_t count;
} DmDeviceList;

typedef struct {
    size_t device_index;
    unsigned int depth;
} DmTreeRow;

int dm_parse_lsblk_line(const char *line, DmDevice *device);
int dm_scan_devices(DmDeviceList *list, char *error, size_t error_size);
void dm_free_devices(DmDeviceList *list);
size_t dm_build_device_tree(const DmDeviceList *list, DmTreeRow *rows,
                           size_t capacity);
int dm_run_privileged(const char *const arguments[], char *output,
                      size_t output_size, char *error, size_t error_size);
int dm_valid_label(const char *label);
int dm_valid_partition_range(const char *start, const char *end);
int dm_supported_disk(const DmDevice *device);
int dm_supported_partition(const DmDeviceList *list, const DmDevice *device);
int dm_device_is_mounted(const DmDevice *device);
int dm_disk_has_mounted_partitions(const DmDeviceList *list,
                                   const DmDevice *disk);
const DmDevice *dm_find_disk(const DmDeviceList *list, const DmDevice *device);

#endif
