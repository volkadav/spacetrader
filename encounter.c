/* encounter.c: Travel, wilderness, and bar encounter events and their branching outcomes. */
#include "encounter.h"

#include "combat.h"
#include "market.h"
#include "player.h"
#include "util.h"
#include "world.h"

/**
 * Purpose: Implements contraband base value.
 * Parameters:
 *   - player (const player_t *): Player state used for this operation.
 * Returns:
 *   - int: Computed numeric result for this routine.
 */
static int contraband_base_value(const player_t *player) {
    return (player_cargo_quantity(player, COMMODITY_NARCOTICS) * COMMODITIES[COMMODITY_NARCOTICS].base_price) +
           (player_cargo_quantity(player, COMMODITY_STOLEN_GOODS) * COMMODITIES[COMMODITY_STOLEN_GOODS].base_price);
}

/**
 * Purpose: Implements confiscate contraband.
 * Parameters:
 *   - game (game_t *): Game state this routine reads and/or updates.
 */
static void confiscate_contraband(game_t *game) {
    int base_value = contraband_base_value(&game->player);
    int fine;

    if (base_value <= 0) {
        game_log(game, "A patrol searches your cargo and finds nothing to seize.");
        return;
    }

    player_remove_cargo(&game->player, COMMODITY_NARCOTICS, player_cargo_quantity(&game->player, COMMODITY_NARCOTICS));
    player_remove_cargo(&game->player, COMMODITY_STOLEN_GOODS, player_cargo_quantity(&game->player, COMMODITY_STOLEN_GOODS));
    fine = clamp_int(((base_value + 9) / 10) * 10, 100, 100000);
    if (game->player.credits >= fine) {
        game->player.credits -= fine;
    } else {
        fine = game->player.credits;
        game->player.credits = 0;
    }
    game->player.reputation--;
    game_log(game, "Patrol confiscates contraband and fines you %d cr.", fine);
}

/**
 * Purpose: Implements pick wild enemy.
 * Parameters:
 *   - location (location_id_t): Location identifier used to select travel or market context.
 *   - game (game_t *): Game state this routine reads and/or updates.
 * Returns:
 *   - enemy_id_t: Return value describing the outcome of this routine.
 */
static enemy_id_t pick_wild_enemy(location_id_t location, game_t *game) {
    switch (location) {
    case LOCATION_DUSTWALLOW:
        return rng_chance(game, 40) ? ENEMY_GIANT_SERPENT : ENEMY_FERAL_DOG;
    case LOCATION_IRONPASS:
        return rng_chance(game, 50) ? ENEMY_ROCK_CAT : ENEMY_BANDIT;
    case LOCATION_SALTMARSH:
        return rng_chance(game, 50) ? ENEMY_GIANT_SERPENT : ENEMY_BANDIT;
    case LOCATION_BARRENS:
        return rng_chance(game, 60) ? ENEMY_ROCK_CAT : ENEMY_WILD_BOAR;
    default:
        return ENEMY_FERAL_DOG;
    }
}

/**
 * Purpose: Implements grant abandoned cargo.
 * Parameters:
 *   - game (game_t *): Game state this routine reads and/or updates.
 */
static void grant_abandoned_cargo(game_t *game) {
    commodity_id_t commodity;
    int quantity;

    switch (game->player.location) {
    case LOCATION_SALTMARSH:
        commodity = rng_chance(game, 50) ? COMMODITY_STOLEN_GOODS : COMMODITY_CONSTRUCTION_MATL;
        quantity = 1;
        break;
    case LOCATION_BARRENS:
        commodity = rng_chance(game, 50) ? COMMODITY_GEMSTONES : COMMODITY_SPICES;
        quantity = 1;
        break;
    default:
        commodity = rng_chance(game, 50) ? COMMODITY_TOOLS_HARDWARE : COMMODITY_FOOD_RATIONS;
        quantity = rng_chance(game, 50) ? 1 : 2;
        break;
    }

    if (player_add_cargo(&game->player, commodity, quantity)) {
        game_log(game, "You salvage %d x %s.", quantity, COMMODITIES[commodity].name);
    } else {
        game_log(game, "You find %s but lack the cargo room to keep it.", COMMODITIES[commodity].name);
    }
}

/**
 * Purpose: Implements encounter on travel.
 * Parameters:
 *   - game (game_t *): Game state this routine reads and/or updates.
 *   - from (location_id_t): Location identifier used to select travel or market context.
 *   - to (location_id_t): Location identifier used to select travel or market context.
 */
