/*
 * dock_pins.c — read, validate, persist and launch desktop application pins.
 */
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "dock_core.h"
#include "dock_pins.h"
#include "dock_theme.h"

#define DESKTOP_ID_LENGTH DOCK_TEXT_LENGTH
#define EXEC_ARG_MAX 64

static int desktop_exec_valid(const DockPinnedApp *app);

static int truthy(const char *value) {
    return value && (strcasecmp(value, "true") == 0 ||
                     strcmp(value, "1") == 0);
}

static int executable_available(const char *name) {
    if (!name || !name[0]) {
        return 0;
    }
    if (strchr(name, '/')) {
        return access(name, X_OK) == 0;
    }
    const char *path = getenv("PATH");
    if (!path) {
        return 0;
    }
    char copy[DOCK_TEXT_LENGTH * 4];
    snprintf(copy, sizeof(copy), "%s", path);
    char *save = NULL;
    for (char *part = strtok_r(copy, ":", &save); part;
         part = strtok_r(NULL, ":", &save)) {
        char candidate[DOCK_TEXT_LENGTH * 2];
        snprintf(candidate, sizeof(candidate), "%s/%s", part, name);
        if (access(candidate, X_OK) == 0) {
            return 1;
        }
    }
    return 0;
}

static int desktop_id_valid(const char *id) {
    if (!id || !id[0]) {
        return 0;
    }
    size_t length = strlen(id);
    if (length <= 8 || length >= DESKTOP_ID_LENGTH ||
        strcmp(id + length - 8, ".desktop") != 0) {
        return 0;
    }
    for (size_t i = 0; i < length; i++) {
        unsigned char c = (unsigned char)id[i];
        if (!(isalnum(c) || c == '.' || c == '_' || c == '-')) {
            return 0;
        }
    }
    return strstr(id, "..") == NULL;
}

static int join_path(char *out, size_t size, const char *directory,
                     const char *name) {
    size_t directory_length = strlen(directory);
    size_t name_length = strlen(name);
    if (directory_length + name_length + 2 > size) {
        return 0;
    }
    memcpy(out, directory, directory_length);
    out[directory_length] = '/';
    memcpy(out + directory_length + 1, name, name_length + 1);
    return 1;
}

static int parse_entry(const char *path, const char *id, DockPinnedApp *app,
                       char *reason, unsigned int reason_size) {
    memset(app, 0, sizeof(*app));
    snprintf(app->desktop_id, sizeof(app->desktop_id), "%s", id);
    snprintf(app->path, sizeof(app->path), "%s", path);
    FILE *file = fopen(path, "rb");
    if (!file) {
        snprintf(reason, reason_size, "cannot read desktop entry");
        return 0;
    }

    char type[64] = "";
    char hidden[16] = "";
    char no_display[16] = "";
    char try_exec[DOCK_TEXT_LENGTH] = "";
    int in_group = 0;
    int has_group = 0;
    char line[DOCK_TEXT_LENGTH * 2];
    while (fgets(line, sizeof(line), file)) {
        size_t length = strlen(line);
        if (length == sizeof(line) - 1 && line[length - 1] != '\n') {
            fclose(file);
            snprintf(reason, reason_size, "desktop entry has an overlong line");
            return 0;
        }
        while (length && (line[length - 1] == '\n' ||
                          line[length - 1] == '\r')) {
            line[--length] = '\0';
        }
        if (line[0] == '[') {
            in_group = strcmp(line, "[Desktop Entry]") == 0;
            has_group |= in_group;
            continue;
        }
        if (!in_group || !line[0] || line[0] == '#') {
            continue;
        }
        char *equals = strchr(line, '=');
        if (!equals) {
            continue;
        }
        *equals++ = '\0';
        if (strcmp(line, "Type") == 0) {
            snprintf(type, sizeof(type), "%s", equals);
        } else if (strcmp(line, "Name") == 0) {
            snprintf(app->name, sizeof(app->name), "%s", equals);
        } else if (strcmp(line, "Exec") == 0) {
            snprintf(app->exec, sizeof(app->exec), "%s", equals);
        } else if (strcmp(line, "Icon") == 0) {
            snprintf(app->icon_name, sizeof(app->icon_name), "%s", equals);
        } else if (strcmp(line, "StartupWMClass") == 0) {
            snprintf(app->wm_class, sizeof(app->wm_class), "%s", equals);
        } else if (strcmp(line, "Hidden") == 0) {
            snprintf(hidden, sizeof(hidden), "%s", equals);
        } else if (strcmp(line, "NoDisplay") == 0) {
            snprintf(no_display, sizeof(no_display), "%s", equals);
        } else if (strcmp(line, "TryExec") == 0) {
            snprintf(try_exec, sizeof(try_exec), "%s", equals);
        }
    }
    int read_error = ferror(file);
    fclose(file);

    if (read_error) {
        snprintf(reason, reason_size, "could not read desktop entry");
        return 0;
    }
    if (!has_group) {
        snprintf(reason, reason_size, "missing [Desktop Entry] group");
        return 0;
    }
    if (strcmp(type, "Application") != 0) {
        snprintf(reason, reason_size, "Type is not Application");
        return 0;
    }
    if (!app->name[0]) {
        snprintf(reason, reason_size, "missing Name");
        return 0;
    }
    if (!app->exec[0]) {
        snprintf(reason, reason_size, "missing Exec");
        return 0;
    }
    if (truthy(hidden) || truthy(no_display)) {
        snprintf(reason, reason_size, "entry is Hidden or NoDisplay");
        return 0;
    }
    if (try_exec[0] && !executable_available(try_exec)) {
        snprintf(reason, reason_size, "TryExec program is unavailable");
        return 0;
    }
    if (!app->wm_class[0]) {
        snprintf(app->wm_class, sizeof(app->wm_class), "%.*s",
                 (int)(strlen(id) - 8), id);
    }
    app->valid = 1;
    if (!desktop_exec_valid(app)) {
        app->valid = 0;
        snprintf(reason, reason_size, "invalid or unavailable Exec program");
        return 0;
    }
    return 1;
}

