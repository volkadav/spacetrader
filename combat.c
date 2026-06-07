/* combat.c: Enemy definitions and the turn-based combat flow, rewards, and outcomes. */
#include "combat.h"

#include <ncurses.h>

#include "player.h"
#include "util.h"
#include "world.h"

const enemy_t ENEMIES[ENEMY_COUNT] = {
    [ENEMY_FERAL_DOG] = {"Feral Dog", 4, 0, 1, 10, 25, false, 8, 20},
    [ENEMY_WILD_BOAR] = {"Wild Boar", 8, 1, 3, 20, 0, false, 15, 30},
    [ENEMY_GIANT_SERPENT] = {"Giant Serpent", 12, 0, 2, 20, 10, true, 20, 45},
    [ENEMY_ROCK_CAT] = {"Rock Cat", 10, 1, 3, 15, 20, false, 20, 40},
    [ENEMY_MUGGER] = {"Mugger", 6, 0, 2, 0, 40, false, 10, 30},
    [ENEMY_BANDIT] = {"Bandit", 8, 1, 4, 10, 30, false, 20, 50},
    [ENEMY_BANDIT_LEADER] = {"Bandit Leader", 12, 2, 6, 15, 10, false, 60, 120},
    [ENEMY_BAR_BRAWLER] = {"Bar Brawler", 8, 0, 1, 0, 25, false, 10, 50}
};

/**
 * Purpose: Implements draw combat screen.
 * Parameters:
 *   - game (const game_t *): Game state this routine reads and/or updates.
 *   - enemy (const enemy_t *): Input argument used by this routine.
 *   - enemy_hp (int): Input argument used by this routine.
 *   - mule_hp (int): Input argument used by this routine.
 *   - mule_has_cart (bool): Boolean flag controlling conditional behavior.
 *   - title (const char *): Text input used for display, messaging, or formatting.
 */
static void draw_combat_screen(const game_t *game,
                               const enemy_t *enemy,
                               int enemy_hp,
                               int mule_hp,
                               bool mule_has_cart,
                               const char *title) {
    clear();
    mvprintw(1, 2, "%s", title);
    mvprintw(3, 2, "You:    HP %d/%d  Weapon: %s  Armor: %s",
             game->player.hp,
             game->player.max_hp,
             WEAPONS[game->player.weapon].name,
             ARMORS[game->player.armor].name);
    mvprintw(4, 2, "Enemy:  %s  HP %d/%d  DR %d",
             enemy->name,
             enemy_hp,
             enemy->max_hp,
             enemy->dr);
    if (mule_hp > 0) {
        mvprintw(5,
                 2,
                 "Mule:   HP %d/8  Attack: %s",
                 mule_hp,
                 mule_has_cart ? "Bite 1" : "Bite 1 / Kick 2");
    } else {
        move(5, 2);
        clrtoeol();
    }
    mvprintw(7, 2, "[A]ttack  [D]efend  [B]andage  [M]edkit  [F]lee  [I]ntimidate");
    if (game->poison_turns > 0) {
        mvprintw(9, 2, "Poisoned: %d turns remaining", game->poison_turns);
    } else {
        move(9, 2);
        clrtoeol();
    }
    mvprintw(11, 2, "Last log:");
    for (int i = 0; i < game->log.count && i < 5; ++i) {
        int index = (game->log.head - game->log.count + i + NEWS_TICKER_LINES) % NEWS_TICKER_LINES;
        mvprintw(12 + i, 4, "%s", game->log.text[index]);
    }
    refresh();
}

/**
 * Purpose: Implements combat run.
 * Parameters:
 *   - game (game_t *): Game state this routine reads and/or updates.
 *   - enemy_id (enemy_id_t): Identifier selecting the target entity for this operation.
 *   - title (const char *): Text input used for display, messaging, or formatting.
 * Returns:
 *   - combat_result_t: Return value describing the outcome of this routine.
 */
