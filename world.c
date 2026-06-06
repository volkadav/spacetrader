/* world.c: World/location definitions plus travel, arrival, and ground-drop behavior. */
#include "world.h"

#include <stdio.h>
#include <string.h>

#include "encounter.h"
#include "market.h"
#include "player.h"
#include "save.h"
#include "util.h"

const location_def_t LOCATIONS[MAX_LOCATIONS] = {
    [LOCATION_STARPORT] = {
        .name = "Starport",
        .kind = LOCATION_KIND_HUB,
        .art =
            "      __|__\n"
            " --+--(___)--+--\n"
            "   |  BERTH  |\n"
            ".--+----+----+--.\n"
            "| PORT AUTHORITY |\n",
        .description = "The Reach's loud, transactional hub. Your impounded ship waits here for 10,000 credits.",
        .neighbors = {LOCATION_ASHFIELD, LOCATION_BROKENHILL, LOCATION_MILLHAVEN, LOCATION_COLDWATER},
        .neighbor_count = 4
    },
    [LOCATION_ASHFIELD] = {
        .name = "Ashfield",
        .kind = LOCATION_KIND_SETTLEMENT,
        .art =
            " _____   _____   _____ \n"
            "| . . | | . . | | HAY |\n"
            "|FIELD| |FIELD| |BARN |\n",
        .description = "Pale grain fields and a dusty crossroads. Cheap food, modest liquor, hungry demand for tools.",
        .neighbors = {LOCATION_STARPORT, LOCATION_DUSTWALLOW, LOCATION_IRONPASS},
        .neighbor_count = 3
    },
    [LOCATION_BROKENHILL] = {
        .name = "Brokenhill",
        .kind = LOCATION_KIND_SETTLEMENT,
        .art =
            "   /\\      /\\      /\\\n"
            "  /##\\    /##\\    /##\\\n"
            " /####\\  /####\\  /####\\\n"
            "=====[ SHAFT 3 ]=======\n",
        .description = "Ore dust, mine shafts, and hard stares. The cheapest metal on the Reach moves through here.",
        .neighbors = {LOCATION_STARPORT, LOCATION_IRONPASS, LOCATION_SALTMARSH},
        .neighbor_count = 3
    },
    [LOCATION_MILLHAVEN] = {
        .name = "Millhaven",
        .kind = LOCATION_KIND_SETTLEMENT,
        .art =
            " [SILO]   [SILO]   [SILO]\n"
            " |    |   |    |   |    |\n"
            " |    |   |    |   |    |\n"
            " [MILLHAVEN COMMONS]\n",
        .description = "A quiet farming collective with surplus grain and little patience for outsiders.",
        .neighbors = {LOCATION_STARPORT, LOCATION_SALTMARSH, LOCATION_BARRENS},
        .neighbor_count = 3
    },
    [LOCATION_COLDWATER] = {
        .name = "Coldwater",
        .kind = LOCATION_KIND_SETTLEMENT,
        .art =
            "~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~\n"
            "~ ~ ~ RIVER CROSSING FERRY ~ ~ ~\n"
            "~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~\n"
            " COLDWATER STATION\n",
        .description = "A river trade post with steady stock and balanced prices. Reliable, never cheap enough to brag about.",
        .neighbors = {LOCATION_STARPORT, LOCATION_BARRENS, LOCATION_DUSTWALLOW},
        .neighbor_count = 3
    },
    [LOCATION_DUSTWALLOW] = {
        .name = "Dustwallow",
        .kind = LOCATION_KIND_WILDERNESS,
        .art =
            "~~ ~~ ~~ DUSTWALLOW FENS ~~ ~~\n"
            "fog, reeds, drowned trees\n"
            "~~ ~~ ~~ ~~ ~~ ~~ ~~ ~~ ~~ ~~\n",
        .description = "Swampy wetlands rich in herbs and furs, with poor footing and something large in the mist.",
        .neighbors = {LOCATION_ASHFIELD, LOCATION_COLDWATER},
        .neighbor_count = 2
    },
    [LOCATION_IRONPASS] = {
        .name = "Ironpass",
        .kind = LOCATION_KIND_WILDERNESS,
        .art =
            "    /\\      /\\      /\\\n"
            "   /##\\____/##\\____/##\\\n"
            "==/####\\==/####\\==/####\\==\n"
            "  scree, rust, switchbacks\n",
        .description = "A narrow mountain pass littered with rusted equipment, exposed ore, and bandit ambush spots.",
        .neighbors = {LOCATION_ASHFIELD, LOCATION_BROKENHILL},
        .neighbor_count = 2
    },
    [LOCATION_SALTMARSH] = {
        .name = "Saltmarsh",
        .kind = LOCATION_KIND_WILDERNESS,
        .art =
            "=-=-=-=-=-=-=-=-=-=-=-=\n"
            "~~     SALTMARSH    ~~\n"
            "~~~~~~~~~~~~~~~~~~~~~~~\n",
        .description = "Half-buried wreckage and soft tidal channels. Salvage is common; storms arrive without warning.",
        .neighbors = {LOCATION_BROKENHILL, LOCATION_MILLHAVEN},
        .neighbor_count = 2
    },
    [LOCATION_BARRENS] = {
        .name = "The Barrens",
        .kind = LOCATION_KIND_WILDERNESS,
        .art =
            "._._*.__ THE BARRENS __.*_._.\n"
            ".__._.._*.__._.._*.__._..__.\n"
            "._..__._.._*.__._..__._  (x x)\n"
            ".__._.._*.__._.._*.__._  |~~~|\n"
            "._*.__._..__.*_._..__._*_.\n",
        .description = "Cracked clay, bare rock, and gemstone veins close to the surface. Brutal and lucrative.",
        .neighbors = {LOCATION_MILLHAVEN, LOCATION_COLDWATER},
        .neighbor_count = 2
    }
};

