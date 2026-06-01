/* gamble.h: Types and function declarations for blackjack and roulette subsystems. */
#ifndef SPACE_TRADER_GAMBLE_H
#define SPACE_TRADER_GAMBLE_H

#include <stdbool.h>
#include <stddef.h>

#include "game.h"

typedef enum {
    ROULETTE_BET_STRAIGHT = 0,
    ROULETTE_BET_COLOR,
    ROULETTE_BET_PARITY,
    ROULETTE_BET_RANGE,
    ROULETTE_BET_DOZEN,
    ROULETTE_BET_COLUMN
} roulette_bet_type_t;

typedef enum {
    ROULETTE_COLOR_RED = 0,
    ROULETTE_COLOR_BLACK
} roulette_color_t;

typedef struct {
    roulette_bet_type_t type;
    int value;
} roulette_bet_t;

int gamble_draw_card(game_t *game);
void gamble_format_card(int card, char *buffer, size_t buffer_size);
int gamble_blackjack_value(const int *cards, int card_count, bool *soft);
bool gamble_blackjack_is_natural(const int *cards, int card_count);
bool gamble_blackjack_can_double(int card_count, bool first_action);

int gamble_roulette_spin(game_t *game);
bool gamble_roulette_is_red(int number);
bool gamble_roulette_bet_wins(const roulette_bet_t *bet, int number);
int gamble_roulette_profit_multiplier(const roulette_bet_t *bet);
void gamble_roulette_describe_bet(const roulette_bet_t *bet, char *buffer, size_t buffer_size);

#endif
