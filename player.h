/* player.h: Player inventory, equipment, cargo capacity, and shop item interfaces. */
#ifndef SPACE_TRADER_PLAYER_H
#define SPACE_TRADER_PLAYER_H

#include "game.h"

typedef enum {
    SHOP_ITEM_BANDAGE = 0,
    SHOP_ITEM_MEDKIT,
    SHOP_ITEM_PROSPECTING_KIT,
    SHOP_ITEM_STURDY_PACK,
    SHOP_ITEM_LUCKY_CHARM,
    SHOP_ITEM_MULE,
    SHOP_ITEM_CART,
    SHOP_ITEM_CARGO_HOVER,
    SHOP_ITEM_KNIFE,
    SHOP_ITEM_MACHETE,
    SHOP_ITEM_SLUGTHROWER,
    SHOP_ITEM_LASER_PISTOL,
    SHOP_ITEM_HUNTING_RIFLE,
    SHOP_ITEM_BLASTER,
    SHOP_ITEM_LEATHER_JACKET,
    SHOP_ITEM_LIGHT_VEST,
    SHOP_ITEM_HEAVY_VEST,
    SHOP_ITEM_COMBAT_ARMOR,
    SHOP_ITEM_COUNT
} shop_item_id_t;

typedef enum {
    SHOP_KIND_SUPPLY = 0,
    SHOP_KIND_UPGRADE,
    SHOP_KIND_WEAPON,
    SHOP_KIND_ARMOR
} shop_item_kind_t;

typedef struct {
    const char *name;
    shop_item_kind_t kind;
    int cost;
    const char *description;
} shop_item_def_t;

typedef struct {
    const char *name;
    int damage;
    int cost;
} weapon_def_t;

typedef struct {
    const char *name;
    int dr;
    int flee_penalty;
    int cost;
} armor_def_t;

extern const shop_item_def_t SHOP_ITEMS[SHOP_ITEM_COUNT];
extern const weapon_def_t WEAPONS[WEAPON_COUNT];
extern const armor_def_t ARMORS[ARMOR_COUNT];

void player_init(player_t *player);
int player_cargo_capacity_tenths(const player_t *player);
int player_cargo_used_tenths(const player_t *player);
int player_cargo_quantity(const player_t *player, commodity_id_t commodity);
bool player_add_cargo(player_t *player, commodity_id_t commodity, int quantity);
bool player_remove_cargo(player_t *player, commodity_id_t commodity, int quantity);
bool player_has_space_for(const player_t *player, commodity_id_t commodity, int quantity);
void player_heal(player_t *player, int amount);
bool player_use_bandage(game_t *game);
bool player_use_medkit(game_t *game);
int player_weapon_damage(const player_t *player);
int player_armor_dr(const player_t *player);
int player_flee_penalty(const player_t *player);
bool player_can_offer_shop_item(location_id_t location, shop_item_id_t item);
bool player_buy_shop_item(game_t *game, shop_item_id_t item);
bool player_owns_shop_item(const player_t *player, shop_item_id_t item);

#endif
