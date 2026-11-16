#include "disk_ops.h"

#include <X11/Xlib.h>
#include <X11/Xft/Xft.h>
#include <X11/keysym.h>
#include <X11/Xutil.h>
#include <ctype.h>
#include <errno.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#define TEXT_SIZE 512
#define BUTTON_COUNT 12

/* Base layout metrics */
#define HEADER_H          44
#define STATUS_H          26
#define TABLE_ROW_H       28
#define BAR_H             36
#define BUTTON_H          34
#define PAD               10
#define GAP               8

typedef struct {
    char title[TEXT_SIZE];
    char background[32];
    char panel[32];
    char panel_edge[32];
    char field[32];
    char text[32];
    char text_muted[32];
    char accent[32];
    char selection[32];
    char bar_ext[32];
    char bar_ntfs[32];
    char bar_fat[32];
    char bar_swap[32];
    char bar_other[32];
    char bar_unalloc[32];
    int width;
    int height;
    int font_size;
} Config;

typedef enum {
    ACTION_REFRESH,
    ACTION_MOUNT,
    ACTION_UNMOUNT,
    ACTION_LOGIN_AUTOMOUNT,
    ACTION_NEW_PARTITION,
    ACTION_DELETE_PARTITION,
    ACTION_FORMAT,
    ACTION_USER,
    ACTION_READ,
    ACTION_WRITE,
    ACTION_GPT,
    ACTION_MBR,
    ACTION_QUIT
} Action;

typedef struct {
    char label[64];
    Action action;
    int x;
    int y;
    int width;
} Button;

typedef struct {
    Display *display;
    int screen;
    Window window;
    Atom delete_window;
    Pixmap buffer;
    GC gc;
    XftDraw *draw;
    XftFont *font;
    XftColor background;
    XftColor panel;
    XftColor panel_edge;
    XftColor field;
    XftColor text;
    XftColor text_muted;
    XftColor accent;
    XftColor selection;
    XftColor bar_ext;
    XftColor bar_ntfs;
    XftColor bar_fat;
    XftColor bar_swap;
    XftColor bar_other;
    XftColor bar_unalloc;
    Config config;
    DmDeviceList devices;
    DmTreeRow tree_rows[DM_MAX_DEVICES];
    size_t tree_count;
    size_t selected;
    int scroll;
    int owner_read;
    int owner_write;
    char username[TEXT_SIZE];
    char status[TEXT_SIZE];
    Button buttons[BUTTON_COUNT];
} App;

static void copy_text(char *destination, size_t size, const char *value) {
    if (size > 0) {
        snprintf(destination, size, "%s", value != NULL ? value : "");
    }
}

static char *trim(char *text) {
    while (isspace((unsigned char)*text)) {
        text++;
    }
    char *end = text + strlen(text);
    while (end > text && isspace((unsigned char)end[-1])) {
        end--;
    }
    *end = '\0';
    return text;
}

static void config_defaults(Config *config) {
    memset(config, 0, sizeof(*config));
    copy_text(config->title, sizeof(config->title), "GnuChan Disk Manager");
    copy_text(config->background, sizeof(config->background), "#0f172a");
    copy_text(config->panel, sizeof(config->panel), "#1e293b");
    copy_text(config->panel_edge, sizeof(config->panel_edge), "#334155");
    copy_text(config->field, sizeof(config->field), "#162033");
    copy_text(config->text, sizeof(config->text), "#e2e8f0");
    copy_text(config->text_muted, sizeof(config->text_muted), "#94a3b8");
    copy_text(config->accent, sizeof(config->accent), "#38bdf8");
    copy_text(config->selection, sizeof(config->selection), "#0ea5e9");
    copy_text(config->bar_ext, sizeof(config->bar_ext), "#3b82f6");
    copy_text(config->bar_ntfs, sizeof(config->bar_ntfs), "#22c55e");
    copy_text(config->bar_fat, sizeof(config->bar_fat), "#f59e0b");
    copy_text(config->bar_swap, sizeof(config->bar_swap), "#a855f7");
    copy_text(config->bar_other, sizeof(config->bar_other), "#64748b");
    copy_text(config->bar_unalloc, sizeof(config->bar_unalloc), "#334155");
    config->width = 980;
    config->height = 680;
    config->font_size = 13;
}

static void apply_setting(Config *config, const char *name, const char *value) {
    if (strcmp(name, "Title") == 0) {
        copy_text(config->title, sizeof(config->title), value);
    } else if (strcmp(name, "Background") == 0) {
        copy_text(config->background, sizeof(config->background), value);
    } else if (strcmp(name, "Panel") == 0) {
        copy_text(config->panel, sizeof(config->panel), value);
    } else if (strcmp(name, "PanelEdge") == 0) {
        copy_text(config->panel_edge, sizeof(config->panel_edge), value);
    } else if (strcmp(name, "Field") == 0) {
        copy_text(config->field, sizeof(config->field), value);
    } else if (strcmp(name, "Text") == 0) {
        copy_text(config->text, sizeof(config->text), value);
    } else if (strcmp(name, "TextMuted") == 0) {
        copy_text(config->text_muted, sizeof(config->text_muted), value);
    } else if (strcmp(name, "Accent") == 0) {
        copy_text(config->accent, sizeof(config->accent), value);
    } else if (strcmp(name, "Selection") == 0) {
        copy_text(config->selection, sizeof(config->selection), value);
    } else if (strcmp(name, "BarExt") == 0) {
        copy_text(config->bar_ext, sizeof(config->bar_ext), value);
    } else if (strcmp(name, "BarNtfs") == 0) {
        copy_text(config->bar_ntfs, sizeof(config->bar_ntfs), value);
    } else if (strcmp(name, "BarFat") == 0) {
        copy_text(config->bar_fat, sizeof(config->bar_fat), value);
    } else if (strcmp(name, "BarSwap") == 0) {
        copy_text(config->bar_swap, sizeof(config->bar_swap), value);
    } else if (strcmp(name, "BarOther") == 0) {
        copy_text(config->bar_other, sizeof(config->bar_other), value);
    } else if (strcmp(name, "BarUnalloc") == 0) {
        copy_text(config->bar_unalloc, sizeof(config->bar_unalloc), value);
    } else if (strcmp(name, "Width") == 0 || strcmp(name, "Height") == 0 ||
               strcmp(name, "FontSize") == 0) {
        char *end = NULL;
        long number = strtol(value, &end, 10);
        if (end == value || *trim(end) != '\0' || number > 2400 ||
            (strcmp(name, "Width") == 0 && number < 760) ||
            (strcmp(name, "Height") == 0 && number < 460) ||
            (strcmp(name, "FontSize") == 0 &&
             (number < 8 || number > 64))) {
            return;
        }
        if (strcmp(name, "Width") == 0) {
            config->width = (int)number;
        } else if (strcmp(name, "Height") == 0) {
            config->height = (int)number;
        } else {
            config->font_size = (int)number;
        }
    }
}

static void read_config_file(Config *config, const char *path) {
    FILE *file = fopen(path, "r");
    if (file == NULL) {
        return;
    }
    char *line = NULL;
    size_t capacity = 0;
    while (getline(&line, &capacity, file) >= 0) {
        char *entry = trim(line);
        if (*entry == '\0' || *entry == '#') {
            continue;
        }
        char *equals = strchr(entry, '=');
        if (equals == NULL) {
            continue;
        }
        *equals = '\0';
        char *name = trim(entry);
        char *value = trim(equals + 1);
        size_t length = strlen(value);
        if (length >= 2 &&
            ((value[0] == '"' && value[length - 1] == '"') ||
             (value[0] == '\'' && value[length - 1] == '\''))) {
            value[length - 1] = '\0';
            value++;
        }
        apply_setting(config, name, value);
    }
    free(line);
    fclose(file);
}

static void read_config(Config *config) {
    read_config_file(config, "/usr/local/share/gnuchandiskmanager/config.conf");
    read_config_file(config, "/usr/share/gnuchandiskmanager/config.conf");
    char path[TEXT_SIZE];
    const char *xdg_home = getenv("XDG_CONFIG_HOME");
    const char *home = getenv("HOME");
    if (xdg_home != NULL && xdg_home[0] == '/') {
        snprintf(path, sizeof(path), "%s/GnuChanDiskManager/config.conf",
                 xdg_home);
        read_config_file(config, path);
        return;
    }
    if (home != NULL && home[0] == '/') {
        snprintf(path, sizeof(path), "%s/.config/GnuChanDiskManager/config.conf",
                 home);
        read_config_file(config, path);
    }
}

static int allocate_color(App *app, const char *name, XftColor *color) {
    return XftColorAllocName(app->display,
                             DefaultVisual(app->display, app->screen),
                             DefaultColormap(app->display, app->screen),
                             name, color);
}

