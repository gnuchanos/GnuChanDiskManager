#include "disk_ops.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static void test_parse_lsblk_line(void) {
    const char *line =
        "PATH=\"/dev/nvme0n1p1\" NAME=\"nvme0n1p1\" SIZE=\"1073741824\" "
        "TYPE=\"part\" FSTYPE=\"ext4\" LABEL=\"Work\\x20Disk\" "
        "MOUNTPOINTS=\"/media/alex/My\\x20Disk\" MODEL=\"\" "
        "PKNAME=\"nvme0n1\" PARTN=\"1\" RM=\"0\" RO=\"0\"";
    DmDevice device;
    assert(dm_parse_lsblk_line(line, &device));
    assert(strcmp(device.path, "/dev/nvme0n1p1") == 0);
    assert(strcmp(device.filesystem, "ext4") == 0);
    assert(strcmp(device.label, "Work Disk") == 0);
    assert(strcmp(device.mountpoint, "/media/alex/My Disk") == 0);
    assert(strcmp(device.parent, "nvme0n1") == 0);
    assert(strcmp(device.partition_number, "1") == 0);
    assert(!device.removable);
    assert(!device.readonly);
}

static void test_format_validation(void) {
    assert(dm_valid_label("Data_01"));
    assert(dm_valid_label("a.b-c"));
    assert(!dm_valid_label(""));
    assert(!dm_valid_label("this-label-is-too-long"));
    assert(!dm_valid_label("contains space"));
    assert(!dm_valid_label("a;rm"));

    assert(dm_valid_partition_range("1MiB", "100MiB"));
    assert(dm_valid_partition_range("1GiB", "2GiB"));
    assert(dm_valid_partition_range("1MiB", "2GiB"));
    assert(!dm_valid_partition_range("1MiB", "1MiB"));
    assert(!dm_valid_partition_range("2GiB", "1GiB"));
    assert(!dm_valid_partition_range("0MiB", "1GiB"));
    assert(!dm_valid_partition_range("1MiB;bad", "2GiB"));
    assert(!dm_valid_partition_range("1MiB", "2GiB extra"));
}

static void test_disk_relationships(void) {
    DmDevice devices[] = {
        {.path = "/dev/sda", .name = "sda", .type = "disk"},
        {.path = "/dev/sda1", .name = "sda1", .type = "part",
         .parent = "sda", .mountpoint = "/"},
        {.path = "/dev/sdb", .name = "sdb", .type = "disk"},
        {.path = "/dev/sdb1", .name = "sdb1", .type = "part",
         .parent = "sdb"},
        {.path = "/dev/sdc", .name = "sdc", .type = "disk"},
        {.path = "/dev/sdc2", .name = "sdc2", .type = "part",
         .parent = "sdc"},
        {.path = "/dev/dm-0", .name = "dm-0", .type = "lvm",
         .parent = "sdc2"},
        {.path = "/dev/mapper/data", .name = "data", .type = "lvm",
         .parent = "dm-0", .mountpoint = "/home"}
    };
    DmDeviceList list = {.devices = devices, .count = 8};
    assert(dm_find_disk(&list, &devices[1]) == &devices[0]);
    assert(dm_find_disk(&list, &devices[3]) == &devices[2]);
    assert(dm_find_disk(&list, &devices[7]) == &devices[4]);
    assert(dm_device_is_mounted(&devices[1]));
    assert(!dm_device_is_mounted(&devices[3]));
    assert(dm_disk_has_mounted_partitions(&list, &devices[0]));
    assert(!dm_disk_has_mounted_partitions(&list, &devices[2]));
    assert(dm_disk_has_mounted_partitions(&list, &devices[4]));

    DmTreeRow rows[8];
    assert(dm_build_device_tree(&list, rows, 8) == 8);
    assert(rows[0].device_index == 0 && rows[0].depth == 0);
    assert(rows[1].device_index == 1 && rows[1].depth == 1);
    assert(rows[2].device_index == 2 && rows[2].depth == 0);
    assert(rows[3].device_index == 3 && rows[3].depth == 1);
    assert(rows[4].device_index == 4 && rows[4].depth == 0);
    assert(rows[5].device_index == 5 && rows[5].depth == 1);
    assert(rows[6].device_index == 6 && rows[6].depth == 2);
    assert(rows[7].device_index == 7 && rows[7].depth == 3);
}

static void test_supported_device_names(void) {
    DmDevice device = {.path = "/dev/sda", .name = "sda", .type = "disk"};
    assert(dm_supported_disk(&device));
    strcpy(device.name, "sda1");
    assert(!dm_supported_disk(&device));
    strcpy(device.name, "loop0");
    strcpy(device.path, "/dev/loop0");
    assert(!dm_supported_disk(&device));
    strcpy(device.name, "nvme1n1");
    strcpy(device.path, "/dev/nvme1n1");
    assert(dm_supported_disk(&device));
}

static void test_scan_devices(void) {
    DmDeviceList list = {0};
    char error[512] = {0};
    assert(dm_scan_devices(&list, error, sizeof(error)));
    assert(error[0] == '\0');
    assert(list.count > 0);
    for (size_t i = 0; i < list.count; i++) {
        assert(list.devices[i].path[0] == '/');
    }
    dm_free_devices(&list);
    assert(list.devices == NULL);
    assert(list.count == 0);
}

int main(void) {
    test_parse_lsblk_line();
    test_format_validation();
    test_disk_relationships();
    test_supported_device_names();
    test_scan_devices();
    puts("disk operations tests passed");
    return 0;
}
