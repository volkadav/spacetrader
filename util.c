#include "util.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "market.h"
#include "player.h"
#include "world.h"

int clamp_int(int value, int min_value, int max_value) {
    if (value < min_value) {
        return min_value;
    }
    if (value > max_value) {
        return max_value;
    }
    return value;
}

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

int rng_range(game_t *game, int min_value, int max_value) {
    uint32_t span = (uint32_t)(max_value - min_value + 1);
    return min_value + (int)(rng_next(game) % span);
}

bool rng_chance(game_t *game, int percent) {
    return rng_range(game, 1, 100) <= percent;
}

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

void game_set_game_over(game_t *game, const char *reason) {
    game->state = GAME_STATE_GAME_OVER;
    game->player.hp = 0;
    snprintf(game->end_reason, sizeof(game->end_reason), "%s", reason);
    game_log(game, "%s", reason);
}

void game_advance_turn(game_t *game) {
    game->turn++;
    world_decay_drops(game);
}

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
    player_init(&game->player);
    market_init_all(game);
    world_arrive(game, LOCATION_STARPORT, false);
    game_log(game, "Rumour: Millhaven's drowning in grain. Starport prices are strong.");
}

static uint32_t crc32_table[256];
static bool crc32_table_ready = false;

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

uint32_t crc32_bytes(const void *data, size_t length) {
    const unsigned char *bytes = (const unsigned char *)data;
    uint32_t crc = 0xffffffffu;

    crc32_init();
    for (size_t i = 0; i < length; ++i) {
        crc = crc32_table[(crc ^ bytes[i]) & 0xffu] ^ (crc >> 8);
    }
    return crc ^ 0xffffffffu;
}

bool home_file_path(const char *filename, char *buffer, size_t buffer_size) {
    const char *home = getenv("HOME");

    if (home == NULL || home[0] == '\0') {
        return false;
    }

    if (snprintf(buffer, buffer_size, "%s/%s", home, filename) >= (int)buffer_size) {
        return false;
    }
    return true;
}