static int allocate_colors(App *app) {
    return allocate_color(app, app->config.background, &app->background) &&
           allocate_color(app, app->config.panel, &app->panel) &&
           allocate_color(app, app->config.panel_edge, &app->panel_edge) &&
           allocate_color(app, app->config.field, &app->field) &&
           allocate_color(app, app->config.text, &app->text) &&
           allocate_color(app, app->config.text_muted, &app->text_muted) &&
           allocate_color(app, app->config.accent, &app->accent) &&
           allocate_color(app, app->config.selection, &app->selection) &&
           allocate_color(app, app->config.bar_ext, &app->bar_ext) &&
           allocate_color(app, app->config.bar_ntfs, &app->bar_ntfs) &&
           allocate_color(app, app->config.bar_fat, &app->bar_fat) &&
           allocate_color(app, app->config.bar_swap, &app->bar_swap) &&
           allocate_color(app, app->config.bar_other, &app->bar_other) &&
           allocate_color(app, app->config.bar_unalloc, &app->bar_unalloc);
}

static void format_size(const char *bytes_str, char *output,
                        size_t output_size) {
    output[0] = '-';
    output[1] = '\0';
    if (bytes_str[0] == '\0') {
        return;
    }
    errno = 0;
    unsigned long long bytes = strtoull(bytes_str, NULL, 10);
    if (errno != 0) {
        return;
    }
    if (bytes >= 1024ULL * 1024ULL * 1024ULL) {
        snprintf(output, output_size, "%.1f GiB",
                 (double)bytes / (1024.0 * 1024.0 * 1024.0));
    } else if (bytes >= 1024ULL * 1024ULL) {
        snprintf(output, output_size, "%.1f MiB",
                 (double)bytes / (1024.0 * 1024.0));
    } else {
        snprintf(output, output_size, "%llu B", bytes);
    }
}


static void draw_text(App *app, int x, int baseline, const char *value,
                      XftColor *color) {
    if (value == NULL || value[0] == '\0') {
        return;
    }
    size_t length = strlen(value);
    if (length > 160) {
        length = 160;
    }
    XftDrawStringUtf8(app->draw, color, app->font, x, baseline,
                      (const FcChar8 *)value, (int)length);
}

static void fill_rect(App *app, XftColor *color, int x, int y,
                      int width, int height) {
    if (width <= 0 || height <= 0) {
        return;
    }
    XSetForeground(app->display, app->gc, color->pixel);
    XFillRectangle(app->display, app->buffer, app->gc, x, y,
                   (unsigned int)width, (unsigned int)height);
}

static void outline_rect(App *app, XftColor *color, int x, int y,
                         int width, int height) {
    if (width <= 1 || height <= 1) {
        return;
    }
    XSetForeground(app->display, app->gc, color->pixel);
    XDrawRectangle(app->display, app->buffer, app->gc, x, y,
                   (unsigned int)(width - 1), (unsigned int)(height - 1));
}

static int text_width(App *app, const char *value) {
    XGlyphInfo extents;
    XftTextExtentsUtf8(app->display, app->font, (const FcChar8 *)value,
                       (int)strlen(value), &extents);
    return extents.xOff;
}

static void draw_text_fit(App *app, int x, int baseline, int width,
                          const char *value, XftColor *color) {
    if (value == NULL || value[0] == '\0' || width <= 0) {
        return;
    }
    char clipped[TEXT_SIZE + 4];
    copy_text(clipped, sizeof(clipped), value);
    if (text_width(app, clipped) <= width) {
        draw_text(app, x, baseline, clipped, color);
        return;
    }
    size_t length = strlen(clipped);
    while (length > 0) {
        length--;
        while (length > 0 &&
               ((unsigned char)clipped[length] & 0xc0U) == 0x80U) {
            length--;
        }
        clipped[length] = '\0';
        memcpy(clipped + length, "...", 4);
        if (text_width(app, clipped) <= width) {
            draw_text(app, x, baseline, clipped, color);
            return;
        }
        clipped[length] = '\0';
    }
}

static void draw_button(App *app, Button *button) {
    int is_toggle = (button->action == ACTION_READ ||
                     button->action == ACTION_WRITE);
    int toggle_on = 0;
    int disabled = 0;
    if (button->action == ACTION_READ) {
        toggle_on = app->owner_read;
        disabled = (app->username[0] == '\0');
    } else if (button->action == ACTION_WRITE) {
        toggle_on = app->owner_write;
        disabled = (app->username[0] == '\0');
    }
    int is_primary = (button->action == ACTION_MOUNT || button->action == ACTION_FORMAT);
    int is_danger = (button->action == ACTION_DELETE_PARTITION || button->action == ACTION_GPT || button->action == ACTION_MBR);

    XftColor *bg = &app->field;
    XftColor *fg = &app->text;
    XftColor *edge = &app->panel_edge;
    if (disabled) {
        bg = &app->background; fg = &app->text_muted;
    } else if (is_toggle) {
        bg = toggle_on ? &app->selection : &app->field;
        if (toggle_on) edge = &app->accent;
    } else if (is_primary) {
        bg = &app->selection; edge = &app->accent;
    } else if (is_danger) {
        bg = &app->field;
    }
    /* button body with subtle inner highlight */
    fill_rect(app, bg, button->x, button->y, button->width, 32);
    outline_rect(app, edge, button->x, button->y, button->width, 32);
    fill_rect(app, edge, button->x, button->y, button->width, 2);

    if (is_toggle) {
        int box_size = 12;
        int box_x = button->x + 10;
        int box_y = button->y + 10;
        outline_rect(app, disabled ? &app->text_muted : &app->accent,
                     box_x, box_y, box_size, box_size);
        if (toggle_on && !disabled) {
            fill_rect(app, &app->accent, box_x + 3, box_y + 3,
                      box_size - 6, box_size - 6);
        }
        draw_text_fit(app, button->x + 28, button->y + 21,
                      button->width - 36, button->label, fg);
    } else {
        int tw = text_width(app, button->label);
        int tx = button->x + (button->width - tw) / 2;
        if (tx < button->x + 6) tx = button->x + 6;
        draw_text_fit(app, tx, button->y + 21, button->width - 12, button->label, fg);
    }
}

static int append_button(App *app, int index, const char *label, Action action,
                         int x, int y, int width) {
    if (index >= BUTTON_COUNT) {
        return index;
    }
    snprintf(app->buttons[index].label, sizeof(app->buttons[index].label),
             "%.63s", label);
    app->buttons[index].action = action;
    app->buttons[index].x = x;
    app->buttons[index].y = y;
    app->buttons[index].width = width;
    draw_button(app, &app->buttons[index]);
    return index + 1;
}

static int table_top(const App *app) {
    (void)app;
    return HEADER_H + 8 + 56 + 8 + 4;
}

static int buttons_area_height(void) {
    return PAD + BUTTON_H * 3 + GAP * 2 + PAD;
}

static int visible_rows(const App *app) {
    int table_y = table_top(app);
    int bottom = app->config.height - STATUS_H - buttons_area_height();
    int rows = (bottom - (table_y + 24) - 4) / TABLE_ROW_H;
    return rows > 0 ? rows : 1;
}


static int login_automount_enabled(const char *uuid);