static int applications_dirs(char dirs[][DOCK_TEXT_LENGTH * 2], int max_dirs) {
    int count = 0;
    const char *data_home = getenv("XDG_DATA_HOME");
    const char *home = getenv("HOME");
    char fallback[DOCK_TEXT_LENGTH * 2];
    if ((!data_home || !data_home[0]) && home && home[0]) {
        snprintf(fallback, sizeof(fallback), "%s/.local/share", home);
        data_home = fallback;
    }
    if (data_home && data_home[0] && count < max_dirs) {
        if (join_path(dirs[count], sizeof(dirs[0]), data_home, "applications")) {
            count++;
        }
    }
    const char *data_dirs = getenv("XDG_DATA_DIRS");
    char defaults[] = "/usr/local/share:/usr/share";
    char copy[DOCK_TEXT_LENGTH * 8];
    snprintf(copy, sizeof(copy), "%s",
             data_dirs && data_dirs[0] ? data_dirs : defaults);
    char *save = NULL;
    for (char *part = strtok_r(copy, ":", &save);
         part && count < max_dirs; part = strtok_r(NULL, ":", &save)) {
        if (join_path(dirs[count], sizeof(dirs[0]), part, "applications")) {
            count++;
        }
    }
    return count;
}

static void desktop_stem(const char *id, char *stem, unsigned int size) {
    size_t length = strlen(id);
    if (length > 8) {
        length -= 8;
    }
    if (length >= size) {
        length = size - 1;
    }
    memcpy(stem, id, length);
    stem[length] = '\0';
}

static int matching_class(const DockPinnedApp *app, const char *wm_class) {
    if (!wm_class || !wm_class[0]) {
        return 0;
    }
    return strcasecmp(app->wm_class, wm_class) == 0;
}

static int find_entry_for_class(const char *wm_class, DockPinnedApp *app,
                                char *reason, unsigned int reason_size) {
    char dirs[32][DOCK_TEXT_LENGTH * 2];
    int dir_count = applications_dirs(dirs, 32);
    char invalid_reason[DOCK_TEXT_LENGTH] = "";
    for (int d = 0; d < dir_count; d++) {
        DIR *dir = opendir(dirs[d]);
        if (!dir) {
            continue;
        }
        struct dirent *entry;
        while ((entry = readdir(dir)) != NULL) {
            if (!desktop_id_valid(entry->d_name)) {
                continue;
            }
            char full[DOCK_TEXT_LENGTH * 3];
            if (!join_path(full, sizeof(full), dirs[d], entry->d_name)) {
                continue;
            }
            DockPinnedApp candidate;
            char why[DOCK_TEXT_LENGTH] = "";
            int valid = parse_entry(full, entry->d_name, &candidate,
                                    why, sizeof(why));
            char stem[DOCK_TEXT_LENGTH];
            desktop_stem(entry->d_name, stem, sizeof(stem));
            int matches = strcasecmp(stem, wm_class) == 0 ||
                          (candidate.wm_class[0] &&
                           strcasecmp(candidate.wm_class, wm_class) == 0);
            if (matches && valid) {
                *app = candidate;
                closedir(dir);
                return 1;
            }
            if (matches && !invalid_reason[0]) {
                snprintf(invalid_reason, sizeof(invalid_reason), "%s", why);
            }
        }
        closedir(dir);
    }
    snprintf(reason, reason_size, "%s",
             invalid_reason[0] ? invalid_reason : "no matching desktop entry");
    return 0;
}

