/* main.c: Program entry point that initializes game state and starts the UI loop. */
#include <string.h>

#include "game.h"
#include "ui.h"

/**
 * Purpose: Implements main.
 * Parameters:
 *   - None.
 * Returns:
 *   - int: Computed numeric result for this routine.
 */
int main(void) {
    game_t game;

    memset(&game, 0, sizeof(game));
    ui_run(&game);
    return 0;
}