combat_result_t combat_run(game_t *game, enemy_id_t enemy_id, const char *title) {
    const enemy_t *enemy = &ENEMIES[enemy_id];
    int enemy_hp = enemy->max_hp;
    int mule_hp = game->player.has_mule ? 8 : 0;
    bool defending = false;

    while (game->player.hp > 0 && enemy_hp > 0) {
        int ch;

        if (game->poison_turns > 0) {
            game->poison_turns--;
            game->player.hp--;
            game_log(game, "Poison bites for 1 HP.");
            if (game->player.hp <= 0) {
                break;
            }
        }

        draw_combat_screen(game, enemy, enemy_hp, mule_hp, game->player.has_cart, title);
        ch = getch();

        switch (ch) {
        case 'a':
        case 'A': {
            int damage = clamp_int(player_weapon_damage(&game->player) + rng_range(game, -1, 1) - enemy->dr, 0, 99);
            enemy_hp -= damage;
            game_log(game, "You hit %s for %d HP.", enemy->name, damage);
            break;
        }
        case 'd':
        case 'D':
            defending = true;
            game_log(game, "You brace for the next hit.");
            break;
        case 'b':
        case 'B':
            if (!player_use_bandage(game)) {
                continue;
            }
            break;
        case 'm':
        case 'M':
            if (!player_use_medkit(game)) {
                continue;
            }
            break;
        case 'f':
        case 'F': {
            int chance = clamp_int(50 - enemy->flee_penalty - player_flee_penalty(&game->player), 10, 90);
            if (rng_chance(game, chance)) {
                game_log(game, "You break contact with %s.", enemy->name);
                return COMBAT_RESULT_ESCAPED;
            }
            game->player.hp--;
            game_log(game, "You fail to flee and take 1 HP while scrambling away.");
            break;
        }
        case 'i':
        case 'I': {
            int morale = 15 + (game->player.reputation * 10);
            bool likely = enemy->flees_at_pct > 0 && (enemy_hp * 100) <= (enemy->max_hp * enemy->flees_at_pct);
            if (rng_chance(game, likely ? morale + 20 : morale)) {
                game_log(game, "%s loses nerve and backs off.", enemy->name);
                return COMBAT_RESULT_ESCAPED;
            }
            game_log(game, "%s is not impressed.", enemy->name);
            break;
        }
        default:
            continue;
        }

        if (enemy_hp <= 0) {
            break;
        }

        if (mule_hp > 0) {
            int mule_base_damage = game->player.has_cart ? 1 : rng_chance(game, 50) ? 1 : 2;
            int mule_damage = clamp_int(mule_base_damage - enemy->dr, 0, 99);

            enemy_hp -= mule_damage;
            if (mule_base_damage == 2) {
                game_log(game, "Your mule kicks %s for %d HP.", enemy->name, mule_damage);
            } else {
                game_log(game, "Your mule bites %s for %d HP.", enemy->name, mule_damage);
            }
            if (enemy_hp <= 0) {
                break;
            }
        }

        {
            bool hits_mule = mule_hp > 0 && rng_chance(game, 35);
            int defend_bonus = defending ? 2 : 0;
            defending = false;

            if (hits_mule) {
                int damage = clamp_int(enemy->damage + rng_range(game, -1, 1), 0, 99);
                mule_hp -= damage;
                game_log(game, "%s hits your mule for %d HP.", enemy->name, damage);
                if (mule_hp <= 0) {
                    mule_hp = 0;
                    world_handle_mule_death(game);
                }
            } else {
                int armor = player_armor_dr(&game->player) + defend_bonus;
                int damage = clamp_int(enemy->damage + rng_range(game, -1, 1) - armor, 0, 99);
                game->player.hp -= damage;
                game_log(game, "%s hits you for %d HP.", enemy->name, damage);
                if (enemy->poisonous && damage > 0 && rng_chance(game, 50)) {
                    game->poison_turns = clamp_int(game->poison_turns + 3, 0, 6);
                    game_log(game, "Poison burns into the wound.");
                }
            }
        }
    }

    if (game->player.hp <= 0) {
        game_set_game_over(game, "You died in a fight on Kepler's Reach.");
        return COMBAT_RESULT_LOST;
    }

    {
        int payout = rng_range(game, enemy->bounty_min, enemy->bounty_max);
        game->player.credits += payout;
        game_log(game, "%s is down. You recover %d cr from the scene.", enemy->name, payout);
    }

    if (enemy_id == ENEMY_MUGGER || enemy_id == ENEMY_BANDIT || enemy_id == ENEMY_BANDIT_LEADER) {
        if (rng_chance(game, 30)) {
            if (player_add_cargo(&game->player, COMMODITY_STOLEN_GOODS, 1)) {
                game_log(game, "You find 1 x Stolen Goods among the remains.");
            }
        }
    }
    if (enemy_id == ENEMY_BANDIT || enemy_id == ENEMY_BANDIT_LEADER) {
        if (rng_chance(game, 20)) {
            if (player_add_cargo(&game->player, COMMODITY_NARCOTICS, 1)) {
                game_log(game, "You find 1 x Narcotics among the remains.");
            }
        }
    }

    if (enemy->max_hp > 10) {
        game->player.reputation++;
        game_log(game, "Word of the kill spreads. Reputation +1.");
    }
    return COMBAT_RESULT_WON;
}
