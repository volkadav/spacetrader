/* encounter.h: Public hooks for triggering travel, wilderness, and bar encounter events. */
#ifndef SPACE_TRADER_ENCOUNTER_H
#define SPACE_TRADER_ENCOUNTER_H

#include "game.h"

void encounter_on_travel(game_t *game, location_id_t from, location_id_t to);
void encounter_on_wilderness_turn(game_t *game);
void encounter_on_bar_entry(game_t *game);

#endif
