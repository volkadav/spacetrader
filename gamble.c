/* gamble.c: Blackjack and roulette mechanics used by bar gambling menus. */
#include "gamble.h"

#include <stdio.h>

#include "util.h"

/**
 * Purpose: Implements gamble draw card.
 * Parameters:
 *   - game (game_t *): Game state this routine reads and/or updates.
 * Returns:
 *   - int: Computed numeric result for this routine.
 */
int gamble_draw_card(game_t *game) {
    return rng_range(game, 0, 51);
}

/**
 * Purpose: Implements gamble format card.
 * Parameters:
 *   - card (int): Input argument used by this routine.
 *   - buffer (char *): Output buffer populated by this routine.
 *   - buffer_size (size_t): Output buffer populated by this routine.
 */
void gamble_format_card(int card, char *buffer, size_t buffer_size) {
    static const char *RANKS[] = {
        "A", "2", "3", "4", "5", "6", "7", "8", "9", "10", "J", "Q", "K"
    };
    static const char SUITS[] = {'C', 'D', 'H', 'S'};
    int rank;
    int suit;

    if (buffer_size == 0) {
        return;
    }

    if (card < 0 || card > 51) {
        snprintf(buffer, buffer_size, "??");
        return;
    }

    rank = card % 13;
    suit = card / 13;
    snprintf(buffer, buffer_size, "%s%c", RANKS[rank], SUITS[suit]);
}

/**
 * Purpose: Implements gamble blackjack value.
 * Parameters:
 *   - cards (const int *): Input argument used by this routine.
 *   - card_count (int): Numeric input used by this routine for calculations or limits.
 *   - soft (bool *): Input argument used by this routine.
 * Returns:
 *   - int: Computed numeric result for this routine.
 */
int gamble_blackjack_value(const int *cards, int card_count, bool *soft) {
    int total = 0;
    int aces = 0;
    bool hand_is_soft = false;

    for (int i = 0; i < card_count; ++i) {
        int rank = (cards[i] % 13) + 1;

        if (rank == 1) {
            aces++;
        } else if (rank >= 10) {
            total += 10;
        } else {
            total += rank;
        }
    }

    total += aces;
    while (aces > 0 && total + 10 <= 21) {
        total += 10;
        aces--;
        hand_is_soft = true;
    }

    if (soft != NULL) {
        *soft = hand_is_soft;
    }
    return total;
}

/**
 * Purpose: Implements gamble blackjack is natural.
 * Parameters:
 *   - cards (const int *): Input argument used by this routine.
 *   - card_count (int): Numeric input used by this routine for calculations or limits.
 * Returns:
 *   - bool: True when the operation succeeds or the condition is met; false otherwise.
 */
bool gamble_blackjack_is_natural(const int *cards, int card_count) {
    return card_count == 2 && gamble_blackjack_value(cards, card_count, NULL) == 21;
}

/**
 * Purpose: Implements gamble blackjack can double.
 * Parameters:
 *   - card_count (int): Numeric input used by this routine for calculations or limits.
 *   - first_action (bool): Boolean flag controlling conditional behavior.
 * Returns:
 *   - bool: True when the operation succeeds or the condition is met; false otherwise.
 */
bool gamble_blackjack_can_double(int card_count, bool first_action) {
    return first_action && card_count == 2;
}

/**
 * Purpose: Implements gamble roulette spin.
 * Parameters:
 *   - game (game_t *): Game state this routine reads and/or updates.
 * Returns:
 *   - int: Computed numeric result for this routine.
 */
int gamble_roulette_spin(game_t *game) {
    return rng_range(game, 0, 36);
}

/**
 * Purpose: Implements gamble roulette is red.
 * Parameters:
 *   - number (int): Input argument used by this routine.
 * Returns:
 *   - bool: True when the operation succeeds or the condition is met; false otherwise.
 */
bool gamble_roulette_is_red(int number) {
    switch (number) {
    case 1:
    case 3:
    case 5:
    case 7:
    case 9:
    case 12:
    case 14:
    case 16:
    case 18:
    case 19:
    case 21:
    case 23:
    case 25:
    case 27:
    case 30:
    case 32:
    case 34:
    case 36:
        return true;
    default:
        return false;
    }
}