static int find_entry_by_id(const char *id, DockPinnedApp *app,
                            char *reason, unsigned int reason_size) {
    char dirs[32][DOCK_TEXT_LENGTH * 2];
    int dir_count = applications_dirs(dirs, 32);
    if (!desktop_id_valid(id)) {
        snprintf(reason, reason_size, "invalid desktop entry ID");
        return 0;
    }
    for (int i = 0; i < dir_count; i++) {
        char full[DOCK_TEXT_LENGTH * 3];
        if (!join_path(full, sizeof(full), dirs[i], id)) {
            continue;
        }
        if (access(full, R_OK) == 0) {
            return parse_entry(full, id, app, reason, reason_size);
        }
    }
    snprintf(reason, reason_size, "desktop entry is no longer installed");
    return 0;
}

static void pins_file_path(char *path, unsigned int size,
                           char *directory, unsigned int directory_size) {
    char config[DOCK_TEXT_LENGTH];
    dock_config_path(config, sizeof(config));
    char *slash = strrchr(config, '/');
    if (!slash) {
        path[0] = '\0';
        directory[0] = '\0';
        return;
    }
    *slash = '\0';
    snprintf(directory, directory_size, "%s", config);
    snprintf(path, size, "%s/PinnedApps.conf", directory);
}

static int save_pins(DockCore *core) {
    char path[DOCK_TEXT_LENGTH * 2];
    char directory[DOCK_TEXT_LENGTH];
    pins_file_path(path, sizeof(path), directory, sizeof(directory));
    if (!path[0]) {
        return 0;
    }
    if (mkdir(directory, 0700) != 0 && errno != EEXIST) {
        return 0;
    }
    char temporary[DOCK_TEXT_LENGTH * 2];
    size_t path_length = strlen(path);
    if (path_length + sizeof(".tmp.XXXXXX") > sizeof(temporary)) {
        return 0;
    }
    memcpy(temporary, path, path_length);
    memcpy(temporary + path_length, ".tmp.XXXXXX", sizeof(".tmp.XXXXXX"));
    int fd = mkstemp(temporary);
    if (fd < 0) {
        return 0;
    }
    FILE *file = fdopen(fd, "w");
    if (!file) {
        close(fd);
        unlink(temporary);
        return 0;
    }
    int ok = 1;
    for (int i = 0; i < core->pin_count; i++) {
        if (fprintf(file, "%s\n", core->pins[i].desktop_id) < 0) {
            ok = 0;
            break;
        }
    }
    if (fclose(file) != 0) {
        ok = 0;
    }
    if (ok && rename(temporary, path) == 0) {
        return 1;
    }
    unlink(temporary);
    return 0;
}

void dock_pins_load(DockCore *core) {
    char path[DOCK_TEXT_LENGTH * 2];
    char directory[DOCK_TEXT_LENGTH];
    pins_file_path(path, sizeof(path), directory, sizeof(directory));
    if (!path[0]) {
        return;
    }
    FILE *file = fopen(path, "r");
    if (!file) {
        return;
    }
    char line[DOCK_TEXT_LENGTH * 2];
    while (core->pin_count < DOCK_MAX_PINNED && fgets(line, sizeof(line), file)) {
        line[strcspn(line, "\r\n")] = '\0';
        if (!line[0] || line[0] == '#') {
            continue;
        }
        if (!desktop_id_valid(line) || dock_pins_is_id(core, line)) {
            fprintf(stderr, "gnuchandock: ignoring invalid or duplicate pin '%s'\n",
                    line);
            continue;
        }
        DockPinnedApp *app = &core->pins[core->pin_count];
        char reason[DOCK_TEXT_LENGTH] = "";
        int valid = find_entry_by_id(line, app, reason, sizeof(reason));
        if (!valid) {
            fprintf(stderr, "gnuchandock: pinned desktop entry '%s' is invalid: %s\n",
                    line, reason);
            continue;
        }
        int duplicate_class = 0;
        for (int i = 0; i < core->pin_count; i++) {
            if (strcasecmp(core->pins[i].wm_class, app->wm_class) == 0) {
                duplicate_class = 1;
                break;
            }
        }
        if (duplicate_class) {
            fprintf(stderr,
                    "gnuchandock: ignoring duplicate pin '%s' for class '%s'\n",
                    line, app->wm_class);
            continue;
        }
        core->pin_count++;
    }
    if (core->pin_count == DOCK_MAX_PINNED && fgets(line, sizeof(line), file)) {
        fprintf(stderr, "gnuchandock: ignoring pins beyond the limit of %d\n",
                DOCK_MAX_PINNED);
    }
    fclose(file);
}

