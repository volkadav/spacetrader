/* main.c: Program entry point that initializes game state and starts the UI
 * loop. */
#include <string.h>

#include "game.h"
#include "ui.h"

/**
 * Purpose: initializes game, starts game loop.
 */
int main(void) {
  game_t game;

  memset(&game, 0, sizeof(game));
  ui_run(&game);
  return 0;
}
