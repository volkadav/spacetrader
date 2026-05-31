#include "ui.h"

#include <ctype.h>
#include <ncurses.h>
#include <signal.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "combat.h"
#include "encounter.h"
#include "gamble.h"
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
static const int MIN_TERMINAL_COLS = 80;
static const int MIN_TERMINAL_ROWS = 24;
static bool ui_colors_enabled = false;
static volatile sig_atomic_t ui_resize_pending = 0;
static bool confirm_exit_game(void);
static void blackjack_menu(game_t *game);
static void roulette_menu(game_t *game);
static void ui_prepare_screen(void);
static int ui_getch(void);

typedef enum {
    REP_TIER_LOW = 0,
    REP_TIER_NEUTRAL,
    REP_TIER_HIGH
} reputation_tier_t;

typedef struct {
    const char *name;
    const char *low;
    const char *neutral;
    const char *high;
} npc_dialogue_t;

static const npc_dialogue_t BAR_NPCS[MAX_LOCATIONS] = {
    [LOCATION_STARPORT] = {
        "Mags",
        "You look like trouble. Drink fast and don't start anything.",
        "What'll it be. And don't lean on the bar.",
        "There you are. Usual spot's free. First one's on the house tonight."
    },
    [LOCATION_ASHFIELD] = {
        "Old Henk",
        "You're new. That's fine. Keep your hands where I can see 'em, that's all I ask.",
        "Pull up a stool, friend. What can I get you?",
        "Ha! Look who's back. Sit down, sit down - I saved you some of the good batch."
    },
    [LOCATION_BROKENHILL] = {
        "Rook",
        "Pay first. Drink. Leave. That's the whole arrangement.",
        "What do you want.",
        "Good to see a straight face in here. The usual?"
    },
    [LOCATION_COLDWATER] = {
        "Lena Vasquez",
        "A quiet bar is a good bar. You seem like you might complicate that.",
        "What can I get you?",
        "I heard something you might find useful. Drink first - then we'll talk."
    },
    [LOCATION_MILLHAVEN] = {
        "Cam",
        "We serve everyone here. I didn't say we trusted everyone.",
        "Evening. What do you need?",
        "Good to have you in. The harvest batch just came through - it's better than last year."
    }
};

static const npc_dialogue_t HEAL_NPCS[MAX_LOCATIONS] = {
    [LOCATION_STARPORT] = {
        "Dr. Ananya Patel",
        "I treat everyone. That doesn't mean I have to converse with everyone.",
        "Where does it hurt. How long ago. Sit down.",
        "You again. Let me take a look - and try to avoid whatever caused this."
    },
    [LOCATION_ASHFIELD] = {
        "Nurse Sera Yun",
        "I'll treat you because that's my job. Don't make it harder than it has to be.",
        "Come in, sit there. Tell me what happened.",
        "Glad you came in before it got worse. Let me sort this out - and I'll keep the fee reasonable."
    },
    [LOCATION_BROKENHILL] = {
        "Doc Ferris",
        "I patch up anyone who comes through that door. Doesn't mean I have to like it.",
        "Sit down. This won't take long.",
        "Ah, you again. Still getting into scrapes. Let me take a look - I'll charge you the workers' rate."
    },
    [LOCATION_COLDWATER] = {
        "Sister Brynn",
        "Healing is given freely. Whether I do it warmly depends on you.",
        "Come in, sit down. Let me see what we're working with.",
        "Back again - I'm glad you know where to come. I'll put the kettle on."
    },
    [LOCATION_MILLHAVEN] = {
        "Elder Maris",
        "I heal who comes to me. I don't ask questions. I do ask that you return the favour.",
        "What happened, and when. Give me the facts.",
        "Sit, sit. You look like you've earned a proper rest. I'll see to this."
    }
};

static const npc_dialogue_t TRADE_NPCS[MAX_LOCATIONS] = {
    [LOCATION_STARPORT] = {
        "Juno Creed",
        "I don't do charity and I don't do credit. Cash, up front.",
        "Here to buy or to look? Either way, don't touch the merchandise.",
        "I've been saving something back for you, actually. Come have a look."
    },
    [LOCATION_ASHFIELD] = {
        "Dottie Marsh",
        "I run a clean shop. You cause trouble, you take it elsewhere.",
        "Looking for something in particular, or just browsing?",
        "I kept a few things back since I thought you might be coming through. Have a look."
    },
    [LOCATION_BROKENHILL] = {
        "Gal Okonkwo",
        "My prices aren't negotiable and neither am I. Buy or don't.",
        "Stock list is on the board. Let me know what you need.",
        "Always good to deal with someone who knows the value of things. I'll see what I can do on the price."
    },
    [LOCATION_COLDWATER] = {
        "Tomasz Wick",
        "Coldwater's a trading town. You want something, you pay for it. Simple.",
        "Good day. Stock's fresh in. The board shows what we have.",
        "Ah, my favourite customer. Let me show you what came in on the last run."
    },
    [LOCATION_MILLHAVEN] = {
        "Pell",
        "We trade with everyone. The collective voted on it. Doesn't mean I have to enjoy it.",
        "Welcome to the collective store. We have what we have.",
        "Oh good, it's you. I kept some of the good grain aside - I thought you might be passing through."
    }
};

