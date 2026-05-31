#include "prospect.h"

#include "encounter.h"
#include "player.h"
#include "util.h"

static bool award_find(game_t *game, commodity_id_t commodity, int quantity) {
    if (!player_add_cargo(&game->player, commodity, quantity)) {
        game_log(game, "You find %d x %s but cannot carry it.", quantity, COMMODITIES[commodity].name);
        return false;
    }
    game_log(game, "Prospecting turns up %d x %s.", quantity, COMMODITIES[commodity].name);
    return true;
}

static commodity_id_t common_pick(game_t *game) {
    switch (game->player.location) {
    case LOCATION_BARRENS:
        return COMMODITY_CONSTRUCTION_MATL;
    case LOCATION_SALTMARSH:
        return rng_chance(game, 50) ? COMMODITY_GRAIN_SEED : COMMODITY_CONSTRUCTION_MATL;
    default:
        return rng_chance(game, 50) ? COMMODITY_FOOD_RATIONS : COMMODITY_GRAIN_SEED;
    }
}

static commodity_id_t useful_pick(game_t *game) {
    switch (game->player.location) {
    case LOCATION_DUSTWALLOW:
        return rng_chance(game, 65) ? COMMODITY_MEDICINAL_HERBS : COMMODITY_FURS_HIDES;
    case LOCATION_IRONPASS:
        return rng_chance(game, 70) ? COMMODITY_RAW_ORE : COMMODITY_REFINED_METAL;
    case LOCATION_SALTMARSH:
        return rng_chance(game, 50) ? COMMODITY_FURS_HIDES : COMMODITY_CONSTRUCTION_MATL;
    case LOCATION_BARRENS:
        return rng_chance(game, 70) ? COMMODITY_RAW_ORE : COMMODITY_SPICES;
    default:
        return COMMODITY_RAW_ORE;
    }
}

void prospect_run(game_t *game) {
    int bonus;
    int roll;

    if (LOCATIONS[game->player.location].kind != LOCATION_KIND_WILDERNESS) {
        game_log(game, "There is nothing to prospect here.");
        return;
    }
    if (!game->player.has_prospecting_kit) {
        game_log(game, "You need a prospecting kit first.");
        return;
    }

    encounter_on_wilderness_turn(game);
    if (game->state == GAME_STATE_GAME_OVER) {
        return;
    }

    game_advance_turn(game);
    bonus = 0;
    if (game->player.has_prospecting_kit) {
        bonus += 15;
    }
    bonus += game->prospect_bonus;
    game->prospect_bonus = 0;
    roll = rng_range(game, 1, 100) + bonus;

    if (roll <= 20) {
        game_log(game, "You come back empty-handed.");
    } else if (roll <= 40) {
        award_find(game, common_pick(game), rng_range(game, 1, 2));
    } else if (roll <= 60) {
        award_find(game, useful_pick(game), 1);
    } else if (roll <= 75) {
        commodity_id_t uncommon = rng_chance(game, 40) ? COMMODITY_LIQUOR :
                                  (rng_chance(game, 50) ? COMMODITY_TOOLS_HARDWARE : COMMODITY_REFINED_METAL);
        award_find(game, uncommon, 1);
    } else if (roll <= 88) {
        award_find(game, rng_chance(game, 60) ? COMMODITY_SPICES : COMMODITY_MEDICINAL_HERBS, 1);
    } else if (roll <= 96) {
        award_find(game, COMMODITY_GEMSTONES, 1);
    } else if (roll <= 99) {
        if (rng_chance(game, 50)) {
            award_find(game, useful_pick(game), 1);
            award_find(game, rng_chance(game, 50) ? COMMODITY_TOOLS_HARDWARE : COMMODITY_LIQUOR, 1);
        } else {
            award_find(game, rng_chance(game, 50) ? COMMODITY_RAW_ORE : COMMODITY_CONSTRUCTION_MATL, 1);
        }
    } else {
        award_find(game, COMMODITY_ARTIFACTS, 1);
    }
}
