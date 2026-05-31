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
static const int LEFT_TEXT_X = 2;
static const int LEFT_TEXT_WIDTH = 45;
static const int MAX_WRAP_LINES = 96;
static const int HP_COLOR_PAIR_HEALTHY = 1;
static const int HP_COLOR_PAIR_WOUNDED = 2;
static const int HP_COLOR_PAIR_CRITICAL = 3;
static bool ui_colors_enabled = false;
static bool confirm_exit_game(void);

static int hp_color_pair(const game_t *game) {
    int hp = game->player.hp;

    if (!ui_colors_enabled) {
        return 0;
    }

    if (hp >= 7) {
        return HP_COLOR_PAIR_HEALTHY;
    }
    if (hp >= 4) {
        return HP_COLOR_PAIR_WOUNDED;
    }
    return HP_COLOR_PAIR_CRITICAL;
}

static void draw_hp_bar(const game_t *game, int row, int col) {
    int max_hp = game->player.max_hp > 0 ? game->player.max_hp : 1;
    int filled = clamp_int((game->player.hp * 10) / max_hp, 0, 10);
    int color_pair = hp_color_pair(game);

    mvprintw(row, col, "HP: ");
    if (color_pair != 0) {
        attron(COLOR_PAIR(color_pair));
    }
    for (int i = 0; i < filled; ++i) {
        addch('#');
    }
    if (color_pair != 0) {
        attroff(COLOR_PAIR(color_pair));
    }
    for (int i = filled; i < 10; ++i) {
        addch('.');
    }

    printw(" %d/%d", game->player.hp, game->player.max_hp);
}

static int clamp_scroll_offset(int scroll, int total_lines, int visible_rows) {
    int max_scroll = total_lines - visible_rows;

    if (max_scroll < 0) {
        max_scroll = 0;
    }
    if (scroll < 0) {
        return 0;
    }
    if (scroll > max_scroll) {
        return max_scroll;
    }
    return scroll;
}

static int wrap_text_lines(const char *text, int width, char lines[][LOG_LINE_LENGTH], int max_lines) {
    const char *cursor = text;
    int count = 0;

    if (text == NULL || width <= 0 || max_lines <= 0) {
        return 0;
    }

    while (*cursor != '\0' && count < max_lines) {
        const char *segment_end = cursor;

        while (*segment_end != '\0' && *segment_end != '\n') {
            segment_end++;
        }

        if (cursor == segment_end) {
            lines[count][0] = '\0';
            count++;
        } else {
            const char *segment = cursor;

            while (segment < segment_end && count < max_lines) {
                size_t take;

                while (segment < segment_end && *segment == ' ') {
                    segment++;
                }
                if (segment >= segment_end) {
                    break;
                }

                take = (size_t)(segment_end - segment);
                if (take > (size_t)width) {
                    size_t split = (size_t)width;

                    while (split > 0 && segment[split] != ' ') {
                        split--;
                    }
                    if (split > 0) {
                        take = split;
                    } else {
                        take = (size_t)width;
                    }
                }
                while (take > 0 && segment[take - 1] == ' ') {
                    take--;
                }
                if (take == 0) {
                    take = 1;
                }
                if (take >= LOG_LINE_LENGTH) {
                    take = LOG_LINE_LENGTH - 1;
                }

                memcpy(lines[count], segment, take);
                lines[count][take] = '\0';
                count++;

                segment += take;
                while (segment < segment_end && *segment == ' ') {
                    segment++;
                }
            }
        }

        cursor = (*segment_end == '\n') ? (segment_end + 1) : segment_end;
    }

    return count;
}

static void draw_clipped_multiline(const char *text, int row, int col, int width, int height) {
    const char *cursor = text;

    for (int i = 0; i < height; ++i) {
        mvhline(row + i, col, ' ', width);
    }
    if (text == NULL || width <= 0 || height <= 0) {
        return;
    }

    for (int line = 0; line < height && *cursor != '\0'; ++line) {
        const char *line_end = cursor;
        size_t take;

        while (*line_end != '\0' && *line_end != '\n') {
            line_end++;
        }

        take = (size_t)(line_end - cursor);
        if (take > (size_t)width) {
            take = (size_t)width;
        }

        mvaddnstr(row + line, col, cursor, (int)take);
        cursor = (*line_end == '\n') ? (line_end + 1) : line_end;
    }
}

