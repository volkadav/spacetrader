/* ui.c: Ncurses rendering, input handling, and menu flows for all gameplay screens. */
#include "ui.h"

#include <ctype.h>
#include <ncurses.h>
#include <signal.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#include "combat.h"
#include "encounter.h"
#include "gamble.h"
#include "market.h"
#include "player.h"
#include "prospect.h"
#include "save.h"
#include "util.h"
#include "version.h"
#include "world.h"

static const int LEFT_WIDTH = 48;
static const int RIGHT_X = 50;
static const int LEFT_TEXT_X = 2;
static const int LEFT_TEXT_WIDTH = 45;
static const int MAX_WRAP_LINES = 96;
static const int HP_COLOR_PAIR_HEALTHY = 1;
static const int HP_COLOR_PAIR_WOUNDED = 2;
static const int HP_COLOR_PAIR_CRITICAL = 3;
static const int UI_BASE_COLS = 80;
static const int UI_BASE_ROWS = 24;
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
        "What'll it be? And don't lean on the bar.",
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
        "What do you want?",
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
        "Where does it hurt? How long ago? Sit down.",
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
        "What happened, and when? Give me the facts.",
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

/**
 * Purpose: Implements reputation tier for.
 * Parameters:
 *   - reputation (int): Input argument used by this routine.
 * Returns:
 *   - reputation_tier_t: Return value describing the outcome of this routine.
 */
static reputation_tier_t reputation_tier_for(int reputation) {
    if (reputation <= -2) {
        return REP_TIER_LOW;
    }
    if (reputation >= 2) {
        return REP_TIER_HIGH;
    }
    return REP_TIER_NEUTRAL;
}

/**
 * Purpose: Implements npc dialogue for.
 * Parameters:
 *   - dialogues (const npc_dialogue_t *): Input argument used by this routine.
 *   - location (location_id_t): Location identifier used to select travel or market context.
 * Returns:
 *   - const npc_dialogue_t *: Pointer result selected by this routine.
 */
static const npc_dialogue_t *npc_dialogue_for(const npc_dialogue_t *dialogues, location_id_t location) {
    if (dialogues == NULL || location < 0 || location >= MAX_LOCATIONS) {
        return NULL;
    }
    return dialogues[location].name != NULL ? &dialogues[location] : NULL;
}

/**
 * Purpose: Implements npc dialogue line.
 * Parameters:
 *   - npc (const npc_dialogue_t *): Input argument used by this routine.
 *   - reputation (int): Input argument used by this routine.
 * Returns:
 *   - const char *: Pointer result selected by this routine.
 */
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

/**
 * Purpose: Implements format npc speaker.
 * Parameters:
 *   - npc (const npc_dialogue_t *): Input argument used by this routine.
 *   - profession (const char *): Input argument used by this routine.
 *   - buffer (char *): Output buffer populated by this routine.
 *   - buffer_size (size_t): Output buffer populated by this routine.
 */
static void format_npc_speaker(const npc_dialogue_t *npc,
                               const char *profession,
                               char *buffer,
                               size_t buffer_size) {
    if (buffer == NULL || buffer_size == 0) {
        return;
    }

    if (profession == NULL || profession[0] == '\0') {
        profession = "local";
    }

    if (npc != NULL && npc->name != NULL && npc->name[0] != '\0') {
        snprintf(buffer, buffer_size, "%s the %s says:", npc->name, profession);
    } else {
        snprintf(buffer, buffer_size, "The %s says:", profession);
    }
}

/**
 * Purpose: Implements format npc quote.
 * Parameters:
 *   - line (const char *): Input argument used by this routine.
 *   - buffer (char *): Output buffer populated by this routine.
 *   - buffer_size (size_t): Output buffer populated by this routine.
 */
static void format_npc_quote(const char *line, char *buffer, size_t buffer_size) {
    if (buffer == NULL || buffer_size == 0) {
        return;
    }
    if (line == NULL || line[0] == '\0') {
        buffer[0] = '\0';
        return;
    }

    snprintf(buffer, buffer_size, "\"%s\"", line);
}

/**
 * Purpose: Implements on terminal resize.
 * Parameters:
 *   - signum (int): Input argument used by this routine.
 */
static void on_terminal_resize(int signum) {
    (void)signum;
    ui_resize_pending = 1;
}

/**
 * Purpose: Implements terminal too small.
 * Parameters:
 *   - None.
 * Returns:
 *   - bool: True when the operation succeeds or the condition is met; false otherwise.
 */
static bool terminal_too_small(void) {
    return LINES < MIN_TERMINAL_ROWS || COLS < MIN_TERMINAL_COLS;
}

/**
 * Purpose: Implements apply pending resize.
 * Parameters:
 *   - None.
 */