/**
 * Purpose: Implements world get location.
 * Parameters:
 *   - location (location_id_t): Location identifier used to select travel or market context.
 * Returns:
 *   - const location_def_t *: Pointer result selected by this routine.
 */
const location_def_t *world_get_location(location_id_t location) {
    return &LOCATIONS[location];
}

/**
 * Purpose: Implements world are connected.
 * Parameters:
 *   - from (location_id_t): Location identifier used to select travel or market context.
 *   - to (location_id_t): Location identifier used to select travel or market context.
 * Returns:
 *   - bool: True when the operation succeeds or the condition is met; false otherwise.
 */
bool world_are_connected(location_id_t from, location_id_t to) {
    const location_def_t *location = &LOCATIONS[from];

    for (int i = 0; i < location->neighbor_count; ++i) {
        if ((location_id_t)location->neighbors[i] == to) {
            return true;
        }
    }
    return false;
}

/**
 * Purpose: Implements drop value.
 * Parameters:
 *   - slot (const drop_slot_t *): Input argument used by this routine.
 * Returns:
 *   - int: Computed numeric result for this routine.
 */
static int drop_value(const drop_slot_t *slot) {
    if (!slot->occupied) {
        return 0;
    }
    if (slot->kind == DROP_KIND_CART) {
        return SHOP_ITEMS[SHOP_ITEM_CART].cost;
    }
    if (slot->kind == DROP_KIND_COMMODITY) {
        return COMMODITIES[slot->commodity].base_price * slot->quantity;
    }
    return 0;
}

/**
 * Purpose: Implements clear drop slot.
 * Parameters:
 *   - slot (drop_slot_t *): Input argument used by this routine.
 */
static void clear_drop_slot(drop_slot_t *slot) {
    slot->occupied = false;
    slot->kind = DROP_KIND_NONE;
    slot->commodity = COMMODITY_FOOD_RATIONS;
    slot->quantity = 0;
    slot->age = 0;
}

/**
 * Purpose: Implements on drop removed.
 * Parameters:
 *   - game (game_t *): Game state this routine reads and/or updates.
 *   - slot (const drop_slot_t *): Input argument used by this routine.
 *   - scavenged (bool): Boolean flag controlling conditional behavior.
 */
static void on_drop_removed(game_t *game, const drop_slot_t *slot, bool scavenged) {
    if (!slot->occupied || slot->kind != DROP_KIND_CART) {
        return;
    }

    if (!game->player.has_cart) {
        game->player.owns_cart = false;
        if (scavenged) {
            game_log(game, "Scavengers hauled off an abandoned cart.");
        }
    }
}

