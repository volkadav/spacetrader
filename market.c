/* market.c: Commodity catalog, dynamic pricing, market refresh, and buy/sell operations. */
#include "market.h"

#include <stdio.h>
#include <string.h>

#include "player.h"
#include "util.h"

const commodity_def_t COMMODITIES[NUM_COMMODITIES] = {
    [COMMODITY_FOOD_RATIONS] = {"Food Rations", "Consumable", 10, 10, true, false},
    [COMMODITY_GRAIN_SEED] = {"Grain & Seed", "Consumable", 6, 10, true, false},
    [COMMODITY_LIVESTOCK] = {"Livestock", "Consumable", 250, 200, true, true},
    [COMMODITY_LIQUOR] = {"Liquor", "Consumable", 35, 5, true, false},
    [COMMODITY_MEDICINAL_HERBS] = {"Medicinal Herbs", "Medicine", 50, 5, true, false},
    [COMMODITY_NARCOTICS] = {"Narcotics", "Medicine", 200, 5, false, false},
    [COMMODITY_RAW_ORE] = {"Raw Ore", "Industrial", 150, 200, true, true},
    [COMMODITY_REFINED_METAL] = {"Refined Metal", "Industrial", 400, 150, true, true},
    [COMMODITY_CONSTRUCTION_MATL] = {"Construction Matl.", "Industrial", 200, 200, true, true},
    [COMMODITY_TOOLS_HARDWARE] = {"Tools & Hardware", "Industrial", 55, 10, true, false},
    [COMMODITY_FURS_HIDES] = {"Furs & Hides", "Luxury", 60, 10, true, false},
    [COMMODITY_SPICES] = {"Spices", "Luxury", 120, 5, true, false},
    [COMMODITY_GEMSTONES] = {"Gemstones", "Luxury", 300, 1, true, false},
    [COMMODITY_ARTIFACTS] = {"Artifacts", "Luxury", 500, 2, true, false},
    [COMMODITY_STOLEN_GOODS] = {"Stolen Goods", "Illegal", 30, 10, false, false}
};

static const int FENCE_BUY_MODIFIERS[MAX_LOCATIONS][NUM_COMMODITIES] = {
    [LOCATION_STARPORT] = {
        [COMMODITY_NARCOTICS] = 110,
        [COMMODITY_STOLEN_GOODS] = 80,
    },
    [LOCATION_ASHFIELD] = {
        [COMMODITY_NARCOTICS] = 130,
        [COMMODITY_STOLEN_GOODS] = 90,
    },
    [LOCATION_BROKENHILL] = {
        [COMMODITY_NARCOTICS] = 130,
        [COMMODITY_STOLEN_GOODS] = 90,
    },
    [LOCATION_MILLHAVEN] = {
        [COMMODITY_NARCOTICS] = 130,
        [COMMODITY_STOLEN_GOODS] = 90,
    },
    [LOCATION_COLDWATER] = {
        [COMMODITY_NARCOTICS] = 130,
        [COMMODITY_STOLEN_GOODS] = 90,
    }
};

static const int LOCATION_MODIFIERS[MAX_LOCATIONS][NUM_COMMODITIES] = {
    [LOCATION_STARPORT] = {
        130, 120, 115, 100, 100, 100, 110, 80, 110, 80, 140, 110, 110, 140, 100
    },
    [LOCATION_ASHFIELD] = {
        70, 70, 105, 80, 105, 100, 150, 140, 110, 130, 95, 115, 110, 100, 100
    },
    [LOCATION_BROKENHILL] = {
        145, 130, 110, 110, 150, 100, 65, 75, 85, 100, 100, 120, 110, 100, 100
    },
    [LOCATION_MILLHAVEN] = {
        65, 60, 105, 100, 110, 100, 145, 150, 105, 140, 80, 150, 120, 100, 100
    },
    [LOCATION_COLDWATER] = {
        100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100
    },
    [LOCATION_DUSTWALLOW] = {100},    /* remaining entries default to 0; wilderness has no market */
    [LOCATION_IRONPASS] = {100},      /* remaining entries default to 0; wilderness has no market */
    [LOCATION_SALTMARSH] = {100},     /* remaining entries default to 0; wilderness has no market */
    [LOCATION_BARRENS] = {100}        /* remaining entries default to 0; wilderness has no market */
};

