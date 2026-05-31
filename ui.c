#include "ui.h"

#include <ctype.h>
#include <ncurses.h>
#include <stdio.h>
#include <string.h>

#include "combat.h"
#include "encounter.h"
#include "market.h"
#include "player.h"
#include "prospect.h"
#include "save.h"
#include "util.h"
#include "world.h"

static const int LEFT_WIDTH = 48;
static const int RIGHT_X = 50;

static void draw_hp_bar(const game_t *game, int row, int col) {
    int filled = (game->player.hp * 10) / game->player.max_hp;
    mvprintw(row, col, "HP: ");
    for (int i = 0; i < 10; ++i) {
        addch(i < filled ? '#' : '.');
    }
    printw(" %d/%d", game->player.hp, game->player.max_hp);
}

static void draw_inventory_panel(const game_t *game) {
    int used = player_cargo_used_tenths(&game->player);
    int capacity = player_cargo_capacity_tenths(&game->player);

    mvprintw(1, RIGHT_X, "Inventory (%d.%d/%d.%d u)",
             used / 10, used % 10, capacity / 10, capacity % 10);
    for (int i = 0; i < 12; ++i) {
        move(2 + i, RIGHT_X);
        clrtoeol();
    }
    for (int i = 0; i < game->player.cargo_count && i < 10; ++i) {
        const cargo_stack_t *stack = &game->player.cargo[i];
        int units = COMMODITIES[stack->commodity].units_tenths * stack->quantity;
        mvprintw(2 + i, RIGHT_X, "%-17s x%-3d %2d.%d u",
                 COMMODITIES[stack->commodity].name,
                 stack->quantity,
                 units / 10,
                 units % 10);
    }

    mvprintw(14, RIGHT_X, "Weapon: %s", WEAPONS[game->player.weapon].name);
    mvprintw(15, RIGHT_X, "Armor:  %s", ARMORS[game->player.armor].name);
    mvprintw(16, RIGHT_X, "Bandages: %d", game->player.bandages);
    mvprintw(17, RIGHT_X, "Medkit uses: %d", game->player.medkit_uses);
}

static void draw_log_panel(const game_t *game) {
    int shown = game->log.count < 3 ? game->log.count : 3;

    mvprintw(21, 2, "Log:");
    for (int i = 0; i < 3; ++i) {
        move(22 + i, 2);
        clrtoeol();
    }
    for (int i = 0; i < shown; ++i) {
        int index = (game->log.head - shown + i + NEWS_TICKER_LINES) % NEWS_TICKER_LINES;
        mvprintw(22 + i, 2, "> %s", game->log.text[index]);
    }
}

static void draw_base_frame(const game_t *game, const char *commands) {
    const location_def_t *location = world_get_location(game->player.location);

    clear();
    mvhline(0, 0, '=', 80);
    mvprintw(0, 2, "SPACE TRADER");
    mvprintw(0, 30, "Turn: %d", game->turn);
    mvprintw(0, 45, "Credits: %d", game->player.credits);
    mvprintw(0, 65, "Rep: %d", game->player.reputation);
    for (int row = 1; row < 21; ++row) {
        mvaddch(row, LEFT_WIDTH, '|');
    }
    mvhline(20, 0, '-', 80);
    draw_inventory_panel(game);
    draw_hp_bar(game, 19, 2);
    mvprintw(19, 26, "Location: %s", location->name);
    mvprintw(20, 2, "%s", commands);
    draw_log_panel(game);
    refresh();
}