/**
 * Purpose: Implements world place drop.
 * Parameters:
 *   - game (game_t *): Game state this routine reads and/or updates.
 *   - location (location_id_t): Location identifier used to select travel or market context.
 *   - kind (drop_kind_t): Input argument used by this routine.
 *   - commodity (commodity_id_t): Commodity identifier to inspect, add, remove, or price.
 *   - quantity (int): Numeric input used by this routine for calculations or limits.
 * Returns:
 *   - bool: True when the operation succeeds or the condition is met; false otherwise.
 */
static bool world_place_drop(game_t *game,
                             location_id_t location,
                             drop_kind_t kind,
                             commodity_id_t commodity,
                             int quantity) {
    location_drops_t *drops = &game->drops[location];
    int empty_slot = -1;
    int worst_slot = -1;

    for (int i = 0; i < MAX_DROP_STACKS_PER_LOCATION; ++i) {
        drop_slot_t *slot = &drops->slots[i];

        if (!slot->occupied) {
            if (empty_slot < 0) {
                empty_slot = i;
            }
            continue;
        }

        if (kind == DROP_KIND_COMMODITY &&
            slot->kind == DROP_KIND_COMMODITY &&
            slot->commodity == commodity) {
            slot->quantity += quantity;
            slot->age = 0;
            return true;
        }
        if (kind == DROP_KIND_CART && slot->kind == DROP_KIND_CART) {
            slot->age = 0;
            return true;
        }

        if (worst_slot < 0 ||
            drops->slots[i].age > drops->slots[worst_slot].age ||
            (drops->slots[i].age == drops->slots[worst_slot].age &&
             drop_value(&drops->slots[i]) < drop_value(&drops->slots[worst_slot]))) {
            worst_slot = i;
        }
    }

    if (empty_slot < 0) {
        empty_slot = worst_slot;
    }
    if (empty_slot < 0) {
        return false;
    }

    if (drops->slots[empty_slot].occupied) {
        on_drop_removed(game, &drops->slots[empty_slot], false);
    }
    drops->slots[empty_slot].occupied = true;
    drops->slots[empty_slot].kind = kind;
    drops->slots[empty_slot].commodity = commodity;
    drops->slots[empty_slot].quantity = quantity;
    drops->slots[empty_slot].age = 0;
    return true;
}

/**
 * Purpose: Implements world note drops.
 * Parameters:
 *   - game (game_t *): Game state this routine reads and/or updates.
 *   - location (location_id_t): Location identifier used to select travel or market context.
 */
static void world_note_drops(game_t *game, location_id_t location) {
    int count = world_visible_drop_count(game, location);
    if (count > 0) {
        game_log(game, "%d dropped stack%s here. Press G to collect.", count, count == 1 ? "" : "s");
    }
}

/**
 * Purpose: Implements world autosave.
 * Parameters:
 *   - game (game_t *): Game state this routine reads and/or updates.
 */
static void world_autosave(game_t *game) {
    char error[128];

    if (!save_game(game, error, sizeof(error))) {
        game_log(game, "Autosave failed: %s", error);
    }
}

/**
 * Purpose: Implements world arrive.
 * Parameters:
 *   - game (game_t *): Game state this routine reads and/or updates.
 *   - location (location_id_t): Location identifier used to select travel or market context.
 *   - travelled (bool): Boolean flag controlling conditional behavior.
 */
void world_arrive(game_t *game, location_id_t location, bool travelled) {
    game->player.location = location;
    if (market_has_open_market(location)) {
        market_refresh_location(game, location, false);
    }
    if (travelled) {
        game_log(game, "Arrived at %s.", LOCATIONS[location].name);
    }
    world_note_drops(game, location);
    world_autosave(game);
}

/**
 * Purpose: Implements world travel.
 * Parameters:
 *   - game (game_t *): Game state this routine reads and/or updates.
 *   - destination (location_id_t): Location identifier used to select travel or market context.
 * Returns:
 *   - bool: True when the operation succeeds or the condition is met; false otherwise.
 */
