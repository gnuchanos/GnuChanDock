/*
 * dock_pins.h — desktop applications pinned independently of running windows.
 */
#ifndef GNUCHANDOCK_PINS_H
#define GNUCHANDOCK_PINS_H

#include "dock_config.h"
#include "dock_icon.h"

#define DOCK_MAX_PINNED DOCK_MAX_ITEMS

typedef struct DockPinnedApp {
    char desktop_id[DOCK_TEXT_LENGTH];
    char path[DOCK_TEXT_LENGTH * 2];
    char name[DOCK_TEXT_LENGTH];
    char wm_class[DOCK_TEXT_LENGTH];
    char exec[DOCK_TEXT_LENGTH];
    char icon_name[DOCK_TEXT_LENGTH];
    DockIcon icon;
    int valid;
} DockPinnedApp;

typedef struct DockCore DockCore;
typedef struct DockItem DockItem;

void dock_pins_load(DockCore *core);
void dock_pins_free_icons(DockCore *core);
int dock_pins_is_id(const DockCore *core, const char *desktop_id);
int dock_pins_action(DockCore *core, const char *wm_class,
                     const char *desktop_id, char *message,
                     unsigned int message_size);
void dock_pins_context_action(DockCore *core, const DockItem *item,
                              char *message, unsigned int message_size,
                              int *enabled);
int dock_pins_launch(const DockPinnedApp *app);

#endif