static void draw_location_view(const game_t *game) {
    const location_def_t *location = world_get_location(game->player.location);
    const char *commands;

    if (location->kind == LOCATION_KIND_WILDERNESS) {
        commands = "[P]rospect [R]est [T]ravel [I]nventory [G]round [V]Save [Q]uit";
    } else if (game->player.location == LOCATION_STARPORT) {
        commands = "[M]arket [B]ar [H]ospital [N]otice [T]ravel [I]nv [X]Pay [V]Save [Q]uit";
    } else {
        commands = "[S]tore [B]ar [C]linic [T]ravel [I]nventory [G]round [V]Save [Q]uit";
    }

    draw_base_frame(game, commands);
    mvprintw(2, 2, "%s", location->name);
    mvprintw(4, 2, "%s", location->art);
    mvprintw(9, 2, "%s", location->description);
    mvprintw(12, 2, "Routes:");
    for (int i = 0; i < location->neighbor_count; ++i) {
        mvprintw(13 + i, 4, "%d. %s", i + 1, LOCATIONS[location->neighbors[i]].name);
    }
    if (world_visible_drop_count(game, game->player.location) > 0) {
        mvprintw(17, 2, "Dropped cargo is visible here.");
    }
    refresh();
}

static void show_notice_board(game_t *game) {
    commodity_id_t commodity = COMMODITY_GRAIN_SEED;
    int running = 1;

    while (running) {
        draw_base_frame(game, "[Left/Right] commodity  [Esc] back");
        mvprintw(2, 2, "Starport Notice Board");
        mvprintw(4, 2, "Tracking: %s", COMMODITIES[commodity].name);
        for (int i = 0; i < MAX_LOCATIONS; ++i) {
            if (!market_has_open_market((location_id_t)i) || !game->markets[i].known) {
                continue;
            }
            mvprintw(6 + i, 4, "%-11s %4d cr  stock %2d",
                     LOCATIONS[i].name,
                     game->markets[i].prices[commodity],
                     game->markets[i].stock[commodity]);
        }
        refresh();

        switch (getch()) {
        case KEY_LEFT:
            commodity = (commodity == 0) ? (NUM_COMMODITIES - 1) : (commodity - 1);
            break;
        case KEY_RIGHT:
            commodity = (commodity + 1) % NUM_COMMODITIES;
            break;
        case 27:
        case 'q':
        case 'Q':
            running = 0;
            break;
        default:
            break;
        }
    }
}

static void inventory_menu(game_t *game) {
    int selected = 0;
    int running = 1;

    while (running) {
        draw_base_frame(game, "[Up/Down] select  [D]rop one  [A]drop all  [B]andage  [M]edkit  [Esc]");
        mvprintw(2, 2, "Inventory");
        if (game->player.cargo_count == 0) {
            mvprintw(4, 2, "No cargo.");
        }
        for (int i = 0; i < game->player.cargo_count; ++i) {
            int units = COMMODITIES[game->player.cargo[i].commodity].units_tenths * game->player.cargo[i].quantity;
            if (i == selected) {
                attron(A_REVERSE);
            }
            mvprintw(4 + i, 2, "%-20s x%-3d %2d.%d u",
                     COMMODITIES[game->player.cargo[i].commodity].name,
                     game->player.cargo[i].quantity,
                     units / 10,
                     units % 10);
            if (i == selected) {
                attroff(A_REVERSE);
            }
        }
        refresh();

        switch (getch()) {
        case KEY_UP:
            if (selected > 0) {
                selected--;
            }
            break;
        case KEY_DOWN:
            if (selected + 1 < game->player.cargo_count) {
                selected++;
            }
            break;
        case 'd':
        case 'D':
            if (game->player.cargo_count > 0) {
                cargo_stack_t stack = game->player.cargo[selected];
                player_remove_cargo(&game->player, stack.commodity, 1);
                world_drop_commodity(game, game->player.location, stack.commodity, 1);
                game_log(game, "Dropped 1 x %s.", COMMODITIES[stack.commodity].name);
            }
            break;
        case 'a':
        case 'A':
            if (game->player.cargo_count > 0) {
                cargo_stack_t stack = game->player.cargo[selected];
                player_remove_cargo(&game->player, stack.commodity, stack.quantity);
                world_drop_commodity(game, game->player.location, stack.commodity, stack.quantity);
                game_log(game, "Dropped %d x %s.", stack.quantity, COMMODITIES[stack.commodity].name);
                if (selected >= game->player.cargo_count && selected > 0) {
                    selected--;
                }
            }
            break;
        case 'b':
        case 'B':
            player_use_bandage(game);
            break;
        case 'm':
        case 'M':
            player_use_medkit(game);
            break;
        case 27:
        case 'q':
        case 'Q':
            running = 0;
            break;
        default:
            break;
        }
    }
}