void dock_pins_free_icons(DockCore *core) {
    for (int i = 0; i < core->pin_count; i++) {
        dock_icon_free(core->display, &core->pins[i].icon);
    }
}

int dock_pins_is_id(const DockCore *core, const char *desktop_id) {
    if (!core || !desktop_id || !desktop_id[0]) {
        return 0;
    }
    for (int i = 0; i < core->pin_count; i++) {
        if (strcmp(core->pins[i].desktop_id, desktop_id) == 0) {
            return 1;
        }
    }
    return 0;
}

static int pin_index_for_class(const DockCore *core, const char *wm_class) {
    for (int i = 0; i < core->pin_count; i++) {
        if (core->pins[i].valid && matching_class(&core->pins[i], wm_class)) {
            return i;
        }
    }
    return -1;
}

void dock_pins_context_action(DockCore *core, const DockItem *item,
                              char *message, unsigned int message_size,
                              int *enabled) {
    *enabled = 0;
    if (item->desktop_id[0] ||
        pin_index_for_class(core, item->wm_class) >= 0) {
        snprintf(message, message_size, "Unpin from dock");
        *enabled = 1;
        return;
    }
    DockPinnedApp app;
    char reason[DOCK_TEXT_LENGTH] = "";
    if (find_entry_for_class(item->wm_class, &app, reason, sizeof(reason))) {
        if (dock_pins_is_id(core, app.desktop_id)) {
            snprintf(message, message_size, "Already pinned");
            return;
        }
        snprintf(message, message_size, "Pin to dock");
        *enabled = 1;
        return;
    }
    snprintf(message, message_size, "Unavailable: %.80s", reason);
}

int dock_pins_action(DockCore *core, const char *wm_class,
                     const char *desktop_id, char *message,
                     unsigned int message_size) {
    int index = desktop_id && desktop_id[0]
                    ? -1 : pin_index_for_class(core, wm_class);
    if (desktop_id && desktop_id[0]) {
        for (int i = 0; i < core->pin_count; i++) {
            if (strcmp(core->pins[i].desktop_id, desktop_id) == 0) {
                index = i;
                break;
            }
        }
    }
    if (index >= 0) {
        DockPinnedApp removed = core->pins[index];
        for (int i = index; i + 1 < core->pin_count; i++) {
            core->pins[i] = core->pins[i + 1];
        }
        core->pin_count--;
        if (!save_pins(core)) {
            for (int i = core->pin_count; i > index; i--) {
                core->pins[i] = core->pins[i - 1];
            }
            core->pins[index] = removed;
            core->pin_count++;
            snprintf(message, message_size, "Could not save pin changes");
            return 0;
        }
        dock_icon_free(core->display, &removed.icon);
        snprintf(message, message_size, "Unpinned");
        return 1;
    }
    if (core->pin_count >= DOCK_MAX_PINNED) {
        snprintf(message, message_size, "Dock pin limit reached");
        return 0;
    }
    DockPinnedApp app;
    char reason[DOCK_TEXT_LENGTH] = "";
    if (!find_entry_for_class(wm_class, &app, reason, sizeof(reason))) {
        snprintf(message, message_size, "Cannot pin: %.80s", reason);
        return 0;
    }
    if (dock_pins_is_id(core, app.desktop_id)) {
        snprintf(message, message_size, "Already pinned");
        return 0;
    }
    core->pins[core->pin_count++] = app;
    if (!save_pins(core)) {
        core->pin_count--;
        dock_icon_free(core->display, &app.icon);
        snprintf(message, message_size, "Could not save pin changes");
        return 0;
    }
    snprintf(message, message_size, "Pinned");
    return 1;
}

static int append_text(char *out, size_t size, size_t *used,
                       const char *text) {
    size_t length = strlen(text);
    if (*used + length >= size) {
        return 0;
    }
    memcpy(out + *used, text, length);
    *used += length;
    out[*used] = '\0';
    return 1;
}

