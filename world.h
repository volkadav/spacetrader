/* world.h: APIs for world navigation, arrival effects, and persistent ground drops. */
#ifndef SPACE_TRADER_WORLD_H
#define SPACE_TRADER_WORLD_H

#include "game.h"

const location_def_t *world_get_location(location_id_t location);
bool world_are_connected(location_id_t from, location_id_t to);
void world_arrive(game_t *game, location_id_t location, bool travelled);
bool world_travel(game_t *game, location_id_t destination);
void world_rest(game_t *game);
bool world_pay_impound(game_t *game);
void world_decay_drops(game_t *game);
bool world_drop_commodity(game_t *game, location_id_t location, commodity_id_t commodity, int quantity);
bool world_drop_cart(game_t *game, location_id_t location);
bool world_pickup_drop(game_t *game, location_id_t location, int slot_index);
int world_visible_drop_count(const game_t *game, location_id_t location);
void world_handle_mule_death(game_t *game);

#endif