static void ground_menu(game_t *game) {
    int selected = 0;
    int running = 1;

    while (running) {
        draw_base_frame(game, "[Up/Down] select  [Enter] pick up  [Esc]");
        mvprintw(2, 2, "Ground cargo");
        for (int i = 0; i < MAX_DROP_STACKS_PER_LOCATION; ++i) {
            drop_slot_t *slot = &game->drops[game->player.location].slots[i];
            if (i == selected) {
                attron(A_REVERSE);
            }
            if (slot->occupied) {
                mvprintw(4 + i, 2, "%-20s x%-3d", COMMODITIES[slot->commodity].name, slot->quantity);
            } else {
                mvprintw(4 + i, 2, "(empty)");
            }
            if (i == selected) {
                attroff(A_REVERSE);
            }
        }
        refresh();

        switch (getch()) {
        case KEY_UP:
            if (selected > 0) {
                selected--;
            }
            break;
        case KEY_DOWN:
            if (selected + 1 < MAX_DROP_STACKS_PER_LOCATION) {
                selected++;
            }
            break;
        case '\n':
        case KEY_ENTER:
            world_pickup_drop(game, game->player.location, selected);
            break;
        case 27:
        case 'q':
        case 'Q':
            running = 0;
            break;
        default:
            break;
        }
    }
}

static void trade_menu(game_t *game, bool fence) {
    int selected = 0;
    int running = 1;

    while (running) {
        draw_base_frame(game, fence ? "[Up/Down] [S]ell one [A]ll [Esc]" :
                               "[Up/Down] [B]uy one [M]ax [S]ell one [A]ll [Esc]");
        mvprintw(2, 2, fence ? "Back-room Fence" : "Commodity Market");
        for (int i = 0; i < NUM_COMMODITIES; ++i) {
            int own = player_cargo_quantity(&game->player, (commodity_id_t)i);
            int price = fence ? market_fence_price((commodity_id_t)i) :
                                (market_can_sell_openly(game->player.location, (commodity_id_t)i)
                                     ? market_sell_price(game, game->player.location, (commodity_id_t)i)
                                     : market_buy_price(game, game->player.location, (commodity_id_t)i));
            int stock = game->markets[game->player.location].stock[i];
            bool show = fence ? (i == COMMODITY_NARCOTICS || i == COMMODITY_STOLEN_GOODS || i == COMMODITY_ARTIFACTS)
                              : true;

            if (!show) {
                continue;
            }
            if (i == selected) {
                attron(A_REVERSE);
            }
            mvprintw(4 + i, 2, "%-22s %4d cr  stock:%2d own:%2d",
                     COMMODITIES[i].name, price, stock, own);
            if (i == selected) {
                attroff(A_REVERSE);
            }
        }
        refresh();

        switch (getch()) {
        case KEY_UP:
            selected = (selected == 0) ? (NUM_COMMODITIES - 1) : (selected - 1);
            break;
        case KEY_DOWN:
            selected = (selected + 1) % NUM_COMMODITIES;
            break;
        case 'b':
        case 'B':
            if (!fence) {
                market_buy(game, game->player.location, (commodity_id_t)selected, 1);
            }
            break;
        case 'm':
        case 'M':
            if (!fence) {
                while (market_buy(game, game->player.location, (commodity_id_t)selected, 1)) {
                }
            }
            break;
        case 's':
        case 'S':
            if (fence) {
                if (player_remove_cargo(&game->player, (commodity_id_t)selected, 1)) {
                    int payout = market_fence_price((commodity_id_t)selected);
                    game->player.credits += payout;
                    game_log(game, "Fenced 1 x %s for %d cr.", COMMODITIES[selected].name, payout);
                }
            } else {
                market_sell(game, game->player.location, (commodity_id_t)selected, 1);
            }
            break;
        case 'a':
        case 'A': {
            int qty = player_cargo_quantity(&game->player, (commodity_id_t)selected);
            if (qty > 0) {
                if (fence) {
                    player_remove_cargo(&game->player, (commodity_id_t)selected, qty);
                    game->player.credits += market_fence_price((commodity_id_t)selected) * qty;
                    game_log(game, "Fenced %d x %s.", qty, COMMODITIES[selected].name);
                } else {
                    market_sell(game, game->player.location, (commodity_id_t)selected, qty);
                }
            }
            break;
        }
        case 27:
        case 'q':
        case 'Q':
            running = 0;
            break;
        default:
            break;
        }
    }
}