static void draw_app(App *app) {
    fill_rect(app, &app->background, 0, 0, app->config.width, app->config.height);

    /* Header */
    fill_rect(app, &app->panel, 0, 0, app->config.width, HEADER_H);
    fill_rect(app, &app->accent, 0, HEADER_H - 3, app->config.width, 3);
    draw_text(app, PAD + 4, 28, app->config.title, &app->text);
    char hdr[128];
    snprintf(hdr, sizeof(hdr), "%zu device(s)", app->devices.count);
    int hw = text_width(app, hdr);
    draw_text(app, app->config.width - hw - PAD - 4, 28, hdr, &app->text_muted);

    int y = HEADER_H + 8;

    /* Info card */
    const DmDevice *selected = app->selected < app->devices.count ? &app->devices.devices[app->selected] : NULL;
    int card_x = PAD, card_w = app->config.width - PAD * 2;
    int info_h = 56;
    fill_rect(app, &app->panel, card_x, y, card_w, info_h);
    outline_rect(app, &app->panel_edge, card_x, y, card_w, info_h);

    char info1[TEXT_SIZE];
    if (selected != NULL) {
        char size_str[32];
        format_size(selected->size, size_str, sizeof(size_str));
        const char *mnt = selected->mountpoint[0] ? selected->mountpoint : "not mounted";
        snprintf(info1, sizeof(info1), "%.64s   |   %.24s   |   %.16s   |   %.24s   |   %.160s",
                 selected->path, size_str,
                 selected->type[0] ? selected->type : "-",
                 selected->filesystem[0] ? selected->filesystem : "no fs", mnt);
    } else {
        copy_text(info1, sizeof(info1), "No device selected. Choose a disk or partition from the list below.");
    }
    draw_text_fit(app, card_x + 10, y + 22, card_w - 20, info1, &app->text);

    char info2[TEXT_SIZE];
    if (app->username[0] == '\0') {
        snprintf(info2, sizeof(info2), "Format: ext4   |   Owner: none   |   Mount=now, Login Auto-mount=on session login");
    } else {
        snprintf(info2, sizeof(info2), "Format: ext4   |   Owner: %.32s   |   Access next fmt: %s,%s,exec   |   Login Auto-mount=%s",
                 app->username, app->owner_read ? "read" : "no-read", app->owner_write ? "write" : "no-write",
                 (selected && selected->uuid[0] && login_automount_enabled(selected->uuid)) ? "On" : "Off");
    }
    draw_text_fit(app, card_x + 10, y + 42, card_w - 20, info2, &app->text_muted);
    y += info_h + 8;

    /* partition bar removed for more table space */
    y += 4;

    /* Buttons area reserved at bottom (3 rows) */
    int buttons_h = buttons_area_height();
    int status_y = app->config.height - STATUS_H;
    int buttons_top = status_y - buttons_h;
    if (buttons_top < y + 80) buttons_top = y + 80;

    /* Table */
    int table_hdr_y = y;
    int table_width = app->config.width - PAD * 2;
    fill_rect(app, &app->panel, PAD, table_hdr_y, table_width, 24);
    outline_rect(app, &app->panel_edge, PAD, table_hdr_y, table_width, 24);

    int device_width = table_width * 24 / 100;
    int size_width = table_width * 12 / 100;
    int type_width = table_width * 10 / 100;
    int filesystem_width = table_width * 12 / 100;
    int label_width = table_width * 14 / 100;
    int mount_width = table_width - device_width - size_width - type_width - filesystem_width - label_width;
    if (mount_width < 80) mount_width = 80;

    int x = PAD + 8;
    int hdr_text_y = table_hdr_y + 17;
    draw_text_fit(app, x, hdr_text_y, device_width - 10, "DEVICE", &app->accent); x += device_width;
    draw_text_fit(app, x, hdr_text_y, size_width - 10, "SIZE", &app->accent); x += size_width;
    draw_text_fit(app, x, hdr_text_y, type_width - 10, "TYPE", &app->accent); x += type_width;
    draw_text_fit(app, x, hdr_text_y, filesystem_width - 10, "FS", &app->accent); x += filesystem_width;
    draw_text_fit(app, x, hdr_text_y, label_width - 10, "LABEL", &app->accent); x += label_width;
    draw_text_fit(app, x, hdr_text_y, mount_width - 10, "MOUNT", &app->accent);

    int rows_area_y = table_hdr_y + 24;
    int rows_area_h = buttons_top - rows_area_y - 4;
    if (rows_area_h < TABLE_ROW_H) rows_area_h = TABLE_ROW_H;
    int rows = rows_area_h / TABLE_ROW_H;
    if (rows < 1) rows = 1;

    int max_scroll = (int)app->tree_count - rows;
    if (max_scroll < 0) max_scroll = 0;
    if (app->scroll > max_scroll) app->scroll = max_scroll;

    size_t end = (size_t)app->scroll + (size_t)rows;
    if (end > app->tree_count) end = app->tree_count;
    for (size_t i = (size_t)app->scroll; i < end; i++) {
        size_t device_index = app->tree_rows[i].device_index;
        int row = (int)(i - (size_t)app->scroll);
        int yrow = rows_area_y + row * TABLE_ROW_H;
        int table_w_inner = app->config.width - PAD * 2;
        const DmDevice *device = &app->devices.devices[device_index];
        int selected_row = (device_index == app->selected);
        if (selected_row) {
            fill_rect(app, &app->selection, PAD, yrow, table_w_inner, 27);
            fill_rect(app, &app->accent, PAD, yrow, 3, 27);
        } else if (row % 2 == 1) {
            fill_rect(app, &app->field, PAD, yrow, table_w_inner, 27);
        }

        char formatted_size[32];
        format_size(device->size, formatted_size, sizeof(formatted_size));
        int indent = (int)app->tree_rows[i].depth * 16;
        char tree_name[DM_FIELD_SIZE + 8];
        snprintf(tree_name, sizeof(tree_name), "%s%s",
                 app->tree_rows[i].depth == 0 ? "" : (app->tree_rows[i].depth == 1 ? "|- " : "+- "),
                 device->name);
        int cx = PAD + 8;
        draw_text_fit(app, cx + indent, yrow + 19, device_width - indent - 10, tree_name, &app->text); cx += device_width;
        draw_text_fit(app, cx, yrow + 19, size_width - 10, formatted_size, &app->text); cx += size_width;
        draw_text_fit(app, cx, yrow + 19, type_width - 10, device->type, &app->text); cx += type_width;
        draw_text_fit(app, cx, yrow + 19, filesystem_width - 10, device->filesystem[0] ? device->filesystem : "-", &app->text); cx += filesystem_width;
        draw_text_fit(app, cx, yrow + 19, label_width - 10, device->label[0] ? device->label : "-", &app->text); cx += label_width;
        if (device->mountpoint[0] != '\0') {
            char mntxt[DM_FIELD_SIZE + 8];
            snprintf(mntxt, sizeof(mntxt), "● %.120s", device->mountpoint);
            draw_text_fit(app, cx, yrow + 19, mount_width - 10, mntxt, &app->accent);
        } else {
            draw_text_fit(app, cx, yrow + 19, mount_width - 10, "not mounted", &app->text_muted);
        }
    }

    /* Buttons: 3 rows x 4 columns for better fit/scaling */
    int index = 0;
    int cols = 4;
    int bw = (app->config.width - PAD * 2 - GAP * (cols - 1)) / cols;
    if (bw < 140) bw = (app->config.width - PAD * 2 - GAP * (cols - 1)) / cols;
    const DmDevice *sel = app->selected < app->devices.count ? &app->devices.devices[app->selected] : NULL;
    int login_on = (sel && sel->uuid[0] && login_automount_enabled(sel->uuid));
    char login_lbl[40]; snprintf(login_lbl, sizeof(login_lbl), "Auto-mount: %s", login_on ? "On" : "Off");

    struct { const char *label; Action action; } btns[12] = {
        {"Refresh", ACTION_REFRESH}, {"Mount", ACTION_MOUNT}, {"Unmount", ACTION_UNMOUNT}, {"Auto-mount", ACTION_LOGIN_AUTOMOUNT},
        {"Format ext4", ACTION_FORMAT}, {"New Partition", ACTION_NEW_PARTITION}, {"Delete Partition", ACTION_DELETE_PARTITION}, {"Create GPT", ACTION_GPT},
        {"Create MBR", ACTION_MBR}, {"Owner", ACTION_USER}, {"Read", ACTION_READ}, {"Write", ACTION_WRITE}
    };
    char owner_lbl[80]; snprintf(owner_lbl, sizeof(owner_lbl), "Owner: %.24s", app->username[0]?app->username:"none");
    char read_lbl[32]; snprintf(read_lbl, sizeof(read_lbl), "Read: %s", app->owner_read?"On":"Off");
    char write_lbl[32]; snprintf(write_lbl, sizeof(write_lbl), "Write: %s", app->owner_write?"On":"Off");
    for (int i = 0; i < 12; i++) {
        int r = i / cols, c = i % cols;
        int bx = PAD + c * (bw + GAP);
        int by = buttons_top + PAD + r * (BUTTON_H + GAP);
        const char *label = btns[i].label;
        Action act = btns[i].action;
        if (act == ACTION_LOGIN_AUTOMOUNT) label = login_lbl;
        else if (act == ACTION_USER) label = owner_lbl;
        else if (act == ACTION_READ) label = read_lbl;
        else if (act == ACTION_WRITE) label = write_lbl;
        if (i == 11) { label = "Quit"; act = ACTION_QUIT; }
        index = append_button(app, index, label, act, bx, by, bw);
    }

    /* Status bar */
    fill_rect(app, &app->panel, 0, status_y, app->config.width, STATUS_H);
    fill_rect(app, &app->accent, 0, status_y, app->config.width, 2);
    draw_text_fit(app, PAD, status_y + 17, app->config.width - PAD * 2, app->status, &app->text);

    XCopyArea(app->display, app->buffer, app->window, app->gc, 0, 0,
              (unsigned int)app->config.width, (unsigned int)app->config.height, 0, 0);
    XFlush(app->display);
}

