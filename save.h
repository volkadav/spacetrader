#ifndef SPACE_TRADER_SAVE_H
#define SPACE_TRADER_SAVE_H

#include <stddef.h>

#include "game.h"

typedef enum {
    SAVE_LOAD_OK = 0,
    SAVE_LOAD_NOT_FOUND,
    SAVE_LOAD_CORRUPT,
    SAVE_LOAD_ERROR
} save_load_result_t;

bool save_game(const game_t *game, char *error_buffer, size_t error_buffer_size);
save_load_result_t load_game(game_t *game, char *error_buffer, size_t error_buffer_size);
void load_high_scores(game_t *game);
void record_high_score(game_t *game, const char *outcome);

#endif