static void store_menu(game_t *game) {
    int selected = 0;
    int running = 1;

    while (running) {
        draw_base_frame(game, "[Up/Down] [Enter] buy [Esc]");
        mvprintw(2, 2, game->player.location == LOCATION_STARPORT ? "Starport Market Gear" : "Settlement Store");
        for (int i = 0; i < SHOP_ITEM_COUNT; ++i) {
            if (!player_can_offer_shop_item(game->player.location, (shop_item_id_t)i)) {
                continue;
            }
            if (i == selected) {
                attron(A_REVERSE);
            }
            mvprintw(4 + i, 2, "%-20s %4d cr  %s%s",
                     SHOP_ITEMS[i].name,
                     (game->player.reputation >= 2 ? (SHOP_ITEMS[i].cost * 95) / 100 : SHOP_ITEMS[i].cost),
                     SHOP_ITEMS[i].description,
                     player_owns_shop_item(&game->player, (shop_item_id_t)i) ? " [owned]" : "");
            if (i == selected) {
                attroff(A_REVERSE);
            }
        }
        refresh();

        switch (getch()) {
        case KEY_UP:
            selected = (selected == 0) ? (SHOP_ITEM_COUNT - 1) : (selected - 1);
            break;
        case KEY_DOWN:
            selected = (selected + 1) % SHOP_ITEM_COUNT;
            break;
        case '\n':
        case KEY_ENTER:
            player_buy_shop_item(game, (shop_item_id_t)selected);
            break;
        case 27:
        case 'q':
        case 'Q':
            running = 0;
            break;
        default:
            break;
        }
    }
}

static void bar_menu(game_t *game) {
    int running = 1;

    encounter_on_bar_entry(game);
    while (running) {
        draw_base_frame(game, "[R]oom [F]ence [U]rumour [B]rawl [Esc]");
        mvprintw(2, 2, "Bar");
        mvprintw(4, 2, "Room: 20 cr for +2 HP");
        mvprintw(5, 2, "Fence: contraband and artifacts at 70%% of base price");
        mvprintw(6, 2, "Brawl: straight-up fistfight for local cash");
        refresh();

        switch (getch()) {
        case 'r':
        case 'R':
            if (game->player.credits >= 20) {
                game->player.credits -= 20;
                player_heal(&game->player, 2);
                game_advance_turn(game);
                game_log(game, "You rent a room and recover 2 HP.");
            } else {
                game_log(game, "You cannot afford a room.");
            }
            break;
        case 'f':
        case 'F':
            trade_menu(game, true);
            break;
        case 'u':
        case 'U':
            if (game->player.credits >= 10) {
                char rumour[128];
                game->player.credits -= 10;
                market_generate_rumour(game, rumour, sizeof(rumour));
                game_log(game, "Rumour: %s", rumour);
            } else {
                game_log(game, "The barkeep has no charity rumours left.");
            }
            break;
        case 'b':
        case 'B':
            if (combat_run(game, ENEMY_BAR_BRAWLER, "Bar Brawl") == COMBAT_RESULT_WON) {
                int purse = rng_range(game, 10, 50);
                game->player.credits += purse;
                game->player.reputation++;
                game_log(game, "You take the purse and the room's respect. +%d cr, rep +1.", purse);
            }
            if (game->state == GAME_STATE_GAME_OVER) {
                return;
            }
            break;
        case 27:
        case 'q':
        case 'Q':
            running = 0;
            break;
        default:
            break;
        }
    }
}

