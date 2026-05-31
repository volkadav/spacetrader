#include "player.h"

#include <string.h>

#include "util.h"

const weapon_def_t WEAPONS[WEAPON_COUNT] = {
    [WEAPON_FISTS] = {"Fists", 1, 0},
    [WEAPON_KNIFE] = {"Knife", 2, 25},
    [WEAPON_MACHETE] = {"Machete", 3, 50},
    [WEAPON_SLUGTHROWER] = {"Slugthrower Pistol", 4, 125},
    [WEAPON_LASER_PISTOL] = {"Laser Pistol", 5, 250},
    [WEAPON_HUNTING_RIFLE] = {"Hunting Rifle", 6, 500},
    [WEAPON_BLASTER] = {"Blaster", 8, 1000}
};

const armor_def_t ARMORS[ARMOR_COUNT] = {
    [ARMOR_NONE] = {"None", 0, 0, 0},
    [ARMOR_LEATHER_JACKET] = {"Leather Jacket", 1, 0, 80},
    [ARMOR_LIGHT_VEST] = {"Light Vest", 2, 0, 200},
    [ARMOR_HEAVY_VEST] = {"Heavy Vest", 3, 5, 450},
    [ARMOR_COMBAT_ARMOR] = {"Combat Armor", 5, 10, 900}
};

const shop_item_def_t SHOP_ITEMS[SHOP_ITEM_COUNT] = {
    [SHOP_ITEM_BANDAGE] = {"Bandage", SHOP_KIND_SUPPLY, 15, "+1 HP, single use"},
    [SHOP_ITEM_MEDKIT] = {"Medkit", SHOP_KIND_SUPPLY, 60, "3 uses, +2 HP per use"},
    [SHOP_ITEM_PROSPECTING_KIT] = {"Prospecting Kit", SHOP_KIND_UPGRADE, 150, "+15 to prospect rolls"},
    [SHOP_ITEM_STURDY_PACK] = {"Sturdy Pack", SHOP_KIND_UPGRADE, 100, "Pack capacity 10 -> 20"},
    [SHOP_ITEM_LUCKY_CHARM] = {"Lucky Charm", SHOP_KIND_UPGRADE, 80, "Safer encounter tables"},
    [SHOP_ITEM_MULE] = {"Mule", SHOP_KIND_UPGRADE, 200, "+10 cargo capacity"},
    [SHOP_ITEM_CART] = {"Cart", SHOP_KIND_UPGRADE, 400, "Adds +20 more cargo with mule"},
    [SHOP_ITEM_CARGO_HOVER] = {"Cargo Hover", SHOP_KIND_UPGRADE, 1500, "+100 cargo capacity"},
    [SHOP_ITEM_KNIFE] = {"Knife", SHOP_KIND_WEAPON, 25, "2 damage"},
    [SHOP_ITEM_MACHETE] = {"Machete", SHOP_KIND_WEAPON, 50, "3 damage"},
    [SHOP_ITEM_SLUGTHROWER] = {"Slugthrower Pistol", SHOP_KIND_WEAPON, 125, "4 damage"},
    [SHOP_ITEM_LASER_PISTOL] = {"Laser Pistol", SHOP_KIND_WEAPON, 250, "5 damage"},
    [SHOP_ITEM_HUNTING_RIFLE] = {"Hunting Rifle", SHOP_KIND_WEAPON, 500, "6 damage"},
    [SHOP_ITEM_BLASTER] = {"Blaster", SHOP_KIND_WEAPON, 1000, "8 damage"},
    [SHOP_ITEM_LEATHER_JACKET] = {"Leather Jacket", SHOP_KIND_ARMOR, 80, "DR 1"},
    [SHOP_ITEM_LIGHT_VEST] = {"Light Vest", SHOP_KIND_ARMOR, 200, "DR 2"},
    [SHOP_ITEM_HEAVY_VEST] = {"Heavy Vest", SHOP_KIND_ARMOR, 450, "DR 3, -5 flee"},
    [SHOP_ITEM_COMBAT_ARMOR] = {"Combat Armor", SHOP_KIND_ARMOR, 900, "DR 5, -10 flee"}
};

static void compact_cargo(player_t *player) {
    int write_index = 0;

    for (int i = 0; i < player->cargo_count; ++i) {
        if (player->cargo[i].quantity > 0) {
            if (write_index != i) {
                player->cargo[write_index] = player->cargo[i];
            }
            write_index++;
        }
    }
    player->cargo_count = write_index;
}

static int discount_price(const game_t *game, int price) {
    if (game->player.reputation >= 2) {
        return (price * 95) / 100;
    }
    return price;
}