static reputation_tier_t reputation_tier_for(int reputation) {
    if (reputation <= -2) {
        return REP_TIER_LOW;
    }
    if (reputation >= 2) {
        return REP_TIER_HIGH;
    }
    return REP_TIER_NEUTRAL;
}

static const npc_dialogue_t *npc_dialogue_for(const npc_dialogue_t *dialogues, location_id_t location) {
    if (dialogues == NULL || location < 0 || location >= MAX_LOCATIONS) {
        return NULL;
    }
    return dialogues[location].name != NULL ? &dialogues[location] : NULL;
}

static const char *npc_dialogue_line(const npc_dialogue_t *npc, int reputation) {
    if (npc == NULL) {
        return "";
    }
    switch (reputation_tier_for(reputation)) {
    case REP_TIER_LOW:
        return npc->low;
    case REP_TIER_HIGH:
        return npc->high;
    default:
        return npc->neutral;
    }
}

static const char *npc_name_or(const npc_dialogue_t *npc, const char *fallback) {
    if (npc != NULL && npc->name != NULL) {
        return npc->name;
    }
    return fallback;
}

static void on_terminal_resize(int signum) {
    (void)signum;
    ui_resize_pending = 1;
}

static bool terminal_too_small(void) {
    return LINES < MIN_TERMINAL_ROWS || COLS < MIN_TERMINAL_COLS;
}

static void apply_pending_resize(void) {
    if (!ui_resize_pending) {
        return;
    }

    ui_resize_pending = 0;
    endwin();
    refresh();
    clear();
}

static void draw_terminal_size_warning(void) {
    int row = LINES > 7 ? (LINES / 2) - 2 : 0;
    int col = COLS > 44 ? (COLS - 44) / 2 : 0;

    clear();
    mvprintw(row, col, "SPACE TRADER requires a terminal of 80x24.");
    mvprintw(row + 1, col, "Current size: %dx%d", COLS, LINES);
    mvprintw(row + 3, col, "Resize the terminal to continue.");
    mvprintw(row + 4, col, "The game redraws automatically on SIGWINCH.");
    refresh();
}

static void wait_for_terminal_space(void) {
    while (terminal_too_small()) {
        int ch;

        draw_terminal_size_warning();
        ch = getch();
        if (ch == KEY_RESIZE || ui_resize_pending) {
            ui_resize_pending = 1;
            apply_pending_resize();
        }
    }
}

static void ui_prepare_screen(void) {
    apply_pending_resize();
    wait_for_terminal_space();
}

