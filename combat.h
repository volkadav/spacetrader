#ifndef SPACE_TRADER_COMBAT_H
#define SPACE_TRADER_COMBAT_H

#include "game.h"

typedef enum {
    ENEMY_FERAL_DOG = 0,
    ENEMY_WILD_BOAR,
    ENEMY_GIANT_SERPENT,
    ENEMY_ROCK_CAT,
    ENEMY_MUGGER,
    ENEMY_BANDIT,
    ENEMY_BANDIT_LEADER,
    ENEMY_BAR_BRAWLER,
    ENEMY_COUNT
} enemy_id_t;

typedef enum {
    COMBAT_RESULT_WON = 0,
    COMBAT_RESULT_ESCAPED,
    COMBAT_RESULT_LOST
} combat_result_t;

typedef struct {
    const char *name;
    int max_hp;
    int dr;
    int damage;
    int flee_penalty;
    int flees_at_pct;
    bool poisonous;
    int bounty_min;
    int bounty_max;
} enemy_t;

extern const enemy_t ENEMIES[ENEMY_COUNT];

combat_result_t combat_run(game_t *game, enemy_id_t enemy_id, const char *title);

#endif