static void apply_pending_resize(void) {
    if (!ui_resize_pending) {
        return;
    }

    ui_resize_pending = 0;
    endwin();
    refresh();
    clear();
}

/**
 * Purpose: Implements draw terminal size warning.
 * Parameters:
 *   - None.
 */
static void draw_terminal_size_warning(void) {
    int row = LINES > 7 ? (LINES / 2) - 2 : 0;
    int col = COLS > 44 ? (COLS - 44) / 2 : 0;

    clear();
    mvwprintw(stdscr, row, col, "SPACE TRADER requires a terminal of 80x24.");
    mvwprintw(stdscr, row + 1, col, "Current size: %dx%d", COLS, LINES);
    mvwprintw(stdscr, row + 3, col, "Resize the terminal to continue.");
    mvwprintw(stdscr, row + 4, col, "The game redraws automatically on SIGWINCH.");
    refresh();
}

/**
 * Purpose: Implements wait for terminal space.
 * Parameters:
 *   - None.
 */
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

/**
 * Purpose: Implements ui prepare screen.
 * Parameters:
 *   - None.
 */
static void ui_prepare_screen(void) {
    apply_pending_resize();
    wait_for_terminal_space();
}

/**
 * Purpose: Implements ui getch.
 * Parameters:
 *   - None.
 * Returns:
 *   - int: Computed numeric result for this routine.
 */
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

/**
 * Purpose: Implements ui scale position.
 * Parameters:
 *   - value (int): Numeric input used by this routine for calculations or limits.
 *   - base (int): Input argument used by this routine.
 *   - actual (int): Input argument used by this routine.
 * Returns:
 *   - int: Computed numeric result for this routine.
 */
static int ui_scale_position(int value, int base, int actual) {
    long long scaled;

    if (actual <= 1) {
        return 0;
    }
    if (value <= 0) {
        return 0;
    }
    if (actual <= base) {
        return value < actual ? value : (actual - 1);
    }
    scaled = ((long long)value * (actual - 1) + ((base - 1) / 2)) / (base - 1);
    if (scaled < 0) {
        return 0;
    }
    if (scaled >= actual) {
        return actual - 1;
    }
    return (int)scaled;
}

/**
 * Purpose: Implements ui scale span.
 * Parameters:
 *   - value (int): Numeric input used by this routine for calculations or limits.
 *   - base (int): Input argument used by this routine.
 *   - actual (int): Input argument used by this routine.
 * Returns:
 *   - int: Computed numeric result for this routine.
 */
static int ui_scale_span(int value, int base, int actual) {
    long long scaled;

    if (value <= 0) {
        return value;
    }
    if (actual <= base) {
        return value;
    }
    scaled = ((long long)value * actual + (base - 1)) / base;
    if (scaled < 1) {
        return 1;
    }
    if (scaled > actual) {
        return actual;
    }
    return (int)scaled;
}

/**
 * Purpose: Implements ui scale x.
 * Parameters:
 *   - x (int): Input argument used by this routine.
 * Returns:
 *   - int: Computed numeric result for this routine.
 */
static int ui_scale_x(int x) {
    return ui_scale_position(x, UI_BASE_COLS, COLS);
}

/**
 * Purpose: Implements ui scale y.
 * Parameters:
 *   - y (int): Input argument used by this routine.
 * Returns:
 *   - int: Computed numeric result for this routine.
 */
static int ui_scale_y(int y) {
    return ui_scale_position(y, UI_BASE_ROWS, LINES);
}

/**
 * Purpose: Implements ui scale w.
 * Parameters:
 *   - w (int): Input argument used by this routine.
 * Returns:
 *   - int: Computed numeric result for this routine.
 */
static int ui_scale_w(int w) {
    return ui_scale_span(w, UI_BASE_COLS, COLS);
}

/**
 * Purpose: Implements ui move scaled.
 * Parameters:
 *   - y (int): Input argument used by this routine.
 *   - x (int): Input argument used by this routine.
 * Returns:
 *   - int: Computed numeric result for this routine.
 */
static int ui_move_scaled(int y, int x) {
    return wmove(stdscr, ui_scale_y(y), ui_scale_x(x));
}

/**
 * Purpose: Implements ui mvaddnstr scaled.
 * Parameters:
 *   - y (int): Input argument used by this routine.
 *   - x (int): Input argument used by this routine.
 *   - text (const char *): Text input used for display, messaging, or formatting.
 *   - n (int): Input argument used by this routine.
 * Returns:
 *   - int: Computed numeric result for this routine.
 */
