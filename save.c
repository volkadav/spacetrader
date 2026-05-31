#include "save.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "market.h"
#include "util.h"

#define SAVE_MAGIC 0x53545244u
#define SAVE_VERSION 1u
#define SCORE_MAGIC 0x53545253u

typedef struct {
    uint32_t magic;
    uint32_t version;
    uint32_t checksum;
    game_t game;
} save_blob_t;

typedef struct {
    uint32_t magic;
    uint32_t version;
    int score_count;
    high_score_t scores[MAX_HIGH_SCORES];
} score_blob_t;

static int score_value(const game_t *game) {
    return game->player.credits + market_estimate_cargo_value(game) + (game->player.reputation * 250) +
           (game->player_won ? 5000 : 0);
}

bool save_game(const game_t *game, char *error_buffer, size_t error_buffer_size) {
    char path[512];
    FILE *fp;
    save_blob_t blob;

    if (!home_file_path(".spacetrader-save.bin", path, sizeof(path))) {
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

bool load_game(game_t *game, char *error_buffer, size_t error_buffer_size) {
    char path[512];
    FILE *fp;
    save_blob_t blob;

    if (!home_file_path(".spacetrader-save.bin", path, sizeof(path))) {
        snprintf(error_buffer, error_buffer_size, "Could not determine save path.");
        return false;
    }
    fp = fopen(path, "rb");
    if (fp == NULL) {
        snprintf(error_buffer, error_buffer_size, "No save file found.");
        return false;
    }
    if (fread(&blob, sizeof(blob), 1, fp) != 1) {
        fclose(fp);
        snprintf(error_buffer, error_buffer_size, "Save file is unreadable.");
        return false;
    }
    fclose(fp);

    if (blob.magic != SAVE_MAGIC || blob.version != SAVE_VERSION) {
        snprintf(error_buffer, error_buffer_size, "Save file version mismatch.");
        return false;
    }
    if (blob.checksum != crc32_bytes(&blob.game, sizeof(blob.game))) {
        snprintf(error_buffer, error_buffer_size, "Save file checksum failed.");
        return false;
    }

    *game = blob.game;
    game->running = true;
    return true;
}

void load_high_scores(game_t *game) {
    char path[512];
    FILE *fp;
    score_blob_t blob;

    if (!home_file_path(".spacetrader-scores.bin", path, sizeof(path))) {
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

    if (!home_file_path(".spacetrader-scores.bin", path, sizeof(path))) {
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