static int exec_arguments(const DockPinnedApp *app,
                          char storage[EXEC_ARG_MAX][DOCK_TEXT_LENGTH],
                          char *argv[EXEC_ARG_MAX + 1]) {
    char raw[EXEC_ARG_MAX][DOCK_TEXT_LENGTH] = {{0}};
    int raw_count = 0;
    const char *cursor = app->exec;
    while (*cursor) {
        while (isspace((unsigned char)*cursor)) {
            cursor++;
        }
        if (!*cursor) {
            break;
        }
        if (raw_count >= EXEC_ARG_MAX) {
            return 0;
        }
        size_t used = 0;
        int quoted = 0;
        int started = 0;
        while (*cursor && (quoted || !isspace((unsigned char)*cursor))) {
            char c = *cursor++;
            if (c == '"' ) {
                quoted = !quoted;
                started = 1;
            } else if (c == '\\' && *cursor) {
                if (!append_text(raw[raw_count], DOCK_TEXT_LENGTH, &used,
                                 (char[]){*cursor++, '\0'})) {
                    return 0;
                }
                started = 1;
            } else {
                if (used + 1 >= DOCK_TEXT_LENGTH) {
                    return 0;
                }
                raw[raw_count][used++] = c;
                raw[raw_count][used] = '\0';
                started = 1;
            }
        }
        if (quoted || !started) {
            return 0;
        }
        raw_count++;
    }
    if (raw_count == 0) {
        return 0;
    }

    int argc = 0;
    for (int i = 0; i < raw_count; i++) {
        if (strcmp(raw[i], "%i") == 0) {
            if (app->icon_name[0] && argc + 2 < EXEC_ARG_MAX) {
                snprintf(storage[argc++], DOCK_TEXT_LENGTH, "--icon");
                snprintf(storage[argc++], DOCK_TEXT_LENGTH, "%s",
                         app->icon_name);
            }
            continue;
        }
        char expanded[DOCK_TEXT_LENGTH] = "";
        size_t used = 0;
        for (size_t at = 0; raw[i][at]; at++) {
            if (raw[i][at] != '%') {
                char one[2] = {raw[i][at], '\0'};
                if (!append_text(expanded, sizeof(expanded), &used, one)) {
                    return 0;
                }
                continue;
            }
            char code = raw[i][++at];
            if (!code) {
                return 0;
            }
            const char *replacement = "";
            if (code == '%') {
                replacement = "%";
            } else if (code == 'c') {
                replacement = app->name;
            } else if (code == 'k') {
                replacement = app->path;
            } else if (strchr("fFuUdDnNvVm", code)) {
                replacement = "";
            } else {
                return 0;
            }
            if (!append_text(expanded, sizeof(expanded), &used, replacement)) {
                return 0;
            }
        }
        if (!expanded[0]) {
            continue;
        }
        if (argc >= EXEC_ARG_MAX) {
            return 0;
        }
        snprintf(storage[argc++], DOCK_TEXT_LENGTH, "%s", expanded);
    }
    if (!argc) {
        return 0;
    }
    for (int i = 0; i < argc; i++) {
        argv[i] = storage[i];
    }
    argv[argc] = NULL;
    return argc;
}

static int desktop_exec_valid(const DockPinnedApp *app) {
    char storage[EXEC_ARG_MAX][DOCK_TEXT_LENGTH] = {{0}};
    char *argv[EXEC_ARG_MAX + 1];
    return exec_arguments(app, storage, argv) > 0 &&
           executable_available(argv[0]);
}

int dock_pins_launch(const DockPinnedApp *app) {
    if (!app || !app->valid) {
        fprintf(stderr, "gnuchandock: cannot launch an invalid desktop entry\n");
        return 0;
    }
    char storage[EXEC_ARG_MAX][DOCK_TEXT_LENGTH] = {{0}};
    char *argv[EXEC_ARG_MAX + 1];
    if (!exec_arguments(app, storage, argv)) {
        fprintf(stderr, "gnuchandock: invalid Exec in '%s'\n", app->desktop_id);
        return 0;
    }
    pid_t pid = fork();
    if (pid < 0) {
        perror("gnuchandock: could not launch pinned app");
        return 0;
    }
    if (pid == 0) {
        setsid();
        execvp(argv[0], argv);
        perror("gnuchandock: could not launch pinned app");
        _exit(127);
    }
    return 1;
}
