#include "disk_ops.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

static void copy_field(char *destination, size_t destination_size,
                       const char *value) {
    if (destination_size == 0) {
        return;
    }
    snprintf(destination, destination_size, "%s", value);
}

static int parse_pair_value(const char **cursor, char *value,
                            size_t value_size) {
    size_t used = 0;
    if (**cursor != '"') {
        return 0;
    }
    (*cursor)++;
    while (**cursor != '\0' && **cursor != '"') {
        unsigned char character = (unsigned char)*(*cursor)++;
        if (character == '\\' && **cursor != '\0') {
            if ((*cursor)[0] == 'x' &&
                (*cursor)[1] != '\0' && (*cursor)[2] != '\0' &&
                isxdigit((unsigned char)(*cursor)[1]) &&
                isxdigit((unsigned char)(*cursor)[2])) {
                char hex[3] = {(*cursor)[1], (*cursor)[2], '\0'};
                character = (unsigned char)strtoul(hex, NULL, 16);
                *cursor += 3;
            } else {
                character = (unsigned char)*(*cursor)++;
            }
        }
        if (used + 1 < value_size) {
            value[used++] = (char)character;
        }
    }
    if (**cursor != '"') {
        return 0;
    }
    (*cursor)++;
    value[used] = '\0';
    return 1;
}

int dm_parse_lsblk_line(const char *line, DmDevice *device) {
    if (line == NULL || device == NULL) {
        return 0;
    }
    memset(device, 0, sizeof(*device));
    const char *cursor = line;
    while (*cursor != '\0') {
        while (isspace((unsigned char)*cursor)) {
            cursor++;
        }
        if (*cursor == '\0') {
            break;
        }
        const char *key_start = cursor;
        while (*cursor != '\0' && *cursor != '=' &&
               !isspace((unsigned char)*cursor)) {
            cursor++;
        }
        if (*cursor != '=') {
            return 0;
        }
        size_t key_length = (size_t)(cursor - key_start);
        cursor++;
        char value[DM_FIELD_SIZE];
        if (!parse_pair_value(&cursor, value, sizeof(value))) {
            return 0;
        }

        if (key_length == 4 && strncmp(key_start, "PATH", key_length) == 0) {
            copy_field(device->path, sizeof(device->path), value);
        } else if (key_length == 4 &&
                   strncmp(key_start, "NAME", key_length) == 0) {
            copy_field(device->name, sizeof(device->name), value);
        } else if (key_length == 4 &&
                   strncmp(key_start, "SIZE", key_length) == 0) {
            copy_field(device->size, sizeof(device->size), value);
        } else if (key_length == 4 &&
                   strncmp(key_start, "TYPE", key_length) == 0) {
            copy_field(device->type, sizeof(device->type), value);
        } else if (key_length == 6 &&
                   strncmp(key_start, "FSTYPE", key_length) == 0) {
            copy_field(device->filesystem, sizeof(device->filesystem), value);
        } else if (key_length == 5 &&
                   strncmp(key_start, "LABEL", key_length) == 0) {
            copy_field(device->label, sizeof(device->label), value);
        } else if (key_length == 11 &&
                   strncmp(key_start, "MOUNTPOINTS", key_length) == 0) {
            copy_field(device->mountpoint, sizeof(device->mountpoint), value);
        } else if (key_length == 4 &&
                   strncmp(key_start, "UUID", key_length) == 0) {
            copy_field(device->uuid, sizeof(device->uuid), value);
        } else if (key_length == 5 &&
                   strncmp(key_start, "MODEL", key_length) == 0) {
            copy_field(device->model, sizeof(device->model), value);
        } else if (key_length == 6 &&
                   strncmp(key_start, "PKNAME", key_length) == 0) {
            copy_field(device->parent, sizeof(device->parent), value);
        } else if (key_length == 5 &&
                   strncmp(key_start, "PARTN", key_length) == 0) {
            copy_field(device->partition_number,
                       sizeof(device->partition_number), value);
        } else if (key_length == 2 && strncmp(key_start, "RM", key_length) == 0) {
            device->removable = strcmp(value, "1") == 0;
        } else if (key_length == 2 && strncmp(key_start, "RO", key_length) == 0) {
            device->readonly = strcmp(value, "1") == 0;
        }
    }
    return device->path[0] == '/' && device->type[0] != '\0';
}