static int ui_mvaddnstr_scaled(int y, int x, const char *text, int n) {
    int sx = ui_scale_x(x);
    int sy = ui_scale_y(y);
    int limit = COLS - sx;
    int draw_count = n;

    if (wmove(stdscr, sy, sx) == ERR) {
        return ERR;
    }
    if (text == NULL || limit <= 0) {
        return ERR;
    }
    if (draw_count >= 0) {
        draw_count = ui_scale_w(draw_count);
        if (draw_count > limit) {
            draw_count = limit;
        }
    } else {
        draw_count = limit;
    }
    return waddnstr(stdscr, text, draw_count);
}

/**
 * Purpose: Implements ui mvprintw scaled.
 * Parameters:
 *   - y (int): Input argument used by this routine.
 *   - x (int): Input argument used by this routine.
 *   - fmt (const char *): Text input used for display, messaging, or formatting.
 *   - ... (...): Additional variadic arguments consumed by the format logic.
 * Returns:
 *   - int: Computed numeric result for this routine.
 */
static int ui_mvprintw_scaled(int y, int x, const char *fmt, ...) {
    char buffer[1024];
    va_list args;

    va_start(args, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);
    return ui_mvaddnstr_scaled(y, x, buffer, -1);
}

/**
 * Purpose: Implements ui mvhline scaled.
 * Parameters:
 *   - y (int): Input argument used by this routine.
 *   - x (int): Input argument used by this routine.
 *   - ch (chtype): Input argument used by this routine.
 *   - n (int): Input argument used by this routine.
 * Returns:
 *   - int: Computed numeric result for this routine.
 */
static int ui_mvhline_scaled(int y, int x, chtype ch, int n) {
    int sx = ui_scale_x(x);
    int sy = ui_scale_y(y);
    int draw = ui_scale_w(n);

    if (draw < 0) {
        return ERR;
    }
    if (sx >= COLS || sy >= LINES) {
        return ERR;
    }
    if (draw > COLS - sx) {
        draw = COLS - sx;
    }
    if (wmove(stdscr, sy, sx) == ERR) {
        return ERR;
    }
    return whline(stdscr, ch, draw);
}

/**
 * Purpose: Implements ui mvaddch scaled.
 * Parameters:
 *   - y (int): Input argument used by this routine.
 *   - x (int): Input argument used by this routine.
 *   - ch (const chtype): Input argument used by this routine.
 * Returns:
 *   - int: Computed numeric result for this routine.
 */
static int ui_mvaddch_scaled(int y, int x, const chtype ch) {
    return mvwaddch(stdscr, ui_scale_y(y), ui_scale_x(x), ch);
}

/**
 * Purpose: Implements ui draw vertical separator.
 * Parameters:
 *   - x (int): Input argument used by this routine.
 *   - y_start (int): Input argument used by this routine.
 *   - y_end (int): Input argument used by this routine.
 *   - ch (chtype): Input argument used by this routine.
 */
static void ui_draw_vertical_separator(int x, int y_start, int y_end, chtype ch) {
    int sx = ui_scale_x(x);
    int sy0 = ui_scale_y(y_start);
    int sy1 = ui_scale_y(y_end);
    int len;

    if (sy1 < sy0) {
        int tmp = sy0;
        sy0 = sy1;
        sy1 = tmp;
    }
    if (sx < 0 || sx >= COLS) {
        return;
    }
    if (sy0 < 0) {
        sy0 = 0;
    }
    if (sy1 >= LINES) {
        sy1 = LINES - 1;
    }
    if (sy0 > sy1) {
        return;
    }
    len = sy1 - sy0 + 1;
    if (wmove(stdscr, sy0, sx) == ERR) {
        return;
    }
    wvline(stdscr, ch, len);
}

/**
 * Purpose: Implements ui draw horizontal separator.
 * Parameters:
 *   - y (int): Input argument used by this routine.
 *   - x_start (int): Input argument used by this routine.
 *   - x_end (int): Input argument used by this routine.
 *   - ch (chtype): Input argument used by this routine.
 */
static void ui_draw_horizontal_separator(int y, int x_start, int x_end, chtype ch) {
    int sy = ui_scale_y(y);
    int sx0 = ui_scale_x(x_start);
    int sx1 = ui_scale_x(x_end);
    int len;

    if (sx1 < sx0) {
        int tmp = sx0;
        sx0 = sx1;
        sx1 = tmp;
    }
    if (sy < 0 || sy >= LINES) {
        return;
    }
    if (sx0 < 0) {
        sx0 = 0;
    }
    if (sx1 >= COLS) {
        sx1 = COLS - 1;
    }
    if (sx0 > sx1) {
        return;
    }
    len = sx1 - sx0 + 1;
    if (wmove(stdscr, sy, sx0) == ERR) {
        return;
    }
    whline(stdscr, ch, len);
}

#ifdef mvaddnstr
#undef mvaddnstr
#endif
#ifdef mvhline
#undef mvhline
#endif
#ifdef mvaddch
#undef mvaddch
#endif
#ifdef move
#undef move
#endif