void encounter_on_travel(game_t *game, location_id_t from, location_id_t to) {
    bool adjacent_to_wilderness = LOCATIONS[from].kind == LOCATION_KIND_WILDERNESS ||
                                  LOCATIONS[to].kind == LOCATION_KIND_WILDERNESS;

    if (!rng_chance(game, adjacent_to_wilderness ? 35 : 20)) {
        game_log(game, "The trip was uneventful.");
        return;
    }

    switch (rng_range(game, 1, 6)) {
    case 1:
        confiscate_contraband(game);
        break;
    case 2: {
        char rumour[128];
        market_generate_rumour(game, rumour, sizeof(rumour));
        game_log(game, "A travelling merchant swaps gossip: %s", rumour);
        break;
    }
    case 3:
        combat_run(game, ENEMY_MUGGER, "Road Encounter: Mugger");
        break;
    case 4:
        if (game->player.bandages > 0) {
            game->player.bandages--;
            game->player.reputation++;
            game_log(game, "You patch up a lost traveller. Reputation +1.");
        } else if (game->player.credits >= 10) {
            game->player.credits -= 10;
            game->player.reputation++;
            game_log(game, "You buy provisions for a stranded traveller. Reputation +1.");
        } else {
            game_log(game, "A lost traveller asks for help. You have nothing to spare.");
        }
        break;
    default:
        game_log(game, "The road offers nothing but dust and distance.");
        break;
    }
}

/**
 * Purpose: Implements encounter on wilderness turn.
 * Parameters:
 *   - game (game_t *): Game state this routine reads and/or updates.
 */
void encounter_on_wilderness_turn(game_t *game) {
    int roll;

    if (LOCATIONS[game->player.location].kind != LOCATION_KIND_WILDERNESS) {
        return;
    }
    if (!rng_chance(game, 50)) {
        game_log(game, "The wilderness stays quiet for a while.");
        return;
    }

    roll = rng_range(game, 1, 10);
    if (game->player.has_lucky_charm) {
        int second = rng_range(game, 1, 10);
        if (second > roll) {
            roll = second;
        }
    }

    /*
     * Wilderness encounter outcomes, ordered from most-negative (lowest
     * roll) to most-positive (highest roll).  The lucky charm operates by
     * taking the better of two independent rolls, so this ordering ensures
     * that "better" consistently means "safer / more rewarding".
     */
    switch (roll) {
    case 1:
    case 2:
        combat_run(game, pick_wild_enemy(game->player.location, game), "Wilderness Encounter");
        break;
    case 3:
    case 4:
        if (game->player.credits >= 25 && game->player.hp <= (game->player.max_hp / 2)) {
            int bribe = rng_range(game, 10, 40);
            bribe = clamp_int(bribe, 0, game->player.credits);
            game->player.credits -= bribe;
            game_log(game, "Bandits shake you down for %d cr and vanish.", bribe);
        } else {
            combat_run(game, rng_chance(game, 20) ? ENEMY_BANDIT_LEADER : ENEMY_BANDIT, "Wilderness Encounter");
        }
        break;
    case 5:
        if (game->player.cargo_count > 0) {
            cargo_stack_t stack = game->player.cargo[rng_range(game, 0, game->player.cargo_count - 1)];
            player_remove_cargo(&game->player, stack.commodity, 1);
            world_drop_commodity(game, game->player.location, stack.commodity, 1);
            game_log(game, "A sudden washout strips 1 x %s from your pack.", COMMODITIES[stack.commodity].name);
        } else {
            game_log(game, "A flash flood hits, but you keep your footing.");
        }
        if (rng_chance(game, 50)) {
            game->player.hp -= 1;
            game_log(game, "You take 1 HP from the scramble.");
            if (game->player.hp <= 0) {
                game_set_game_over(game, "You were finished by the wilds.");
            }
        }
        break;
    case 6:
        if (rng_chance(game, 50)) {
            game_log(game, "You skirt a sinkhole at the last moment.");
        } else {
            int damage = rng_range(game, 1, 2);
            game->player.hp -= damage;
            game_log(game, "Bad water and worse luck cost you %d HP.", damage);
            if (game->player.hp <= 0) {
                game_set_game_over(game, "The wilderness finally claimed you.");
            }
        }
        break;
    case 7:
        game_log(game, "The turn passes without incident.");
        break;
    case 8:
        if (rng_range(game, 1, 6) >= rng_range(game, 1, 6)) {
            game->prospect_bonus += 10;
            game_log(game, "You outwork a rival prospector. Next prospect roll gains +10.");
        } else {
            game_log(game, "A rival prospector strips the obvious seams before you can.");
        }
        break;
    case 9:
        if (game->player.bandages > 0) {
            game->player.bandages--;
            game->player.reputation++;
            game_log(game, "You help an injured traveller. Reputation +1.");
        } else if (game->player.credits >= 20) {
            game->player.credits -= 20;
            game->player.reputation++;
            game_log(game, "You buy passage for an injured traveller. Reputation +1.");
        } else {
            game_log(game, "You find an injured traveller and can only wish them luck.");
        }
        break;
    default:
        grant_abandoned_cargo(game);
        break;
    }
}

/**
 * Purpose: Implements encounter on bar entry.
 * Parameters:
 *   - game (game_t *): Game state this routine reads and/or updates.
 */
void encounter_on_bar_entry(game_t *game) {
    int roll = rng_range(game, 1, 100);

    if (roll <= 30) {
        char rumour[128];
        market_generate_rumour(game, rumour, sizeof(rumour));
        game_log(game, "Bar rumour: %s", rumour);
    } else if (roll <= 50 && game->player.reputation < 2) {
        game_log(game, "A local sizes you up for a brawl.");
    } else {
        game_log(game, "Quiet evening. Mostly regulars and bad cards.");
    }
}
