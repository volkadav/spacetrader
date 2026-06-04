/* save.c: Save/load serialization, corrupt-save handling, and persistent high-score storage. */
#include "save.h"

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "market.h"
#include "util.h"

#define SAVE_MAGIC 0x53504143u
#define SAVE_VERSION 2u
#define SCORE_MAGIC 0x53545253u
#define SCORE_VERSION 1u
#define SAVE_FILENAME ".spacetrader.sav"
#define SCORE_FILENAME ".spacetrader.scores"

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

static char save_path_override[PATH_MAX];
static bool save_path_override_set = false;

typedef struct {
    uint32_t magic;
    uint16_t version;
    uint16_t reserved;
    uint32_t checksum;
    game_t game;
} save_blob_t;

typedef struct {
    uint32_t magic;
    uint32_t version;
    int score_count;
    high_score_t scores[MAX_HIGH_SCORES];
} score_blob_t;

/**
 * Purpose: Implements score value.
 * Parameters:
 *   - game (const game_t *): Game state this routine reads and/or updates.
 * Returns:
 *   - int: Computed numeric result for this routine.
 */
static int score_value(const game_t *game) {
    return game->player.credits + game->player.bank_balance + market_estimate_cargo_value(game) + (game->turn * 10);
}

/**
 * Purpose: Implements append suffix.
 * Parameters:
 *   - path (const char *): Input argument used by this routine.
 *   - suffix (const char *): Input argument used by this routine.
 *   - buffer (char *): Output buffer populated by this routine.
 *   - buffer_size (size_t): Output buffer populated by this routine.
 * Returns:
 *   - bool: True when the operation succeeds or the condition is met; false otherwise.
 */
static bool append_suffix(const char *path, const char *suffix, char *buffer, size_t buffer_size) {
    return snprintf(buffer, buffer_size, "%s%s", path, suffix) < (int)buffer_size;
}

/**
 * Purpose: Implements save path directory.
 * Parameters:
 *   - path (const char *): Input argument used by this routine.
 *   - directory (char *): Output buffer populated by this routine.
 *   - directory_size (size_t): Output buffer populated by this routine.
 * Returns:
 *   - bool: True when the operation succeeds or the condition is met; false otherwise.
 */
static bool save_path_directory(const char *path, char *directory, size_t directory_size) {
    const char *slash;
    size_t prefix_len;

    if (path == NULL || directory == NULL || directory_size == 0 || path[0] == '\0') {
        return false;
    }
    if (path[strlen(path) - 1] == '/') {
        return false;
    }

    slash = strrchr(path, '/');
    if (slash == NULL) {
        return snprintf(directory, directory_size, ".") < (int)directory_size;
    }

    prefix_len = (size_t)(slash - path);
    if (prefix_len == 0) {
        return snprintf(directory, directory_size, "/") < (int)directory_size;
    }
    if (prefix_len >= directory_size) {
        return false;
    }

    memcpy(directory, path, prefix_len);
    directory[prefix_len] = '\0';
    return true;
}

/**
 * Purpose: Implements validate override path.
 * Parameters:
 *   - path (const char *): Input argument used by this routine.
 *   - error_buffer (char *): Output buffer populated by this routine.
 *   - error_buffer_size (size_t): Output buffer populated by this routine.
 * Returns:
 *   - bool: True when the operation succeeds or the condition is met; false otherwise.
 */