static void error_dialog(App *app, const char *title, const char *message) {
    XSetWindowAttributes attributes;
    attributes.background_pixel = app->panel.pixel;
    attributes.event_mask = ExposureMask | KeyPressMask | ButtonPressMask;
    int width = 660;
    int height = 190;
    Window dialog = XCreateWindow(
        app->display, RootWindow(app->display, app->screen),
        (DisplayWidth(app->display, app->screen) - width) / 2,
        (DisplayHeight(app->display, app->screen) - height) / 2,
        (unsigned int)width, (unsigned int)height, 1,
        DefaultDepth(app->display, app->screen), InputOutput,
        DefaultVisual(app->display, app->screen),
        CWBackPixel | CWEventMask, &attributes);
    XStoreName(app->display, dialog, title);
    XMapRaised(app->display, dialog);
    XSync(app->display, False);
    XGrabKeyboard(app->display, dialog, False, GrabModeAsync, GrabModeAsync,
                  CurrentTime);
    XftDraw *dialog_draw = XftDrawCreate(
        app->display, dialog, DefaultVisual(app->display, app->screen),
        DefaultColormap(app->display, app->screen));
    int open = 1;
    while (open) {
        XEvent event;
        XNextEvent(app->display, &event);
        if (event.type == Expose && event.xexpose.window != dialog) {
            XPutBackEvent(app->display, &event);
            continue;
        }
        if (event.type == Expose) {
            XftDrawRect(dialog_draw, &app->panel, 0, 0, width, height);
            XftDrawStringUtf8(dialog_draw, &app->accent, app->font, 20, 34,
                              (const FcChar8 *)title, (int)strlen(title));
            char wrapped[4][128] = {{0}};
            size_t line = 0;
            size_t column = 0;
            for (const char *cursor = message;
                 *cursor != '\0' && line < 4; cursor++) {
                if (*cursor == '\n' || column >= 88) {
                    line++;
                    column = 0;
                    if (*cursor == '\n' || line >= 4) {
                        continue;
                    }
                }
                wrapped[line][column++] = *cursor;
                wrapped[line][column] = '\0';
            }
            for (size_t i = 0; i <= line && i < 4; i++) {
                XftDrawStringUtf8(
                    dialog_draw, &app->text, app->font, 20,
                    68 + (int)i * 23, (const FcChar8 *)wrapped[i],
                    (int)strlen(wrapped[i]));
            }
            XftDrawStringUtf8(dialog_draw, &app->text_muted, app->font, 20,
                              height - 24, (const FcChar8 *)"Press Enter or click to close.",
                              (int)strlen("Press Enter or click to close."));
        } else if (event.type == KeyPress || event.type == ButtonPress) {
            open = 0;
        }
    }
    XUngrabKeyboard(app->display, CurrentTime);
    XftDrawDestroy(dialog_draw);
    XDestroyWindow(app->display, dialog);
    XFlush(app->display);
}

static int prompt_text(App *app, const char *title, const char *message,
                       char *value, size_t value_size) {
    if (value_size == 0) {
        return 0;
    }
    value[0] = '\0';
    XSetWindowAttributes attributes;
    attributes.background_pixel = app->panel.pixel;
    attributes.event_mask = ExposureMask | KeyPressMask | ButtonPressMask;
    int width = 700;
    int height = 230;
    Window dialog = XCreateWindow(
        app->display, RootWindow(app->display, app->screen),
        (DisplayWidth(app->display, app->screen) - width) / 2,
        (DisplayHeight(app->display, app->screen) - height) / 2,
        (unsigned int)width, (unsigned int)height, 1,
        DefaultDepth(app->display, app->screen), InputOutput,
        DefaultVisual(app->display, app->screen),
        CWBackPixel | CWEventMask, &attributes);
    XStoreName(app->display, dialog, title);
    XMapRaised(app->display, dialog);
    XSync(app->display, False);
    XGrabKeyboard(app->display, dialog, False, GrabModeAsync, GrabModeAsync,
                  CurrentTime);
    XftDraw *dialog_draw = XftDrawCreate(
        app->display, dialog, DefaultVisual(app->display, app->screen),
        DefaultColormap(app->display, app->screen));
    size_t used = 0;
    int accepted = 0;
    int open = 1;
    while (open) {
        XEvent event;
        XNextEvent(app->display, &event);
        if (event.type == Expose) {
            XftDrawRect(dialog_draw, &app->panel, 0, 0, width, height);
            XftDrawStringUtf8(dialog_draw, &app->accent, app->font, 18, 32,
                              (const FcChar8 *)title, (int)strlen(title));
            char line[128];
            size_t offset = 0;
            int baseline = 66;
            while (message[offset] != '\0' && baseline < 132) {
                size_t length = 0;
                while (message[offset + length] != '\0' &&
                       message[offset + length] != '\n' && length < 94) {
                    line[length] = message[offset + length];
                    length++;
                }
                line[length] = '\0';
                XftDrawStringUtf8(dialog_draw, &app->text, app->font, 18,
                                  baseline, (const FcChar8 *)line,
                                  (int)length);
                offset += length;
                while (message[offset] == ' ' || message[offset] == '\n') {
                    offset++;
                }
                baseline += 21;
            }
            XftDrawRect(dialog_draw, &app->field, 18, 148, width - 36, 34);
            XftDrawStringUtf8(
                dialog_draw, &app->text, app->font, 26, 171,
                (const FcChar8 *)value, (int)strlen(value));
            XftDrawStringUtf8(
                dialog_draw, &app->text_muted, app->font, 18, 209,
                (const FcChar8 *)"Enter to continue, Escape to cancel.",
                36);
        } else if (event.type == KeyPress) {
            char input[32];
            KeySym symbol;
            int length = XLookupString(&event.xkey, input, sizeof(input) - 1,
                                       &symbol, NULL);
            if (symbol == XK_Return || symbol == XK_KP_Enter) {
                accepted = used > 0;
                open = 0;
            } else if (symbol == XK_Escape) {
                open = 0;
            } else if (symbol == XK_BackSpace || symbol == XK_Delete) {
                if (used > 0) {
                    value[--used] = '\0';
                    XClearArea(app->display, dialog, 0, 140, width, 90, True);
                }
            } else {
                int changed = 0;
                for (int i = 0; i < length && used + 1 < value_size; i++) {
                    unsigned char character = (unsigned char)input[i];
                    if (character >= 32 && character < 127) {
                        value[used++] = (char)character;
                        value[used] = '\0';
                        changed = 1;
                    }
                }
                if (changed) {
                    XClearArea(app->display, dialog, 0, 140, width, 90, True);
                }
            }
        } else if (event.type == ButtonPress) {
            if (event.xbutton.y > 180) {
                accepted = used > 0;
                open = 0;
            } else if (event.xbutton.y >= 145) {
                XClearArea(app->display, dialog, 0, 140, width, 90, True);
            }
        }
    }
    XUngrabKeyboard(app->display, CurrentTime);
    XftDrawDestroy(dialog_draw);
    XDestroyWindow(app->display, dialog);
    XFlush(app->display);
    return accepted;
}

static int current_passwd_user(struct passwd *result, char *buffer,
                               size_t buffer_size) {
    struct passwd *entry = getpwuid(getuid());
    if (entry != NULL && entry->pw_uid < 1000) {
        const char *sudo_user = getenv("SUDO_USER");
        entry = sudo_user != NULL ? getpwnam(sudo_user) : NULL;
    }
    if (entry == NULL || entry->pw_uid < 1000 || entry->pw_uid == 65534) {
        setpwent();
        while ((entry = getpwent()) != NULL) {
            if (entry->pw_uid >= 1000 && entry->pw_uid != 65534) {
                break;
            }
        }
        endpwent();
    }
    if (entry == NULL || entry->pw_uid < 1000 || entry->pw_uid == 65534) {
        return 0;
    }
    copy_text(buffer, buffer_size, entry->pw_name);
    *result = *entry;
    return 1;
}

