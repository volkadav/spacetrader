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

const location_def_t *world_get_location(location_id_t location) {
    return &LOCATIONS[location];
}

bool world_are_connected(location_id_t from, location_id_t to) {
    const location_def_t *location = &LOCATIONS[from];

    for (int i = 0; i < location->neighbor_count; ++i) {
        if ((location_id_t)location->neighbors[i] == to) {
            return true;
        }
    }
    if (from == LOCATION_STARPORT && to == LOCATION_COLDWATER) {
        return true;
    }
    return false;
}

static int drop_value(const drop_slot_t *slot) {
    return COMMODITIES[slot->commodity].base_price * slot->quantity;
}

static void world_note_drops(game_t *game, location_id_t location) {
    int count = world_visible_drop_count(game, location);
    if (count > 0) {
        game_log(game, "%d dropped stack%s here. Press G to collect.", count, count == 1 ? "" : "s");
    }
}

static void world_autosave(game_t *game) {
    char error[128];

    if (!save_game(game, error, sizeof(error))) {
        game_log(game, "Autosave failed: %s", error);
    }
}

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
                drop->occupied = false;
            }
        }
    }
}

bool world_drop_commodity(game_t *game, location_id_t location, commodity_id_t commodity, int quantity) {
    location_drops_t *drops = &game->drops[location];
    int empty_slot = -1;
    int worst_slot = -1;

    for (int i = 0; i < MAX_DROP_STACKS_PER_LOCATION; ++i) {
        drop_slot_t *slot = &drops->slots[i];
        if (slot->occupied && slot->commodity == commodity) {
            slot->quantity += quantity;
            slot->age = 0;
            return true;
        }
        if (!slot->occupied && empty_slot < 0) {
            empty_slot = i;
        }
        if (slot->occupied) {
            if (worst_slot < 0 ||
                drops->slots[i].age > drops->slots[worst_slot].age ||
                (drops->slots[i].age == drops->slots[worst_slot].age &&
                 drop_value(&drops->slots[i]) < drop_value(&drops->slots[worst_slot]))) {
                worst_slot = i;
            }
        }
    }

    if (empty_slot < 0) {
        empty_slot = worst_slot;
    }
    if (empty_slot < 0) {
        return false;
    }

    drops->slots[empty_slot].occupied = true;
    drops->slots[empty_slot].commodity = commodity;
    drops->slots[empty_slot].quantity = quantity;
    drops->slots[empty_slot].age = 0;
    return true;
}

bool world_pickup_drop(game_t *game, location_id_t location, int slot_index) {
    drop_slot_t *slot;

    if (slot_index < 0 || slot_index >= MAX_DROP_STACKS_PER_LOCATION) {
        return false;
    }
    slot = &game->drops[location].slots[slot_index];
    if (!slot->occupied) {
        return false;
    }
    if (!player_add_cargo(&game->player, slot->commodity, slot->quantity)) {
        game_log(game, "Not enough space for %s.", COMMODITIES[slot->commodity].name);
        return false;
    }

    game_log(game, "Picked up %d x %s.", slot->quantity, COMMODITIES[slot->commodity].name);
    slot->occupied = false;
    return true;
}

int world_visible_drop_count(const game_t *game, location_id_t location) {
    int count = 0;

    for (int i = 0; i < MAX_DROP_STACKS_PER_LOCATION; ++i) {
        if (game->drops[location].slots[i].occupied) {
            count++;
        }
    }
    return count;
}