static bool validate_override_path(const char *path, char *error_buffer, size_t error_buffer_size) {
    struct stat path_stat;

    if (path == NULL || path[0] == '\0') {
        snprintf(error_buffer, error_buffer_size, "Save path cannot be empty.");
        return false;
    }
    if (strlen(path) >= PATH_MAX) {
        snprintf(error_buffer, error_buffer_size, "Save path is too long.");
        return false;
    }

    if (stat(path, &path_stat) == 0) {
        if (!S_ISREG(path_stat.st_mode)) {
            snprintf(error_buffer, error_buffer_size, "Save path must point to a regular file.");
            return false;
        }
        if (access(path, R_OK) != 0) {
            snprintf(error_buffer, error_buffer_size, "Save file is not readable: %s", strerror(errno));
            return false;
        }
        if (access(path, W_OK) != 0) {
            snprintf(error_buffer, error_buffer_size, "Save file is not writable: %s", strerror(errno));
            return false;
        }
        return true;
    }

    if (errno != ENOENT) {
        snprintf(error_buffer, error_buffer_size, "Could not inspect save path: %s", strerror(errno));
        return false;
    }

    {
        char directory[PATH_MAX];
        struct stat directory_stat;

        if (!save_path_directory(path, directory, sizeof(directory))) {
            snprintf(error_buffer, error_buffer_size, "Save path must include a valid filename.");
            return false;
        }
        if (stat(directory, &directory_stat) != 0) {
            snprintf(error_buffer, error_buffer_size, "Save directory is not accessible: %s", strerror(errno));
            return false;
        }
        if (!S_ISDIR(directory_stat.st_mode)) {
            snprintf(error_buffer, error_buffer_size, "Save directory path is not a directory.");
            return false;
        }
        if (access(directory, W_OK | X_OK) != 0) {
            snprintf(error_buffer, error_buffer_size, "Save directory is not writable: %s", strerror(errno));
            return false;
        }
    }

    return true;
}

/**
 * Purpose: Implements resolve save path.
 * Parameters:
 *   - path (char *): Output buffer populated by this routine.
 *   - path_size (size_t): Output buffer populated by this routine.
 * Returns:
 *   - bool: True when the operation succeeds or the condition is met; false otherwise.
 */
static bool resolve_save_path(char *path, size_t path_size) {
    if (save_path_override_set) {
        return snprintf(path, path_size, "%s", save_path_override) < (int)path_size;
    }
    return home_file_path(SAVE_FILENAME, path, path_size);
}

/**
 * Purpose: Implements sanitize loaded game.
 * Parameters:
 *   - game (game_t *): Game state this routine reads and/or updates.
 */
