/* main.c: Program entry point that initializes game state and starts the UI
 * loop. */
#include <stdio.h>
#include <string.h>

#include "game.h"
#include "save.h"
#include "ui.h"
#include "version.h"

/**
 * Purpose: Implements print usage.
 * Parameters:
 *   - program_name (const char *): Text input used for display, messaging, or
 * formatting.
 */
static void print_usage(const char *program_name) {
  const char *name = (program_name != NULL && program_name[0] != '\0')
                         ? program_name
                         : "spacetrader";

  printf("Usage: %s [--version|--help|--savefile PATH]\n", name);
  printf("See `man 6 spacetrader` for gameplay details and controls.\n");
}

/**
 * Purpose: initializes game, starts game loop.
 */
int main(int argc, char **argv) {
  game_t game;
  char error[256];

  if (argc == 2 && strcmp(argv[1], "--help") == 0) {
    print_usage(argv[0]);
    return 0;
  }

  for (int i = 1; i < argc; ++i) {
    if (strcmp(argv[i], "--version") == 0) {
      printf("%s\n", SPACE_TRADER_VERSION);
      return 0;
    }
    if (strcmp(argv[i], "--savefile") == 0) {
      if (i + 1 >= argc) {
        fprintf(stderr, "Missing path after --savefile.\n");
        fprintf(stderr, "See `man 6 spacetrader` for usage details.\n");
        return 2;
      }
      if (!save_set_path_override(argv[i + 1], error, sizeof(error))) {
        fprintf(stderr, "Invalid --savefile path: %s\n", error);
        return 2;
      }
      i++;
      continue;
    }

    fprintf(stderr, "Unknown option: %s\n", argv[i]);
    fprintf(stderr, "Try --help or see `man 6 spacetrader`.\n");
    return 2;
  }

  memset(&game, 0, sizeof(game));
  ui_run(&game);
  return 0;
}