void player_init(player_t *player) {
    memset(player, 0, sizeof(*player));
    player->hp = 10;
    player->max_hp = 10;
    player->credits = 100;
    player->location = LOCATION_STARPORT;
    player->weapon = WEAPON_FISTS;
    player->armor = ARMOR_NONE;
}

int player_cargo_capacity_tenths(const player_t *player) {
    int capacity = player->has_sturdy_pack ? 200 : 100;

    if (player->has_mule) {
        capacity += player->has_cart ? 300 : 100;
    }
    if (player->has_hover) {
        capacity += 1000;
    }
    return capacity;
}

int player_cargo_used_tenths(const player_t *player) {
    int total = 0;

    for (int i = 0; i < player->cargo_count; ++i) {
        total += COMMODITIES[player->cargo[i].commodity].units_tenths * player->cargo[i].quantity;
    }
    return total;
}

int player_cargo_quantity(const player_t *player, commodity_id_t commodity) {
    for (int i = 0; i < player->cargo_count; ++i) {
        if (player->cargo[i].commodity == commodity) {
            return player->cargo[i].quantity;
        }
    }
    return 0;
}

bool player_has_space_for(const player_t *player, commodity_id_t commodity, int quantity) {
    int needed = COMMODITIES[commodity].units_tenths * quantity;
    return player_cargo_used_tenths(player) + needed <= player_cargo_capacity_tenths(player);
}

bool player_add_cargo(player_t *player, commodity_id_t commodity, int quantity) {
    if (!player_has_space_for(player, commodity, quantity)) {
        return false;
    }

    for (int i = 0; i < player->cargo_count; ++i) {
        if (player->cargo[i].commodity == commodity) {
            player->cargo[i].quantity += quantity;
            return true;
        }
    }

    if (player->cargo_count >= MAX_CARGO_STACKS) {
        return false;
    }

    player->cargo[player->cargo_count].commodity = commodity;
    player->cargo[player->cargo_count].quantity = quantity;
    player->cargo_count++;
    return true;
}

bool player_remove_cargo(player_t *player, commodity_id_t commodity, int quantity) {
    for (int i = 0; i < player->cargo_count; ++i) {
        if (player->cargo[i].commodity == commodity) {
            if (player->cargo[i].quantity < quantity) {
                return false;
            }
            player->cargo[i].quantity -= quantity;
            compact_cargo(player);
            return true;
        }
    }
    return false;
}

void player_heal(player_t *player, int amount) {
    player->hp = clamp_int(player->hp + amount, 0, player->max_hp);
}

bool player_use_bandage(game_t *game) {
    if (game->player.bandages <= 0) {
        game_log(game, "You do not have a bandage.");
        return false;
    }
    if (game->player.hp >= game->player.max_hp && game->poison_turns == 0) {
        game_log(game, "You are already in good shape.");
        return false;
    }

    game->player.bandages--;
    player_heal(&game->player, 1);
    if (game->poison_turns > 0) {
        game->poison_turns = 0;
        game_log(game, "Bandage applied. The poison stops burning through you.");
    } else {
        game_log(game, "Bandage applied. +1 HP.");
    }
    return true;
}

bool player_use_medkit(game_t *game) {
    if (game->player.medkit_uses <= 0) {
        game_log(game, "Your medkit is empty.");
        return false;
    }
    if (game->player.hp >= game->player.max_hp && game->poison_turns == 0) {
        game_log(game, "You are already fully healed.");
        return false;
    }

    game->player.medkit_uses--;
    player_heal(&game->player, 2);
    if (game->poison_turns > 0) {
        game->poison_turns = 0;
        game_log(game, "Medkit applied. +2 HP and the poison is treated.");
    } else {
        game_log(game, "Medkit applied. +2 HP.");
    }
    return true;
}

int player_weapon_damage(const player_t *player) {
    return WEAPONS[player->weapon].damage;
}

int player_armor_dr(const player_t *player) {
    return ARMORS[player->armor].dr;
}

int player_flee_penalty(const player_t *player) {
    return ARMORS[player->armor].flee_penalty;
}

bool player_can_offer_shop_item(location_id_t location, shop_item_id_t item) {
    if (LOCATIONS[location].kind == LOCATION_KIND_WILDERNESS) {
        return false;
    }
    if (item == SHOP_ITEM_CARGO_HOVER) {
        return location == LOCATION_STARPORT;
    }
    return true;
}