static int read_command(const char *const arguments[], char *output,
                        size_t output_size, char *error, size_t error_size) {
    int pipe_fds[2];
    if (pipe(pipe_fds) != 0) {
        snprintf(error, error_size, "pipe: %s", strerror(errno));
        return 0;
    }
    pid_t child = fork();
    if (child < 0) {
        int saved_errno = errno;
        close(pipe_fds[0]);
        close(pipe_fds[1]);
        snprintf(error, error_size, "fork: %s", strerror(saved_errno));
        return 0;
    }
    if (child == 0) {
        close(pipe_fds[0]);
        if (dup2(pipe_fds[1], STDOUT_FILENO) < 0 ||
            dup2(pipe_fds[1], STDERR_FILENO) < 0) {
            _exit(126);
        }
        close(pipe_fds[1]);
        execvp(arguments[0], (char *const *)arguments);
        dprintf(STDERR_FILENO, "%s: %s\n", arguments[0], strerror(errno));
        _exit(127);
    }

    close(pipe_fds[1]);
    size_t used = 0;
    int truncated = 0;
    char buffer[1024];
    ssize_t count;
    while ((count = read(pipe_fds[0], buffer, sizeof(buffer))) > 0) {
        if (output != NULL && output_size > 0 && used + 1 < output_size) {
            size_t available = output_size - used - 1;
            size_t amount = (size_t)count < available ? (size_t)count : available;
            memcpy(output + used, buffer, amount);
            used += amount;
            output[used] = '\0';
            truncated = amount < (size_t)count;
        } else if (count > 0) {
            truncated = 1;
        }
    }
    int read_errno = errno;
    close(pipe_fds[0]);
    int status = 0;
    while (waitpid(child, &status, 0) < 0) {
        if (errno != EINTR) {
            snprintf(error, error_size, "waitpid: %s", strerror(errno));
            return 0;
        }
    }
    if (count < 0) {
        snprintf(error, error_size, "read: %s", strerror(read_errno));
        return 0;
    }
    if (truncated) {
        snprintf(error, error_size, "Command output exceeded the safe limit.");
        return 0;
    }
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        if (output != NULL && output[0] != '\0') {
            snprintf(error, error_size, "%s", output);
        } else if (WIFSIGNALED(status)) {
            snprintf(error, error_size, "Command terminated by signal %d.",
                     WTERMSIG(status));
        } else {
            snprintf(error, error_size, "Command failed with status %d.",
                     WEXITSTATUS(status));
        }
        return 0;
    }
    return 1;
}

int dm_scan_devices(DmDeviceList *list, char *error, size_t error_size) {
    if (list == NULL || error == NULL || error_size == 0) {
        return 0;
    }
    list->devices = NULL;
    list->count = 0;
    error[0] = '\0';
    const char *arguments[] = {
        "/usr/bin/lsblk", "--pairs", "--bytes", "--output",
        "PATH,NAME,SIZE,TYPE,FSTYPE,LABEL,MOUNTPOINTS,MODEL,PKNAME,PARTN,UUID,RM,RO",
        NULL
    };
    char output[65536] = {0};
    if (!read_command(arguments, output, sizeof(output), error, error_size)) {
        return 0;
    }
    DmDevice *devices = calloc(DM_MAX_DEVICES, sizeof(*devices));
    if (devices == NULL) {
        snprintf(error, error_size, "Could not allocate the device list.");
        return 0;
    }
    char *save = NULL;
    for (char *line = strtok_r(output, "\n", &save); line != NULL;
         line = strtok_r(NULL, "\n", &save)) {
        DmDevice device;
        if (dm_parse_lsblk_line(line, &device)) {
            if (list->count == DM_MAX_DEVICES) {
                free(devices);
                snprintf(error, error_size, "Too many block devices.");
                return 0;
            }
            devices[list->count++] = device;
        }
    }
    list->devices = devices;
    return 1;
}