static void heal_menu(game_t *game, bool hospital) {
    int running = 1;
    int per_hp = hospital ? 50 : (game->player.reputation >= 2 ? 20 : 25);
    int cap = hospital ? 8 : 3;

    while (running) {
        draw_base_frame(game, "[H]eal 1  [M]ax  [D]onate (clinic)  [Esc]");
        mvprintw(2, 2, hospital ? "Starport Medical" : "Settlement Clinic");
        mvprintw(4, 2, "%s price: %d cr per HP, up to %d HP per visit",
                 hospital ? "Hospital" : "Clinic", per_hp, cap);
        refresh();

        switch (getch()) {
        case 'h':
        case 'H':
            if (game->player.hp >= game->player.max_hp) {
                game_log(game, "You do not need treatment.");
            } else if (game->player.credits < per_hp) {
                game_log(game, "You cannot afford treatment.");
            } else {
                game->player.credits -= per_hp;
                player_heal(&game->player, 1);
                game_log(game, "Treatment restores 1 HP.");
            }
            break;
        case 'm':
        case 'M': {
            int missing = game->player.max_hp - game->player.hp;
            int heal = clamp_int(missing, 0, cap);
            int cost = heal * per_hp;
            if (heal <= 0) {
                game_log(game, "You do not need treatment.");
            } else if (game->player.credits < cost) {
                game_log(game, "You cannot afford full treatment.");
            } else {
                game->player.credits -= cost;
                player_heal(&game->player, heal);
                game_log(game, "Treatment restores %d HP.", heal);
            }
            break;
        }
        case 'd':
        case 'D':
            if (!hospital && game->player.credits >= 50) {
                game->player.credits -= 50;
                game->player.reputation++;
                game_log(game, "You donate 50 cr to the clinic. Reputation +1.");
            }
            break;
        case 27:
        case 'q':
        case 'Q':
            running = 0;
            break;
        default:
            break;
        }
    }
}

static void travel_menu(game_t *game) {
    int selected = 0;
    const location_def_t *location = world_get_location(game->player.location);
    int running = 1;

    while (running) {
        draw_base_frame(game, "[Up/Down] [Enter] travel [Esc]");
        mvprintw(2, 2, "Travel from %s", location->name);
        for (int i = 0; i < location->neighbor_count; ++i) {
            location_id_t next = (location_id_t)location->neighbors[i];
            if (i == selected) {
                attron(A_REVERSE);
            }
            mvprintw(4 + i, 2, "%s (%s)", LOCATIONS[next].name,
                     LOCATIONS[next].kind == LOCATION_KIND_WILDERNESS ? "wilderness" : "settlement");
            if (i == selected) {
                attroff(A_REVERSE);
            }
        }
        refresh();

        switch (getch()) {
        case KEY_UP:
            if (selected > 0) {
                selected--;
            }
            break;
        case KEY_DOWN:
            if (selected + 1 < location->neighbor_count) {
                selected++;
            }
            break;
        case '\n':
        case KEY_ENTER:
            world_travel(game, (location_id_t)location->neighbors[selected]);
            running = 0;
            break;
        case 27:
        case 'q':
        case 'Q':
            running = 0;
            break;
        default:
            break;
        }
    }
}

static void title_screen(game_t *game) {
    int running = 1;
    char error[128];

    while (running && game->running) {
        load_high_scores(game);
        clear();
        mvprintw(2, 24, "SPACE TRADER");
        mvprintw(4, 18, "[N]ew Game   [L]oad Game   [Q]uit");
        mvprintw(6, 18, "First cut: trading, travel, prospecting, combat, saves.");
        mvprintw(8, 18, "High Scores");
        for (int i = 0; i < game->score_count && i < MAX_HIGH_SCORES; ++i) {
            mvprintw(10 + i, 18, "%2d. %-12s %6d  %s",
                     i + 1,
                     game->scores[i].name,
                     game->scores[i].score,
                     game->scores[i].outcome);
        }
        refresh();

        switch (getch()) {
        case 'n':
        case 'N':
            game_init_new(game);
            load_high_scores(game);
            running = 0;
            break;
        case 'l':
        case 'L':
            if (!load_game(game, error, sizeof(error))) {
                game_log(game, "%s", error);
            } else {
                load_high_scores(game);
                running = 0;
            }
            break;
        case 'q':
        case 'Q':
            game->running = false;
            running = 0;
            break;
        default:
            break;
        }
    }
}