static void sanitize_loaded_game(game_t *game) {
    int cargo_count;
    int write_index = 0;

    game->state = (game_state_t)clamp_int((int)game->state, (int)GAME_STATE_TITLE, (int)GAME_STATE_VICTORY);
    game->player.location = (location_id_t)clamp_int((int)game->player.location, 0, MAX_LOCATIONS - 1);
    game->player.weapon = (weapon_t)clamp_int((int)game->player.weapon, (int)WEAPON_FISTS, (int)(WEAPON_COUNT - 1));
    game->player.armor = (armor_t)clamp_int((int)game->player.armor, (int)ARMOR_NONE, (int)(ARMOR_COUNT - 1));

    game->player.max_hp = clamp_int(game->player.max_hp, 1, 99);
    game->player.hp = clamp_int(game->player.hp, 0, game->player.max_hp);
    game->player.credits = clamp_int(game->player.credits, 0, 1000000000);
    game->player.bank_balance = clamp_int(game->player.bank_balance, 0, 1000000000);
    game->player.reputation = clamp_int(game->player.reputation, -999, 999);
    game->player.bandages = clamp_int(game->player.bandages, 0, 9999);
    game->player.medkit_uses = clamp_int(game->player.medkit_uses, 0, 9999);
    game->turn = clamp_int(game->turn, 0, 1000000000);
    game->poison_turns = clamp_int(game->poison_turns, 0, 99);
    game->prospect_bonus = clamp_int(game->prospect_bonus, -999, 9999);
    game->score_count = clamp_int(game->score_count, 0, MAX_HIGH_SCORES);
    game->log.head = clamp_int(game->log.head, 0, NEWS_TICKER_LINES - 1);
    game->log.count = clamp_int(game->log.count, 0, NEWS_TICKER_LINES);

    game->player.has_prospecting_kit = !!game->player.has_prospecting_kit;
    game->player.has_sturdy_pack = !!game->player.has_sturdy_pack;
    game->player.has_lucky_charm = !!game->player.has_lucky_charm;
    game->player.has_mule = !!game->player.has_mule;
    game->player.has_cart = !!game->player.has_cart;
    game->player.owns_cart = !!game->player.owns_cart;
    game->player.has_hover = !!game->player.has_hover;
    game->running = !!game->running;
    game->player_won = !!game->player_won;
    game->score_recorded = !!game->score_recorded;

    cargo_count = clamp_int(game->player.cargo_count, 0, MAX_CARGO_STACKS);
    for (int i = 0; i < cargo_count; ++i) {
        cargo_stack_t stack = game->player.cargo[i];

        if (stack.commodity < 0 || stack.commodity >= NUM_COMMODITIES || stack.quantity <= 0) {
            continue;
        }
        if (write_index != i) {
            game->player.cargo[write_index] = stack;
        }
        write_index++;
    }
    for (int i = write_index; i < MAX_CARGO_STACKS; ++i) {
        game->player.cargo[i].commodity = COMMODITY_FOOD_RATIONS;
        game->player.cargo[i].quantity = 0;
    }
    game->player.cargo_count = write_index;

    for (int location = 0; location < MAX_LOCATIONS; ++location) {
        for (int commodity = 0; commodity < NUM_COMMODITIES; ++commodity) {
            game->markets[location].stock[commodity] =
                (int16_t)clamp_int(game->markets[location].stock[commodity], 0, 30000);
            game->markets[location].prices[commodity] =
                clamp_int(game->markets[location].prices[commodity], 1, 1000000);
        }
        game->markets[location].known = !!game->markets[location].known;

        for (int slot = 0; slot < MAX_DROP_STACKS_PER_LOCATION; ++slot) {
            drop_slot_t *drop = &game->drops[location].slots[slot];

            if (!drop->occupied) {
                drop->kind = DROP_KIND_NONE;
                drop->commodity = COMMODITY_FOOD_RATIONS;
                drop->quantity = 0;
                drop->age = 0;
                continue;
            }

            if (drop->kind < DROP_KIND_NONE || drop->kind > DROP_KIND_CART) {
                drop->occupied = false;
                drop->kind = DROP_KIND_NONE;
                drop->commodity = COMMODITY_FOOD_RATIONS;
                drop->quantity = 0;
                drop->age = 0;
                continue;
            }

            if (drop->kind == DROP_KIND_COMMODITY &&
                (drop->commodity < 0 || drop->commodity >= NUM_COMMODITIES || drop->quantity <= 0)) {
                drop->occupied = false;
                drop->kind = DROP_KIND_NONE;
                drop->commodity = COMMODITY_FOOD_RATIONS;
                drop->quantity = 0;
                drop->age = 0;
                continue;
            }

            if (drop->kind == DROP_KIND_CART) {
                drop->commodity = COMMODITY_FOOD_RATIONS;
                drop->quantity = 1;
            }
            drop->age = clamp_int(drop->age, 0, 1000000);
        }
    }
}

/**
 * Purpose: Implements archive corrupt save.
 * Parameters:
 *   - save_path (const char *): Input argument used by this routine.
 *   - backup_path (char *): Input argument used by this routine.
 *   - backup_path_size (size_t): Capacity value, typically in bytes, for the associated buffer or container.
 * Returns:
 *   - bool: True when the operation succeeds or the condition is met; false otherwise.
 */
static bool archive_corrupt_save(const char *save_path, char *backup_path, size_t backup_path_size) {
    if (!append_suffix(save_path, ".bak", backup_path, backup_path_size)) {
        return false;
    }

    remove(backup_path);
    return rename(save_path, backup_path) == 0;
}

/**
 * Purpose: Implements save set path override.
 * Parameters:
 *   - path (const char *): Input argument used by this routine.
 *   - error_buffer (char *): Output buffer populated by this routine.
 *   - error_buffer_size (size_t): Output buffer populated by this routine.
 * Returns:
 *   - bool: True when the operation succeeds or the condition is met; false otherwise.
 */