#define mvprintw(y, x, ...) ui_mvprintw_scaled((y), (x), __VA_ARGS__)
#define mvaddnstr(y, x, s, n) ui_mvaddnstr_scaled((y), (x), (s), (n))
#define mvhline(y, x, ch, n) ui_mvhline_scaled((y), (x), (ch), (n))
#define mvaddch(y, x, ch) ui_mvaddch_scaled((y), (x), (ch))
#define move(y, x) ui_move_scaled((y), (x))

/**
 * Purpose: Implements hp color pair.
 * Parameters:
 *   - game (const game_t *): Game state this routine reads and/or updates.
 * Returns:
 *   - int: Computed numeric result for this routine.
 */
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

/**
 * Purpose: Implements draw hp bar.
 * Parameters:
 *   - game (const game_t *): Game state this routine reads and/or updates.
 *   - row (int): UI layout coordinate or dimension used for rendering.
 *   - col (int): UI layout coordinate or dimension used for rendering.
 */
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

/**
 * Purpose: Implements clamp scroll offset.
 * Parameters:
 *   - scroll (int): UI layout coordinate or dimension used for rendering.
 *   - total_lines (int): Input argument used by this routine.
 *   - visible_rows (int): Input argument used by this routine.
 * Returns:
 *   - int: Computed numeric result for this routine.
 */
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

/**
 * Purpose: Implements wrap text lines.
 * Parameters:
 *   - text (const char *): Text input used for display, messaging, or formatting.
 *   - width (int): UI layout coordinate or dimension used for rendering.
 *   - char lines[][LOG_LINE_LENGTH] (char lines[][LOG_LINE_LENGTH]): Input argument used by this routine.
 *   - max_lines (int): Input argument used by this routine.
 * Returns:
 *   - int: Computed numeric result for this routine.
 */
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

/**
 * Purpose: Implements draw clipped multiline.
 * Parameters:
 *   - text (const char *): Text input used for display, messaging, or formatting.
 *   - row (int): UI layout coordinate or dimension used for rendering.
 *   - col (int): UI layout coordinate or dimension used for rendering.
 *   - width (int): UI layout coordinate or dimension used for rendering.
 *   - height (int): UI layout coordinate or dimension used for rendering.
 */
static void draw_clipped_multiline(const char *text, int row, int col, int width, int height) {
    const char *cursor = text;
    int start_row = ui_scale_y(row);
    int start_col = ui_scale_x(col);
    int clip_width = width;
    int clear_width = width;

    if (clip_width >= LOG_LINE_LENGTH) {
        clip_width = LOG_LINE_LENGTH - 1;
    }
    if (clear_width >= LOG_LINE_LENGTH) {
        clear_width = LOG_LINE_LENGTH - 1;
    }
    if (start_col >= COLS || start_row >= LINES || width <= 0 || height <= 0) {
        return;
    }
    if (clear_width > COLS - start_col) {
        clear_width = COLS - start_col;
    }
    if (clip_width > clear_width) {
        clip_width = clear_width;
    }

    for (int i = 0; i < height; ++i) {
        int draw_row = start_row + i;

        if (draw_row >= LINES) {
            break;
        }
        wmove(stdscr, draw_row, start_col);
        whline(stdscr, ' ', clear_width);
    }
    if (text == NULL) {
        return;
    }

    for (int line = 0; line < height && *cursor != '\0'; ++line) {
        int draw_row = start_row + line;
        const char *line_end = cursor;
        size_t take;

        if (draw_row >= LINES) {
            break;
        }
        while (*line_end != '\0' && *line_end != '\n') {
            line_end++;
        }

        take = (size_t)(line_end - cursor);
        if (take > (size_t)clip_width) {
            take = (size_t)clip_width;
        }

        wmove(stdscr, draw_row, start_col);
        waddnstr(stdscr, cursor, (int)take);
        cursor = (*line_end == '\n') ? (line_end + 1) : line_end;
    }
}

/**
 * Purpose: Implements draw wrapped text.
 * Parameters:
 *   - text (const char *): Text input used for display, messaging, or formatting.
 *   - row (int): UI layout coordinate or dimension used for rendering.
 *   - col (int): UI layout coordinate or dimension used for rendering.
 *   - width (int): UI layout coordinate or dimension used for rendering.
 *   - height (int): UI layout coordinate or dimension used for rendering.
 *   - scroll (int): UI layout coordinate or dimension used for rendering.
 * Returns:
 *   - int: Computed numeric result for this routine.
 */