static int select_user(App *app) {
    char names[128][TEXT_SIZE / 2];
    size_t count = 0;
    size_t selection = 0;
    setpwent();
    struct passwd *entry;
    while ((entry = getpwent()) != NULL) {
        if (entry->pw_uid < 1000 || entry->pw_uid == 65534 ||
            count == sizeof(names) / sizeof(names[0])) {
            continue;
        }
        copy_text(names[count], sizeof(names[count]), entry->pw_name);
        if (strcmp(entry->pw_name, app->username) == 0) {
            selection = count;
        }
        count++;
    }
    endpwent();
    if (count == 0) {
        error_dialog(app, "No user accounts",
                     "No regular local user accounts are available.");
        return 0;
    }

    int width = 460;
    int row_height = 30;
    int list_height = (int)count * row_height;
    int height = list_height + 112;
    int maximum_height = DisplayHeight(app->display, app->screen) - 48;
    if (height > maximum_height) {
        height = maximum_height;
    }
    int visible = (height - 92) / row_height;
    if (visible < 1) {
        visible = 1;
    }
    int scroll = 0;
    if ((int)selection >= visible) {
        scroll = (int)selection - visible + 1;
    }

    XSetWindowAttributes attributes;
    attributes.background_pixel = app->panel.pixel;
    attributes.event_mask = ExposureMask | KeyPressMask | ButtonPressMask;
    Window dialog = XCreateWindow(
        app->display, RootWindow(app->display, app->screen),
        (DisplayWidth(app->display, app->screen) - width) / 2,
        (DisplayHeight(app->display, app->screen) - height) / 2,
        (unsigned int)width, (unsigned int)height, 1,
        DefaultDepth(app->display, app->screen), InputOutput,
        DefaultVisual(app->display, app->screen),
        CWBackPixel | CWEventMask, &attributes);
    XStoreName(app->display, dialog, "Select user");
    XMapRaised(app->display, dialog);
    XSync(app->display, False);
    XGrabKeyboard(app->display, dialog, False, GrabModeAsync, GrabModeAsync,
                  CurrentTime);
    XftDraw *dialog_draw = XftDrawCreate(
        app->display, dialog, DefaultVisual(app->display, app->screen),
        DefaultColormap(app->display, app->screen));
    int open = 1;
    int accepted = 0;
    while (open) {
        XEvent event;
        XNextEvent(app->display, &event);
        if (event.type == Expose) {
            XftDrawRect(dialog_draw, &app->panel, 0, 0, width, height);
            XftDrawStringUtf8(dialog_draw, &app->accent, app->font, 20, 30,
                              (const FcChar8 *)"Choose the disk owner",
                              (int)strlen("Choose the disk owner"));
            XftDrawStringUtf8(
                dialog_draw, &app->text_muted, app->font, 20, 52,
                (const FcChar8 *)"Click an account or use Up/Down and Enter.",
                (int)strlen("Click an account or use Up/Down and Enter."));
            for (int row = 0; row < visible; row++) {
                size_t index = (size_t)(scroll + row);
                if (index >= count) {
                    break;
                }
                int y = 66 + row * row_height;
                if (index == selection) {
                    XftDrawRect(dialog_draw, &app->selection, 12, y,
                                (unsigned int)(width - 24),
                                (unsigned int)row_height);
                }
                XftDrawStringUtf8(
                    dialog_draw, &app->text, app->font, 24, y + 20,
                    (const FcChar8 *)names[index],
                    (int)strlen(names[index]));
            }
            int footer_y = height - 20;
            XftDrawStringUtf8(
                dialog_draw, &app->text_muted, app->font, 20, footer_y,
                (const FcChar8 *)"Escape cancels", (int)strlen("Escape cancels"));
        } else if (event.type == KeyPress) {
            KeySym symbol;
            char input[8];
            XLookupString(&event.xkey, input, sizeof(input), &symbol, NULL);
            if (symbol == XK_Escape) {
                open = 0;
            } else if (symbol == XK_Return || symbol == XK_KP_Enter) {
                accepted = 1;
                open = 0;
            } else if (symbol == XK_Up && selection > 0) {
                selection--;
                if ((int)selection < scroll) {
                    scroll = (int)selection;
                }
                XClearArea(app->display, dialog, 0, 56, width,
                           (unsigned int)(height - 76), True);
            } else if (symbol == XK_Down && selection + 1 < count) {
                selection++;
                if ((int)selection >= scroll + visible) {
                    scroll = (int)selection - visible + 1;
                }
                XClearArea(app->display, dialog, 0, 56, width,
                           (unsigned int)(height - 76), True);
            }
        } else if (event.type == ButtonPress) {
            if (event.xbutton.button == Button4 && scroll > 0) {
                scroll--;
                XClearArea(app->display, dialog, 0, 56, width,
                           (unsigned int)(height - 76), True);
            } else if (event.xbutton.button == Button5 &&
                       scroll + visible < (int)count) {
                scroll++;
                XClearArea(app->display, dialog, 0, 56, width,
                           (unsigned int)(height - 76), True);
            } else if (event.xbutton.button == Button1 &&
                       event.xbutton.y >= 66 &&
                       event.xbutton.y < 66 + visible * row_height) {
                int row = (event.xbutton.y - 66) / row_height;
                if (row >= 0 && row < visible &&
                    (size_t)(scroll + row) < count) {
                    selection = (size_t)(scroll + row);
                    accepted = 1;
                    open = 0;
                }
            }
        }
    }
    XUngrabKeyboard(app->display, CurrentTime);
    XftDrawDestroy(dialog_draw);
    XDestroyWindow(app->display, dialog);
    XFlush(app->display);
    if (!accepted) {
        return 0;
    }
    copy_text(app->username, sizeof(app->username), names[selection]);
    app->owner_read = 1;
    app->owner_write = 1;
    snprintf(app->status, sizeof(app->status),
             "Owner: %.64s  |  Read: %s  Write: %s  (applied on next ext4 format).",
             app->username, app->owner_read ? "ON" : "OFF",
             app->owner_write ? "ON" : "OFF");
    return 1;
}

static int confirm_device(App *app, const DmDevice *device,
                          const char *operation) {
    char message[TEXT_SIZE];
    snprintf(message, sizeof(message),
             "%s permanently changes %s. Type the exact device path to confirm.",
             operation, device->path);
    char typed[TEXT_SIZE];
    if (!prompt_text(app, "Confirm destructive operation", message, typed,
                     sizeof(typed))) {
        return 0;
    }
    if (strcmp(typed, device->path) != 0) {
        error_dialog(app, "Confirmation did not match",
                     "No changes were made. The typed device path must match exactly.");
        return 0;
    }
    return 1;
}

static const char *system_program(const char *name, char *path,
                                  size_t path_size) {
    static const char *directories[] = {
        "/usr/sbin", "/sbin", "/usr/bin", "/bin"
    };
    for (size_t i = 0; i < sizeof(directories) / sizeof(directories[0]); i++) {
        int length = snprintf(path, path_size, "%s/%s", directories[i], name);
        if (length > 0 && (size_t)length < path_size && access(path, X_OK) == 0) {
            return path;
        }
    }
    return NULL;
}

static int run_operation(App *app, const char *const arguments[],
                         const char *success_message) {
    char output[TEXT_SIZE * 2] = {0};
    char error[TEXT_SIZE * 2] = {0};
    if (!dm_run_privileged(arguments, output, sizeof(output), error,
                           sizeof(error))) {
        app->status[0] = '\0';
        error_dialog(app, "Disk operation failed",
                     error[0] != '\0' ? error : "The operation could not be completed.");
        return 0;
    }
    copy_text(app->status, sizeof(app->status), success_message);
    return 1;
}

static int refresh_devices(App *app) {
    DmDeviceList refreshed = {0};
    char error[TEXT_SIZE * 2] = {0};
    if (!dm_scan_devices(&refreshed, error, sizeof(error))) {
        error_dialog(app, "Could not read disk devices", error);
        return 0;
    }
    dm_free_devices(&app->devices);
    app->devices = refreshed;
    app->tree_count = dm_build_device_tree(
        &app->devices, app->tree_rows, DM_MAX_DEVICES);
    app->selected = app->devices.count > 0 ? 0 : (size_t)-1;
    app->scroll = 0;
    copy_text(app->status, sizeof(app->status), "Device list refreshed.");
    return 1;
}

static const DmDevice *selected_device(App *app) {
    if (app->selected >= app->devices.count) {
        error_dialog(app, "No device selected", "Select a device first.");
        return NULL;
    }
    return &app->devices.devices[app->selected];
}

static int apply_permissions(const char *device_path, unsigned int mode,
                             char *error, size_t error_size) {
    char mode_str[8];
    snprintf(mode_str, sizeof(mode_str), "%04o", mode);

    if (device_path == NULL || device_path[0] != '/') {
        if (error && error_size > 0)
            snprintf(error, error_size, "Invalid device path");
        return -1;
    }
    for (const char *p = device_path; *p; ++p) {
        unsigned char c = (unsigned char)*p;
        if (!(isalnum(c) || c == '/' || c == '_' || c == '-' || c == '.')) {
            if (error && error_size > 0)
                snprintf(error, error_size, "Unsafe device path");
            return -1;
        }
    }

    char command[TEXT_SIZE * 2];
    snprintf(command, sizeof(command),
             "MNT=$(mktemp -d /tmp/gnuchan-XXXXXX) && "
             "mount '%s' \"$MNT\" && "
             "chmod %s \"$MNT\" && "
             "umount \"$MNT\" && "
             "rmdir \"$MNT\"",
             device_path, mode_str);
    const char *arguments[] = {"/bin/sh", "-c", command, NULL};
    char output[TEXT_SIZE * 2] = {0};
    return dm_run_privileged(arguments, output, sizeof(output),
                             error, error_size);
}

