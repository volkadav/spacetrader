#ifndef SPACE_TRADER_MARKET_H
#define SPACE_TRADER_MARKET_H

#include <stddef.h>

#include "game.h"

void market_init_all(game_t *game);
void market_refresh_location(game_t *game, location_id_t location, bool force);
bool market_has_open_market(location_id_t location);
bool market_can_buy_openly(location_id_t location, commodity_id_t commodity);
bool market_can_sell_openly(location_id_t location, commodity_id_t commodity);
bool market_buy(game_t *game, location_id_t location, commodity_id_t commodity, int quantity);
bool market_sell(game_t *game, location_id_t location, commodity_id_t commodity, int quantity);
int market_buy_price(const game_t *game, location_id_t location, commodity_id_t commodity);
int market_sell_price(const game_t *game, location_id_t location, commodity_id_t commodity);
int market_fence_price(commodity_id_t commodity);
void market_generate_rumour(game_t *game, char *buffer, size_t buffer_size);
int market_estimate_cargo_value(const game_t *game);

#endif
