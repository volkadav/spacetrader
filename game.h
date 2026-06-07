/* game.h: Core game data model, constants, enums, and shared state structures. */
#ifndef SPACE_TRADER_GAME_H
#define SPACE_TRADER_GAME_H

#include <stdbool.h>
#include <stdint.h>

#define MAX_LOCATIONS 9
#define NUM_COMMODITIES 15
#define MAX_NEIGHBORS 4
#define MAX_CARGO_STACKS 24
#define MAX_DROP_STACKS_PER_LOCATION 8
#define NEWS_TICKER_LINES 8
#define LOG_LINE_LENGTH 120
#define MAX_HIGH_SCORES 10

typedef enum {
    GAME_STATE_TITLE = 0,
    GAME_STATE_LOCATION,
    GAME_STATE_GAME_OVER,
    GAME_STATE_VICTORY
} game_state_t;

typedef enum {
    LOCATION_STARPORT = 0,
    LOCATION_ASHFIELD,
    LOCATION_BROKENHILL,
    LOCATION_MILLHAVEN,
    LOCATION_COLDWATER,
    LOCATION_DUSTWALLOW,
    LOCATION_IRONPASS,
    LOCATION_SALTMARSH,
    LOCATION_BARRENS
} location_id_t;

typedef enum {
    LOCATION_KIND_HUB = 0,
    LOCATION_KIND_SETTLEMENT,
    LOCATION_KIND_WILDERNESS
} location_kind_t;

typedef enum {
    COMMODITY_FOOD_RATIONS = 0,
    COMMODITY_GRAIN_SEED,
    COMMODITY_LIVESTOCK,
    COMMODITY_LIQUOR,
    COMMODITY_MEDICINAL_HERBS,
    COMMODITY_NARCOTICS,
    COMMODITY_RAW_ORE,
    COMMODITY_REFINED_METAL,
    COMMODITY_CONSTRUCTION_MATL,
    COMMODITY_TOOLS_HARDWARE,
    COMMODITY_FURS_HIDES,
    COMMODITY_SPICES,
    COMMODITY_GEMSTONES,
    COMMODITY_ARTIFACTS,
    COMMODITY_STOLEN_GOODS
} commodity_id_t;

typedef enum {
    WEAPON_FISTS = 0,
    WEAPON_KNIFE,
    WEAPON_MACHETE,
    WEAPON_SLUGTHROWER,
    WEAPON_LASER_PISTOL,
    WEAPON_HUNTING_RIFLE,
    WEAPON_BLASTER,
    WEAPON_COUNT
} weapon_t;

typedef enum {
    ARMOR_NONE = 0,
    ARMOR_LEATHER_JACKET,
    ARMOR_LIGHT_VEST,
    ARMOR_HEAVY_VEST,
    ARMOR_COMBAT_ARMOR,
    ARMOR_COUNT
} armor_t;

typedef struct {
    const char *name;
    const char *category;
    int base_price;
    int units_tenths;
    bool legal;
    bool bulk;
} commodity_def_t;

typedef struct {
    const char *name;
    location_kind_t kind;
    const char *art;
    const char *description;
    int neighbors[MAX_NEIGHBORS];
    int neighbor_count;
} location_def_t;

typedef struct {
    commodity_id_t commodity;
    int quantity;
} cargo_stack_t;

typedef enum {
    DROP_KIND_NONE = 0,
    DROP_KIND_COMMODITY,
    DROP_KIND_CART
} drop_kind_t;

typedef struct {
    bool occupied;
    drop_kind_t kind;
    commodity_id_t commodity;
    int quantity;
    int age;
} drop_slot_t;

typedef struct {
    drop_slot_t slots[MAX_DROP_STACKS_PER_LOCATION];
} location_drops_t;

typedef struct {
    int16_t stock[NUM_COMMODITIES];
    int prices[NUM_COMMODITIES];
    int last_refresh_turn;
    bool known;
} market_state_t;

typedef struct {
    char text[NEWS_TICKER_LINES][LOG_LINE_LENGTH];
    int head;
    int count;
} news_log_t;

typedef struct {
    char name[16];
    int score;
    int turns;
    int credits;
    int cargo_value;
    char outcome[16];
} high_score_t;

typedef struct {
    int hp;
    int max_hp;
    int credits;
    int bank_balance;
    int reputation;
    location_id_t location;
    weapon_t weapon;
    armor_t armor;
    int bandages;
    int medkit_uses;
    bool has_prospecting_kit;
    bool has_sturdy_pack;
    bool has_lucky_charm;
    bool has_mule;
    bool has_cart;
    bool owns_cart;
    bool has_hover;
    cargo_stack_t cargo[MAX_CARGO_STACKS];
    int cargo_count;
    int wanted_level;
} player_t;

typedef struct {
    uint32_t rng_state;
    int turn;
    int poison_turns;
    int prospect_bonus;
    bool running;
    bool player_won;
    bool score_recorded;
    game_state_t state;
    char end_reason[96];
    player_t player;
    market_state_t markets[MAX_LOCATIONS];
    location_drops_t drops[MAX_LOCATIONS];
    news_log_t log;
    high_score_t scores[MAX_HIGH_SCORES];
    int score_count;
    bool stash_rumor_active;
    location_id_t stash_location;
    commodity_id_t stash_commodity;
    bool pending_bribe;
    int bribe_amount;
} game_t;

extern const commodity_def_t COMMODITIES[NUM_COMMODITIES];
extern const location_def_t LOCATIONS[MAX_LOCATIONS];

#endif