static int draw_wrapped_text(const char *text, int row, int col, int width, int height, int scroll) {
    char lines[MAX_WRAP_LINES][LOG_LINE_LENGTH];
    int wrap_width = ui_scale_w(width);
    int line_count;
    int offset;

    if (wrap_width >= LOG_LINE_LENGTH) {
        wrap_width = LOG_LINE_LENGTH - 1;
    }
    line_count = wrap_text_lines(text, wrap_width, lines, MAX_WRAP_LINES);
    offset = clamp_scroll_offset(scroll, line_count, height);

    for (int i = 0; i < height; ++i) {
        int line_index = offset + i;

        mvhline(row + i, col, ' ', width);
        if (line_index < line_count) {
            mvaddnstr(row + i, col, lines[line_index], width);
        }
    }

    return line_count;
}

/**
 * Purpose: Implements draw inventory panel.
 * Parameters:
 *   - game (const game_t *): Game state this routine reads and/or updates.
 */
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

/**
 * Purpose: Implements draw log panel.
 * Parameters:
 *   - game (const game_t *): Game state this routine reads and/or updates.
 */
static void draw_log_panel(const game_t *game) {
    int shown = game->log.count < 3 ? game->log.count : 3;
    int col = ui_scale_x(2);
    int top_row = ui_scale_y(21);
    int available_rows = LINES - top_row;

    if (col >= COLS || top_row >= LINES || available_rows <= 0) {
        return;
    }

    for (int i = 0; i < available_rows; ++i) {
        wmove(stdscr, top_row + i, col);
        wclrtoeol(stdscr);
    }

    if (available_rows >= 4) {
        mvwprintw(stdscr, top_row, col, "Log:");
        for (int i = 0; i < shown && (top_row + 1 + i) < LINES; ++i) {
            int index = (game->log.head - shown + i + NEWS_TICKER_LINES) % NEWS_TICKER_LINES;
            mvwprintw(stdscr, top_row + 1 + i, col, "> %s", game->log.text[index]);
        }
        return;
    }

    if (shown > available_rows) {
        shown = available_rows;
    }
    for (int i = 0; i < shown; ++i) {
        int index = (game->log.head - shown + i + NEWS_TICKER_LINES) % NEWS_TICKER_LINES;
        mvwprintw(stdscr, top_row + i, col, i == 0 ? "Log: > %s" : "> %s", game->log.text[index]);
    }
}

/**
 * Purpose: Implements draw base frame.
 * Parameters:
 *   - game (const game_t *): Game state this routine reads and/or updates.
 *   - commands (const char *): Text input used for display, messaging, or formatting.
 */
static void draw_base_frame(const game_t *game, const char *commands) {
    const location_def_t *location = world_get_location(game->player.location);

    ui_prepare_screen();
    clear();
    ui_draw_horizontal_separator(0, 0, 79, '=');
    mvprintw(0, 2, "SPACE TRADER (%s)", SPACE_TRADER_VERSION);
    mvprintw(0, 30, "Turn: %d", game->turn);
    mvprintw(0, 45, "Credits: %d", game->player.credits);
    mvprintw(0, 65, "Rep: %d", game->player.reputation);
    ui_draw_horizontal_separator(20, 0, 79, '-');
    ui_draw_vertical_separator(LEFT_WIDTH, 1, 20, '|');
    mvaddch(20, LEFT_WIDTH, '+');
    draw_inventory_panel(game);
    draw_hp_bar(game, 19, 2);
    mvprintw(19, 26, "Location: %s", location->name);
    mvprintw(20, 2, "%s", commands);
    draw_log_panel(game);
    refresh();
}

/**
 * Purpose: Implements draw location view.
 * Parameters:
 *   - game (const game_t *): Game state this routine reads and/or updates.
 *   - description_scroll (int): Input argument used by this routine.
 * Returns:
 *   - int: Computed numeric result for this routine.
 */
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
    ui_draw_vertical_separator(LEFT_WIDTH, 1, 20, '|');
    ui_draw_horizontal_separator(12, 0, 79, '-');
    mvaddch(12, LEFT_WIDTH, '+');
    mvaddch(20, LEFT_WIDTH, '+');
    refresh();

    return description_lines;
}

/**
 * Purpose: Implements show notice board.
 * Parameters:
 *   - game (game_t *): Game state this routine reads and/or updates.
 */
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

/**
 * Purpose: Implements inventory menu.
 * Parameters:
 *   - game (game_t *): Game state this routine reads and/or updates.
 */
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

/**
 * Purpose: Implements ground menu.
 * Parameters:
 *   - game (game_t *): Game state this routine reads and/or updates.
 */
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