bool world_travel(game_t *game, location_id_t destination) {
    location_id_t origin = game->player.location;

    if (!world_are_connected(origin, destination)) {
        game_log(game, "No route from %s to %s.", LOCATIONS[origin].name, LOCATIONS[destination].name);
        return false;
    }

    encounter_on_travel(game, origin, destination);
    if (game->state == GAME_STATE_GAME_OVER) {
        return false;
    }

    game->player.location = destination;
    game_advance_turn(game);
    world_arrive(game, destination, true);
    return true;
}

/**
 * Purpose: Implements world rest.
 * Parameters:
 *   - game (game_t *): Game state this routine reads and/or updates.
 */
void world_rest(game_t *game) {
    if (LOCATIONS[game->player.location].kind != LOCATION_KIND_WILDERNESS) {
        game_log(game, "You can only sleep rough in the wilderness.");
        return;
    }

    encounter_on_wilderness_turn(game);
    if (game->state == GAME_STATE_GAME_OVER) {
        return;
    }

    game_advance_turn(game);
    player_heal(&game->player, 1);
    game_log(game, "You sleep rough and recover 1 HP.");
}

/**
 * Purpose: Implements world pay impound.
 * Parameters:
 *   - game (game_t *): Game state this routine reads and/or updates.
 * Returns:
 *   - bool: True when the operation succeeds or the condition is met; false otherwise.
 */
bool world_pay_impound(game_t *game) {
    if (game->player.location != LOCATION_STARPORT) {
        game_log(game, "You can only pay the impound fee at Starport.");
        return false;
    }
    if (game->player.credits < 10000) {
        game_log(game, "You need 10,000 credits to clear the impound.");
        return false;
    }

    game->player.credits -= 10000;
    game->player_won = true;
    game->state = GAME_STATE_VICTORY;
    snprintf(game->end_reason, sizeof(game->end_reason), "You cleared the impound and lifted off.");
    return true;
}

/**
 * Purpose: Implements world decay drops.
 * Parameters:
 *   - game (game_t *): Game state this routine reads and/or updates.
 */
void world_decay_drops(game_t *game) {
    for (int location = 0; location < MAX_LOCATIONS; ++location) {
        if ((location_id_t)location == game->player.location) {
            continue;
        }
        for (int slot = 0; slot < MAX_DROP_STACKS_PER_LOCATION; ++slot) {
            drop_slot_t *drop = &game->drops[location].slots[slot];
            if (!drop->occupied) {
                continue;
            }
            drop->age++;
            if (rng_chance(game, 10)) {
                on_drop_removed(game, drop, true);
                clear_drop_slot(drop);
            }
        }
    }
}

/**
 * Purpose: Implements world drop commodity.
 * Parameters:
 *   - game (game_t *): Game state this routine reads and/or updates.
 *   - location (location_id_t): Location identifier used to select travel or market context.
 *   - commodity (commodity_id_t): Commodity identifier to inspect, add, remove, or price.
 *   - quantity (int): Numeric input used by this routine for calculations or limits.
 * Returns:
 *   - bool: True when the operation succeeds or the condition is met; false otherwise.
 */
bool world_drop_commodity(game_t *game, location_id_t location, commodity_id_t commodity, int quantity) {
    return world_place_drop(game, location, DROP_KIND_COMMODITY, commodity, quantity);
}

/**
 * Purpose: Implements world drop cart.
 * Parameters:
 *   - game (game_t *): Game state this routine reads and/or updates.
 *   - location (location_id_t): Location identifier used to select travel or market context.
 * Returns:
 *   - bool: True when the operation succeeds or the condition is met; false otherwise.
 */
bool world_drop_cart(game_t *game, location_id_t location) {
    bool dropped = world_place_drop(game, location, DROP_KIND_CART, COMMODITY_FOOD_RATIONS, 1);

    if (dropped) {
        game->player.owns_cart = true;
    }
    return dropped;
}

/**
 * Purpose: Implements world pickup drop.
 * Parameters:
 *   - game (game_t *): Game state this routine reads and/or updates.
 *   - location (location_id_t): Location identifier used to select travel or market context.
 *   - slot_index (int): Input argument used by this routine.
 * Returns:
 *   - bool: True when the operation succeeds or the condition is met; false otherwise.
 */