static int draw_wrapped_text(const char *text, int row, int col, int width, int height, int scroll) {
    char lines[MAX_WRAP_LINES][LOG_LINE_LENGTH];
    int line_count = wrap_text_lines(text, width, lines, MAX_WRAP_LINES);
    int offset = clamp_scroll_offset(scroll, line_count, height);

    for (int i = 0; i < height; ++i) {
        int line_index = offset + i;

        mvhline(row + i, col, ' ', width);
        if (line_index < line_count) {
            mvaddnstr(row + i, col, lines[line_index], width);
        }
    }

    return line_count;
}

static void draw_inventory_panel(const game_t *game) {
    int used = player_cargo_used_tenths(&game->player);
    int capacity = player_cargo_capacity_tenths(&game->player);

    mvprintw(2, RIGHT_X, "Inventory (%d.%d/%d.%d u)",
             used / 10, used % 10, capacity / 10, capacity % 10);
    for (int i = 0; i < 9; ++i) {
        move(3 + i, RIGHT_X);
        clrtoeol();
    }
    for (int i = 0; i < game->player.cargo_count && i < 9; ++i) {
        const cargo_stack_t *stack = &game->player.cargo[i];
        int units = COMMODITIES[stack->commodity].units_tenths * stack->quantity;
        mvprintw(3 + i, RIGHT_X, "%-17s x%-3d %2d.%d u",
                 COMMODITIES[stack->commodity].name,
                 stack->quantity,
                 units / 10,
                 units % 10);
    }

    mvprintw(15, RIGHT_X, "Weapon: %s", WEAPONS[game->player.weapon].name);
    mvprintw(16, RIGHT_X, "Armor:  %s", ARMORS[game->player.armor].name);
    mvprintw(17, RIGHT_X, "Bandages: %d", game->player.bandages);
    mvprintw(18, RIGHT_X, "Medkit uses: %d", game->player.medkit_uses);
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
    mvhline(20, 0, '-', 80);
    for (int row = 1; row <= 20; ++row) {
        mvaddch(row, LEFT_WIDTH, '|');
    }
    mvaddch(20, LEFT_WIDTH, '+');
    draw_inventory_panel(game);
    draw_hp_bar(game, 19, 2);
    mvprintw(19, 26, "Location: %s", location->name);
    mvprintw(20, 2, "%s", commands);
    draw_log_panel(game);
    refresh();
}

