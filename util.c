/* util.c: Shared utility helpers for RNG, logging, checksums, and path handling. */
#include "util.h"

#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "market.h"
#include "player.h"
#include "world.h"

/**
 * Purpose: Implements clamp int.
 * Parameters:
 *   - value (int): Numeric input used by this routine for calculations or limits.
 *   - min_value (int): Numeric input used by this routine for calculations or limits.
 *   - max_value (int): Numeric input used by this routine for calculations or limits.
 * Returns:
 *   - int: Computed numeric result for this routine.
 */
int clamp_int(int value, int min_value, int max_value) {
    if (value < min_value) {
        return min_value;
    }
    if (value > max_value) {
        return max_value;
    }
    return value;
}

/**
 * Purpose: Implements rng next.
 * Parameters:
 *   - game (game_t *): Game state this routine reads and/or updates.
 * Returns:
 *   - uint32_t: Computed numeric result for this routine.
 */
uint32_t rng_next(game_t *game) {
    uint32_t x = game->rng_state;
    if (x == 0) {
        x = 0x9e3779b9u;
    }
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    game->rng_state = x;
    return x;
}

/**
 * Purpose: Implements rng range.
 * Parameters:
 *   - game (game_t *): Game state this routine reads and/or updates.
 *   - min_value (int): Numeric input used by this routine for calculations or limits.
 *   - max_value (int): Numeric input used by this routine for calculations or limits.
 * Returns:
 *   - int: Computed numeric result for this routine.
 */
int rng_range(game_t *game, int min_value, int max_value) {
    uint32_t span;
    if (min_value >= max_value) {
        return min_value;
    }
    span = (uint32_t)((long)max_value - (long)min_value) + 1U;
    return min_value + (int)(rng_next(game) % span);
}

/**
 * Purpose: Implements rng chance.
 * Parameters:
 *   - game (game_t *): Game state this routine reads and/or updates.
 *   - percent (int): Input argument used by this routine.
 * Returns:
 *   - bool: True when the operation succeeds or the condition is met; false otherwise.
 */
bool rng_chance(game_t *game, int percent) {
    return rng_range(game, 1, 100) <= percent;
}

/**
 * Purpose: Implements game log.
 * Parameters:
 *   - game (game_t *): Game state this routine reads and/or updates.
 *   - fmt (const char *): Text input used for display, messaging, or formatting.
 *   - ... (...): Additional variadic arguments consumed by the format logic.
 */
void game_log(game_t *game, const char *fmt, ...) {
    va_list args;
    char *line = game->log.text[game->log.head];

    va_start(args, fmt);
    vsnprintf(line, LOG_LINE_LENGTH, fmt, args);
    va_end(args);

    game->log.head = (game->log.head + 1) % NEWS_TICKER_LINES;
    if (game->log.count < NEWS_TICKER_LINES) {
        game->log.count++;
    }
}

/**
 * Purpose: Implements game set game over.
 * Parameters:
 *   - game (game_t *): Game state this routine reads and/or updates.
 *   - reason (const char *): Text input used for display, messaging, or formatting.
 */
void game_set_game_over(game_t *game, const char *reason) {
    game->state = GAME_STATE_GAME_OVER;
    game->player.hp = 0;
    snprintf(game->end_reason, sizeof(game->end_reason), "%s", reason);
    game_log(game, "%s", reason);
}

/**
 * Purpose: Implements apply bank interest.
 * Parameters:
 *   - game (game_t *): Game state this routine reads and/or updates.
 */
static void apply_bank_interest(game_t *game) {
    int interest;

    if (game->player.bank_balance <= 0) {
        return;
    }

    interest = game->player.bank_balance / 100;
    if (interest <= 0) {
        return;
    }

    game->player.bank_balance = clamp_int(game->player.bank_balance + interest, 0, INT_MAX);
}

/**
 * Purpose: Implements game advance turn.
 * Parameters:
 *   - game (game_t *): Game state this routine reads and/or updates.
 */
void game_advance_turn(game_t *game) {
    game->turn++;
    apply_bank_interest(game);
    world_decay_drops(game);
    if (game->player.wanted_level > 0 && (game->turn % 5) == 0) {
        game->player.wanted_level--;
    }
}

/**
 * Purpose: Implements game init new.
 * Parameters:
 *   - game (game_t *): Game state this routine reads and/or updates.
 */
void game_init_new(game_t *game) {
    high_score_t old_scores[MAX_HIGH_SCORES];
    int old_score_count = game->score_count;

    memcpy(old_scores, game->scores, sizeof(old_scores));
    memset(game, 0, sizeof(*game));
    memcpy(game->scores, old_scores, sizeof(old_scores));
    game->score_count = old_score_count;

    game->rng_state = (uint32_t)time(NULL) ^ (uint32_t)getpid();
    game->running = true;
    game->state = GAME_STATE_LOCATION;
    game->stash_rumor_active = false;
    game->pending_bribe = false;
    player_init(&game->player);
    market_init_all(game);
    world_arrive(game, LOCATION_STARPORT, false);
    game_log(game, "Rumour: Millhaven's drowning in grain. Starport prices are strong.");
}

static uint32_t crc32_table[256];
static bool crc32_table_ready = false;

/**
 * Purpose: Implements crc32 init.
 * Parameters:
 *   - None.
 */
static void crc32_init(void) {
    if (crc32_table_ready) {
        return;
    }

    for (uint32_t i = 0; i < 256; ++i) {
        uint32_t c = i;
        for (int j = 0; j < 8; ++j) {
            if (c & 1u) {
                c = 0xedb88320u ^ (c >> 1);
            } else {
                c >>= 1;
            }
        }
        crc32_table[i] = c;
    }

    crc32_table_ready = true;
}

/**
 * Purpose: Implements crc32 bytes.
 * Parameters:
 *   - data (const void *): Input argument used by this routine.
 *   - length (size_t): Input argument used by this routine.
 * Returns:
 *   - uint32_t: Computed numeric result for this routine.
 */
uint32_t crc32_bytes(const void *data, size_t length) {
    const unsigned char *bytes = (const unsigned char *)data;
    uint32_t crc = 0xffffffffu;

    crc32_init();
    for (size_t i = 0; i < length; ++i) {
        crc = crc32_table[(crc ^ bytes[i]) & 0xffu] ^ (crc >> 8);
    }
    return crc ^ 0xffffffffu;
}

/**
 * Purpose: Implements home file path.
 * Parameters:
 *   - filename (const char *): Input argument used by this routine.
 *   - buffer (char *): Output buffer populated by this routine.
 *   - buffer_size (size_t): Output buffer populated by this routine.
 * Returns:
 *   - bool: True when the operation succeeds or the condition is met; false otherwise.
 */
bool home_file_path(const char *filename, char *buffer, size_t buffer_size) {
    const char *home = getenv("HOME");

    if (home == NULL || home[0] == '\0') {
        return false;
    }

    int result = snprintf(buffer, buffer_size, "%s/%s", home, filename);
    return result >= 0 && result < (int)buffer_size;
}