static int ui_getch(void) {
    int ch;

    apply_pending_resize();
    ch = getch();
    if (ch == KEY_RESIZE) {
        ui_resize_pending = 1;
        apply_pending_resize();
    } else if (ui_resize_pending) {
        apply_pending_resize();
    }
    return ch;
}

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

    ui_prepare_screen();
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

        switch (ui_getch()) {
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

        switch (ui_getch()) {
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
            mvhline(4 + i, LEFT_TEXT_X, ' ', LEFT_TEXT_WIDTH);
            if (i == selected) {
                attron(A_REVERSE);
            }
            if (slot->occupied) {
                if (slot->kind == DROP_KIND_CART) {
                    mvprintw(4 + i, 2, "%-20s", "Cart");
                } else {
                    mvprintw(4 + i, 2, "%-20s x%-3d", COMMODITIES[slot->commodity].name, slot->quantity);
                }
            } else {
                mvprintw(4 + i, 2, "(empty)");
            }
            if (i == selected) {
                attroff(A_REVERSE);
            }
        }
        refresh();

        switch (ui_getch()) {
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
    const int list_row = fence ? 4 : 5;
    const int list_height = fence ? 14 : 9;
    const int list_count_row = fence ? 3 : 4;
    const npc_dialogue_t *trade_npc = npc_dialogue_for(TRADE_NPCS, game->player.location);
    const char *trade_line = npc_dialogue_line(trade_npc, game->player.reputation);

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
        if (!fence) {
            mvprintw(3, 2, "%s says:", npc_name_or(trade_npc, "Trader"));
        }
        if (option_count == 0) {
            mvprintw(4, 2, "No commodities available.");
            if (!fence) {
                draw_wrapped_text(trade_line, 15, LEFT_TEXT_X, LEFT_TEXT_WIDTH, 4, 0);
            }
            refresh();
            switch (ui_getch()) {
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

        mvprintw(list_count_row, 2, "Showing %d-%d of %d",
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
        if (!fence) {
            mvprintw(14, 2, "%s says:", npc_name_or(trade_npc, "Trader"));
            draw_wrapped_text(trade_line, 15, LEFT_TEXT_X, LEFT_TEXT_WIDTH, 4, 0);
        }
        refresh();

        switch (ui_getch()) {
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
    const int list_row = 7;
    const int list_height = 7;
    const npc_dialogue_t *store_npc = npc_dialogue_for(TRADE_NPCS, game->player.location);
    const char *store_line = npc_dialogue_line(store_npc, game->player.reputation);

    for (int i = 0; i < SHOP_ITEM_COUNT; ++i) {
        if (player_can_offer_shop_item(game->player.location, (shop_item_id_t)i)) {
            options[option_count++] = i;
        }
    }

    while (running) {
        draw_base_frame(game, "[Up/Down] [PgUp/PgDn] [Enter] buy [Esc]");
        mvprintw(2, 2, game->player.location == LOCATION_STARPORT ? "Starport Market Gear" : "Settlement Store");
        mvprintw(3, 2, "%s says:", npc_name_or(store_npc, "Storekeeper"));
        draw_wrapped_text(store_line, 4, LEFT_TEXT_X, LEFT_TEXT_WIDTH, 2, 0);
        if (option_count == 0) {
            mvprintw(7, 2, "No items available.");
            refresh();
            switch (ui_getch()) {
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

        mvprintw(6, 2, "Showing %d-%d of %d",
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

        mvprintw(14, 2, "Description:");
        if (option_count > 0) {
            draw_wrapped_text(SHOP_ITEMS[options[selected]].description, 15, LEFT_TEXT_X, LEFT_TEXT_WIDTH, 4, 0);
        } else {
            draw_wrapped_text("No items available.", 15, LEFT_TEXT_X, LEFT_TEXT_WIDTH, 4, 0);
        }
        refresh();

        switch (ui_getch()) {
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

typedef struct {
    char digits[8];
} bet_input_t;

static int cap_bet_by_credits(int max_bet, int credits) {
    if (credits <= 0) {
        return 0;
    }
    return credits < max_bet ? credits : max_bet;
}

static bool bet_input_has_digits(const bet_input_t *input) {
    return input->digits[0] != '\0';
}

static void bet_input_clear(bet_input_t *input) {
    input->digits[0] = '\0';
}

static void bet_input_append_digit(bet_input_t *input, int digit) {
    size_t length = strlen(input->digits);

    if (length + 1 >= sizeof(input->digits)) {
        return;
    }
    input->digits[length] = (char)('0' + digit);
    input->digits[length + 1] = '\0';
}

static void bet_input_backspace(bet_input_t *input) {
    size_t length = strlen(input->digits);

    if (length > 0) {
        input->digits[length - 1] = '\0';
    }
}

static void adjust_bet_from_input(int *bet, const bet_input_t *input, int min_bet, int max_allowed) {
    int parsed;

    if (!bet_input_has_digits(input)) {
        return;
    }
    parsed = atoi(input->digits);
    if (parsed <= 0) {
        return;
    }
    *bet = clamp_int(parsed, min_bet, max_allowed);
}

static void handle_bet_key(int key, int *bet, bet_input_t *input, int min_bet, int max_bet, int credits) {
    int max_allowed = cap_bet_by_credits(max_bet, credits);

    if (max_allowed < min_bet) {
        return;
    }

    switch (key) {
    case '+':
        bet_input_clear(input);
        *bet = clamp_int(*bet + 5, min_bet, max_allowed);
        break;
    case '-':
        bet_input_clear(input);
        *bet = clamp_int(*bet - 5, min_bet, max_allowed);
        break;
    case KEY_BACKSPACE:
    case 127:
    case '\b':
        if (bet_input_has_digits(input)) {
            bet_input_backspace(input);
            adjust_bet_from_input(bet, input, min_bet, max_allowed);
        }
        break;
    default:
        if (key >= '0' && key <= '9') {
            bet_input_append_digit(input, key - '0');
            adjust_bet_from_input(bet, input, min_bet, max_allowed);
        }
        break;
    }
}

static void format_blackjack_hand(const int *cards, int card_count, bool hide_hole, char *buffer, size_t buffer_size) {
    size_t used = 0;

    if (buffer_size == 0) {
        return;
    }
    buffer[0] = '\0';

    for (int i = 0; i < card_count; ++i) {
        char card_label[8];
        int written;

        if (hide_hole && i == 1) {
            snprintf(card_label, sizeof(card_label), "??");
        } else {
            gamble_format_card(cards[i], card_label, sizeof(card_label));
        }

        written = snprintf(buffer + used, buffer_size - used, "%s[%s]", i == 0 ? "" : " ", card_label);
        if (written < 0) {
            break;
        }
        if ((size_t)written >= (buffer_size - used)) {
            used = buffer_size - 1;
            break;
        }
        used += (size_t)written;
    }
}

static int settle_blackjack_round(game_t *game,
                                  int wager,
                                  const int *player_cards,
                                  int player_count,
                                  int *dealer_cards,
                                  int *dealer_count,
                                  char *result_line,
                                  size_t result_line_size) {
    int player_total = gamble_blackjack_value(player_cards, player_count, NULL);
    int dealer_total = gamble_blackjack_value(dealer_cards, *dealer_count, NULL);

    if (player_total > 21) {
        snprintf(result_line, result_line_size, "You bust at %d. You lose %d cr.", player_total, wager);
        return 0;
    }

    while (dealer_total < 17 && *dealer_count < 12) {
        dealer_cards[*dealer_count] = gamble_draw_card(game);
        (*dealer_count)++;
        dealer_total = gamble_blackjack_value(dealer_cards, *dealer_count, NULL);
    }

    if (dealer_total > 21) {
        snprintf(result_line, result_line_size, "Dealer busts at %d. You win %d cr.", dealer_total, wager);
        return wager * 2;
    }
    if (player_total > dealer_total) {
        snprintf(result_line, result_line_size, "You win %d to %d. +%d cr.", player_total, dealer_total, wager);
        return wager * 2;
    }
    if (player_total == dealer_total) {
        snprintf(result_line, result_line_size, "Push at %d. Bet returned.", player_total);
        return wager;
    }

    snprintf(result_line, result_line_size, "Dealer wins %d to %d. You lose %d cr.", dealer_total, player_total, wager);
    return 0;
}

static void blackjack_menu(game_t *game) {
    enum {
        BLACKJACK_BETTING = 0,
        BLACKJACK_PLAYING,
        BLACKJACK_RESULT
    } state = BLACKJACK_BETTING;
    const int min_bet = 5;
    const int max_bet = 500;
    int bet = min_bet;
    int wager = 0;
    int player_cards[12];
    int dealer_cards[12];
    int player_count = 0;
    int dealer_count = 0;
    bool first_action = false;
    int running = 1;
    bet_input_t bet_input = {{0}};
    char result_line[LOG_LINE_LENGTH] = {0};

    while (running) {
        int max_allowed = cap_bet_by_credits(max_bet, game->player.credits);
        bool can_play = max_allowed >= min_bet;
        bool hide_hole = state == BLACKJACK_PLAYING;
        char dealer_hand[LOG_LINE_LENGTH];
        char player_hand[LOG_LINE_LENGTH];
        int player_total = gamble_blackjack_value(player_cards, player_count, NULL);
        int dealer_total = gamble_blackjack_value(dealer_cards, dealer_count, NULL);

        draw_base_frame(game,
                        state == BLACKJACK_BETTING ? "[+/-/0-9] bet  [Enter] deal  [Esc]"
                                                   : (state == BLACKJACK_PLAYING ? "[H]it [S]tand [D]ouble [Esc]"
                                                                                 : "[Enter] next hand  [Esc]"));
        mvprintw(2, 2, "Blackjack Table");
        mvprintw(4, 2, "Rules: dealer stands on soft 17, blackjack pays 3:2.");
        mvprintw(5, 2, "Limits: 5 to 500 credits.");

        if (state == BLACKJACK_BETTING) {
            mvprintw(7, 2, "Current bet: %d cr", bet);
            if (bet_input_has_digits(&bet_input)) {
                mvprintw(8, 2, "Typed bet: %s", bet_input.digits);
            } else {
                mvhline(8, 2, ' ', LEFT_TEXT_WIDTH);
            }
            if (!can_play) {
                mvprintw(10, 2, "You need at least %d cr to sit at this table.", min_bet);
            } else {
                mvprintw(10, 2, "Press Enter to deal.");
            }
        } else {
            format_blackjack_hand(dealer_cards, dealer_count, hide_hole, dealer_hand, sizeof(dealer_hand));
            format_blackjack_hand(player_cards, player_count, false, player_hand, sizeof(player_hand));
            mvprintw(7, 2, "Dealer: %s", dealer_hand);
            if (hide_hole) {
                mvprintw(8, 2, "Dealer total: ?");
            } else {
                mvprintw(8, 2, "Dealer total: %d", dealer_total);
            }
            mvprintw(10, 2, "Player: %s", player_hand);
            mvprintw(11, 2, "Player total: %d", player_total);
            mvprintw(13, 2, "Wager: %d cr", wager);
        }

        if (result_line[0] != '\0') {
            mvaddnstr(15, 2, result_line, LEFT_TEXT_WIDTH);
        } else {
            mvhline(15, 2, ' ', LEFT_TEXT_WIDTH);
        }

        refresh();

        {
            int key = ui_getch();

            switch (key) {
        case 27:
        case 'q':
        case 'Q':
            if (state == BLACKJACK_PLAYING) {
                snprintf(result_line, sizeof(result_line), "Finish the current hand before leaving the table.");
            } else {
                running = 0;
            }
            break;
        case '\n':
        case KEY_ENTER:
            if (state == BLACKJACK_BETTING) {
                bool player_blackjack;
                bool dealer_blackjack;
                int payout = 0;

                if (!can_play) {
                    break;
                }

                wager = clamp_int(bet, min_bet, max_allowed);
                game->player.credits -= wager;
                player_count = 0;
                dealer_count = 0;
                player_cards[player_count++] = gamble_draw_card(game);
                dealer_cards[dealer_count++] = gamble_draw_card(game);
                player_cards[player_count++] = gamble_draw_card(game);
                dealer_cards[dealer_count++] = gamble_draw_card(game);
                first_action = true;
                state = BLACKJACK_PLAYING;
                result_line[0] = '\0';

                player_blackjack = gamble_blackjack_is_natural(player_cards, player_count);
                dealer_blackjack = gamble_blackjack_is_natural(dealer_cards, dealer_count);
                if (player_blackjack || dealer_blackjack) {
                    if (player_blackjack && dealer_blackjack) {
                        payout = wager;
                        snprintf(result_line, sizeof(result_line), "Both players have blackjack. Push.");
                    } else if (player_blackjack) {
                        payout = (wager * 5) / 2;
                        snprintf(result_line, sizeof(result_line), "Blackjack! You win %d cr.", (payout - wager));
                    } else {
                        snprintf(result_line, sizeof(result_line), "Dealer has blackjack. You lose %d cr.", wager);
                    }
                    game->player.credits += payout;
                    game_log(game, "%s", result_line);
                    state = BLACKJACK_RESULT;
                }
            } else if (state == BLACKJACK_RESULT) {
                int refreshed_max = cap_bet_by_credits(max_bet, game->player.credits);

                state = BLACKJACK_BETTING;
                result_line[0] = '\0';
                bet_input_clear(&bet_input);
                if (refreshed_max >= min_bet) {
                    bet = clamp_int(bet, min_bet, refreshed_max);
                }
            }
            break;
        case 'h':
        case 'H':
            if (state == BLACKJACK_PLAYING) {
                if (player_count < 12) {
                    int payout;

                    player_cards[player_count++] = gamble_draw_card(game);
                    first_action = false;
                    if (gamble_blackjack_value(player_cards, player_count, NULL) > 21) {
                        payout = settle_blackjack_round(game,
                                                        wager,
                                                        player_cards,
                                                        player_count,
                                                        dealer_cards,
                                                        &dealer_count,
                                                        result_line,
                                                        sizeof(result_line));
                        game->player.credits += payout;
                        game_log(game, "%s", result_line);
                        state = BLACKJACK_RESULT;
                    }
                }
            }
            break;
        case 's':
        case 'S':
            if (state == BLACKJACK_PLAYING) {
                int payout = settle_blackjack_round(game,
                                                    wager,
                                                    player_cards,
                                                    player_count,
                                                    dealer_cards,
                                                    &dealer_count,
                                                    result_line,
                                                    sizeof(result_line));
                game->player.credits += payout;
                game_log(game, "%s", result_line);
                state = BLACKJACK_RESULT;
            }
            break;
        case 'd':
        case 'D':
            if (state == BLACKJACK_PLAYING) {
                if (!gamble_blackjack_can_double(player_count, first_action)) {
                    snprintf(result_line, sizeof(result_line), "Double is only available as your first action.");
                } else if (game->player.credits < wager) {
                    snprintf(result_line, sizeof(result_line), "Not enough credits to double this hand.");
                } else {
                    int payout;

                    game->player.credits -= wager;
                    wager *= 2;
                    if (player_count < 12) {
                        player_cards[player_count++] = gamble_draw_card(game);
                    }
                    first_action = false;
                    payout = settle_blackjack_round(game,
                                                    wager,
                                                    player_cards,
                                                    player_count,
                                                    dealer_cards,
                                                    &dealer_count,
                                                    result_line,
                                                    sizeof(result_line));
                    game->player.credits += payout;
                    game_log(game, "%s", result_line);
                    state = BLACKJACK_RESULT;
                }
            }
            break;
        default:
            if (state == BLACKJACK_BETTING) {
                handle_bet_key(key, &bet, &bet_input, min_bet, max_bet, game->player.credits);
            }
            break;
        }
        }
    }
}

static roulette_bet_t build_roulette_bet(int selected,
                                         int straight_value,
                                         int color_value,
                                         int parity_value,
                                         int range_value,
                                         int dozen_value,
                                         int column_value) {
    roulette_bet_t bet = {ROULETTE_BET_STRAIGHT, straight_value};

    switch (selected) {
    case 0:
        bet.type = ROULETTE_BET_STRAIGHT;
        bet.value = straight_value;
        break;
    case 1:
        bet.type = ROULETTE_BET_COLOR;
        bet.value = color_value;
        break;
    case 2:
        bet.type = ROULETTE_BET_PARITY;
        bet.value = parity_value;
        break;
    case 3:
        bet.type = ROULETTE_BET_RANGE;
        bet.value = range_value;
        break;
    case 4:
        bet.type = ROULETTE_BET_DOZEN;
        bet.value = dozen_value;
        break;
    default:
        bet.type = ROULETTE_BET_COLUMN;
        bet.value = column_value;
        break;
    }

    return bet;
}

static void roulette_menu(game_t *game) {
    const int min_bet = 5;
    const int max_bet = 200;
    int running = 1;
    int selected = 0;
    int straight_value = 17;
    int color_value = ROULETTE_COLOR_RED;
    int parity_value = 0;
    int range_value = 0;
    int dozen_value = 0;
    int column_value = 0;
    int bet = min_bet;
    bet_input_t bet_input = {{0}};
    char last_result[LOG_LINE_LENGTH] = {0};

    while (running) {
        int max_allowed = cap_bet_by_credits(max_bet, game->player.credits);
        bool can_play = max_allowed >= min_bet;
        roulette_bet_t active_bet = build_roulette_bet(selected,
                                                       straight_value,
                                                       color_value,
                                                       parity_value,
                                                       range_value,
                                                       dozen_value,
                                                       column_value);
        char active_bet_text[64];

        gamble_roulette_describe_bet(&active_bet, active_bet_text, sizeof(active_bet_text));

        draw_base_frame(game, "[Up/Down] type [Left/Right] option [+/-/0-9] wager [Enter] spin [Esc]");
        mvprintw(2, 2, "Roulette Table");
        mvprintw(3, 2, "European wheel (0-36). Max wager per spin: %d cr.", max_bet);

        for (int row = 0; row < 6; ++row) {
            if (row == selected) {
                attron(A_REVERSE);
            }
            switch (row) {
            case 0:
                mvprintw(5 + row, 2, "Straight number: %-2d (35:1)", straight_value);
                break;
            case 1:
                mvprintw(5 + row, 2, "Color: %-5s (1:1)", color_value == ROULETTE_COLOR_RED ? "Red" : "Black");
                break;
            case 2:
                mvprintw(5 + row, 2, "Odd/Even: %-4s (1:1)", parity_value == 0 ? "Odd" : "Even");
                break;
            case 3:
                mvprintw(5 + row, 2, "Low/High: %-5s (1:1)", range_value == 0 ? "1-18" : "19-36");
                break;
            case 4:
                mvprintw(5 + row, 2, "Dozen: %-6s (2:1)", dozen_value == 0 ? "1st 12" : (dozen_value == 1 ? "2nd 12" : "3rd 12"));
                break;
            default:
                mvprintw(5 + row, 2, "Column: %-5s (2:1)", column_value == 0 ? "1st" : (column_value == 1 ? "2nd" : "3rd"));
                break;
            }
            if (row == selected) {
                attroff(A_REVERSE);
            }
        }

        mvprintw(12, 2, "Current bet: %s", active_bet_text);
        mvprintw(13, 2, "Wager: %d cr", bet);
        if (bet_input_has_digits(&bet_input)) {
            mvprintw(14, 2, "Typed wager: %s", bet_input.digits);
        } else {
            mvhline(14, 2, ' ', LEFT_TEXT_WIDTH);
        }

        if (!can_play) {
            mvprintw(15, 2, "You need at least %d credits to place a wager.", min_bet);
        } else {
            mvhline(15, 2, ' ', LEFT_TEXT_WIDTH);
        }

        if (last_result[0] != '\0') {
            mvaddnstr(17, 2, last_result, LEFT_TEXT_WIDTH);
        } else {
            mvhline(17, 2, ' ', LEFT_TEXT_WIDTH);
        }

        refresh();

        {
            int key = ui_getch();

            switch (key) {
        case KEY_UP:
            selected = (selected == 0) ? 5 : (selected - 1);
            break;
        case KEY_DOWN:
            selected = (selected + 1) % 6;
            break;
        case KEY_LEFT:
            switch (selected) {
            case 0:
                straight_value = (straight_value == 0) ? 36 : (straight_value - 1);
                break;
            case 1:
                color_value = color_value == ROULETTE_COLOR_RED ? ROULETTE_COLOR_BLACK : ROULETTE_COLOR_RED;
                break;
            case 2:
                parity_value ^= 1;
                break;
            case 3:
                range_value ^= 1;
                break;
            case 4:
                dozen_value = (dozen_value == 0) ? 2 : (dozen_value - 1);
                break;
            default:
                column_value = (column_value == 0) ? 2 : (column_value - 1);
                break;
            }
            break;
        case KEY_RIGHT:
            switch (selected) {
            case 0:
                straight_value = (straight_value + 1) % 37;
                break;
            case 1:
                color_value = color_value == ROULETTE_COLOR_RED ? ROULETTE_COLOR_BLACK : ROULETTE_COLOR_RED;
                break;
            case 2:
                parity_value ^= 1;
                break;
            case 3:
                range_value ^= 1;
                break;
            case 4:
                dozen_value = (dozen_value + 1) % 3;
                break;
            default:
                column_value = (column_value + 1) % 3;
                break;
            }
            break;
        case '\n':
        case KEY_ENTER:
            if (can_play) {
                static const char spinner[] = "|/-\\";
                roulette_bet_t settled_bet = build_roulette_bet(selected,
                                                                straight_value,
                                                                color_value,
                                                                parity_value,
                                                                range_value,
                                                                dozen_value,
                                                                column_value);
                char settled_bet_text[64];
                int wager = clamp_int(bet, min_bet, max_allowed);
                int spin;
                bool won;
                int payout = 0;
                int net;
                const char *color_name;

                game->player.credits -= wager;
                for (int i = 0; i < 12; ++i) {
                    mvprintw(16, 2, "Spinning wheel... %c", spinner[i % 4]);
                    refresh();
                    napms(35);
                }

                spin = gamble_roulette_spin(game);
                won = gamble_roulette_bet_wins(&settled_bet, spin);
                if (won) {
                    payout = wager * (gamble_roulette_profit_multiplier(&settled_bet) + 1);
                }
                game->player.credits += payout;
                net = payout - wager;
                color_name = spin == 0 ? "green" : (gamble_roulette_is_red(spin) ? "red" : "black");
                gamble_roulette_describe_bet(&settled_bet, settled_bet_text, sizeof(settled_bet_text));

                if (won) {
                    snprintf(last_result,
                             sizeof(last_result),
                             "Wheel: %d (%s). You win %d cr on %s.",
                             spin,
                             color_name,
                             net,
                             settled_bet_text);
                } else {
                    snprintf(last_result,
                             sizeof(last_result),
                             "Wheel: %d (%s). You lose %d cr on %s.",
                             spin,
                             color_name,
                             wager,
                             settled_bet_text);
                }
                game_log(game, "%s", last_result);
            }
            break;
        case 27:
        case 'q':
        case 'Q':
            running = 0;
            break;
        default:
            handle_bet_key(key, &bet, &bet_input, min_bet, max_bet, game->player.credits);
            break;
        }
        }
    }
}

static void bar_menu(game_t *game) {
    int running = 1;
    const npc_dialogue_t *bar_npc = npc_dialogue_for(BAR_NPCS, game->player.location);
    const char *bar_line = npc_dialogue_line(bar_npc, game->player.reputation);

    encounter_on_bar_entry(game);
    while (running) {
        draw_base_frame(game, "[R]oom [F]ence R[u]mors [B]rawl [J]ackjack R[o]ulette [Esc]");
        mvprintw(2, 2, "Bar");
        mvprintw(3, 2, "%s says:", npc_name_or(bar_npc, "Bartender"));
        mvprintw(4, 2, "Room: 20 cr for +2 HP");
        mvprintw(5, 2, "Fence: contraband and artifacts at 70%% of base price");
        mvprintw(6, 2, "Brawl: straight-up fistfight for local cash");
        mvprintw(7, 2, "Blackjack: min 5, max 500, dealer stands on soft 17");
        mvprintw(8, 2, "Roulette: European wheel, max 200 per spin");
        draw_wrapped_text(bar_line, 10, LEFT_TEXT_X, LEFT_TEXT_WIDTH, 3, 0);
        refresh();

        switch (ui_getch()) {
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
        case 'j':
        case 'J':
            blackjack_menu(game);
            break;
        case 'o':
        case 'O':
            roulette_menu(game);
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
    const npc_dialogue_t *heal_npc = npc_dialogue_for(HEAL_NPCS, game->player.location);
    const char *heal_line = npc_dialogue_line(heal_npc, game->player.reputation);

    while (running) {
        draw_base_frame(game, "[H]eal 1  [M]ax  [D]onate (clinic)  [Esc]");
        mvprintw(2, 2, hospital ? "Starport Medical" : "Settlement Clinic");
        mvprintw(3, 2, "%s says:", npc_name_or(heal_npc, hospital ? "Doctor" : "Healer"));
        snprintf(service_text, sizeof(service_text), "%s price: %d cr per HP, up to %d HP per visit",
                 hospital ? "Hospital" : "Clinic", per_hp, cap);
        draw_wrapped_text(service_text, 4, LEFT_TEXT_X, LEFT_TEXT_WIDTH, 2, 0);
        draw_wrapped_text(heal_line, 7, LEFT_TEXT_X, LEFT_TEXT_WIDTH, 4, 0);
        refresh();

        switch (ui_getch()) {
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

        switch (ui_getch()) {
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

static bool prompt_new_game_after_bad_save(const char *message) {
    int ch;

    while (1) {
        ui_prepare_screen();
        clear();
        mvprintw(3, 2, "Saved game could not be loaded.");
        draw_wrapped_text(message, 5, 2, 76, 3, 0);
        mvprintw(9, 2, "Start a new game now? [Y]es / [N]o");
        refresh();

        ch = ui_getch();
        if (ch == 'y' || ch == 'Y') {
            return true;
        }
        if (ch == 'n' || ch == 'N' || ch == 27 || ch == 'q' || ch == 'Q') {
            return false;
        }
    }
}

static void title_screen(game_t *game) {
    int running = 1;
    char error[128];

    while (running && game->running) {
        ui_prepare_screen();
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

        switch (ui_getch()) {
        case 'n':
        case 'N':
            game_init_new(game);
            load_high_scores(game);
            running = 0;
            break;
        case 'l':
        case 'L': {
            save_load_result_t load_result = load_game(game, error, sizeof(error));

            if (load_result == SAVE_LOAD_OK) {
                load_high_scores(game);
                running = 0;
            } else if (load_result == SAVE_LOAD_CORRUPT) {
                if (prompt_new_game_after_bad_save(error)) {
                    game_init_new(game);
                    load_high_scores(game);
                    running = 0;
                } else {
                    game_log(game, "%s", error);
                }
            } else {
                game_log(game, "%s", error);
            }
            break;
        }
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

static int end_score_value(const game_t *game) {
    return game->player.credits + market_estimate_cargo_value(game) + (game->turn * 10);
}

static void draw_liftoff_animation(const game_t *game) {
    static const char *frames[] = {
        "  .   *      .      *       .\n"
        "        ___________________________\n"
        "   _____|___________________________|_____\n"
        "  |                                       |\n"
        "  |  F R E E B I R D  - Reg. KR-7712      |\n"
        "  |_______________________________________|\n"
        "    {*}       {*}       {*}       {*}\n"
        "  *  }  *   *  }  *   *  }  *   *  }  *\n"
        " ====[ LAUNCH PAD 4 - PORT VEGA STARPORT ]====",
        "  .   *      .      *       .\n"
        "        ___________________________\n"
        "   _____|___________________________|_____\n"
        "  |                                       |\n"
        "  |  F R E E B I R D  - Reg. KR-7712      |\n"
        "  |_______________________________________|\n"
        "    \\\\ //     \\\\ //     \\\\ //     \\\\ //\n"
        "  *  \\*/  *  *  \\*/  *  *  \\*/  *  *  \\*/  *\n"
        " ====[ LAUNCH PAD 4 - PORT VEGA STARPORT ]====",
        "  .   *      .      *       .\n"
        "             _____________________\n"
        "            /_____________________/\\\n"
        "            \\_____________________\\/\n"
        "                 ||     ||\n"
        "              *  ||  *  ||  *\n"
        "            * *  || * * || * *\n"
        "          *   *  ||*   *||*   *\n"
        " ====[ LAUNCH PAD 4 - PORT VEGA STARPORT ]====",
        "  .   *      .      *       .\n"
        "                _____________\n"
        "               /___________/\\\n"
        "               \\___________\\/\n"
        "                   /\\\n"
        "                  /  \\\n"
        "                * /\\ *\n"
        "              *  /  \\  *\n"
        " ====[ LAUNCH PAD 4 - PORT VEGA STARPORT ]====",
        "  .     *    .     *      .     *\n"
        "     .     *    .      *    .\n"
        "  *    .    *    .    *    .    *\n"
        "     *    .    *    .    *    .\n"
        "  .     *    .     *      .     *\n"
        "\n"
        " ====[ LAUNCH PAD 4 - PORT VEGA STARPORT ]====\n"
        "              (the pad is empty)"
    };
    static const char *captions[] = {
        "FRAME 1 - Lifting off",
        "FRAME 2 - Rising",
        "FRAME 3 - Climbing",
        "FRAME 4 - Departing",
        "FRAME 5 - Gone"
    };

    for (int i = 0; i < 5; ++i) {
        ui_prepare_screen();
        clear();
        mvhline(0, 0, '=', 80);
        mvprintw(0, 2, "SPACE TRADER");
        mvprintw(0, 58, "LIFTOFF SEQUENCE");
        mvhline(20, 0, '-', 80);
        mvprintw(2, 2, "%s", captions[i]);
        draw_clipped_multiline(frames[i], 4, 2, 76, 14);
        mvprintw(21, 2, "%s", game->end_reason);
        refresh();
        napms(170);
    }
}

static void draw_end_stats_panel(const game_t *game, bool victory) {
    int cargo_value = market_estimate_cargo_value(game);
    int total_wealth = game->player.credits + cargo_value;
    int score = end_score_value(game);
    const char *title = victory ? "VICTORY" : "EPITAPH";
    const char *headline = victory ? "FREEBIRD HAS LEFT STARPORT" : "THE DUST CLAIMS ANOTHER TRADER";
    const char *farewell = victory
                               ? "Fare thee well, trader. The stars are yours now."
                               : "Another run ends in silence. The Reach remembers in scars.";

    ui_prepare_screen();
    clear();
    mvhline(0, 0, '=', 80);
    mvprintw(0, 2, "SPACE TRADER");
    mvprintw(0, 59, "%s", victory ? "LIFTOFF" : "GAME OVER");
    mvhline(20, 0, '-', 80);

    attron(A_BOLD);
    mvprintw(2, 35, "%s", title);
    attroff(A_BOLD);
    mvprintw(4, 2, "%s", headline);
    draw_wrapped_text(game->end_reason, 6, 2, 76, 2, 0);
    mvprintw(9, 4, "Turns taken   : %d", game->turn);
    mvprintw(10, 4, "Wealth (cash) : %d Cr", game->player.credits);
    mvprintw(11, 4, "Cargo value   : %d Cr", cargo_value);
    mvprintw(12, 4, "Total wealth  : %d Cr", total_wealth);
    mvprintw(14, 4, "SCORE         : %d pts", score);
    mvprintw(15, 4, "(credits + cargo value + turns_survived x 10)");
    mvprintw(17, 2, "%s", farewell);
    mvprintw(19, 2, "[ PRESS ANY KEY ]");
    refresh();
}

static void end_screen(game_t *game, const char *label) {
    bool victory = game->state == GAME_STATE_VICTORY;

    if (victory) {
        draw_liftoff_animation(game);
    }
    draw_end_stats_panel(game, victory);
    ui_getch();
    record_high_score(game, label);
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

    while (1) {
        ui_prepare_screen();
        move(LINES - 2, 0);
        clrtoeol();
        mvprintw(LINES - 2, 2, "Quit game? [Y]es / [N]o");
        refresh();
        ch = ui_getch();
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
    ch = ui_getch();
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
    signal(SIGWINCH, on_terminal_resize);
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
    ui_resize_pending = 1;
    ui_prepare_screen();

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
