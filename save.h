#ifndef SPACE_TRADER_SAVE_H
#define SPACE_TRADER_SAVE_H

#include <stddef.h>

#include "game.h"

bool save_game(const game_t *game, char *error_buffer, size_t error_buffer_size);
bool load_game(game_t *game, char *error_buffer, size_t error_buffer_size);
void load_high_scores(game_t *game);
void record_high_score(game_t *game, const char *outcome);

#endif