static void change_partition_table(App *app, const char *table) {
    const DmDevice *device = selected_device(app);
    if (device == NULL) {
        return;
    }
    if (!dm_supported_disk(device)) {
        error_dialog(app, "Unsupported device",
                     "Choose a writable physical disk (SATA, USB, NVMe, MMC, or virtual disk).");
        return;
    }
    if (dm_disk_has_mounted_partitions(&app->devices, device)) {
        error_dialog(app, "Disk is in use",
                     "Unmount every partition on this disk before changing its partition table.");
        return;
    }
    if (!confirm_device(app, device, "Creating a new partition table")) {
        return;
    }
    char program[TEXT_SIZE];
    if (system_program("parted", program, sizeof(program)) == NULL) {
        error_dialog(app, "Missing dependency", "The parted utility is not installed.");
        return;
    }
    const char *arguments[] = {
        program, "--script", device->path, "mktable", table, NULL
    };
    char message[TEXT_SIZE];
    snprintf(message, sizeof(message), "Created %s partition table on %s.",
             table, device->path);
    if (run_operation(app, arguments, message)) {
        refresh_devices(app);
    }
}

static void create_partition(App *app) {
    const DmDevice *device = selected_device(app);
    if (device == NULL) {
        return;
    }
    if (!dm_supported_disk(device)) {
        const DmDevice *disk = dm_find_disk(&app->devices, device);
        if (disk == NULL || !dm_supported_disk(disk)) {
            error_dialog(app, "Unsupported device",
                         "Select a writable physical disk with a supported partition table.");
            return;
        }
        device = disk;
    }
    if (dm_disk_has_mounted_partitions(&app->devices, device)) {
        error_dialog(app, "Disk is in use",
                     "Unmount every partition on this disk before creating a partition.");
        return;
    }
    char start[64];
    char end[64];
    if (!prompt_text(app, "New ext4 partition",
                     "Enter the start position (for example 1MiB).",
                     start, sizeof(start)) ||
        !prompt_text(app, "New ext4 partition",
                     "Enter the end position (for example 8GiB).",
                     end, sizeof(end))) {
        return;
    }
    if (!dm_valid_partition_range(start, end)) {
        error_dialog(app, "Invalid partition range",
                     "Use positive MiB or GiB positions, with the end after the start.");
        return;
    }
    if (!confirm_device(app, device, "Creating a partition on")) {
        return;
    }
    char program[TEXT_SIZE];
    if (system_program("parted", program, sizeof(program)) == NULL) {
        error_dialog(app, "Missing dependency", "The parted utility is not installed.");
        return;
    }
    const char *arguments[] = {
        program, "--script", "--align", "optimal", device->path,
        "mkpart", "primary", "ext4", start, end, NULL
    };
    char message[TEXT_SIZE];
    snprintf(message, sizeof(message), "Created an ext4 partition on %s.",
             device->path);
    if (run_operation(app, arguments, message)) {
        refresh_devices(app);
    }
}

static void delete_partition(App *app) {
    const DmDevice *device = selected_device(app);
    if (device == NULL) {
        return;
    }
    if (!dm_supported_partition(&app->devices, device)) {
        error_dialog(app, "Unsupported selection",
                     "Select a partition on a supported writable physical disk.");
        return;
    }
    if (dm_device_is_mounted(device)) {
        error_dialog(app, "Partition is in use",
                     "Unmount this partition before deleting it.");
        return;
    }
    const DmDevice *disk = dm_find_disk(&app->devices, device);
    if (disk == NULL) {
        error_dialog(app, "Could not find parent disk", "Refresh the device list and try again.");
        return;
    }
    if (!confirm_device(app, device, "Deleting this partition")) {
        return;
    }
    char program[TEXT_SIZE];
    if (system_program("parted", program, sizeof(program)) == NULL) {
        error_dialog(app, "Missing dependency", "The parted utility is not installed.");
        return;
    }
    const char *arguments[] = {
        program, "--script", disk->path, "rm", device->partition_number, NULL
    };
    char message[TEXT_SIZE];
    snprintf(message, sizeof(message), "Deleted partition %s.", device->path);
    if (run_operation(app, arguments, message)) {
        refresh_devices(app);
    }
}

static void format_partition(App *app) {
    const DmDevice *device = selected_device(app);
    if (device == NULL) {
        return;
    }
    int whole_disk = strcmp(device->type, "disk") == 0;
    if (whole_disk ? !dm_supported_disk(device)
                   : !dm_supported_partition(&app->devices, device)) {
        error_dialog(app, "Unsupported selection",
                     "Select a supported writable physical disk or one of its partitions.");
        return;
    }
    if ((whole_disk &&
         dm_disk_has_mounted_partitions(&app->devices, device)) ||
        (!whole_disk && dm_device_is_mounted(device))) {
        error_dialog(app, "Device is in use",
                     whole_disk
                         ? "Unmount all partitions and mapped volumes on this disk before formatting it."
                         : "Unmount this partition before formatting it.");
        return;
    }
    if (!select_user(app)) {
        return;
    }
    if (!confirm_device(app, device,
                        whole_disk
                            ? "Formatting the entire disk (all partitions and data will be erased)"
                            : "Formatting this partition")) {
        return;
    }
    char label[64];
    if (!prompt_text(app, "Format as ext4",
                     "Enter a filesystem label (1-16 letters, digits, dot, dash, or underscore).",
                     label, sizeof(label))) {
        return;
    }
    if (!dm_valid_label(label)) {
        error_dialog(app, "Invalid filesystem label",
                     "Use 1-16 ASCII letters, digits, dots, dashes, or underscores.");
        return;
    }
    struct passwd *owner = getpwnam(app->username);
    if (owner == NULL) {
        error_dialog(app, "Account no longer exists",
                     "Choose an existing account before formatting.");
        return;
    }
    char program[TEXT_SIZE];
    const char *mkfs_program =
        system_program("mkfs.ext4", program, sizeof(program));
    if (mkfs_program == NULL) {
        error_dialog(app, "Missing dependency", "The mkfs.ext4 utility is not installed.");
        return;
    }
    /*
     * Format with root_owner to set the root directory owner.  This option
     * is supported since e2fsprogs 1.43 (2016) and avoids the need for a
     * separate chown step.  Permissions are applied below via mount+chmod
     * only when the user chose non-default read/write settings.
     */
    char root_options[128];
    snprintf(root_options, sizeof(root_options),
             "root_owner=%lu:%lu",
             (unsigned long)owner->pw_uid, (unsigned long)owner->pw_gid);
    const char *arguments[] = {
        mkfs_program, "-F", "-L", label, "-E", root_options,
        device->path, NULL
    };
    char message[TEXT_SIZE];
    snprintf(message, sizeof(message),
             "Formatted %s as ext4 for %s.",
             device->path, owner->pw_name);
    if (!run_operation(app, arguments, message)) {
        return;
    }
    /*
     * Apply custom permissions via mount+chmod when the desired mode differs
     * from the ext4 default (0755 = rwxr-xr-x, which gives the owner full
     * read/write/execute access).  Only run when read or write is OFF.
     */
    unsigned int root_mode =
        0100U | (app->owner_read ? 0400U : 0U) |
        (app->owner_write ? 0200U : 0U) | 0055U;
    if (root_mode != 0755) {
        char perm_error[TEXT_SIZE * 2] = {0};
        if (!apply_permissions(device->path, root_mode,
                               perm_error, sizeof(perm_error))) {
            char warning[TEXT_SIZE * 2];
            snprintf(warning, sizeof(warning),
                     "Formatting succeeded, but custom permissions could not be applied:\n%s\n"
                     "The filesystem uses the default permissions (0755).",
                     perm_error[0] != '\0' ? perm_error : "Unknown error");
            error_dialog(app, "Permission warning", warning);
        }
    }
    char result[TEXT_SIZE];
    copy_text(result, sizeof(result), app->status);
    if (refresh_devices(app)) {
        copy_text(app->status, sizeof(app->status), result);
    }
}

static int is_safe_token(const char *value) {
    if (value == NULL || value[0] == '\0') return 0;
    for (const char *p = value; *p; p++) {
        unsigned char c = (unsigned char)*p;
        if (!(isalnum(c) || c == '-' || c == '_' || c == '.')) return 0;
    }
    return 1;
}

static void login_automount_path(const char *uuid, char *out, size_t outsz) {
    const char *home = getenv("HOME");
    if (home == NULL || home[0] == '\0') home = "/tmp";
    snprintf(out, outsz, "%s/.config/autostart/gnuchandiskmanager-automount-%s.desktop", home, uuid);
}

static int login_automount_enabled(const char *uuid) {
    if (!is_safe_token(uuid)) return 0;
    char path[512];
    login_automount_path(uuid, path, sizeof(path));
    return access(path, F_OK) == 0;
}