static const int BASELINE_STOCKS[MAX_LOCATIONS][NUM_COMMODITIES] = {
    [LOCATION_STARPORT] = {8, 7, 0, 6, 4, 0, 3, 6, 2, 8, 2, 2, 1, 0, 0},
    [LOCATION_ASHFIELD] = {14, 16, 0, 7, 3, 0, 0, 0, 4, 3, 2, 1, 0, 0, 0},
    [LOCATION_BROKENHILL] = {4, 3, 1, 4, 1, 0, 10, 7, 8, 4, 1, 1, 1, 0, 0},
    [LOCATION_MILLHAVEN] = {18, 20, 1, 4, 2, 0, 0, 0, 4, 2, 4, 0, 0, 0, 0},
    [LOCATION_COLDWATER] = {7, 7, 0, 5, 3, 0, 3, 2, 4, 6, 3, 2, 1, 0, 0}
};

/**
 * Purpose: Implements stock price factor.
 * Parameters:
 *   - stock (int): Input argument used by this routine.
 * Returns:
 *   - int: Computed numeric result for this routine.
 */
static int stock_price_factor(int stock) {
    if (stock <= 0) {
        return 180;
    }
    if (stock == 1) {
        return 160;
    }
    if (stock == 2) {
        return 145;
    }
    if (stock == 3) {
        return 130;
    }
    if (stock <= 5) {
        return 115;
    }
    if (stock <= 8) {
        return 100;
    }
    if (stock <= 12) {
        return 85;
    }
    return 70;
}

/**
 * Purpose: Implements refresh price.
 * Parameters:
 *   - game (game_t *): Game state this routine reads and/or updates.
 *   - location (location_id_t): Location identifier used to select travel or market context.
 *   - commodity (commodity_id_t): Commodity identifier to inspect, add, remove, or price.
 *   - stock (int): Input argument used by this routine.
 * Returns:
 *   - int: Computed numeric result for this routine.
 */
static int refresh_price(game_t *game, location_id_t location, commodity_id_t commodity, int stock) {
    int price = COMMODITIES[commodity].base_price;
    int jitter = rng_range(game, 90, 110);

    price = (price * LOCATION_MODIFIERS[location][commodity]) / 100;
    price = (price * stock_price_factor(stock)) / 100;
    price = (price * jitter) / 100;
    return clamp_int(price, 1, 100000);
}

/**
 * Purpose: Implements is legal for discount.
 * Parameters:
 *   - commodity (commodity_id_t): Commodity identifier to inspect, add, remove, or price.
 * Returns:
 *   - bool: True when the operation succeeds or the condition is met; false otherwise.
 */
static bool is_legal_for_discount(commodity_id_t commodity) {
    return COMMODITIES[commodity].legal;
}

/**
 * Purpose: Implements apply reputation discount.
 * Parameters:
 *   - game (const game_t *): Game state this routine reads and/or updates.
 *   - commodity (commodity_id_t): Commodity identifier to inspect, add, remove, or price.
 *   - price (int): Numeric input used by this routine for calculations or limits.
 * Returns:
 *   - int: Computed numeric result for this routine.
 */
static int apply_reputation_discount(const game_t *game, commodity_id_t commodity, int price) {
    if (game->player.reputation >= 2 && is_legal_for_discount(commodity)) {
        return (price * 95) / 100;
    }
    return price;
}

/**
 * Purpose: Implements market init all.
 * Parameters:
 *   - game (game_t *): Game state this routine reads and/or updates.
 */
void market_init_all(game_t *game) {
    memset(game->markets, 0, sizeof(game->markets));
    for (int location = 0; location < MAX_LOCATIONS; ++location) {
        if (!market_has_open_market((location_id_t)location)) {
            continue;
        }
        for (int commodity = 0; commodity < NUM_COMMODITIES; ++commodity) {
            game->markets[location].stock[commodity] = BASELINE_STOCKS[location][commodity];
        }
        game->markets[location].last_refresh_turn = -1;
    }
}