static int draw_location_view(const game_t *game, int description_scroll) {
    const location_def_t *location = world_get_location(game->player.location);
    char commands[128];
    const char *ground_option = "";
    bool has_ground = world_visible_drop_count(game, game->player.location) > 0;
    int description_lines;

    if (has_ground) {
        ground_option = " [G]round";
    }

    if (location->kind == LOCATION_KIND_WILDERNESS) {
        snprintf(commands, sizeof(commands), "[1-4]Travel [P]rospect [R]est [I]nventory%s [V]Save [Q]uit", ground_option);
    } else if (game->player.location == LOCATION_STARPORT) {
        snprintf(commands, sizeof(commands), "[1-4]Travel [M]arket [B]ar [H]ospital [N]otice [I]nv%s [X]Pay [V]Save [Q]uit", ground_option);
    } else {
        snprintf(commands, sizeof(commands), "[1-4]Travel [S]tore [B]ar [C]linic [I]nventory%s [V]Save [Q]uit", ground_option);
    }

    draw_base_frame(game, commands);
    mvprintw(2, 2, "%s", location->name);
    draw_clipped_multiline(location->art, 4, LEFT_TEXT_X, LEFT_TEXT_WIDTH, 5);
    description_lines = draw_wrapped_text(location->description, 9, LEFT_TEXT_X, LEFT_TEXT_WIDTH, 3, description_scroll);
    if (description_lines > 3) {
        mvprintw(8, 2, "PgUp/PgDn to scroll description");
    } else {
        mvhline(8, LEFT_TEXT_X, ' ', LEFT_TEXT_WIDTH);
    }
    mvprintw(13, 2, "Routes (1-4):");
    for (int i = 0; i < location->neighbor_count; ++i) {
        mvprintw(14 + i, 4, "%d. %s", i + 1, LOCATIONS[location->neighbors[i]].name);
    }
    if (has_ground) {
        mvprintw(18, 2, "Dropped cargo is visible here.");
    }
    for (int row = 1; row <= 20; ++row) {
        mvaddch(row, LEFT_WIDTH, '|');
    }
    mvhline(12, 0, '-', LEFT_WIDTH);
    mvhline(12, RIGHT_X, '-', 80 - RIGHT_X);
    mvaddch(12, LEFT_WIDTH, '+');
    mvaddch(20, LEFT_WIDTH, '+');
    refresh();

    return description_lines;
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
    int top = 0;
    int running = 1;
    int options[NUM_COMMODITIES];
    int option_count = 0;
    const int list_row = 4;
    const int list_height = 14;

    for (int i = 0; i < NUM_COMMODITIES; ++i) {
        bool show = fence ? (i == COMMODITY_NARCOTICS || i == COMMODITY_STOLEN_GOODS || i == COMMODITY_ARTIFACTS)
                          : true;
        if (show) {
            options[option_count++] = i;
        }
    }

    while (running) {
        draw_base_frame(game, fence ? "[Up/Down] [PgUp/PgDn] [S]ell one [A]ll [Esc]" :
                               "[Up/Down] [PgUp/PgDn] [B]uy one [M]ax [S]ell one [A]ll [Esc]");
        mvprintw(2, 2, fence ? "Back-room Fence" : "Commodity Market");
        if (option_count == 0) {
            mvprintw(4, 2, "No commodities available.");
            refresh();
            switch (getch()) {
            case 27:
            case 'q':
            case 'Q':
                running = 0;
                break;
            default:
                break;
            }
            continue;
        }
        selected = clamp_int(selected, 0, option_count - 1);
        if (selected < top) {
            top = selected;
        } else if (selected >= top + list_height) {
            top = selected - list_height + 1;
        }
        top = clamp_scroll_offset(top, option_count, list_height);

        mvprintw(3, 2, "Showing %d-%d of %d",
                 option_count == 0 ? 0 : (top + 1),
                 option_count == 0 ? 0 : clamp_int(top + list_height, 0, option_count),
                 option_count);
        for (int row = 0; row < list_height; ++row) {
            mvhline(list_row + row, LEFT_TEXT_X, ' ', LEFT_TEXT_WIDTH);
        }

        for (int row = 0; row < list_height && (top + row) < option_count; ++row) {
            int commodity_index = options[top + row];
            int own = player_cargo_quantity(&game->player, (commodity_id_t)commodity_index);
            int price = market_fence_price((commodity_id_t)commodity_index);
            int stock = game->markets[game->player.location].stock[commodity_index];
            char line[LOG_LINE_LENGTH];

            if (!fence) {
                price = market_can_sell_openly(game->player.location, (commodity_id_t)commodity_index)
                            ? market_sell_price(game, game->player.location, (commodity_id_t)commodity_index)
                            : market_buy_price(game, game->player.location, (commodity_id_t)commodity_index);
                snprintf(line, sizeof(line), "%-18.18s %4d cr  st:%2d own:%2d",
                         COMMODITIES[commodity_index].name, price, stock, own);
            } else {
                snprintf(line, sizeof(line), "%-18.18s %4d cr  own:%2d",
                         COMMODITIES[commodity_index].name, price, own);
            }

            if ((top + row) == selected) {
                attron(A_REVERSE);
            }
            mvaddnstr(list_row + row, LEFT_TEXT_X, line, LEFT_TEXT_WIDTH);
            if ((top + row) == selected) {
                attroff(A_REVERSE);
            }
        }
        refresh();

        switch (getch()) {
        case KEY_UP:
            selected = (selected == 0) ? (option_count - 1) : (selected - 1);
            break;
        case KEY_DOWN:
            selected = (selected + 1) % option_count;
            break;
        case KEY_PPAGE:
            selected = clamp_int(selected - list_height, 0, option_count - 1);
            break;
        case KEY_NPAGE:
            selected = clamp_int(selected + list_height, 0, option_count - 1);
            break;
        case 'b':
        case 'B':
            if (!fence) {
                market_buy(game, game->player.location, (commodity_id_t)options[selected], 1);
            }
            break;
        case 'm':
        case 'M':
            if (!fence) {
                while (market_buy(game, game->player.location, (commodity_id_t)options[selected], 1)) {
                }
            }
            break;
        case 's':
        case 'S':
            if (fence) {
                if (player_remove_cargo(&game->player, (commodity_id_t)options[selected], 1)) {
                    int payout = market_fence_price((commodity_id_t)options[selected]);
                    game->player.credits += payout;
                    game_log(game, "Fenced 1 x %s for %d cr.", COMMODITIES[options[selected]].name, payout);
                }
            } else {
                market_sell(game, game->player.location, (commodity_id_t)options[selected], 1);
            }
            break;
        case 'a':
        case 'A': {
            int qty = player_cargo_quantity(&game->player, (commodity_id_t)options[selected]);
            if (qty > 0) {
                if (fence) {
                    player_remove_cargo(&game->player, (commodity_id_t)options[selected], qty);
                    game->player.credits += market_fence_price((commodity_id_t)options[selected]) * qty;
                    game_log(game, "Fenced %d x %s.", qty, COMMODITIES[options[selected]].name);
                } else {
                    market_sell(game, game->player.location, (commodity_id_t)options[selected], qty);
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
    int top = 0;
    int running = 1;
    int options[SHOP_ITEM_COUNT];
    int option_count = 0;
    const int list_row = 4;
    const int list_height = 10;

    for (int i = 0; i < SHOP_ITEM_COUNT; ++i) {
        if (player_can_offer_shop_item(game->player.location, (shop_item_id_t)i)) {
            options[option_count++] = i;
        }
    }

    while (running) {
        draw_base_frame(game, "[Up/Down] [PgUp/PgDn] [Enter] buy [Esc]");
        mvprintw(2, 2, game->player.location == LOCATION_STARPORT ? "Starport Market Gear" : "Settlement Store");
        if (option_count == 0) {
            mvprintw(4, 2, "No items available.");
            refresh();
            switch (getch()) {
            case 27:
            case 'q':
            case 'Q':
                running = 0;
                break;
            default:
                break;
            }
            continue;
        }
        selected = clamp_int(selected, 0, option_count - 1);
        if (selected < top) {
            top = selected;
        } else if (selected >= top + list_height) {
            top = selected - list_height + 1;
        }
        top = clamp_scroll_offset(top, option_count, list_height);

        mvprintw(3, 2, "Showing %d-%d of %d",
                 option_count == 0 ? 0 : (top + 1),
                 option_count == 0 ? 0 : clamp_int(top + list_height, 0, option_count),
                 option_count);
        for (int row = 0; row < list_height; ++row) {
            mvhline(list_row + row, LEFT_TEXT_X, ' ', LEFT_TEXT_WIDTH);
        }

        for (int row = 0; row < list_height && (top + row) < option_count; ++row) {
            int item = options[top + row];
            char line[LOG_LINE_LENGTH];

            snprintf(line, sizeof(line), "%-18s %4d cr%s",
                     SHOP_ITEMS[item].name,
                     (game->player.reputation >= 2 ? (SHOP_ITEMS[item].cost * 95) / 100 : SHOP_ITEMS[item].cost),
                     player_owns_shop_item(&game->player, (shop_item_id_t)item) ? " [owned]" : "");
            if ((top + row) == selected) {
                attron(A_REVERSE);
            }
            mvaddnstr(list_row + row, LEFT_TEXT_X, line, LEFT_TEXT_WIDTH);
            if ((top + row) == selected) {
                attroff(A_REVERSE);
            }
        }

        mvprintw(15, 2, "Description:");
        if (option_count > 0) {
            draw_wrapped_text(SHOP_ITEMS[options[selected]].description, 16, LEFT_TEXT_X, LEFT_TEXT_WIDTH, 3, 0);
        } else {
            draw_wrapped_text("No items available.", 16, LEFT_TEXT_X, LEFT_TEXT_WIDTH, 3, 0);
        }
        refresh();

        switch (getch()) {
        case KEY_UP:
            selected = (selected == 0) ? (option_count - 1) : (selected - 1);
            break;
        case KEY_DOWN:
            selected = (selected + 1) % option_count;
            break;
        case KEY_PPAGE:
            selected = clamp_int(selected - list_height, 0, option_count - 1);
            break;
        case KEY_NPAGE:
            selected = clamp_int(selected + list_height, 0, option_count - 1);
            break;
        case '\n':
        case KEY_ENTER:
            player_buy_shop_item(game, (shop_item_id_t)options[selected]);
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
        draw_base_frame(game, "[R]oom [F]ence R[u]mors [B]rawl [Esc]");
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
    char service_text[LOG_LINE_LENGTH];

    while (running) {
        draw_base_frame(game, "[H]eal 1  [M]ax  [D]onate (clinic)  [Esc]");
        mvprintw(2, 2, hospital ? "Starport Medical" : "Settlement Clinic");
        snprintf(service_text, sizeof(service_text), "%s price: %d cr per HP, up to %d HP per visit",
                 hospital ? "Hospital" : "Clinic", per_hp, cap);
        draw_wrapped_text(service_text, 4, LEFT_TEXT_X, LEFT_TEXT_WIDTH, 2, 0);
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
            if (confirm_exit_game()) {
                game->running = false;
                running = 0;
            }
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

static bool confirm_exit_game(void) {
    int ch;

    move(LINES - 2, 0);
    clrtoeol();
    mvprintw(LINES - 2, 2, "Quit game? [Y]es / [N]o");
    refresh();

    while (1) {
        ch = getch();
        if (ch == 'y' || ch == 'Y') {
            return true;
        }
        if (ch == 'n' || ch == 'N' || ch == 27 || ch == 'q' || ch == 'Q') {
            return false;
        }
    }
}

static void location_loop(game_t *game) {
    static int description_scroll = 0;
    static location_id_t scroll_location = LOCATION_STARPORT;
    static bool scroll_initialized = false;
    int description_lines;
    int ch;

    if (!scroll_initialized || scroll_location != game->player.location) {
        scroll_location = game->player.location;
        description_scroll = 0;
        scroll_initialized = true;
    }

    description_lines = draw_location_view(game, description_scroll);
    ch = getch();
    if (ch >= '1' && ch <= '4') {
        const location_def_t *location = world_get_location(game->player.location);
        int route_index = ch - '1';

        if (route_index < location->neighbor_count) {
            world_travel(game, (location_id_t)location->neighbors[route_index]);
        }
        return;
    }

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
        if (world_visible_drop_count(game, game->player.location) > 0) {
            ground_menu(game);
        }
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
        if (confirm_exit_game()) {
            game->running = false;
        }
        break;
    case KEY_PPAGE:
        description_scroll = clamp_scroll_offset(description_scroll - 1, description_lines, 3);
        break;
    case KEY_NPAGE:
        description_scroll = clamp_scroll_offset(description_scroll + 1, description_lines, 3);
        break;
    default:
        break;
    }
}

void ui_run(game_t *game) {
    int hp_background = -1;

    initscr();
    cbreak();
    noecho();
    keypad(stdscr, true);
    curs_set(0);
    if (has_colors()) {
        start_color();
        if (use_default_colors() == ERR) {
            hp_background = COLOR_BLACK;
        }
        if (init_pair(HP_COLOR_PAIR_HEALTHY, COLOR_GREEN, hp_background) != ERR &&
            init_pair(HP_COLOR_PAIR_WOUNDED, COLOR_YELLOW, hp_background) != ERR &&
            init_pair(HP_COLOR_PAIR_CRITICAL, COLOR_RED, hp_background) != ERR) {
            ui_colors_enabled = true;
        }
    }

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
