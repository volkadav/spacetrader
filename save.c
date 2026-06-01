/* save.c: Save/load serialization, corrupt-save handling, and persistent high-score storage. */
#include "save.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "market.h"
#include "util.h"

#define SAVE_MAGIC 0x53504143u
#define SAVE_VERSION 1u
#define SCORE_MAGIC 0x53545253u
#define SAVE_FILENAME ".spacetrader.sav"
#define SCORE_FILENAME ".spacetrader.scores"

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
    return game->player.credits + market_estimate_cargo_value(game) + (game->turn * 10);
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
 * Purpose: Implements save game.
 * Parameters:
 *   - game (const game_t *): Game state this routine reads and/or updates.
 *   - error_buffer (char *): Output buffer populated by this routine.
 *   - error_buffer_size (size_t): Output buffer populated by this routine.
 * Returns:
 *   - bool: True when the operation succeeds or the condition is met; false otherwise.
 */
bool save_game(const game_t *game, char *error_buffer, size_t error_buffer_size) {
    char path[512];
    FILE *fp;
    save_blob_t blob;

    if (!home_file_path(SAVE_FILENAME, path, sizeof(path))) {
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
    char path[512];
    char backup_path[540];
    FILE *fp;
    save_blob_t blob;
    bool archived;

    if (!home_file_path(SAVE_FILENAME, path, sizeof(path))) {
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

    if (fread(&blob, sizeof(blob), 1, fp) != 1 || blob.magic != SCORE_MAGIC || blob.version != SAVE_VERSION) {
        fclose(fp);
        game->score_count = 0;
        memset(game->scores, 0, sizeof(game->scores));
        return;
    }
    fclose(fp);

    game->score_count = blob.score_count;
    memcpy(game->scores, blob.scores, sizeof(blob.scores));
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
    entry.credits = game->player.credits;
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
    blob.version = SAVE_VERSION;
    blob.score_count = game->score_count;
    memcpy(blob.scores, game->scores, sizeof(blob.scores));

    fp = fopen(path, "wb");
    if (fp != NULL) {
        fwrite(&blob, sizeof(blob), 1, fp);
        fclose(fp);
    }

    game->score_recorded = true;
}