/**
 * Purpose: Implements roulette is black.
 * Parameters:
 *   - number (int): Input argument used by this routine.
 * Returns:
 *   - bool: True when the operation succeeds or the condition is met; false otherwise.
 */
static bool roulette_is_black(int number) {
    return number != 0 && !gamble_roulette_is_red(number);
}

/**
 * Purpose: Implements gamble roulette bet wins.
 * Parameters:
 *   - bet (const roulette_bet_t *): Input argument used by this routine.
 *   - number (int): Input argument used by this routine.
 * Returns:
 *   - bool: True when the operation succeeds or the condition is met; false otherwise.
 */
bool gamble_roulette_bet_wins(const roulette_bet_t *bet, int number) {
    if (bet == NULL || number < 0 || number > 36) {
        return false;
    }

    switch (bet->type) {
    case ROULETTE_BET_STRAIGHT:
        return number == bet->value;
    case ROULETTE_BET_COLOR:
        if (number == 0) {
            return false;
        }
        if (bet->value == ROULETTE_COLOR_RED) {
            return gamble_roulette_is_red(number);
        }
        return roulette_is_black(number);
    case ROULETTE_BET_PARITY:
        if (number == 0) {
            return false;
        }
        if (bet->value == 0) {
            return (number % 2) == 1;
        }
        return (number % 2) == 0;
    case ROULETTE_BET_RANGE:
        if (number == 0) {
            return false;
        }
        if (bet->value == 0) {
            return number >= 1 && number <= 18;
        }
        return number >= 19 && number <= 36;
    case ROULETTE_BET_DOZEN:
        if (number == 0 || bet->value < 0 || bet->value > 2) {
            return false;
        }
        return number >= (bet->value * 12 + 1) && number <= (bet->value * 12 + 12);
    case ROULETTE_BET_COLUMN:
        if (number == 0 || bet->value < 0 || bet->value > 2) {
            return false;
        }
        return ((number - 1) % 3) == bet->value;
    default:
        return false;
    }
}

/**
 * Purpose: Implements gamble roulette profit multiplier.
 * Parameters:
 *   - bet (const roulette_bet_t *): Input argument used by this routine.
 * Returns:
 *   - int: Computed numeric result for this routine.
 */
int gamble_roulette_profit_multiplier(const roulette_bet_t *bet) {
    if (bet == NULL) {
        return 0;
    }

    switch (bet->type) {
    case ROULETTE_BET_STRAIGHT:
        return 35;
    case ROULETTE_BET_COLOR:
    case ROULETTE_BET_PARITY:
    case ROULETTE_BET_RANGE:
        return 1;
    case ROULETTE_BET_DOZEN:
    case ROULETTE_BET_COLUMN:
        return 2;
    default:
        return 0;
    }
}

/**
 * Purpose: Implements gamble roulette describe bet.
 * Parameters:
 *   - bet (const roulette_bet_t *): Input argument used by this routine.
 *   - buffer (char *): Output buffer populated by this routine.
 *   - buffer_size (size_t): Output buffer populated by this routine.
 */
void gamble_roulette_describe_bet(const roulette_bet_t *bet, char *buffer, size_t buffer_size) {
    if (buffer_size == 0) {
        return;
    }
    if (bet == NULL) {
        snprintf(buffer, buffer_size, "(none)");
        return;
    }

    switch (bet->type) {
    case ROULETTE_BET_STRAIGHT:
        snprintf(buffer, buffer_size, "Straight %d", bet->value);
        break;
    case ROULETTE_BET_COLOR:
        snprintf(buffer, buffer_size, "%s", bet->value == ROULETTE_COLOR_RED ? "Red" : "Black");
        break;
    case ROULETTE_BET_PARITY:
        snprintf(buffer, buffer_size, "%s", bet->value == 0 ? "Odd" : "Even");
        break;
    case ROULETTE_BET_RANGE:
        snprintf(buffer, buffer_size, "%s", bet->value == 0 ? "1-18" : "19-36");
        break;
    case ROULETTE_BET_DOZEN:
        snprintf(buffer, buffer_size, "Dozen %d", bet->value + 1);
        break;
    case ROULETTE_BET_COLUMN:
        snprintf(buffer, buffer_size, "Column %d", bet->value + 1);
        break;
    default:
        snprintf(buffer, buffer_size, "(unknown)");
        break;
    }
}