bool player_owns_shop_item(const player_t *player, shop_item_id_t item) {
    switch (item) {
    case SHOP_ITEM_PROSPECTING_KIT:
        return player->has_prospecting_kit;
    case SHOP_ITEM_STURDY_PACK:
        return player->has_sturdy_pack;
    case SHOP_ITEM_LUCKY_CHARM:
        return player->has_lucky_charm;
    case SHOP_ITEM_MULE:
        return player->has_mule;
    case SHOP_ITEM_CART:
        return player->has_cart;
    case SHOP_ITEM_CARGO_HOVER:
        return player->has_hover;
    case SHOP_ITEM_KNIFE:
        return player->weapon == WEAPON_KNIFE;
    case SHOP_ITEM_MACHETE:
        return player->weapon == WEAPON_MACHETE;
    case SHOP_ITEM_SLUGTHROWER:
        return player->weapon == WEAPON_SLUGTHROWER;
    case SHOP_ITEM_LASER_PISTOL:
        return player->weapon == WEAPON_LASER_PISTOL;
    case SHOP_ITEM_HUNTING_RIFLE:
        return player->weapon == WEAPON_HUNTING_RIFLE;
    case SHOP_ITEM_BLASTER:
        return player->weapon == WEAPON_BLASTER;
    case SHOP_ITEM_LEATHER_JACKET:
        return player->armor == ARMOR_LEATHER_JACKET;
    case SHOP_ITEM_LIGHT_VEST:
        return player->armor == ARMOR_LIGHT_VEST;
    case SHOP_ITEM_HEAVY_VEST:
        return player->armor == ARMOR_HEAVY_VEST;
    case SHOP_ITEM_COMBAT_ARMOR:
        return player->armor == ARMOR_COMBAT_ARMOR;
    default:
        return false;
    }
}

bool player_buy_shop_item(game_t *game, shop_item_id_t item) {
    int price = discount_price(game, SHOP_ITEMS[item].cost);

    if (player_owns_shop_item(&game->player, item)) {
        game_log(game, "You already have %s.", SHOP_ITEMS[item].name);
        return false;
    }
    if (!player_can_offer_shop_item(game->player.location, item)) {
        game_log(game, "%s is not sold here.", SHOP_ITEMS[item].name);
        return false;
    }
    if (item == SHOP_ITEM_CART && !game->player.has_mule) {
        game_log(game, "You need a mule before you can buy a cart.");
        return false;
    }
    if (game->player.credits < price) {
        game_log(game, "You cannot afford %s.", SHOP_ITEMS[item].name);
        return false;
    }

    game->player.credits -= price;
    switch (item) {
    case SHOP_ITEM_BANDAGE:
        game->player.bandages++;
        break;
    case SHOP_ITEM_MEDKIT:
        game->player.medkit_uses += 3;
        break;
    case SHOP_ITEM_PROSPECTING_KIT:
        game->player.has_prospecting_kit = true;
        break;
    case SHOP_ITEM_STURDY_PACK:
        game->player.has_sturdy_pack = true;
        break;
    case SHOP_ITEM_LUCKY_CHARM:
        game->player.has_lucky_charm = true;
        break;
    case SHOP_ITEM_MULE:
        game->player.has_mule = true;
        break;
    case SHOP_ITEM_CART:
        game->player.has_cart = true;
        break;
    case SHOP_ITEM_CARGO_HOVER:
        game->player.has_hover = true;
        break;
    case SHOP_ITEM_KNIFE:
        game->player.weapon = WEAPON_KNIFE;
        break;
    case SHOP_ITEM_MACHETE:
        game->player.weapon = WEAPON_MACHETE;
        break;
    case SHOP_ITEM_SLUGTHROWER:
        game->player.weapon = WEAPON_SLUGTHROWER;
        break;
    case SHOP_ITEM_LASER_PISTOL:
        game->player.weapon = WEAPON_LASER_PISTOL;
        break;
    case SHOP_ITEM_HUNTING_RIFLE:
        game->player.weapon = WEAPON_HUNTING_RIFLE;
        break;
    case SHOP_ITEM_BLASTER:
        game->player.weapon = WEAPON_BLASTER;
        break;
    case SHOP_ITEM_LEATHER_JACKET:
        game->player.armor = ARMOR_LEATHER_JACKET;
        break;
    case SHOP_ITEM_LIGHT_VEST:
        game->player.armor = ARMOR_LIGHT_VEST;
        break;
    case SHOP_ITEM_HEAVY_VEST:
        game->player.armor = ARMOR_HEAVY_VEST;
        break;
    case SHOP_ITEM_COMBAT_ARMOR:
        game->player.armor = ARMOR_COMBAT_ARMOR;
        break;
    default:
        return false;
    }

    game_log(game, "Purchased %s for %d cr.", SHOP_ITEMS[item].name, price);
    return true;
}
