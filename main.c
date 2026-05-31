#include <string.h>

#include "game.h"
#include "ui.h"

int main(void) {
    game_t game;

    memset(&game, 0, sizeof(game));
    ui_run(&game);
    return 0;
}