void dm_free_devices(DmDeviceList *list) {
    if (list != NULL) {
        free(list->devices);
        list->devices = NULL;
        list->count = 0;
    }
}

static size_t append_tree_branch(const DmDeviceList *list, size_t parent,
                                 unsigned int depth, DmTreeRow *rows,
                                 size_t capacity, unsigned char *visited,
                                 size_t count) {
    if (depth >= DM_MAX_DEVICES) {
        return count;
    }
    const char *parent_name = list->devices[parent].name;
    for (size_t i = 0; i < list->count && count < capacity; i++) {
        if (visited[i] || list->devices[i].parent[0] == '\0' ||
            strcmp(list->devices[i].parent, parent_name) != 0) {
            continue;
        }
        visited[i] = 1;
        rows[count++] = (DmTreeRow){.device_index = i, .depth = depth};
        count = append_tree_branch(list, i, depth + 1, rows, capacity,
                                   visited, count);
    }
    return count;
}

size_t dm_build_device_tree(const DmDeviceList *list, DmTreeRow *rows,
                            size_t capacity) {
    if (list == NULL || rows == NULL || capacity == 0 ||
        list->devices == NULL || list->count == 0) {
        return 0;
    }
    unsigned char visited[DM_MAX_DEVICES] = {0};
    size_t count = 0;
    size_t limit = list->count < DM_MAX_DEVICES ? list->count : DM_MAX_DEVICES;
    for (size_t i = 0; i < limit && count < capacity; i++) {
        if (visited[i] || list->devices[i].parent[0] != '\0') {
            continue;
        }
        visited[i] = 1;
        rows[count++] = (DmTreeRow){.device_index = i, .depth = 0};
        count = append_tree_branch(list, i, 1, rows, capacity, visited, count);
    }
    for (size_t i = 0; i < limit && count < capacity; i++) {
        if (!visited[i]) {
            visited[i] = 1;
            rows[count++] = (DmTreeRow){.device_index = i, .depth = 0};
            count = append_tree_branch(list, i, 1, rows, capacity, visited,
                                       count);
        }
    }
    return count;
}

int dm_run_privileged(const char *const arguments[], char *output,
                      size_t output_size, char *error, size_t error_size) {
    if (arguments == NULL || arguments[0] == NULL ||
        error == NULL || error_size == 0) {
        return 0;
    }
    error[0] = '\0';
    if (output != NULL && output_size > 0) {
        output[0] = '\0';
    }
    if (geteuid() == 0) {
        return read_command(arguments, output, output_size, error, error_size);
    }
    if (access("/usr/bin/pkexec", X_OK) != 0) {
        snprintf(error, error_size, "pkexec is unavailable: %s",
                 strerror(errno));
        return 0;
    }
    const char *pkexec_arguments[32];
    size_t count = 0;
    pkexec_arguments[count++] = "/usr/bin/pkexec";
    for (size_t i = 0; arguments[i] != NULL; i++) {
        if (count + 1 >= sizeof(pkexec_arguments) / sizeof(pkexec_arguments[0])) {
            snprintf(error, error_size, "Too many command arguments.");
            return 0;
        }
        pkexec_arguments[count++] = arguments[i];
    }
    pkexec_arguments[count] = NULL;
    return read_command(pkexec_arguments, output, output_size, error, error_size);
}

int dm_valid_label(const char *label) {
    if (label == NULL) {
        return 0;
    }
    size_t length = strlen(label);
    if (length == 0 || length > 16) {
        return 0;
    }
    for (size_t i = 0; i < length; i++) {
        unsigned char character = (unsigned char)label[i];
        if (!((character >= 'A' && character <= 'Z') ||
              (character >= 'a' && character <= 'z') ||
              (character >= '0' && character <= '9')) &&
            character != '_' && character != '-' && character != '.') {
            return 0;
        }
    }
    return 1;
}

static int valid_mib_value(const char *value, double *number) {
    if (value == NULL || value[0] == '\0') {
        return 0;
    }
    errno = 0;
    char *end = NULL;
    double parsed = strtod(value, &end);
    if (end == value || errno == ERANGE || parsed <= 0.0) {
        return 0;
    }
    while (isspace((unsigned char)*end)) {
        end++;
    }
    if (strcmp(end, "MiB") != 0 && strcmp(end, "GiB") != 0) {
        return 0;
    }
    *number = strcmp(end, "GiB") == 0 ? parsed * 1024.0 : parsed;
    return *number > 0.0;
}