static void login_automount_toggle(App *app) {
    const DmDevice *device = selected_device(app);
    if (device == NULL) return;
    if (device->uuid[0] == '\0' || !is_safe_token(device->uuid)) {
        error_dialog(app, "Login auto-mount", "Selected device has no usable UUID. Select a formatted partition.");
        return;
    }
    if (device->filesystem[0] == '\0' || strcmp(device->filesystem, "swap") == 0) {
        error_dialog(app, "Login auto-mount", "Selected device has no mountable filesystem.");
        return;
    }

    char path[512];
    login_automount_path(device->uuid, path, sizeof(path));

    if (login_automount_enabled(device->uuid)) {
        if (unlink(path) == 0) {
            snprintf(app->status, sizeof(app->status), "Login auto-mount disabled for %s", device->path);
        } else {
            error_dialog(app, "Login auto-mount", "Could not remove autostart entry.");
        }
        refresh_devices(app);
        return;
    }

    const char *home = getenv("HOME");
    if (home == NULL || home[0] == '\0') {
        error_dialog(app, "Login auto-mount", "HOME directory is not set.");
        return;
    }
    char dir[512];
    snprintf(dir, sizeof(dir), "%s/.config/autostart", home);
    char cmd[700];
    snprintf(cmd, sizeof(cmd), "mkdir -p '%s'", dir);
    int rc = system(cmd);
    (void)rc;

    FILE *f = fopen(path, "w");
    if (f == NULL) {
        error_dialog(app, "Login auto-mount", "Could not create autostart entry.");
        return;
    }
    fprintf(f,
            "[Desktop Entry]\n"
            "Type=Application\n"
            "Name=GnuChanDiskManager auto-mount (%s)\n"
            "Exec=sh -c 'udisksctl mount -b /dev/disk/by-uuid/%s --no-user-interaction >/dev/null 2>&1 || udisksctl mount -b %s --no-user-interaction >/dev/null 2>&1'\n"
            "X-GNOME-Autostart-enabled=true\n"
            "NoDisplay=true\n",
            device->name[0] ? device->name : device->uuid, device->uuid, device->path);
    fclose(f);
    snprintf(app->status, sizeof(app->status), "Login auto-mount enabled for %s (on session login)", device->path);
    refresh_devices(app);
}

static int run_unprivileged(const char *const arguments[], char *output,
                             size_t output_size, char *error, size_t error_size) {
    if (arguments == NULL || arguments[0] == NULL || error == NULL || error_size == 0) {
        return 0;
    }
    error[0] = '\0';
    if (output != NULL && output_size > 0) output[0] = '\0';
    int pipe_fds[2];
    if (pipe(pipe_fds) != 0) {
        snprintf(error, error_size, "pipe: %s", strerror(errno));
        return 0;
    }
    pid_t child = fork();
    if (child < 0) {
        int saved_errno = errno;
        close(pipe_fds[0]); close(pipe_fds[1]);
        snprintf(error, error_size, "fork: %s", strerror(saved_errno));
        return 0;
    }
    if (child == 0) {
        close(pipe_fds[0]);
        dup2(pipe_fds[1], STDOUT_FILENO);
        dup2(pipe_fds[1], STDERR_FILENO);
        close(pipe_fds[1]);
        execvp(arguments[0], (char *const *)arguments);
        dprintf(STDERR_FILENO, "%s: %s\n", arguments[0], strerror(errno));
        _exit(127);
    }
    close(pipe_fds[1]);
    size_t used = 0; char buffer[1024]; ssize_t count;
    while ((count = read(pipe_fds[0], buffer, sizeof(buffer))) > 0) {
        if (output && output_size > 0 && used + 1 < output_size) {
            size_t avail = output_size - used - 1;
            size_t amount = (size_t)count < avail ? (size_t)count : avail;
            memcpy(output + used, buffer, amount);
            used += amount; output[used] = '\0';
        }
    }
    close(pipe_fds[0]);
    int status = 0;
    while (waitpid(child, &status, 0) < 0) if (errno != EINTR) break;
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        if (output && output[0] != '\0') snprintf(error, error_size, "%s", output);
        else snprintf(error, error_size, "Command failed.");
        return 0;
    }
    return 1;
}

static void mount_selected(App *app) {
    const DmDevice *device = selected_device(app);
    if (device == NULL) return;
    if (dm_device_is_mounted(device)) {
        copy_text(app->status, sizeof(app->status), "Selected device is already mounted.");
        return;
    }
    if (strcmp(device->type, "part") != 0 && strcmp(device->type, "disk") != 0) {
        error_dialog(app, "Cannot mount", "Select a partition or disk with a filesystem to mount.");
        return;
    }
    if (device->filesystem[0] == '\0') {
        error_dialog(app, "Cannot mount", "Selected device has no filesystem.");
        return;
    }
    char output[TEXT_SIZE * 2] = {0}, error[TEXT_SIZE * 2] = {0};
    const char *args1[] = {"udisksctl", "mount", "-b", device->path, NULL};
    if (run_unprivileged(args1, output, sizeof(output), error, sizeof(error))) {
        snprintf(app->status, sizeof(app->status), "Mounted %.200s: %.200s", device->path, output[0]?output:"OK");
        refresh_devices(app);
        return;
    }
    /* fallback via privileged mount to /run/media/$USER/<label|name> */
    const char *user = app->username[0] ? app->username : "user";
    const char *tag = device->label[0] ? device->label : device->name;
    char mnt[TEXT_SIZE];
    snprintf(mnt, sizeof(mnt), "/run/media/%.64s/%.64s", user, tag);
    const char *mk[] = {"mkdir", "-p", mnt, NULL};
    char out2[TEXT_SIZE*2]={0}, err2[TEXT_SIZE*2]={0};
    if (!dm_run_privileged(mk, out2, sizeof(out2), err2, sizeof(err2))) {
        error_dialog(app, "Mount failed", error[0] ? error : "Could not create mount point.");
        return;
    }
    const char *mntargs[] = {"mount", device->path, mnt, NULL};
    if (dm_run_privileged(mntargs, out2, sizeof(out2), err2, sizeof(err2))) {
        snprintf(app->status, sizeof(app->status), "Mounted %.200s at %.240s", device->path, mnt);
        refresh_devices(app);
    } else {
        error_dialog(app, "Mount failed", err2[0] ? err2 : (error[0]?error:"Mount command failed."));
    }
}

static void unmount_selected(App *app) {
    const DmDevice *device = selected_device(app);
    if (device == NULL) return;
    if (!dm_device_is_mounted(device)) {
        copy_text(app->status, sizeof(app->status), "Selected device is not mounted.");
        return;
    }
    char output[TEXT_SIZE * 2] = {0}, error[TEXT_SIZE * 2] = {0};
    const char *args1[] = {"udisksctl", "unmount", "-b", device->path, NULL};
    if (run_unprivileged(args1, output, sizeof(output), error, sizeof(error))) {
        snprintf(app->status, sizeof(app->status), "Unmounted %s", device->path);
        refresh_devices(app);
        return;
    }
    const char *args2[] = {"umount", device->path, NULL};
    char out2[TEXT_SIZE*2]={0}, err2[TEXT_SIZE*2]={0};
    if (dm_run_privileged(args2, out2, sizeof(out2), err2, sizeof(err2))) {
        snprintf(app->status, sizeof(app->status), "Unmounted %s", device->path);
        refresh_devices(app);
    } else {
        error_dialog(app, "Unmount failed", err2[0] ? err2 : (error[0]?error:"Unmount command failed."));
    }
}

static void perform_action(App *app, Action action) {
    switch (action) {
    case ACTION_REFRESH:
        refresh_devices(app);
        break;
    case ACTION_MOUNT:
        mount_selected(app);
        break;
    case ACTION_UNMOUNT:
        unmount_selected(app);
        break;
    case ACTION_LOGIN_AUTOMOUNT:
        login_automount_toggle(app);
        break;
    case ACTION_NEW_PARTITION:
        create_partition(app);
        break;
    case ACTION_DELETE_PARTITION:
        delete_partition(app);
        break;
    case ACTION_FORMAT:
        format_partition(app);
        break;
    case ACTION_USER:
        (void)select_user(app);
        break;
    case ACTION_READ:
        if (app->username[0] != '\0') {
            app->owner_read = !app->owner_read;
        }
        break;
    case ACTION_WRITE:
        if (app->username[0] != '\0') {
            app->owner_write = !app->owner_write;
        }
        break;
    case ACTION_GPT:
        change_partition_table(app, "gpt");
        break;
    case ACTION_MBR:
        change_partition_table(app, "msdos");
        break;
    case ACTION_QUIT:
        XDestroyWindow(app->display, app->window);
        app->window = None;
        break;
    }
}