/**
 * Purpose: Implements market has open market.
 * Parameters:
 *   - location (location_id_t): Location identifier used to select travel or market context.
 * Returns:
 *   - bool: True when the operation succeeds or the condition is met; false otherwise.
 */
bool market_has_open_market(location_id_t location) {
    return LOCATIONS[location].kind != LOCATION_KIND_WILDERNESS;
}

/**
 * Purpose: Implements market refresh location.
 * Parameters:
 *   - game (game_t *): Game state this routine reads and/or updates.
 *   - location (location_id_t): Location identifier used to select travel or market context.
 *   - force (bool): Boolean flag controlling conditional behavior.
 */
void market_refresh_location(game_t *game, location_id_t location, bool force) {
    market_state_t *market = &game->markets[location];

    if (!market_has_open_market(location)) {
        return;
    }
    if (!force && market->last_refresh_turn >= game->turn) {
        market->known = true;
        return;
    }

    for (int commodity = 0; commodity < NUM_COMMODITIES; ++commodity) {
        int stock = market->stock[commodity];
        int baseline = BASELINE_STOCKS[location][commodity];

        if (stock < baseline) {
            stock += 3;
            if (stock > baseline) {
                stock = baseline;
            }
        } else if (stock > baseline) {
            stock -= 3;
            if (stock < baseline) {
                stock = baseline;
            }
        }

        market->stock[commodity] = (int16_t)stock;
        market->prices[commodity] = refresh_price(game, location, (commodity_id_t)commodity, stock);
    }

    market->known = true;
    market->last_refresh_turn = game->turn;
}

/**
 * Purpose: Implements market can buy openly.
 * Parameters:
 *   - location (location_id_t): Location identifier used to select travel or market context.
 *   - commodity (commodity_id_t): Commodity identifier to inspect, add, remove, or price.
 * Returns:
 *   - bool: True when the operation succeeds or the condition is met; false otherwise.
 */
bool market_can_buy_openly(location_id_t location, commodity_id_t commodity) {
    if (!market_has_open_market(location)) {
        return false;
    }
    if (!COMMODITIES[commodity].legal) {
        return false;
    }
    return commodity != COMMODITY_ARTIFACTS;
}

/**
 * Purpose: Implements market can sell openly.
 * Parameters:
 *   - location (location_id_t): Location identifier used to select travel or market context.
 *   - commodity (commodity_id_t): Commodity identifier to inspect, add, remove, or price.
 * Returns:
 *   - bool: True when the operation succeeds or the condition is met; false otherwise.
 */
bool market_can_sell_openly(location_id_t location, commodity_id_t commodity) {
    if (!market_has_open_market(location)) {
        return false;
    }
    if (!COMMODITIES[commodity].legal) {
        return false;
    }
    if (commodity == COMMODITY_ARTIFACTS) {
        return location == LOCATION_STARPORT;
    }
    return true;
}

/**
 * Purpose: Implements market buy price.
 * Parameters:
 *   - game (const game_t *): Game state this routine reads and/or updates.
 *   - location (location_id_t): Location identifier used to select travel or market context.
 *   - commodity (commodity_id_t): Commodity identifier to inspect, add, remove, or price.
 * Returns:
 *   - int: Computed numeric result for this routine.
 */
int market_buy_price(const game_t *game, location_id_t location, commodity_id_t commodity) {
    return apply_reputation_discount(game, commodity, game->markets[location].prices[commodity]);
}

/**
 * Purpose: Implements market sell price.
 * Parameters:
 *   - game (const game_t *): Game state this routine reads and/or updates.
 *   - location (location_id_t): Location identifier used to select travel or market context.
 *   - commodity (commodity_id_t): Commodity identifier to inspect, add, remove, or price.
 * Returns:
 *   - int: Computed numeric result for this routine.
 */
int market_sell_price(const game_t *game, location_id_t location, commodity_id_t commodity) {
    if (commodity == COMMODITY_ARTIFACTS && location == LOCATION_STARPORT) {
        return (game->markets[location].prices[commodity] * 90) / 100;
    }
    return game->markets[location].prices[commodity];
}

