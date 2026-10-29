/*
 * GnuChanDock.c — the entry point.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dock_core.h"
#include "gnuchan_lang.h"

#define PROGRAM_NAME    "GnuChanDock"
#define PROGRAM_VERSION "1.0"

static void print_help(void) {
    printf("%s %s\n", PROGRAM_NAME, PROGRAM_VERSION);
    printf("\n");
    printf("%s\n", gnuchan_lang_t("dock_help_text"));
    printf("\n");
    printf("Usage:\n");
    printf("  %s              %s\n", PROGRAM_NAME,
           gnuchan_lang_t("dock_usage_show"));
    printf("  %s --help       %s\n", PROGRAM_NAME,
           gnuchan_lang_t("dock_usage_help"));
    printf("  %s --version    %s\n", PROGRAM_NAME,
           gnuchan_lang_t("dock_usage_version"));
    printf("\n");
    printf("The settings are read from\n");
    printf("  ~/.config/GnuChanDock/GnuChanDock.py\n");
    printf("and every value has a default, so the dock runs whether or not\n");
    printf("that file is there.\n");
}

int main(int argc, char **argv) {
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            print_help();
            return 0;
        }
        if (strcmp(argv[i], "--version") == 0 || strcmp(argv[i], "-v") == 0) {
            printf("%s %s\n", PROGRAM_NAME, PROGRAM_VERSION);
            return 0;
        }
        fprintf(stderr, "%s\n",
                gnuchan_lang_t("error_unknown_argument"));
        fprintf(stderr, "run '%s --help' for the usage\n", PROGRAM_NAME);
        return 2;
    }

    gnuchan_lang_init();

    DockCore core;
    if (dock_core_init(&core) != 0) {
        return 1;
    }

    dock_core_run(&core);
    dock_core_shutdown(&core);
    return 0;
}