bool save_set_path_override(const char *path, char *error_buffer, size_t error_buffer_size) {
    if (!validate_override_path(path, error_buffer, error_buffer_size)) {
        return false;
    }
    snprintf(save_path_override, sizeof(save_path_override), "%s", path);
    save_path_override_set = true;
    return true;
}

/**
 * Purpose: Implements save game.
 * Parameters:
 *   - game (const game_t *): Game state this routine reads and/or updates.
 *   - error_buffer (char *): Output buffer populated by this routine.
 *   - error_buffer_size (size_t): Output buffer populated by this routine.
 * Returns:
 *   - bool: True when the operation succeeds or the condition is met; false otherwise.
 */
bool save_game(const game_t *game, char *error_buffer, size_t error_buffer_size) {
    char path[PATH_MAX];
    FILE *fp;
    save_blob_t blob;

    if (!resolve_save_path(path, sizeof(path))) {
        snprintf(error_buffer, error_buffer_size, "Could not determine save path.");
        return false;
    }

    fp = fopen(path, "wb");
    if (fp == NULL) {
        snprintf(error_buffer, error_buffer_size, "Could not open save file.");
        return false;
    }

    memset(&blob, 0, sizeof(blob));
    blob.magic = SAVE_MAGIC;
    blob.version = SAVE_VERSION;
    blob.game = *game;
    blob.checksum = crc32_bytes(&blob.game, sizeof(blob.game));

    if (fwrite(&blob, sizeof(blob), 1, fp) != 1) {
        fclose(fp);
        snprintf(error_buffer, error_buffer_size, "Failed writing save file.");
        return false;
    }
    fclose(fp);
    return true;
}

/**
 * Purpose: Implements load game.
 * Parameters:
 *   - game (game_t *): Game state this routine reads and/or updates.
 *   - error_buffer (char *): Output buffer populated by this routine.
 *   - error_buffer_size (size_t): Output buffer populated by this routine.
 * Returns:
 *   - save_load_result_t: Return value describing the outcome of this routine.
 */
save_load_result_t load_game(game_t *game, char *error_buffer, size_t error_buffer_size) {
    char path[PATH_MAX];
    char backup_path[PATH_MAX + 32];
    FILE *fp;
    save_blob_t blob;
    bool archived;

    if (!resolve_save_path(path, sizeof(path))) {
        snprintf(error_buffer, error_buffer_size, "Could not determine save path.");
        return SAVE_LOAD_ERROR;
    }
    fp = fopen(path, "rb");
    if (fp == NULL) {
        snprintf(error_buffer, error_buffer_size, "No save file found.");
        return SAVE_LOAD_NOT_FOUND;
    }
    if (fread(&blob, sizeof(blob), 1, fp) != 1) {
        fclose(fp);
        archived = archive_corrupt_save(path, backup_path, sizeof(backup_path));
        if (archived) {
            snprintf(error_buffer, error_buffer_size, "Save is unreadable and was moved to %s.", backup_path);
        } else {
            snprintf(error_buffer, error_buffer_size, "Save is unreadable and could not be archived.");
        }
        return SAVE_LOAD_CORRUPT;
    }
    fclose(fp);

    if (blob.magic != SAVE_MAGIC || blob.version != (uint16_t)SAVE_VERSION) {
        archived = archive_corrupt_save(path, backup_path, sizeof(backup_path));
        if (archived) {
            snprintf(error_buffer, error_buffer_size, "Save version mismatch; moved to %s.", backup_path);
        } else {
            snprintf(error_buffer, error_buffer_size, "Save version mismatch and could not be archived.");
        }
        return SAVE_LOAD_CORRUPT;
    }
    if (blob.checksum != crc32_bytes(&blob.game, sizeof(blob.game))) {
        archived = archive_corrupt_save(path, backup_path, sizeof(backup_path));
        if (archived) {
            snprintf(error_buffer, error_buffer_size, "Save checksum failed; moved to %s.", backup_path);
        } else {
            snprintf(error_buffer, error_buffer_size, "Save checksum failed and could not be archived.");
        }
        return SAVE_LOAD_CORRUPT;
    }

    *game = blob.game;
    sanitize_loaded_game(game);
    game->running = true;
    return SAVE_LOAD_OK;
}