/**
 * Purpose: Implements market fence price.
 * Parameters:
 *   - commodity (commodity_id_t): Commodity identifier to inspect, add, remove, or price.
 * Returns:
 *   - int: Computed numeric result for this routine.
 */
int market_fence_price(location_id_t location, commodity_id_t commodity) {
    (void)location;
    return (COMMODITIES[commodity].base_price * 70) / 100;
}

int market_fence_buy_price(location_id_t location, commodity_id_t commodity) {
    int modifier = FENCE_BUY_MODIFIERS[location][commodity];
    if (modifier == 0) {
        modifier = 100;
    }
    return (COMMODITIES[commodity].base_price * modifier) / 100;
}

bool market_fence_buy(game_t *game, location_id_t location, commodity_id_t commodity, int quantity) {
    int price = market_fence_buy_price(location, commodity);
    int total_price = price * quantity;

    if (commodity != COMMODITY_NARCOTICS && commodity != COMMODITY_STOLEN_GOODS) {
        if (commodity == COMMODITY_ARTIFACTS) {
            game_log(game, "The fence only buys artifacts, not sells them.");
        } else {
            game_log(game, "The fence does not deal in %s.", COMMODITIES[commodity].name);
        }
        return false;
    }
    if (game->player.credits < total_price) {
        game_log(game, "You cannot afford %s.", COMMODITIES[commodity].name);
        return false;
    }
    if (!player_add_cargo(&game->player, commodity, quantity)) {
        game_log(game, "Not enough cargo space.");
        return false;
    }

    game->player.credits -= total_price;
    game_log(game, "Bought %d x %s from the fence for %d cr.", quantity, COMMODITIES[commodity].name, total_price);
    return true;
}

bool market_fence_sell(game_t *game, location_id_t location, commodity_id_t commodity, int quantity) {
    int price = market_fence_price(location, commodity);
    int total_price = price * quantity;

    if (commodity != COMMODITY_NARCOTICS && commodity != COMMODITY_STOLEN_GOODS &&
        commodity != COMMODITY_ARTIFACTS) {
        game_log(game, "The fence does not deal in %s.", COMMODITIES[commodity].name);
        return false;
    }
    if (!player_remove_cargo(&game->player, commodity, quantity)) {
        game_log(game, "You do not have enough %s.", COMMODITIES[commodity].name);
        return false;
    }

    game->player.credits += total_price;
    game_log(game, "Fenced %d x %s for %d cr.", quantity, COMMODITIES[commodity].name, total_price);
    return true;
}

/**
 * Purpose: Implements market buy.
 * Parameters:
 *   - game (game_t *): Game state this routine reads and/or updates.
 *   - location (location_id_t): Location identifier used to select travel or market context.
 *   - commodity (commodity_id_t): Commodity identifier to inspect, add, remove, or price.
 *   - quantity (int): Numeric input used by this routine for calculations or limits.
 * Returns:
 *   - bool: True when the operation succeeds or the condition is met; false otherwise.
 */
bool market_buy(game_t *game, location_id_t location, commodity_id_t commodity, int quantity) {
    market_state_t *market = &game->markets[location];
    int total_price = market_buy_price(game, location, commodity) * quantity;

    if (!market_can_buy_openly(location, commodity)) {
        game_log(game, "%s is not sold openly here.", COMMODITIES[commodity].name);
        return false;
    }
    if (market->stock[commodity] < quantity) {
        game_log(game, "Not enough %s in stock.", COMMODITIES[commodity].name);
        return false;
    }
    if (game->player.credits < total_price) {
        game_log(game, "You cannot afford %s.", COMMODITIES[commodity].name);
        return false;
    }
    if (!player_add_cargo(&game->player, commodity, quantity)) {
        game_log(game, "Not enough cargo space.");
        return false;
    }

    market->stock[commodity] -= quantity;
    game->player.credits -= total_price;
    game_log(game, "Bought %d x %s for %d cr.", quantity, COMMODITIES[commodity].name, total_price);
    return true;
}