bool world_pickup_drop(game_t *game, location_id_t location, int slot_index) {
    drop_slot_t *slot;

    if (slot_index < 0 || slot_index >= MAX_DROP_STACKS_PER_LOCATION) {
        return false;
    }
    slot = &game->drops[location].slots[slot_index];
    if (!slot->occupied) {
        return false;
    }

    if (slot->kind == DROP_KIND_CART) {
        if (!game->player.has_mule) {
            game_log(game, "You need a mule to hitch this cart.");
            return false;
        }
        if (game->player.has_cart) {
            game_log(game, "Your mule is already pulling a cart.");
            return false;
        }
        game->player.has_cart = true;
        game->player.owns_cart = true;
        game_log(game, "You hitch the recovered cart to your mule.");
        clear_drop_slot(slot);
        return true;
    }

    if (!player_add_cargo(&game->player, slot->commodity, slot->quantity)) {
        game_log(game, "Not enough space for %s.", COMMODITIES[slot->commodity].name);
        return false;
    }

    game_log(game, "Picked up %d x %s.", slot->quantity, COMMODITIES[slot->commodity].name);
    clear_drop_slot(slot);
    return true;
}

/**
 * Purpose: Implements world visible drop count.
 * Parameters:
 *   - game (const game_t *): Game state this routine reads and/or updates.
 *   - location (location_id_t): Location identifier used to select travel or market context.
 * Returns:
 *   - int: Computed numeric result for this routine.
 */
int world_visible_drop_count(const game_t *game, location_id_t location) {
    int count = 0;

    for (int i = 0; i < MAX_DROP_STACKS_PER_LOCATION; ++i) {
        if (game->drops[location].slots[i].occupied) {
            count++;
        }
    }
    return count;
}

/**
 * Purpose: Implements spill excess cargo.
 * Parameters:
 *   - game (game_t *): Game state this routine reads and/or updates.
 *   - location (location_id_t): Location identifier used to select travel or market context.
 *   - target_capacity_tenths (int): Input argument used by this routine.
 * Returns:
 *   - int: Computed numeric result for this routine.
 */
static int spill_excess_cargo(game_t *game, location_id_t location, int target_capacity_tenths) {
    int spilled_tenths = 0;

    while (player_cargo_used_tenths(&game->player) > target_capacity_tenths && game->player.cargo_count > 0) {
        cargo_stack_t stack = game->player.cargo[game->player.cargo_count - 1];
        int unit_tenths = COMMODITIES[stack.commodity].units_tenths;
        int overflow_tenths = player_cargo_used_tenths(&game->player) - target_capacity_tenths;
        int quantity = (overflow_tenths + unit_tenths - 1) / unit_tenths;

        quantity = clamp_int(quantity, 1, stack.quantity);
        if (!player_remove_cargo(&game->player, stack.commodity, quantity)) {
            break;
        }

        spilled_tenths += unit_tenths * quantity;
        if (!world_drop_commodity(game, location, stack.commodity, quantity)) {
            game_log(game, "Spilled %d x %s, but scavengers got there first.", quantity, COMMODITIES[stack.commodity].name);
        } else {
            game_log(game, "Spilled %d x %s from overloaded cargo.", quantity, COMMODITIES[stack.commodity].name);
        }
    }

    return spilled_tenths;
}

/**
 * Purpose: Implements world handle mule death.
 * Parameters:
 *   - game (game_t *): Game state this routine reads and/or updates.
 */
void world_handle_mule_death(game_t *game) {
    location_id_t location = game->player.location;
    bool had_cart = game->player.has_cart;
    int spilled_tenths;
    int target_capacity_tenths;

    if (!game->player.has_mule) {
        return;
    }

    game->player.has_mule = false;
    game_log(game, "Your mule collapses and dies.");

    if (had_cart) {
        game->player.has_cart = false;
        if (world_drop_cart(game, location)) {
            game_log(game, "The cart breaks free and is left on the ground.");
        } else {
            game->player.owns_cart = false;
            game_log(game, "The cart is lost in the chaos.");
        }
    }

    target_capacity_tenths = player_cargo_capacity_tenths(&game->player);
    spilled_tenths = spill_excess_cargo(game, location, target_capacity_tenths);
    if (spilled_tenths > 0) {
        game_log(game,
                 "Without mule capacity, %d.%d cargo units spill at %s.",
                 spilled_tenths / 10,
                 spilled_tenths % 10,
                 LOCATIONS[location].name);
    }
}
