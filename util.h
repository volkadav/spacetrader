#ifndef SPACE_TRADER_UTIL_H
#define SPACE_TRADER_UTIL_H

#include <stddef.h>
#include <stdint.h>

#include "game.h"

int clamp_int(int value, int min_value, int max_value);
uint32_t rng_next(game_t *game);
int rng_range(game_t *game, int min_value, int max_value);
bool rng_chance(game_t *game, int percent);
void game_log(game_t *game, const char *fmt, ...);
void game_set_game_over(game_t *game, const char *reason);
void game_advance_turn(game_t *game);
void game_init_new(game_t *game);
uint32_t crc32_bytes(const void *data, size_t length);
bool home_file_path(const char *filename, char *buffer, size_t buffer_size);

#endif