/**
 * Purpose: Implements market sell.
 * Parameters:
 *   - game (game_t *): Game state this routine reads and/or updates.
 *   - location (location_id_t): Location identifier used to select travel or market context.
 *   - commodity (commodity_id_t): Commodity identifier to inspect, add, remove, or price.
 *   - quantity (int): Numeric input used by this routine for calculations or limits.
 * Returns:
 *   - bool: True when the operation succeeds or the condition is met; false otherwise.
 */
bool market_sell(game_t *game, location_id_t location, commodity_id_t commodity, int quantity) {
    market_state_t *market = &game->markets[location];
    int total_price = market_sell_price(game, location, commodity) * quantity;

    if (!market_can_sell_openly(location, commodity)) {
        game_log(game, "No legitimate buyer for %s here.", COMMODITIES[commodity].name);
        return false;
    }
    if (!player_remove_cargo(&game->player, commodity, quantity)) {
        game_log(game, "You do not have enough %s.", COMMODITIES[commodity].name);
        return false;
    }

    market->stock[commodity] = clamp_int(market->stock[commodity] + quantity, 0, 30);
    game->player.credits += total_price;
    game_log(game, "Sold %d x %s for %d cr.", quantity, COMMODITIES[commodity].name, total_price);
    return true;
}

/**
 * Purpose: Implements market generate rumour.
 * Parameters:
 *   - game (game_t *): Game state this routine reads and/or updates.
 *   - buffer (char *): Output buffer populated by this routine.
 *   - buffer_size (size_t): Output buffer populated by this routine.
 */
void market_generate_rumour(game_t *game, char *buffer, size_t buffer_size) {
    int best_location = -1;
    int best_commodity = -1;
    int best_score = 0;
    bool shortage = true;

    for (int location = 0; location < MAX_LOCATIONS; ++location) {
        if (!game->markets[location].known || !market_has_open_market((location_id_t)location)) {
            continue;
        }
        for (int commodity = 0; commodity < NUM_COMMODITIES; ++commodity) {
            int price = game->markets[location].prices[commodity];
            int ratio = (price * 100) / COMMODITIES[commodity].base_price;
            int spread = ratio > 100 ? ratio - 100 : 100 - ratio;

            if (!COMMODITIES[commodity].legal || commodity == COMMODITY_ARTIFACTS) {
                continue;
            }
            if (spread > best_score) {
                best_score = spread;
                best_location = location;
                best_commodity = commodity;
                shortage = ratio > 100;
            }
        }
    }

    if (best_location < 0) {
        snprintf(buffer, buffer_size, "Millhaven's drowning in grain. Starport is paying well.");
        return;
    }

    snprintf(buffer,
             buffer_size,
             "%s is %s %s %s %s.",
             LOCATIONS[best_location].name,
             shortage ? "hungry" : "flush",
             shortage ? "for" : "on",
             COMMODITIES[best_commodity].name,
             shortage ? "right now" : "for the moment");
}

/**
 * Purpose: Implements market estimate cargo value.
 * Parameters:
 *   - game (const game_t *): Game state this routine reads and/or updates.
 * Returns:
 *   - int: Computed numeric result for this routine.
 */
void market_generate_stash_rumour(game_t *game, char *buffer, size_t buffer_size) {
    static const location_id_t wilds[] = {
        LOCATION_DUSTWALLOW, LOCATION_IRONPASS, LOCATION_SALTMARSH, LOCATION_BARRENS
    };
    commodity_id_t commodity;

    game->stash_location = wilds[rng_range(game, 0, 3)];
    if (rng_chance(game, 50)) {
        game->stash_commodity = COMMODITY_NARCOTICS;
        commodity = COMMODITY_NARCOTICS;
    } else {
        game->stash_commodity = COMMODITY_STOLEN_GOODS;
        commodity = COMMODITY_STOLEN_GOODS;
    }
    game->stash_rumor_active = true;

    snprintf(buffer, buffer_size,
             "A hidden cache of %s is stashed somewhere in %s. Worth a look.",
             COMMODITIES[commodity].name, LOCATIONS[game->stash_location].name);
}

int market_estimate_cargo_value(const game_t *game) {
    int total = 0;

    for (int i = 0; i < game->player.cargo_count; ++i) {
        total += COMMODITIES[game->player.cargo[i].commodity].base_price * game->player.cargo[i].quantity;
    }
    return total;
}