/**
 * Purpose: Implements trade menu.
 * Parameters:
 *   - game (game_t *): Game state this routine reads and/or updates.
 *   - fence (bool): Boolean flag controlling conditional behavior.
 */
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
    char trade_speaker[LOG_LINE_LENGTH];
    char trade_quote[LOG_LINE_LENGTH];

    format_npc_speaker(trade_npc, "trader", trade_speaker, sizeof(trade_speaker));
    format_npc_quote(trade_line, trade_quote, sizeof(trade_quote));

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
            if (!fence) {
                draw_wrapped_text(trade_quote, 15, LEFT_TEXT_X, LEFT_TEXT_WIDTH, 4, 0);
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
            mvaddnstr(14, 2, trade_speaker, LEFT_TEXT_WIDTH);
            draw_wrapped_text(trade_quote, 15, LEFT_TEXT_X, LEFT_TEXT_WIDTH, 4, 0);
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

/**
 * Purpose: Implements store menu.
 * Parameters:
 *   - game (game_t *): Game state this routine reads and/or updates.
 */
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
    char store_quote[LOG_LINE_LENGTH];

    format_npc_quote(store_line, store_quote, sizeof(store_quote));

    for (int i = 0; i < SHOP_ITEM_COUNT; ++i) {
        if (player_can_offer_shop_item(game->player.location, (shop_item_id_t)i)) {
            options[option_count++] = i;
        }
    }

    while (running) {
        draw_base_frame(game, "[Up/Down] [PgUp/PgDn] [Enter] buy [Esc]");
        mvprintw(2, 2, game->player.location == LOCATION_STARPORT ? "Starport Market Gear" : "Settlement Store");
        draw_wrapped_text(store_quote, 3, LEFT_TEXT_X, LEFT_TEXT_WIDTH, 2, 0);
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

/**
 * Purpose: Implements cap bet by credits.
 * Parameters:
 *   - max_bet (int): Input argument used by this routine.
 *   - credits (int): Input argument used by this routine.
 * Returns:
 *   - int: Computed numeric result for this routine.
 */
static int cap_bet_by_credits(int max_bet, int credits) {
    if (credits <= 0) {
        return 0;
    }
    return credits < max_bet ? credits : max_bet;
}

/**
 * Purpose: Implements bet input has digits.
 * Parameters:
 *   - input (const bet_input_t *): Input argument used by this routine.
 * Returns:
 *   - bool: True when the operation succeeds or the condition is met; false otherwise.
 */
static bool bet_input_has_digits(const bet_input_t *input) {
    return input->digits[0] != '\0';
}

/**
 * Purpose: Implements bet input clear.
 * Parameters:
 *   - input (bet_input_t *): Input argument used by this routine.
 */
static void bet_input_clear(bet_input_t *input) {
    input->digits[0] = '\0';
}

/**
 * Purpose: Implements bet input append digit.
 * Parameters:
 *   - input (bet_input_t *): Input argument used by this routine.
 *   - digit (int): Input argument used by this routine.
 */
static void bet_input_append_digit(bet_input_t *input, int digit) {
    size_t length = strlen(input->digits);

    if (length + 1 >= sizeof(input->digits)) {
        return;
    }
    input->digits[length] = (char)('0' + digit);
    input->digits[length + 1] = '\0';
}

/**
 * Purpose: Implements bet input backspace.
 * Parameters:
 *   - input (bet_input_t *): Input argument used by this routine.
 */
static void bet_input_backspace(bet_input_t *input) {
    size_t length = strlen(input->digits);

    if (length > 0) {
        input->digits[length - 1] = '\0';
    }
}

/**
 * Purpose: Implements adjust bet from input.
 * Parameters:
 *   - bet (int *): Input argument used by this routine.
 *   - input (const bet_input_t *): Input argument used by this routine.
 *   - min_bet (int): Input argument used by this routine.
 *   - max_allowed (int): Input argument used by this routine.
 */
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

/**
 * Purpose: Implements handle bet key.
 * Parameters:
 *   - key (int): Input argument used by this routine.
 *   - bet (int *): Input argument used by this routine.
 *   - input (bet_input_t *): Input argument used by this routine.
 *   - min_bet (int): Input argument used by this routine.
 *   - max_bet (int): Input argument used by this routine.
 *   - credits (int): Input argument used by this routine.
 */
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

/**
 * Purpose: Implements format blackjack hand.
 * Parameters:
 *   - cards (const int *): Input argument used by this routine.
 *   - card_count (int): Numeric input used by this routine for calculations or limits.
 *   - hide_hole (bool): Boolean flag controlling conditional behavior.
 *   - buffer (char *): Output buffer populated by this routine.
 *   - buffer_size (size_t): Output buffer populated by this routine.
 */
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

/**
 * Purpose: Implements settle blackjack round.
 * Parameters:
 *   - game (game_t *): Game state this routine reads and/or updates.
 *   - wager (int): Input argument used by this routine.
 *   - player_cards (const int *): Player state used for this operation.
 *   - player_count (int): Player state used for this operation.
 *   - dealer_cards (int *): Input argument used by this routine.
 *   - dealer_count (int *): Numeric input used by this routine for calculations or limits.
 *   - result_line (char *): Input argument used by this routine.
 *   - result_line_size (size_t): Capacity value, typically in bytes, for the associated buffer or container.
 * Returns:
 *   - int: Computed numeric result for this routine.
 */
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

/**
 * Purpose: Implements blackjack menu.
 * Parameters:
 *   - game (game_t *): Game state this routine reads and/or updates.
 */
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

/**
 * Purpose: Implements build roulette bet.
 * Parameters:
 *   - selected (int): Input argument used by this routine.
 *   - straight_value (int): Numeric input used by this routine for calculations or limits.
 *   - color_value (int): Numeric input used by this routine for calculations or limits.
 *   - parity_value (int): Numeric input used by this routine for calculations or limits.
 *   - range_value (int): Numeric input used by this routine for calculations or limits.
 *   - dozen_value (int): Numeric input used by this routine for calculations or limits.
 *   - column_value (int): Numeric input used by this routine for calculations or limits.
 * Returns:
 *   - roulette_bet_t: Return value describing the outcome of this routine.
 */
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

/**
 * Purpose: Implements roulette menu.
 * Parameters:
 *   - game (game_t *): Game state this routine reads and/or updates.
 */
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

/**
 * Purpose: Implements bar menu.
 * Parameters:
 *   - game (game_t *): Game state this routine reads and/or updates.
 */
static void bar_menu(game_t *game) {
    int running = 1;
    const npc_dialogue_t *bar_npc = npc_dialogue_for(BAR_NPCS, game->player.location);
    const char *bar_line = npc_dialogue_line(bar_npc, game->player.reputation);
    char bar_speaker[LOG_LINE_LENGTH];
    char bar_quote[LOG_LINE_LENGTH];
    static const char *bar_options =
        "Room [R]: 20 cr for +2 HP\n"
        "Fence [F]: contraband and artifacts at 70% of base price\n"
        "Rumors [U]: buy a local rumor for 10 credits\n"
        "Brawl [B]: straight-up fistfight for local cash\n"
        "Blackjack [J]: min 5, max 500, dealer stands on soft 17\n"
        "Roulette [O]: European wheel, max 200 per spin";

    format_npc_speaker(bar_npc, "bartender", bar_speaker, sizeof(bar_speaker));
    format_npc_quote(bar_line, bar_quote, sizeof(bar_quote));
    encounter_on_bar_entry(game);
    while (running) {
        draw_base_frame(game, "[R]oom [F]ence R[u]mors [B]rawl Black[j]ack R[o]ulette [Esc]");
        mvprintw(2, 2, "Bar");
        mvprintw(3, 2, "Services:");
        draw_wrapped_text(bar_options, 4, LEFT_TEXT_X, LEFT_TEXT_WIDTH, 9, 0);
        mvaddnstr(14, 2, bar_speaker, LEFT_TEXT_WIDTH);
        draw_wrapped_text(bar_quote, 15, LEFT_TEXT_X, LEFT_TEXT_WIDTH, 4, 0);
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

/**
 * Purpose: Implements heal menu.
 * Parameters:
 *   - game (game_t *): Game state this routine reads and/or updates.
 *   - hospital (bool): Boolean flag controlling conditional behavior.
 */
static void heal_menu(game_t *game, bool hospital) {
    int running = 1;
    int per_hp = hospital ? 50 : (game->player.reputation >= 2 ? 20 : 25);
    int cap = hospital ? 8 : 3;
    char service_text[LOG_LINE_LENGTH];
    char heal_speaker[LOG_LINE_LENGTH];
    char heal_quote[LOG_LINE_LENGTH];
    const npc_dialogue_t *heal_npc = npc_dialogue_for(HEAL_NPCS, game->player.location);
    const char *heal_line = npc_dialogue_line(heal_npc, game->player.reputation);

    format_npc_speaker(heal_npc, hospital ? "doctor" : "healer", heal_speaker, sizeof(heal_speaker));

    while (running) {
        draw_base_frame(game, "[H]eal 1  [M]ax  [D]onate (clinic)  [Esc]");
        mvprintw(2, 2, hospital ? "Starport Medical" : "Settlement Clinic");
        mvaddnstr(3, 2, heal_speaker, LEFT_TEXT_WIDTH);
        format_npc_quote(heal_line, heal_quote, sizeof(heal_quote));
        draw_wrapped_text(heal_quote, 4, LEFT_TEXT_X, LEFT_TEXT_WIDTH, 3, 0);
        snprintf(service_text, sizeof(service_text), "%s price: %d cr per HP, up to %d HP per visit",
                 hospital ? "Hospital" : "Clinic", per_hp, cap);
        draw_wrapped_text(service_text, 8, LEFT_TEXT_X, LEFT_TEXT_WIDTH, 2, 0);
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

/**
 * Purpose: Implements travel menu.
 * Parameters:
 *   - game (game_t *): Game state this routine reads and/or updates.
 */
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

/**
 * Purpose: Implements prompt new game after bad save.
 * Parameters:
 *   - message (const char *): Input argument used by this routine.
 * Returns:
 *   - bool: True when the operation succeeds or the condition is met; false otherwise.
 */
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

/**
 * Purpose: Implements title screen.
 * Parameters:
 *   - game (game_t *): Game state this routine reads and/or updates.
 */
static void title_screen(game_t *game) {
    int running = 1;
    char error[128];
    const char *title = "SPACE TRADER";
    const int title_col = 24;

    while (running && game->running) {
        char version_line[32];
        int version_col;

        ui_prepare_screen();
        load_high_scores(game);
        clear();
        snprintf(version_line, sizeof(version_line), "(%s)", SPACE_TRADER_VERSION);
        version_col = title_col + (((int)strlen(title) - (int)strlen(version_line)) / 2);
        if (version_col < 0) {
            version_col = 0;
        }
        mvprintw(2, title_col, "%s", title);
        mvprintw(3, version_col, "%s", version_line);
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

/**
 * Purpose: Implements end score value.
 * Parameters:
 *   - game (const game_t *): Game state this routine reads and/or updates.
 * Returns:
 *   - int: Computed numeric result for this routine.
 */
static int end_score_value(const game_t *game) {
    return game->player.credits + market_estimate_cargo_value(game) + (game->turn * 10);
}

/**
 * Purpose: Implements draw liftoff animation.
 * Parameters:
 *   - game (const game_t *): Game state this routine reads and/or updates.
 */
static void draw_liftoff_animation(const game_t *game) {
    const int frame_delay_ms = 1000;
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
        mvprintw(0, 2, "SPACE TRADER (%s)", SPACE_TRADER_VERSION);
        mvprintw(0, 58, "LIFTOFF SEQUENCE");
        mvhline(20, 0, '-', 80);
        mvprintw(2, 2, "%s", captions[i]);
        draw_clipped_multiline(frames[i], 4, 2, 76, 14);
        mvprintw(21, 2, "%s", game->end_reason);
        refresh();
        napms(frame_delay_ms);
    }
}

/**
 * Purpose: Implements draw end stats panel.
 * Parameters:
 *   - game (const game_t *): Game state this routine reads and/or updates.
 *   - victory (bool): Boolean flag controlling conditional behavior.
 */
static void draw_end_stats_panel(const game_t *game, bool victory) {
    int cargo_value = market_estimate_cargo_value(game);
    int total_wealth = game->player.credits + cargo_value;
    int score = end_score_value(game);
    const char *title = victory ? "VICTORY" : "EPITAPH";
    const char *headline = victory ? "FREEBIRD HAS LEFT STARPORT" : "THE DUST CLAIMS ANOTHER TRADER";
    const char *farewell = victory
                               ? "Fare thee well, trader. The stars are yours now!"
                               : "Another run ends in silence. The Reach remembers in scars.";

    ui_prepare_screen();
    clear();
    mvhline(0, 0, '=', 80);
    mvprintw(0, 2, "SPACE TRADER (%s)", SPACE_TRADER_VERSION);
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

/**
 * Purpose: Implements end screen.
 * Parameters:
 *   - game (game_t *): Game state this routine reads and/or updates.
 *   - label (const char *): Input argument used by this routine.
 */
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

/**
 * Purpose: Implements save current game.
 * Parameters:
 *   - game (game_t *): Game state this routine reads and/or updates.
 */
static void save_current_game(game_t *game) {
    char error[128];

    if (save_game(game, error, sizeof(error))) {
        game_log(game, "Game saved.");
    } else {
        game_log(game, "%s", error);
    }
}

/**
 * Purpose: Implements confirm exit game.
 * Parameters:
 *   - None.
 * Returns:
 *   - bool: True when the operation succeeds or the condition is met; false otherwise.
 */
static bool confirm_exit_game(void) {
    int ch;

    while (1) {
        ui_prepare_screen();
        wmove(stdscr, LINES - 2, 0);
        clrtoeol();
        mvwprintw(stdscr, LINES - 2, 2, "Quit game? [Y]es / [N]o");
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

/**
 * Purpose: Implements location loop.
 * Parameters:
 *   - game (game_t *): Game state this routine reads and/or updates.
 */
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

/**
 * Purpose: Implements ui run.
 * Parameters:
 *   - game (game_t *): Game state this routine reads and/or updates.
 */
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