int dm_valid_partition_range(const char *start, const char *end) {
    double start_mib = 0.0;
    double end_mib = 0.0;
    return valid_mib_value(start, &start_mib) &&
           valid_mib_value(end, &end_mib) && end_mib > start_mib;
}

static int starts_with(const char *value, const char *prefix) {
    return strncmp(value, prefix, strlen(prefix)) == 0;
}

int dm_supported_disk(const DmDevice *device) {
    if (device == NULL || strcmp(device->type, "disk") != 0 ||
        device->path[0] != '/' || device->readonly) {
        return 0;
    }
    const char *name = device->name;
    if (starts_with(name, "sd") || starts_with(name, "hd") ||
        starts_with(name, "vd") || starts_with(name, "xvd")) {
        const char *suffix = name + (starts_with(name, "xvd") ? 3 : 2);
        if (*suffix == '\0') {
            return 0;
        }
        for (; *suffix != '\0'; suffix++) {
            if (!islower((unsigned char)*suffix)) {
                return 0;
            }
        }
        return 1;
    }
    if (starts_with(name, "nvme")) {
        const char *suffix = name + 4;
        if (!isdigit((unsigned char)*suffix)) {
            return 0;
        }
        while (isdigit((unsigned char)*suffix)) {
            suffix++;
        }
        if (*suffix++ != 'n' || !isdigit((unsigned char)*suffix)) {
            return 0;
        }
        while (isdigit((unsigned char)*suffix)) {
            suffix++;
        }
        return *suffix == '\0';
    }
    if (starts_with(name, "mmcblk")) {
        const char *suffix = name + 6;
        if (!isdigit((unsigned char)*suffix)) {
            return 0;
        }
        while (isdigit((unsigned char)*suffix)) {
            suffix++;
        }
        return *suffix == '\0';
    }
    return 0;
}

int dm_supported_partition(const DmDeviceList *list,
                           const DmDevice *device) {
    if (device == NULL || strcmp(device->type, "part") != 0 ||
        device->readonly || device->partition_number[0] == '\0') {
        return 0;
    }
    for (const char *number = device->partition_number; *number != '\0';
         number++) {
        if (!isdigit((unsigned char)*number)) {
            return 0;
        }
    }
    const DmDevice *disk = dm_find_disk(list, device);
    return dm_supported_disk(disk);
}

int dm_device_is_mounted(const DmDevice *device) {
    return device != NULL && device->mountpoint[0] != '\0';
}

const DmDevice *dm_find_disk(const DmDeviceList *list,
                             const DmDevice *device) {
    if (list == NULL || device == NULL) {
        return NULL;
    }
    if (strcmp(device->type, "disk") == 0) {
        return device;
    }
    const char *parent_name = device->parent;
    for (size_t depth = 0; depth < list->count && parent_name[0] != '\0';
         depth++) {
        const DmDevice *parent = NULL;
        for (size_t i = 0; i < list->count; i++) {
            if (strcmp(list->devices[i].name, parent_name) == 0) {
                parent = &list->devices[i];
                break;
            }
        }
        if (parent == NULL) {
            return NULL;
        }
        if (strcmp(parent->type, "disk") == 0) {
            return parent;
        }
        parent_name = parent->parent;
    }
    return NULL;
}

int dm_disk_has_mounted_partitions(const DmDeviceList *list,
                                   const DmDevice *disk) {
    if (list == NULL || disk == NULL || strcmp(disk->type, "disk") != 0) {
        return 1;
    }
    for (size_t i = 0; i < list->count; i++) {
        const DmDevice *device = &list->devices[i];
        const DmDevice *parent_disk = dm_find_disk(list, device);
        if (parent_disk != NULL &&
            strcmp(parent_disk->path, disk->path) == 0 &&
            dm_device_is_mounted(device)) {
            return 1;
        }
    }
    return 0;
}