static void end_screen(game_t *game, const char *label) {
    record_high_score(game, label);
    clear();
    mvprintw(4, 24, "%s", label);
    mvprintw(6, 10, "%s", game->end_reason);
    mvprintw(8, 10, "Turns: %d", game->turn);
    mvprintw(9, 10, "Credits: %d", game->player.credits);
    mvprintw(10, 10, "Cargo value: %d", market_estimate_cargo_value(game));
    mvprintw(12, 10, "Press any key to return to title.");
    refresh();
    getch();
    game->state = GAME_STATE_TITLE;
}

static void save_current_game(game_t *game) {
    char error[128];

    if (save_game(game, error, sizeof(error))) {
        game_log(game, "Game saved.");
    } else {
        game_log(game, "%s", error);
    }
}

static void location_loop(game_t *game) {
    int ch;

    draw_location_view(game);
    ch = getch();
    switch (ch) {
    case 'm':
    case 'M':
        if (game->player.location == LOCATION_STARPORT) {
            trade_menu(game, false);
            store_menu(game);
        }
        break;
    case 's':
    case 'S':
        if (LOCATIONS[game->player.location].kind != LOCATION_KIND_WILDERNESS &&
            game->player.location != LOCATION_STARPORT) {
            trade_menu(game, false);
            store_menu(game);
        }
        break;
    case 'b':
    case 'B':
        if (LOCATIONS[game->player.location].kind != LOCATION_KIND_WILDERNESS) {
            bar_menu(game);
        }
        break;
    case 'h':
    case 'H':
        if (game->player.location == LOCATION_STARPORT) {
            heal_menu(game, true);
        }
        break;
    case 'c':
    case 'C':
        if (LOCATIONS[game->player.location].kind == LOCATION_KIND_SETTLEMENT) {
            heal_menu(game, false);
        }
        break;
    case 'n':
    case 'N':
        if (game->player.location == LOCATION_STARPORT) {
            show_notice_board(game);
        }
        break;
    case 'p':
    case 'P':
        if (LOCATIONS[game->player.location].kind == LOCATION_KIND_WILDERNESS) {
            prospect_run(game);
        }
        break;
    case 'r':
    case 'R':
        if (LOCATIONS[game->player.location].kind == LOCATION_KIND_WILDERNESS) {
            world_rest(game);
        }
        break;
    case 't':
    case 'T':
        travel_menu(game);
        break;
    case 'i':
    case 'I':
        inventory_menu(game);
        break;
    case 'g':
    case 'G':
        ground_menu(game);
        break;
    case 'x':
    case 'X':
        if (game->player.location == LOCATION_STARPORT) {
            world_pay_impound(game);
        }
        break;
    case 'v':
    case 'V':
        save_current_game(game);
        break;
    case 'q':
    case 'Q':
        game->running = false;
        break;
    default:
        break;
    }
}

void ui_run(game_t *game) {
    initscr();
    cbreak();
    noecho();
    keypad(stdscr, true);
    curs_set(0);

    game->running = true;
    game->state = GAME_STATE_TITLE;

    while (game->running) {
        if (game->state == GAME_STATE_TITLE) {
            title_screen(game);
        } else if (game->state == GAME_STATE_LOCATION) {
            location_loop(game);
        } else if (game->state == GAME_STATE_VICTORY) {
            end_screen(game, "LIFTOFF");
        } else if (game->state == GAME_STATE_GAME_OVER) {
            end_screen(game, "EPITAPH");
        }
    }

    endwin();
}