static size_t tree_position(const App *app, size_t device_index) {
    for (size_t i = 0; i < app->tree_count; i++) {
        if (app->tree_rows[i].device_index == device_index) {
            return i;
        }
    }
    return app->tree_count;
}

static int initialize_app(App *app) {
    config_defaults(&app->config);
    read_config(&app->config);
    app->owner_read = 0;
    app->owner_write = 0;
    app->display = XOpenDisplay(NULL);
    if (app->display == NULL) {
        fprintf(stderr, "GnuChanDiskManager requires an X11 display.\n");
        return 0;
    }
    app->screen = DefaultScreen(app->display);
    app->window = XCreateSimpleWindow(
        app->display, RootWindow(app->display, app->screen), 60, 60,
        (unsigned int)app->config.width, (unsigned int)app->config.height, 1,
        BlackPixel(app->display, app->screen),
        BlackPixel(app->display, app->screen));
    XStoreName(app->display, app->window, app->config.title);
    XSelectInput(app->display, app->window,
                 ExposureMask | StructureNotifyMask | ButtonPressMask |
                     KeyPressMask);
    /*
     * Set bit-gravity to NorthWest so existing content is preserved during
     * resize, and set the background pixmap to None so X does not fill newly
     * exposed areas with a solid color (which caused the resize flicker).
     * The application repaints the full window immediately on ConfigureNotify.
     */
    XSetWindowAttributes win_attrs;
    win_attrs.bit_gravity = NorthWestGravity;
    XChangeWindowAttributes(app->display, app->window, CWBitGravity,
                             &win_attrs);
    XSetWindowBackgroundPixmap(app->display, app->window, None);
    XSizeHints hints = {.flags = PMinSize, .min_width = 760, .min_height = 460};
    XSetWMNormalHints(app->display, app->window, &hints);
    XClassHint class_hint = {.res_name = "GnuChanDiskManager",
                             .res_class = "GnuChanDiskManager"};
    XSetClassHint(app->display, app->window, &class_hint);
    app->delete_window = XInternAtom(app->display, "WM_DELETE_WINDOW", False);
    XSetWMProtocols(app->display, app->window, &app->delete_window, 1);
    app->gc = XCreateGC(app->display, app->window, 0, NULL);
    char font_name[64];
    snprintf(font_name, sizeof(font_name), "monospace-%d",
             app->config.font_size);
    app->font = XftFontOpenName(app->display, app->screen, font_name);
    if (app->font == NULL || !allocate_colors(app)) {
        fprintf(stderr, "Could not load the configured Xft font or colors.\n");
        return 0;
    }
    app->buffer = XCreatePixmap(
        app->display, app->window, (unsigned int)app->config.width,
        (unsigned int)app->config.height,
        (unsigned int)DefaultDepth(app->display, app->screen));
    app->draw = XftDrawCreate(
        app->display, app->buffer,
        DefaultVisual(app->display, app->screen),
        DefaultColormap(app->display, app->screen));
    struct passwd owner;
    if (!current_passwd_user(&owner, app->username, sizeof(app->username))) {
        fprintf(stderr, "Could not determine the current local account.\n");
        return 0;
    }
    copy_text(app->status, sizeof(app->status), "Ready. Destructive actions require typing the device path.");
    if (!refresh_devices(app)) {
        return 0;
    }
    XMapWindow(app->display, app->window);
    return 1;
}

static void close_app(App *app) {
    dm_free_devices(&app->devices);
    if (app->display != NULL) {
        if (app->draw != NULL) {
            XftDrawDestroy(app->draw);
        }
        if (app->buffer != None) {
            XFreePixmap(app->display, app->buffer);
        }
        if (app->font != NULL) {
            XftFontClose(app->display, app->font);
        }
        if (app->gc != NULL) {
            XFreeGC(app->display, app->gc);
        }
        if (app->window != None) {
            XDestroyWindow(app->display, app->window);
        }
        XCloseDisplay(app->display);
    }
}

int main(void) {
    App app = {0};
    if (!initialize_app(&app)) {
        close_app(&app);
        return 1;
    }
    while (app.window != None) {
        XEvent event;
        XNextEvent(app.display, &event);
        if (event.type == Expose) {
            /* Only redraw on the last expose in a series */
            if (event.xexpose.count == 0) {
                draw_app(&app);
            }
        } else if (event.type == ConfigureNotify) {
            int new_width = event.xconfigure.width;
            int new_height = event.xconfigure.height;
            /*
             * Coalesce multiple ConfigureNotify events: drain all pending
             * ones so the buffer is recreated only once with the final size.
             */
            XEvent extra;
            while (XCheckTypedWindowEvent(app.display, app.window,
                                          ConfigureNotify, &extra)) {
                new_width = extra.xconfigure.width;
                new_height = extra.xconfigure.height;
            }
            if (new_width != app.config.width ||
                new_height != app.config.height) {
                app.config.width = new_width;
                app.config.height = new_height;
                if (app.draw != NULL) {
                    XftDrawDestroy(app.draw);
                }
                if (app.buffer != None) {
                    XFreePixmap(app.display, app.buffer);
                }
                app.buffer = XCreatePixmap(
                    app.display, app.window,
                    (unsigned int)app.config.width,
                    (unsigned int)app.config.height,
                    (unsigned int)DefaultDepth(app.display, app.screen));
                app.draw = XftDrawCreate(
                    app.display, app.buffer,
                    DefaultVisual(app.display, app.screen),
                    DefaultColormap(app.display, app.screen));
            }
            draw_app(&app);
        } else if (event.type == ButtonPress) {
            int y = event.xbutton.y;
            if (event.xbutton.button == Button4) {
                if (app.scroll > 0) {
                    app.scroll--;
                    draw_app(&app);
                }
            } else if (event.xbutton.button == Button5) {
                int v = visible_rows(&app); int maximum = (int)app.tree_count - v;
                if (maximum < 0) {
                    maximum = 0;
                }
                if (app.scroll < maximum) {
                    app.scroll++;
                    draw_app(&app);
                }
            } else if (event.xbutton.button == Button1) {
                int t_top = table_top(&app);
                int rows_area_y = t_top + 24;
                int buttons_h = buttons_area_height();
                int buttons_top = app.config.height - STATUS_H - buttons_h;
                if (buttons_top < t_top + 80) buttons_top = t_top + 80;
                int rows_area_h = buttons_top - rows_area_y - 4;
                int vrows = rows_area_h / TABLE_ROW_H; if (vrows < 1) vrows = 1;
                if (y >= rows_area_y && y < rows_area_y + vrows * TABLE_ROW_H) {
                    int row = (y - rows_area_y) / TABLE_ROW_H;
                    int index = app.scroll + row;
                    if (index >= 0 && (size_t)index < app.tree_count && row < vrows) {
                        app.selected = app.tree_rows[index].device_index;
                        draw_app(&app);
                    }
                } else {
                    for (int i = 0; i < BUTTON_COUNT; i++) {
                        Button *button = &app.buttons[i];
                        if (event.xbutton.x >= button->x &&
                            event.xbutton.x < button->x + button->width &&
                            event.xbutton.y >= button->y &&
                            event.xbutton.y < button->y + BUTTON_H) {
                            perform_action(&app, button->action);
                            if (app.window != None) draw_app(&app);
                            break;
                        }
                    }
                }
            }
        } else if (event.type == ClientMessage &&
                   (Atom)event.xclient.data.l[0] == app.delete_window) {
            XDestroyWindow(app.display, app.window);
            app.window = None;
        } else if (event.type == KeyPress) {
            KeySym symbol;
            char buffer[8];
            XLookupString(&event.xkey, buffer, sizeof(buffer), &symbol, NULL);
            if (symbol == XK_q || symbol == XK_Escape) {
                XDestroyWindow(app.display, app.window);
                app.window = None;
            } else if (symbol == XK_r || symbol == XK_R) {
                refresh_devices(&app);
                draw_app(&app);
            } else if (symbol == XK_Up && app.tree_count > 0) {
                size_t position = tree_position(&app, app.selected);
                if (position > 0 && position < app.tree_count) {
                    position--;
                    app.selected = app.tree_rows[position].device_index;
                }
                if ((int)position < app.scroll) {
                    app.scroll = (int)position;
                }
                draw_app(&app);
            } else if (symbol == XK_Down &&
                       app.tree_count > 0) {
                size_t position = tree_position(&app, app.selected);
                if (position + 1 < app.tree_count) {
                    position++;
                    app.selected = app.tree_rows[position].device_index;
                }
                int rows = visible_rows(&app);
                if ((int)position >= app.scroll + rows) {
                    app.scroll = (int)position - rows + 1;
                }
                draw_app(&app);
            }
        }
    }
    close_app(&app);
    return 0;
}