/**
 * Purpose: Implements load high scores.
 * Parameters:
 *   - game (game_t *): Game state this routine reads and/or updates.
 */
void load_high_scores(game_t *game) {
    char path[512];
    FILE *fp;
    score_blob_t blob;

    if (!home_file_path(SCORE_FILENAME, path, sizeof(path))) {
        return;
    }

    fp = fopen(path, "rb");
    if (fp == NULL) {
        game->score_count = 0;
        memset(game->scores, 0, sizeof(game->scores));
        return;
    }

    if (fread(&blob, sizeof(blob), 1, fp) != 1 || blob.magic != SCORE_MAGIC || blob.version != SCORE_VERSION) {
        fclose(fp);
        game->score_count = 0;
        memset(game->scores, 0, sizeof(game->scores));
        return;
    }
    fclose(fp);

    game->score_count = clamp_int(blob.score_count, 0, MAX_HIGH_SCORES);
    memcpy(game->scores, blob.scores, sizeof(blob.scores));
    for (int i = 0; i < MAX_HIGH_SCORES; ++i) {
        game->scores[i].name[sizeof(game->scores[i].name) - 1] = '\0';
        game->scores[i].outcome[sizeof(game->scores[i].outcome) - 1] = '\0';
    }
}

/**
 * Purpose: Implements record high score.
 * Parameters:
 *   - game (game_t *): Game state this routine reads and/or updates.
 *   - outcome (const char *): Text input used for display, messaging, or formatting.
 */
void record_high_score(game_t *game, const char *outcome) {
    char path[512];
    FILE *fp;
    score_blob_t blob;
    high_score_t entry;
    const char *user = getenv("USER");
    int insert_at = game->score_count;

    if (game->score_recorded) {
        return;
    }

    memset(&entry, 0, sizeof(entry));
    snprintf(entry.name, sizeof(entry.name), "%s", user != NULL ? user : "TRADER");
    snprintf(entry.outcome, sizeof(entry.outcome), "%s", outcome);
    entry.credits = game->player.credits + game->player.bank_balance;
    entry.cargo_value = market_estimate_cargo_value(game);
    entry.turns = game->turn;
    entry.score = score_value(game);

    if (game->score_count < 0) {
        game->score_count = 0;
    } else if (game->score_count > MAX_HIGH_SCORES) {
        game->score_count = MAX_HIGH_SCORES;
    }

    for (int i = 0; i < game->score_count; ++i) {
        if (game->scores[i].score == entry.score && strcmp(game->scores[i].name, entry.name) == 0) {
            game->score_recorded = true;
            return;
        }
    }

    for (int i = 0; i < game->score_count; ++i) {
        if (entry.score > game->scores[i].score) {
            insert_at = i;
            break;
        }
    }
    if (insert_at < MAX_HIGH_SCORES) {
        if (game->score_count < MAX_HIGH_SCORES) {
            game->score_count++;
        }
        for (int i = game->score_count - 1; i > insert_at; --i) {
            game->scores[i] = game->scores[i - 1];
        }
        game->scores[insert_at] = entry;
    }

    if (!home_file_path(SCORE_FILENAME, path, sizeof(path))) {
        game->score_recorded = true;
        return;
    }

    blob.magic = SCORE_MAGIC;
    blob.version = SCORE_VERSION;
    blob.score_count = game->score_count;
    memcpy(blob.scores, game->scores, sizeof(blob.scores));

    fp = fopen(path, "wb");
    if (fp != NULL) {
        fwrite(&blob, sizeof(blob), 1, fp);
        fclose(fp);
    }

    game->score_recorded = true;
}
