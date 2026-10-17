/*
 * Battleship ncurses client. Keyboard only. Two-player (one enemy grid) and
 * three-player (two enemy grids, pick your target) games.
 */
#include "common.h"
#include "i18n.h"

#include <errno.h>
#include <ncurses.h>
#include <poll.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include <wchar.h>
#include <wctype.h>

int server_main(int argc, char **argv);
int bot_main(const char *host, int port, const char *name);

enum Phase { P_CONNECTING, P_LOBBY, P_PLACING, P_WAIT_OPP, P_PLAYING, P_GAMEOVER, P_DEAD, P_RECONNECT };

enum { C_WATER = 1, C_SHIP, C_HIT, C_MISS, C_CURSOR, C_BAD, C_TITLE, C_STATUS, C_GOLD, C_WAITCUR, C_ST_MINE, C_ST_WAIT };

#define LOG_LINES 3
#define LB_SHOW   10
#define MAX_EN    2

typedef struct { char name[NAME_MAX + 1]; int wins, losses, hits, shots; } LBEntry;
typedef struct { char name[NAME_MAX + 1]; int hits, shots; } SBEntry;
typedef struct {
    int   seat;
    char  name[NAME_MAX + 1];
    Board b;                 /* only shot[] is known */
    int   sunk[NSHIPS];
    int   alive;
    int   away;              /* lost connection, server is waiting */
} Enemy;

static int    sock = -1;
static LineBuf lb;
static int    phase = P_CONNECTING;
static char   client_id[CLIENT_ID_MAX + 1];
static char   my_name[NAME_MAX + 1];
static int    my_seat = 0, nseats = 0;
static Enemy  en[MAX_EN];
static int    nen = 0;           /* 1 = two-player game, 2 = three-player game */
static int    target = 0;        /* which enemy the crosshair is on */
static int    me_alive = 1;
static int    turn_seat = 0;     /* whose turn it is (seat number) */
static Board  mine;
static int    my_turn = 0;
static int    won = 0;
static char   winner_name[NAME_MAX + 1];
static int    rematch_pending = 0;
static int    cur_r = 0, cur_c = 0;
static int    place_idx = 0, place_horiz = 1;
static char   status[160] = "";
static char   logs[LOG_LINES][128];
static int    quit = 0;
static int    dirty = 1;
static int    opt_sound = 0, opt_flash = 0;   /* --sound / --flash turn cues */
static int    show_logo = 0;                   /* --logo: SCOUT banner under the leaderboard */
static int    cell_opt = 0;                    /* --cell override, 0 = auto */
static int    cell_h = 2, cell_w = 4;

/* reconnect state */
static const char *srv_host = "127.0.0.1";
static int    srv_port = DEFAULT_PORT;
static time_t next_reconnect = 0;
static FILE  *debug_fp = NULL;                 /* BATTLESHIP_DEBUG=file: trace of server messages */

/* name entry */
static int    editing_name = 0;
static int    name_for_next = 0;   /* editor opened between rounds: start placing when done */
static char   name_buf[NAME_MAX + 1];

/* leaderboards as sent by the server */
static LBEntry board[LB_SHOW];
static int    nboard = 0;
static SBEntry sboard[LB_SHOW];
static int    nsboard = 0;
static int    g_hits = 0, g_shots = 0;      /* my current game */

static void place_status(void);
static void turn_status(void);
static void turn_cue(void);
static void start_placing(void);

static void logmsg(const char *fmt, ...)
{
    memmove(logs[0], logs[1], sizeof logs[0] * (LOG_LINES - 1));
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(logs[LOG_LINES - 1], sizeof logs[0], fmt, ap);
    va_end(ap);
    dirty = 1;
}

static void set_status(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(status, sizeof status, fmt, ap);
    va_end(ap);
    dirty = 1;
}

static void connection_lost(void)
{
    if (sock >= 0) close(sock);
    sock = -1;
    if (phase == P_DEAD) return;      /* told to stop: no reconnect loop */
    phase = P_RECONNECT;
    next_reconnect = time(NULL) + 1;
    set_status("%s", tr(S_RECONNECTING));
}

static void sendf(const char *fmt, ...)
{
    char msg[LINE_MAX_LEN];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof msg, fmt, ap);
    va_end(ap);
    if (sock >= 0 && net_sendf(sock, "%s", msg) < 0) connection_lost();
}

static const char *shown_name(void) { return my_name[0] ? my_name : client_id; }

static int enemy_idx(int seat)
{
    for (int i = 0; i < nen; i++) if (en[i].seat == seat) return i;
    return -1;
}

static const char *seat_name(int seat)
{
    if (seat == my_seat) return tr(S_YOU);
    int i = enemy_idx(seat);
    return i >= 0 ? en[i].name : "?";
}

static void set_enemy(int i, int seat, const char *name)
{
    en[i].seat = seat;
    snprintf(en[i].name, sizeof en[i].name, "%s", name);
    board_init(&en[i].b);
    memset(en[i].sunk, 0, sizeof en[i].sunk);
    en[i].alive = 1;
    en[i].away = 0;
}

/* ---- Drawing ----------------------------------------------------------- */

#define G_WATER  "\xe2\x89\x88"     /* ≈ */
#define G_SHIP   "\xe2\x96\x88"     /* █ */
#define G_HIT    "\xe2\x95\xb3"     /* ╳ */
#define G_MISS   "\xe2\x97\x8b"     /* ○ */
#define G_GHOST  "\xe2\x96\x93"     /* ▓ */

#define GRID_X   3                  /* room for the row letter */

static int grid_height(void) { return 2 + BOARD_N * cell_h; }
static int grid_width(void)  { return BOARD_N * cell_w + 6; }

static void draw_cell(int y, int x, const char *glyph, int col, int attr)
{
    attron(COLOR_PAIR(col) | attr);
    for (int i = 0; i < cell_h; i++) {
        move(y + i, x);
        for (int k = 0; k < cell_w; k++) addstr(glyph);
    }
    attroff(COLOR_PAIR(col) | attr);
}

/* own grid: ei < 0; enemy grid: ei = index into en[] */
static void draw_grid(int y0, int x0, int ei)
{
    int own = ei < 0;
    int is_target = !own && ei == target && nen > 1;
    const Board *b = own ? &mine : &en[ei].b;
    char title[96];
    if (own) snprintf(title, sizeof title, "%s", tr(S_YOUR_FLEET));
    else if (nen > 1) snprintf(title, sizeof title, "%s%s", en[ei].name, en[ei].alive ? "" : tr(S_OUT_TAG));
    else snprintf(title, sizeof title, "%s", tr(S_ENEMY_WATERS));
    attron(A_BOLD | (is_target && phase == P_PLAYING ? A_REVERSE : 0));
    mvprintw(y0, x0, " %s ", title);
    attroff(A_BOLD | A_REVERSE);
    int gy = y0 + 2, gx = x0;
    for (int c = 0; c < BOARD_N; c++)
        mvprintw(y0 + 1, gx + c * cell_w + (cell_w - 1) / 2 - (c == 9 && cell_w > 2), "%d", c + 1);

    for (int r = 0; r < BOARD_N; r++) {
        mvaddch(gy + r * cell_h + (cell_h - 1) / 2, gx - 2, (chtype)('A' + r));
        for (int c = 0; c < BOARD_N; c++) {
            const char *g = G_WATER;
            int col = C_WATER, attr = 0;
            int shot = b->shot[r][c];
            int ship = own ? b->ship[r][c] : 0;
            if (shot == 2)      { g = G_HIT;  col = C_HIT;  attr = A_BOLD; }
            else if (shot == 1) { g = G_MISS; col = C_MISS; attr = 0; }
            else if (ship)      { g = G_SHIP; col = C_SHIP; attr = 0; }
            if (own && phase == P_PLACING) {
                int len = SHIP_LEN[place_idx];
                int in = place_horiz ? (r == cur_r && c >= cur_c && c < cur_c + len)
                                     : (c == cur_c && r >= cur_r && r < cur_r + len);
                if (in) {
                    g = G_GHOST;
                    col = board_can_place(&mine, place_idx, cur_r, cur_c, place_horiz) ? C_CURSOR : C_BAD;
                    attr = A_BOLD;
                }
            }
            if (!own && ei == target && (phase == P_PLAYING || phase == P_WAIT_OPP) && r == cur_r && c == cur_c) {
                int mine_to_fire = phase == P_PLAYING && my_turn && me_alive && en[ei].alive;
                col  = mine_to_fire ? C_CURSOR : C_WAITCUR;
                attr = A_BOLD;
                if (shot == 0) g = G_GHOST;   /* a solid block so the marker is obvious on plain water */
            }
            draw_cell(gy + r * cell_h, gx + c * cell_w, g, col, attr);
        }
    }
}

static void draw_fleet_summary(int y, int x, int ei)
{
    mvprintw(y, x, "%s", tr(S_SHIPS));
    int px = x + 6;
    for (int i = 0; i < NSHIPS; i++) {
        int dead = ei < 0 ? board_ship_sunk(&mine, i) : en[ei].sunk[i];
        attron(COLOR_PAIR(dead ? C_HIT : C_SHIP) | (dead ? A_BOLD : 0));
        move(y, px);
        for (int k = 0; k < SHIP_LEN[i]; k++) addstr(dead ? G_HIT : G_SHIP);
        attroff(COLOR_PAIR(dead ? C_HIT : C_SHIP) | (dead ? A_BOLD : 0));
        px += SHIP_LEN[i] + 1;
    }
}

static void draw_leaderboard(int y, int x)
{
    attron(COLOR_PAIR(C_GOLD) | A_BOLD);
    mvprintw(y, x, "%s", tr(S_LB_TITLE));
    attroff(COLOR_PAIR(C_GOLD) | A_BOLD);
    if (nboard == 0) { mvprintw(y + 1, x, "%s", tr(S_LB_EMPTY)); return; }
    for (int i = 0; i < nboard; i++) {
        char name[NAME_MAX * 2];
        pad_utf8(name, sizeof name, board[i].name, 16);
        int me = my_name[0] && utf8_casecmp(board[i].name, my_name) == 0;
        attron(me ? A_BOLD : 0);
        mvprintw(y + 1 + i, x, tr(S_LB_ROW), i + 1, name, board[i].wins, board[i].losses,
                 board[i].shots ? board[i].hits * 100 / board[i].shots : 0);
        attroff(me ? A_BOLD : 0);
    }
}

static void draw_session_board(int y, int x)
{
    attron(COLOR_PAIR(C_GOLD) | A_BOLD);
    mvprintw(y, x, "%s", tr(S_SB_TITLE));
    attroff(COLOR_PAIR(C_GOLD) | A_BOLD);
    if (nsboard == 0) { mvprintw(y + 1, x, "%s", tr(S_LB_EMPTY)); return; }
    for (int i = 0; i < nsboard; i++) {
        char name[NAME_MAX * 2];
        pad_utf8(name, sizeof name, sboard[i].name, 16);
        int me = my_name[0] && utf8_casecmp(sboard[i].name, my_name) == 0;
        attron(me ? A_BOLD : 0);
        mvprintw(y + 1 + i, x, tr(S_SB_ROW), i + 1, name, sboard[i].shots ? sboard[i].hits * 100 / sboard[i].shots : 0, sboard[i].hits, sboard[i].shots);
        attroff(me ? A_BOLD : 0);
    }
}

/* Big "SCOUT" banner, 5x5 pixel letters, shown under the leaderboard with --logo. */
static const char *const BANNER[5] = {
    "##### ##### ##### #   # #####",
    "#     #     #   # #   #   #  ",
    "##### #     #   # #   #   #  ",
    "    # #     #   # #   #   #  ",
    "##### ##### ##### #####   #  ",
};
#define BANNER_W 29

static void draw_banner(int y, int x, int pw)
{
    attron(COLOR_PAIR(C_GOLD) | A_BOLD);
    for (int i = 0; i < 5; i++) {
        move(y + i, x);
        for (const char *p = BANNER[i]; *p; p++)
            for (int k = 0; k < pw; k++) addstr(*p == '#' ? G_SHIP : " ");
    }
    attroff(COLOR_PAIR(C_GOLD) | A_BOLD);
}

/* leaderboard, tonight's hit ratio, and the SCOUT banner underneath when there is room */
static void draw_side_panel(int y, int x, int rows, int cols)
{
    draw_leaderboard(y, x);
    int by = y + 1 + LB_SHOW + 2;
    if (by + 1 + LB_SHOW < rows - 1) { draw_session_board(by, x); by += 1 + LB_SHOW + 2; }
    if (!show_logo) return;
    if (by + 5 >= rows - 1) return;
    if (cols >= x + BANNER_W * 2) draw_banner(by, x, 2);
    else if (cols >= x + BANNER_W) draw_banner(by, x, 1);
}

/* rows needed below the title for a given cell height */
static int layout_rows(int ch)
{
    return 2 + 2 * (2 + BOARD_N * ch + 2) + 2 + LOG_LINES + 1;
}

static int layout_cols(int ch)
{
    int cw = 2 * ch;
    return GRID_X + (nen > 1 ? 2 : 1) * (BOARD_N * cw + 6);
}

static void draw_name_screen(int rows, int cols)
{
    int w = 44, h = 9;
    int y0 = rows / 2 - h / 2, x0 = cols / 2 - w / 2;
    if (y0 < 1) y0 = 1;
    if (x0 < 0) x0 = 0;
    attron(COLOR_PAIR(C_GOLD) | A_BOLD);
    mvprintw(y0, x0 + 2, "%s", tr(name_for_next ? S_NAME_NEXT_PROMPT : S_NAME_PROMPT));
    attroff(COLOR_PAIR(C_GOLD) | A_BOLD);
    attron(A_REVERSE);
    mvhline(y0 + 2, x0 + 2, ' ', w - 4);
    mvprintw(y0 + 2, x0 + 3, "%s", name_buf);
    attroff(A_REVERSE);
    attron(COLOR_PAIR(C_CURSOR) | A_BOLD | A_REVERSE);
    mvaddstr(y0 + 2, x0 + 3 + utf8_width(name_buf), " ");
    attroff(COLOR_PAIR(C_CURSOR) | A_BOLD | A_REVERSE);
    mvprintw(y0 + 4, x0 + 2, "%s", tr(name_for_next ? S_NAME_NEXT_HINT : S_NAME_HINT));
    if (my_name[0] && !name_for_next) mvprintw(y0 + 5, x0 + 2, "%s", tr(S_NAME_ESC));
    attron(A_DIM);
    mvprintw(y0 + 7, x0 + 2, tr(S_NAME_CLIENT), client_id, my_seat);
    attroff(A_DIM);
    if (cols >= x0 + w + 32) draw_side_panel(y0, x0 + w + 4, rows, cols);
}

static void draw(void)
{
    erase();
    int rows, cols;
    getmaxyx(stdscr, rows, cols);

    attron(COLOR_PAIR(C_TITLE) | A_BOLD);
    mvprintw(0, 0, " BATTLESHIP ");
    attroff(COLOR_PAIR(C_TITLE) | A_BOLD);
    int tx = 14;
    if (my_seat) { mvprintw(0, tx, tr(S_SEAT_ME), my_seat, shown_name()); tx += 10 + utf8_width(shown_name()) + 4; }
    for (int i = 0; i < nen; i++) {
        mvprintw(0, tx, i == 0 ? tr(S_SEAT_OPP) : tr(S_SEAT_OPP2), en[i].seat, en[i].name);
        tx += 12 + utf8_width(en[i].name) + 3;
    }
    attron(A_DIM);
    mvprintw(0, cols - (int)strlen(BUILD_VERSION) - 2, "v%s", BUILD_VERSION);
    attroff(A_DIM);

    if (editing_name) {
        draw_name_screen(rows, cols);
        attron(COLOR_PAIR(C_STATUS) | A_BOLD);
        mvprintw(rows - 2, 1, "%.*s", cols - 2, status);
        attroff(COLOR_PAIR(C_STATUS) | A_BOLD);
        refresh();
        dirty = 0;
        return;
    }

    cell_h = cell_opt ? cell_opt : (rows >= layout_rows(2) && cols >= layout_cols(2) ? 2 : 1);
    cell_w = 2 * cell_h;
    int need_cols = layout_cols(cell_h) + 2;
    if (rows < layout_rows(cell_h) || cols < need_cols) {
        mvprintw(2, 0, tr(S_TOO_SMALL), need_cols, layout_rows(cell_h), cols, rows);
        refresh();
        dirty = 0;
        return;
    }

    int y = 2;
    int x2 = GRID_X + grid_width();          /* second column */
    if (nen > 1) {
        draw_grid(y, GRID_X, 0);
        draw_grid(y, x2, 1);
        y += grid_height();
        draw_fleet_summary(y, GRID_X, 0);
        draw_fleet_summary(y, x2, 1);
    } else {
        draw_grid(y, GRID_X, 0);
        y += grid_height();
        draw_fleet_summary(y, GRID_X, 0);
    }
    if (phase == P_PLAYING || phase == P_GAMEOVER)
        mvprintw(y, (nen > 1 ? x2 : GRID_X) + 31, tr(S_STAT), g_hits, g_shots, g_shots ? g_hits * 100 / g_shots : 0);
    y += 2;
    int own_y = y;
    draw_grid(y, GRID_X, -1);
    y += grid_height();
    draw_fleet_summary(y, GRID_X, -1);
    y += 2;

    /* leaderboard beside the boards when there is room */
    int lbx = x2;
    int lby = nen > 1 ? own_y : 2;
    if (cols >= lbx + 32) draw_side_panel(lby, lbx, rows, cols);

    mvhline(y - 1, 0, ACS_HLINE, cols);
    int stcol = C_STATUS;
    if (phase == P_PLAYING) stcol = my_turn && me_alive ? C_ST_MINE : C_ST_WAIT;
    attron(COLOR_PAIR(stcol) | A_BOLD);
    if (stcol != C_STATUS) mvhline(y, 0, ' ', cols);
    mvprintw(y, 1, "%.*s", cols - 2, status);
    attroff(COLOR_PAIR(stcol) | A_BOLD);
    for (int i = 0; i < LOG_LINES; i++) mvprintw(y + 1 + i, 1, "%.*s", cols - 2, logs[i]);

    const char *keys = tr(S_KEYS_DEFAULT);
    switch (phase) {
    case P_LOBBY:    keys = tr(S_KEYS_LOBBY); break;
    case P_PLACING:  keys = tr(S_KEYS_PLACING); break;
    case P_PLAYING:  keys = tr(nen > 1 ? S_KEYS_PLAYING3 : S_KEYS_PLAYING); break;
    case P_GAMEOVER: if (rematch_pending) keys = tr(S_KEYS_GAMEOVER); break;
    default: break;
    }
    mvprintw(rows - 1, 1, "%.*s", cols - 2, keys);
    refresh();
    dirty = 0;
}

/* ---- Server messages --------------------------------------------------- */

static void reset_boards(void)
{
    g_hits = g_shots = 0;
    board_init(&mine);
    for (int i = 0; i < nen; i++) {
        board_init(&en[i].b);
        memset(en[i].sunk, 0, sizeof en[i].sunk);
        en[i].alive = 1;
    }
    me_alive = 1;
    target = 0;
    place_idx = 0;
    place_horiz = 1;
    cur_r = cur_c = 0;
}

static void submit_fleet(void)
{
    char f[128];
    fleet_format(&mine, f, sizeof f);
    sendf("FLEET %s", f);
}

/* "Anna|Olle" -> two names */
static void split_names(char *s, char **a, char **b)
{
    *a = s;
    char *bar = strchr(s, '|');
    if (bar) { *bar = 0; *b = bar + 1; } else *b = (char *)"?";
}

static void match_intro(void)
{
    if (phase == P_GAMEOVER) {
        /* same players, next game: keep the final board on screen until the player is ready */
        rematch_pending = 1;
        set_status(tr(S_NEXT_GAME), tr(won ? S_YOU_WON : S_YOU_LOST), en[0].name);
    } else {
        start_placing();
    }
}

static void pick_alive_target(void)
{
    if (nen < 2) { target = 0; return; }
    if (!en[target].alive) target = en[1 - target].alive ? 1 - target : target;
}

static void handle_line(char *line)
{
    char cmd[32];
    int n = 0;
    if (debug_fp) { fprintf(debug_fp, "<- %s\n", line); fflush(debug_fp); }
    if (sscanf(line, "%31s%n", cmd, &n) != 1) return;
    char *arg = line + n;
    while (*arg == ' ') arg++;

    if (strcmp(cmd, "WELCOME") == 0) {
        sscanf(arg, "%d %d", &my_seat, &nseats);
        logmsg(tr(S_CONNECTED), my_seat, nseats);
        if (my_name[0]) sendf("NAME %s", my_name);
    } else if (strcmp(cmd, "NAMEOK") == 0) {
        snprintf(my_name, sizeof my_name, "%s", arg);
        logmsg(tr(S_NAME_SET), my_name);
        if (editing_name && !name_for_next && phase == P_CONNECTING) editing_name = 0;  /* name restored by the server */
    } else if (strcmp(cmd, "OPPNAME") == 0) {
        int seat, k = 0;
        if (sscanf(arg, "%d %n", &seat, &k) == 1) {
            int i = enemy_idx(seat);
            if (i >= 0) {
                logmsg(tr(S_OPP_RENAMED), en[i].name, arg + k);
                snprintf(en[i].name, sizeof en[i].name, "%s", arg + k);
            }
        }
    } else if (strcmp(cmd, "LBCLEAR") == 0) {
        nboard = 0;
    } else if (strcmp(cmd, "LB") == 0) {
        int w, l, h, sh, k = 0;
        if (nboard < LB_SHOW && sscanf(arg, "%d %d %d %d %n", &w, &l, &h, &sh, &k) == 4) {
            board[nboard].wins = w;
            board[nboard].losses = l;
            board[nboard].hits = h;
            board[nboard].shots = sh;
            snprintf(board[nboard].name, sizeof board[nboard].name, "%s", arg + k);
            nboard++;
        }
    } else if (strcmp(cmd, "SBCLEAR") == 0) {
        nsboard = 0;
    } else if (strcmp(cmd, "SB") == 0) {
        int h, sh, k = 0;
        if (nsboard < LB_SHOW && sscanf(arg, "%d %d %n", &h, &sh, &k) == 2) {
            sboard[nsboard].hits = h;
            sboard[nsboard].shots = sh;
            snprintf(sboard[nsboard].name, sizeof sboard[nsboard].name, "%s", arg + k);
            nsboard++;
        }
    } else if (strcmp(cmd, "STAT") == 0) {
        sscanf(arg, "%d %d", &g_hits, &g_shots);
    } else if (strcmp(cmd, "LOBBY") == 0) {
        phase = P_LOBBY;
        rematch_pending = 0;
        nen = 0;
        set_status("%s", tr(S_LOBBY_WAIT));
    } else if (strcmp(cmd, "MATCH") == 0) {
        int seat, k = 0;
        if (sscanf(arg, "%d %n", &seat, &k) >= 1) {
            nen = 1;
            set_enemy(0, seat, arg + k);
            match_intro();
        }
    } else if (strcmp(cmd, "MATCH3") == 0) {
        int s1, s2, k = 0;
        if (sscanf(arg, "%d %d %n", &s1, &s2, &k) == 2) {
            char *a, *b;
            split_names(arg + k, &a, &b);
            nen = 2;
            set_enemy(0, s1, a);
            set_enemy(1, s2, b);
            match_intro();
        }
    } else if (strcmp(cmd, "HOLD") == 0) {
        /* game over while the admin has paused new rounds: stay on the result until MATCH arrives */
        set_status("%s", tr(S_HOLD));
    } else if (strcmp(cmd, "RESTART") == 0) {
        /* admin restarted the round: the MATCH that follows starts a fresh placement */
        phase = P_LOBBY;
        rematch_pending = 0;
        logmsg("%s", tr(S_RESTARTED));
    } else if (strcmp(cmd, "FLEETOK") == 0) {
        phase = P_WAIT_OPP;
        set_status("%s", tr(S_FLEET_ACCEPTED));
    } else if (strcmp(cmd, "START") == 0 || strcmp(cmd, "TURN") == 0) {
        int mine_now = 0, ts = 0;
        sscanf(arg, "%d %d", &mine_now, &ts);
        my_turn = mine_now;
        turn_seat = ts;
        if (strcmp(cmd, "START") == 0) {
            phase = P_PLAYING;
            cur_r = cur_c = 0;   /* crosshair starts at A1, not where the last ship was placed */
            logmsg(tr(S_BATTLE_BEGINS), my_turn ? tr(S_YOU) : seat_name(ts));
        }
        if (phase == P_PLAYING) { turn_status(); turn_cue(); }
    } else if (strcmp(cmd, "SHOT") == 0) {
        int r, c, res, idx, ts = 0;
        if (sscanf(arg, "%d %d %d %d %d", &r, &c, &res, &idx, &ts) >= 4) {
            int i = ts ? enemy_idx(ts) : 0;
            if (i < 0) i = 0;
            en[i].b.shot[r][c] = res == FIRE_MISS ? 1 : 2;
            const char *who = nen > 1 ? en[i].name : tr(S_THE_ENEMY);
            if (res == FIRE_SUNK && idx >= 0 && idx < NSHIPS) {
                en[i].sunk[idx] = 1;
                logmsg(tr(S_YOU_FIRED_SUNK), 'A' + r, c + 1, who, tr(S_SHIP_0 + idx));
            } else
                logmsg(tr(S_YOU_FIRED), 'A' + r, c + 1, tr(res == FIRE_HIT ? S_HIT_EXCL : S_MISS_DOT));
        }
    } else if (strcmp(cmd, "INCOMING") == 0) {
        int r, c, res, idx, from = 0;
        if (sscanf(arg, "%d %d %d %d %d", &r, &c, &res, &idx, &from) >= 4) {
            board_fire(&mine, r, c, NULL);
            const char *who = from ? seat_name(from) : en[0].name;
            if (res == FIRE_SUNK && idx >= 0 && idx < NSHIPS)
                logmsg(tr(S_OPP_FIRED_SUNK), who, 'A' + r, c + 1, tr(S_SHIP_POSS_0 + idx));
            else
                logmsg(tr(S_OPP_FIRED), who, 'A' + r, c + 1, tr(res == FIRE_HIT ? S_HIT_DOT : S_MISS_DOT));
        }
    } else if (strcmp(cmd, "OSHOT") == 0) {
        /* the other attacker fired at one of my enemies: same board, so the hole shows on my grid too */
        int seat, r, c, res, idx, from;
        if (sscanf(arg, "%d %d %d %d %d %d", &seat, &r, &c, &res, &idx, &from) == 6) {
            int i = enemy_idx(seat);
            if (i >= 0 && r >= 0 && r < BOARD_N && c >= 0 && c < BOARD_N) {
                en[i].b.shot[r][c] = res == FIRE_MISS ? 1 : 2;
                if (res == FIRE_SUNK && idx >= 0 && idx < NSHIPS) {
                    en[i].sunk[idx] = 1;
                    logmsg(tr(S_SOMEONE_SUNK), seat_name(from), en[i].name, tr(S_SHIP_0 + idx));
                } else
                    logmsg(tr(S_OTHER_FIRED), seat_name(from), en[i].name, 'A' + r, c + 1, tr(res == FIRE_HIT ? S_HIT_DOT : S_MISS_DOT));
            }
        }
    } else if (strcmp(cmd, "OUT") == 0) {
        int seat = atoi(arg);
        if (seat == my_seat) {
            me_alive = 0;
            logmsg("%s", tr(S_YOU_ARE_OUT));
            set_status("%s", tr(S_YOU_ARE_OUT));
        } else {
            int i = enemy_idx(seat);
            if (i >= 0) { en[i].alive = 0; logmsg(tr(S_ENEMY_OUT), en[i].name); pick_alive_target(); }
        }
    } else if (strcmp(cmd, "GAMEOVER") == 0) {
        char res[8] = "";
        int k = 0;
        sscanf(arg, "%7s %n", res, &k);
        snprintf(winner_name, sizeof winner_name, "%s", arg + k);
        won = strcmp(res, "WIN") == 0;
        phase = P_GAMEOVER;
        rematch_pending = 0;
        if (won) logmsg("%s", tr(S_VICTORY));
        else logmsg(tr(S_DEFEAT), winner_name);
        set_status("%s", tr(won ? S_YOU_WON : S_YOU_LOST));
    } else if (strcmp(cmd, "RESUME") == 0) {
        int ph = 0, seat = 0, k = 0;
        if (sscanf(arg, "%d %d %d %n", &ph, &seat, &my_turn, &k) >= 3) {
            nen = 1;
            set_enemy(0, seat, arg + k);
        }
        reset_boards();
        rematch_pending = 0;
        if (editing_name && !name_for_next) editing_name = 0;
        if (ph == 1) start_placing();
        else if (ph == 2) { phase = P_WAIT_OPP; set_status("%s", tr(S_FLEET_ACCEPTED)); }
        else { phase = P_PLAYING; turn_seat = my_turn ? my_seat : seat; turn_status(); }
        logmsg("%s", tr(S_RESUMED));
    } else if (strcmp(cmd, "RESUME3") == 0) {
        int ph = 0, s1, s2, a1, a2, am, ts, k = 0;
        if (sscanf(arg, "%d %d %d %d %d %d %d %d %n", &ph, &my_turn, &s1, &s2, &a1, &a2, &am, &ts, &k) == 8) {
            char *a, *b;
            split_names(arg + k, &a, &b);
            nen = 2;
            set_enemy(0, s1, a);
            set_enemy(1, s2, b);
            reset_boards();
            en[0].alive = a1;
            en[1].alive = a2;
            me_alive = am;
            turn_seat = ts;
            pick_alive_target();
            rematch_pending = 0;
            if (editing_name && !name_for_next) editing_name = 0;
            if (ph == 1) start_placing();
            else if (ph == 2) { phase = P_WAIT_OPP; set_status("%s", tr(S_FLEET_ACCEPTED)); }
            else { phase = P_PLAYING; turn_status(); }
            logmsg("%s", tr(S_RESUMED));
        }
    } else if (strcmp(cmd, "MYFLEET") == 0) {
        Board b;
        if (fleet_parse(&b, arg)) { mine = b; place_idx = NSHIPS; }
    } else if (strcmp(cmd, "MYSHOTS") == 0) {
        int r, c, k;
        while (sscanf(arg, "%d,%d;%n", &r, &c, &k) == 2) { board_fire(&mine, r, c, NULL); arg += k; }
    } else if (strcmp(cmd, "ENEMYSHOTS") == 0) {
        int seat, k = 0;
        if (sscanf(arg, "%d %n", &seat, &k) == 1) {
            int i = enemy_idx(seat);
            arg += k;
            int r, c, res;
            while (i >= 0 && sscanf(arg, "%d,%d,%d;%n", &r, &c, &res, &k) == 3) {
                if (r >= 0 && r < BOARD_N && c >= 0 && c < BOARD_N) en[i].b.shot[r][c] = (unsigned char)res;
                arg += k;
            }
        }
    } else if (strcmp(cmd, "ENEMYSUNK") == 0) {
        int seat, k = 0;
        if (sscanf(arg, "%d %n", &seat, &k) == 1) {
            int i = enemy_idx(seat);
            arg += k;
            int idx;
            while (i >= 0 && sscanf(arg, "%d;%n", &idx, &k) == 1) { if (idx >= 0 && idx < NSHIPS) en[i].sunk[idx] = 1; arg += k; }
        }
    } else if (strcmp(cmd, "OPPWAIT") == 0 || strcmp(cmd, "OPPBACK") == 0) {
        int i = enemy_idx(atoi(arg));
        if (i >= 0) {
            en[i].away = strcmp(cmd, "OPPWAIT") == 0;
            logmsg(tr(en[i].away ? S_OPP_WAIT : S_OPP_BACK), en[i].name);
            if (phase == P_PLAYING) turn_status();
        }
    } else if (strcmp(cmd, "OPPLEFT") == 0) {
        int i = enemy_idx(atoi(arg));
        logmsg(tr(S_OPP_LEFT), i >= 0 ? en[i].name : "?");
    } else if (strcmp(cmd, "ABORT") == 0) {
        logmsg("%s", tr(S_ADMIN_ABORT));
    } else if (strcmp(cmd, "ERR") == 0) {
        logmsg(tr(S_SERVER_SAYS), arg);
        if (strncmp(arg, "taken over", 10) == 0) {
            /* another client with our id took the seat: do not fight over it */
            phase = P_DEAD;
            editing_name = 0;
            set_status("%s", tr(S_TAKEN_OVER));
        } else if (phase == P_CONNECTING && !my_seat) {
            phase = P_DEAD;
            editing_name = 0;
            set_status(tr(S_REJECTED), arg);
        }
    } else if (strcmp(cmd, "PONG") == 0) {
        /* ignore */
    } else {
        logmsg(tr(S_UNKNOWN_MSG), line);
    }
    dirty = 1;
}

/* ---- Keyboard ---------------------------------------------------------- */

/* Largest cursor row/col for which the ship being placed still fits on the board. */
static int place_max_r(void) { return BOARD_N - (place_horiz ? 1 : SHIP_LEN[place_idx]); }
static int place_max_c(void) { return BOARD_N - (place_horiz ? SHIP_LEN[place_idx] : 1); }

static void clamp_cursor(void)
{
    int max_r = BOARD_N - 1, max_c = BOARD_N - 1;
    if (phase == P_PLACING && place_idx < NSHIPS) { max_r = place_max_r(); max_c = place_max_c(); }
    if (cur_r < 0) cur_r = 0;
    if (cur_c < 0) cur_c = 0;
    if (cur_r > max_r) cur_r = max_r;
    if (cur_c > max_c) cur_c = max_c;
}

static void move_cursor(int dr, int dc)
{
    cur_r += dr;
    cur_c += dc;
    /* three-player: running off the side of one enemy grid lands on the other */
    if (phase == P_PLAYING && nen > 1) {
        if (cur_c >= BOARD_N && target == 0 && en[1].alive) { target = 1; cur_c = 0; }
        else if (cur_c < 0 && target == 1 && en[0].alive) { target = 0; cur_c = BOARD_N - 1; }
    }
    clamp_cursor();
    dirty = 1;
}

static void switch_target(void)
{
    if (nen < 2) return;
    int t = 1 - target;
    if (en[t].alive) target = t;
    dirty = 1;
}

/* Puts the cursor on the first free, valid spot for the ship being placed,
 * scanning row by row from the current position and wrapping around. */
static void place_find_spot(void)
{
    if (place_idx >= NSHIPS) return;
    clamp_cursor();
    int start = cur_r * BOARD_N + cur_c;
    for (int pass = 0; pass < 2; pass++) {
        for (int k = 0; k < BOARD_N * BOARD_N; k++) {
            int idx = (start + k) % (BOARD_N * BOARD_N);
            int r = idx / BOARD_N, c = idx % BOARD_N;
            if (board_can_place(&mine, place_idx, r, c, place_horiz)) { cur_r = r; cur_c = c; return; }
        }
        place_horiz = !place_horiz;   /* no spot in this orientation: try the other one */
        clamp_cursor();
    }
}

static void place_status(void)
{
    if (place_idx < NSHIPS)
        set_status(tr(S_PLACE_SHIP), tr(S_SHIP_0 + place_idx), SHIP_LEN[place_idx]);
}

static void turn_status(void)
{
    if (!me_alive) { set_status("%s", tr(S_YOU_ARE_OUT)); return; }
    int ti = enemy_idx(turn_seat);
    if (ti >= 0 && en[ti].away) { set_status(tr(S_OPP_WAIT), en[ti].name); return; }
    if (my_turn) set_status("%s", tr(nen > 1 ? S_YOUR_TURN3 : S_YOUR_TURN));
    else set_status(tr(S_WAIT_FIRE), seat_name(turn_seat));
}

static void turn_cue(void)
{
    if (!my_turn || !me_alive) return;
    if (opt_sound) beep();
    if (opt_flash) flash();
}

static void name_edit_start(int for_next)
{
    editing_name = 1;
    name_for_next = for_next;
    /* between rounds the field starts empty so whoever sits down has to type their name */
    if (for_next) name_buf[0] = 0;
    else strcpy(name_buf, my_name);
    dirty = 1;
}

static void start_placing(void)
{
    rematch_pending = 0;
    reset_boards();
    phase = P_PLACING;
    place_find_spot();
    if (nen > 1) logmsg(tr(S_MATCHED3), en[0].name, en[1].name);
    else logmsg(tr(S_MATCHED), en[0].name, en[0].seat);
    place_status();
}

static void name_edit_done(void)
{
    editing_name = 0;
    if (name_for_next) {
        name_for_next = 0;
        if (phase == P_GAMEOVER && rematch_pending) start_placing();
    }
}

static void name_key(wint_t wch, int is_key)
{
    dirty = 1;
    if (is_key) {
        if (wch != KEY_BACKSPACE) return;
        goto backspace;
    }
    if (wch == 27) { if (my_name[0] && !name_for_next) name_edit_done(); return; }   /* Esc: keep the old name (not between rounds) */
    if (wch == '\n' || wch == '\r') {
        char clean[NAME_MAX + 1];
        sanitize_name(name_buf, clean, sizeof clean);
        if (!clean[0]) return;
        if (strcmp(clean, my_name) != 0) sendf("NAME %s", clean);
        name_edit_done();
        return;
    }
    if (wch == 127 || wch == 8) goto backspace;
    if (!iswprint(wch)) return;
    {
        char mb[MB_CUR_MAX + 1];
        mbstate_t st;
        memset(&st, 0, sizeof st);
        size_t n = wcrtomb(mb, (wchar_t)wch, &st);
        if (n == (size_t)-1) return;
        size_t len = strlen(name_buf);
        if (len + n > NAME_MAX) return;
        memcpy(name_buf + len, mb, n);
        name_buf[len + n] = 0;
    }
    return;
backspace:
    {
        size_t len = strlen(name_buf);
        while (len > 0 && ((unsigned char)name_buf[len - 1] & 0xC0) == 0x80) len--;
        if (len > 0) len--;
        name_buf[len] = 0;
    }
}

static int is_enter(wint_t wch, int is_key)
{
    return is_key ? wch == KEY_ENTER : (wch == '\n' || wch == '\r');
}

static void handle_key(wint_t wch, int is_key)
{
    if (editing_name) { name_key(wch, is_key); return; }
    if (!is_key) wch = (wint_t)towlower(wch);   /* Caps Lock must not matter for kids */

    if (is_key) {
        switch (wch) {
        case KEY_UP:    move_cursor(-1, 0); break;
        case KEY_DOWN:  move_cursor(1, 0); break;
        case KEY_LEFT:  move_cursor(0, -1); break;
        case KEY_RIGHT: move_cursor(0, 1); break;
        case KEY_BTAB:  switch_target(); break;
        case KEY_RESIZE: dirty = 1; break;
        }
    } else {
        switch (wch) {
        case 'k': move_cursor(-1, 0); break;
        case 'j': move_cursor(1, 0); break;
        case 'h': move_cursor(0, -1); break;
        case 'l': move_cursor(0, 1); break;
        case '\t': switch_target(); break;
        case 'n':
            if (phase == P_LOBBY || phase == P_PLACING) name_edit_start(0);
            else if (phase == P_GAMEOVER && rematch_pending) name_edit_start(1);
            break;
        }
    }

    if (phase == P_PLACING) {
        if (is_enter(wch, is_key) || (!is_key && wch == ' ')) {
            if (!board_can_place(&mine, place_idx, cur_r, cur_c, place_horiz)) {
                logmsg(tr(S_CANT_PLACE), tr(S_SHIP_0 + place_idx));
                return;
            }
            board_place(&mine, place_idx, cur_r, cur_c, place_horiz);
            place_idx++;
            if (place_idx >= NSHIPS) { submit_fleet(); set_status("%s", tr(S_SENDING_FLEET)); }
            else { place_find_spot(); place_status(); }
        } else if ((is_key && wch == KEY_BACKSPACE) || (!is_key && (wch == 127 || wch == 8))) {
            if (place_idx > 0) {
                place_idx--;
                board_unplace(&mine, place_idx);
                cur_r = mine.ships[place_idx].r;
                cur_c = mine.ships[place_idx].c;
                place_horiz = mine.ships[place_idx].horiz;
                place_status();
            }
        } else if (!is_key && wch == 'r') {
            place_horiz = !place_horiz;
            clamp_cursor();   /* slide back in so the rotated ship stays on the board */
            dirty = 1;
        } else if (!is_key && wch == 'a') {
            board_auto_place(&mine);
            place_idx = NSHIPS;
            submit_fleet();
            set_status("%s", tr(S_SENDING_FLEET));
        }
    } else if (phase == P_GAMEOVER && rematch_pending) {
        /* between rounds: confirm or change who is playing, then place */
        if (is_enter(wch, is_key) || (!is_key && wch == ' ')) name_edit_start(1);
    } else if (phase == P_PLAYING) {
        if (is_enter(wch, is_key) || (!is_key && wch == ' ')) {
            if (!me_alive) logmsg("%s", tr(S_YOU_ARE_OUT));
            else if (!my_turn) logmsg("%s", tr(S_NOT_YOUR_TURN));
            else if (nen > 1 && !en[target].alive) logmsg(tr(S_ENEMY_OUT), en[target].name);
            else if (en[target].b.shot[cur_r][cur_c]) logmsg(tr(S_ALREADY_FIRED), 'A' + cur_r, cur_c + 1);
            else if (nen > 1) sendf("FIRE %d %d %d", cur_r, cur_c, en[target].seat);
            else sendf("FIRE %d %d", cur_r, cur_c);
        }
    }
}

/* ---- Test mode: local server + robot opponents in child processes ------ */

static pid_t test_children[3];
static char  test_lb_path[128];

static void test_cleanup(void)
{
    for (int i = 0; i < 3; i++)
        if (test_children[i] > 0) { kill(test_children[i], SIGTERM); waitpid(test_children[i], NULL, 0); }
    if (test_lb_path[0]) unlink(test_lb_path);
}

/* Finds a free TCP port on loopback by binding one and letting it go. */
static int test_free_port(void)
{
    for (int tries = 0; tries < 50; tries++) {
        int port = 20000 + rand() % 20000;
        int fd = net_listen(port);
        if (fd >= 0) { close(fd); return port; }
    }
    return -1;
}

static int test_start(int *port, int players)
{
    *port = test_free_port();
    if (*port < 0) return -1;
    snprintf(test_lb_path, sizeof test_lb_path, "/tmp/battleship-test-%d.txt", (int)getpid());
    char portstr[16], nstr[4];
    snprintf(portstr, sizeof portstr, "%d", *port);
    snprintf(nstr, sizeof nstr, "%d", players);

    pid_t p = fork();
    if (p == 0) {
        if (!freopen("/dev/null", "w", stdout)) _exit(1);
        char *args[] = { "--headless", "--autopair", nstr, "--seats", "3", "--port", portstr, "--leaderboard", test_lb_path };
        _exit(server_main((int)(sizeof args / sizeof args[0]), args));
    }
    if (p < 0) return -1;
    test_children[0] = p;

    static const char *const bot_names[] = { "Robot", "Robot 2" };
    for (int i = 0; i < players - 1; i++) {
        p = fork();
        if (p == 0) _exit(bot_main("127.0.0.1", *port, bot_names[i]));
        if (p < 0) return -1;
        test_children[1 + i] = p;
    }
    return 0;
}

/* ---- Colours ----------------------------------------------------------- */

/* Fixed look on every terminal: entries 16-255 of the xterm palette do not follow
 * the terminal's colour theme, so they are used whenever the terminal has them.
 * The 8-colour fallback is as close as it gets. */
static void pairdef(int id, int fg256, int bg256, int fg8, int bg8)
{
    if (COLORS >= 256) init_pair((short)id, (short)fg256, (short)bg256);
    else init_pair((short)id, (short)fg8, (short)bg8);
}

static void init_palette(void)
{
    /*                 fg256 bg256  fg8          bg8 */
    pairdef(C_WATER,    33,   17,   COLOR_BLUE,  COLOR_BLACK);   /* blue on navy */
    pairdef(C_SHIP,     46,   17,   COLOR_GREEN, COLOR_BLACK);
    pairdef(C_HIT,     196,   52,   COLOR_RED,   COLOR_BLACK);   /* red on dark red */
    pairdef(C_MISS,     51,   17,   COLOR_CYAN,  COLOR_BLACK);
    pairdef(C_CURSOR,   16,  226,   COLOR_BLACK, COLOR_YELLOW);  /* your turn: black on bright yellow */
    pairdef(C_WAITCUR, 231,  240,   COLOR_WHITE, COLOR_BLUE);    /* waiting: white on dark grey */
    pairdef(C_BAD,     231,  196,   COLOR_WHITE, COLOR_RED);
    pairdef(C_TITLE,    16,   51,   COLOR_BLACK, COLOR_CYAN);
    pairdef(C_STATUS,  220,   -1,   COLOR_YELLOW, -1);
    pairdef(C_GOLD,    220,   -1,   COLOR_YELLOW, -1);
    pairdef(C_ST_MINE,  16,   46,   COLOR_BLACK, COLOR_GREEN);
    pairdef(C_ST_WAIT, 231,  240,   COLOR_WHITE, COLOR_BLUE);
}

/* ---- Main -------------------------------------------------------------- */

/* Ctrl+C is the only way out for the kids; exit through the main loop so ncurses is shut down properly. */
static void on_sigint(int sig)
{
    (void)sig;
    quit = 1;
}

/* Default client id: hostname plus terminal, so two clients on one machine never share an id,
 * while a restart in the same terminal window still resumes the same seat. */
static void default_client_id(char *out, size_t n)
{
    char hn[64] = "";
    gethostname(hn, sizeof hn - 1);
    hn[sizeof hn - 1] = 0;
    char *dot = strchr(hn, '.');
    if (dot) *dot = 0;
    const char *tty = ttyname(STDIN_FILENO);
    char suffix[16] = "";
    if (tty) {
        const char *base = strrchr(tty, '/');
        base = base ? base + 1 : tty;              /* "3" of /dev/pts/3, "tty2" of /dev/tty2 */
        const char *kind = strstr(tty, "/pts/") ? "pts" : "";
        snprintf(suffix, sizeof suffix, "-%s%s", kind, base);
    } else {
        snprintf(suffix, sizeof suffix, "-%d", (int)getpid());
    }
    char raw[96];
    snprintf(raw, sizeof raw, "%.*s%s", (int)(CLIENT_ID_MAX - strlen(suffix)), hn, suffix);
    sanitize_id(raw, out, n);
}

static void usage(void)
{
    fprintf(stderr, tr(S_CLIENT_USAGE), DEFAULT_PORT);
}

int client_main(int argc, char **argv)
{
    for (int i = 0; i < argc; i++)
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) { usage(); return 0; }
    if (argc < 1) { usage(); return 2; }
    const char *host = argv[0];
    if (strncmp(host, "--test", 6) == 0) { host = "127.0.0.1"; argv--; argc++; } /* `client --test` without a host */
    int port = DEFAULT_PORT, want_seat = 0, test_mode = 0;
    my_name[0] = 0;
    client_id[0] = 0;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--test") == 0) test_mode = 2;
        else if (strcmp(argv[i], "--test3") == 0) test_mode = 3;
        else if (strcmp(argv[i], "--sound") == 0) opt_sound = 1;
        else if (strcmp(argv[i], "--flash") == 0) opt_flash = 1;
        else if (strcmp(argv[i], "--logo") == 0) show_logo = 1;
        else if ((strcmp(argv[i], "--client") == 0 || strcmp(argv[i], "--id") == 0) && i + 1 < argc) sanitize_id(argv[++i], client_id, sizeof client_id);
        else if (strcmp(argv[i], "--name") == 0 && i + 1 < argc) sanitize_name(argv[++i], my_name, sizeof my_name);
        else if (strcmp(argv[i], "--port") == 0 && i + 1 < argc) port = atoi(argv[++i]);
        else if (strcmp(argv[i], "--seat") == 0 && i + 1 < argc) want_seat = atoi(argv[++i]);
        else if (strcmp(argv[i], "--lang") == 0 && i + 1 < argc) i++; /* handled in main */
        else if (strcmp(argv[i], "--cell") == 0 && i + 1 < argc) { cell_opt = atoi(argv[++i]); if (cell_opt < 1 || cell_opt > 3) cell_opt = 0; }
        else { usage(); return 2; }
    }
    if (!client_id[0]) default_client_id(client_id, sizeof client_id);

    srand((unsigned)time(NULL) ^ (unsigned)getpid());
    if (test_mode) {
        host = "127.0.0.1";
        if (test_start(&port, test_mode) < 0) { fprintf(stderr, "%s", tr(S_TEST_FAILED)); return 1; }
        atexit(test_cleanup);
        /* give the server a moment to start listening */
        for (int attempt = 0; attempt < 50; attempt++) {
            sock = net_connect(host, port);
            if (sock >= 0) break;
            usleep(100000);
        }
    } else
        sock = net_connect(host, port);
    if (sock < 0) {
        fprintf(stderr, tr(S_CONNECT_FAILED), host, port, strerror(errno));
        return 1;
    }
    signal(SIGPIPE, SIG_IGN);
    signal(SIGINT, on_sigint);
    signal(SIGTERM, on_sigint);
    srv_host = host;
    srv_port = port;
    if (getenv("BATTLESHIP_DEBUG")) debug_fp = fopen(getenv("BATTLESHIP_DEBUG"), "a");
    sendf("HELLO %d %s", want_seat, client_id);
    if (!my_name[0]) name_edit_start(0);

    initscr();
    cbreak();
    noecho();
    keypad(stdscr, TRUE);
    nodelay(stdscr, TRUE);
    curs_set(0);
    if (has_colors()) {
        start_color();
        use_default_colors();
        init_palette();
    }
    set_status("%s", tr(S_CONNECTING));
    if (test_mode) logmsg("%s", tr(S_TEST_MODE));

    while (!quit) {
        if (dirty) draw();
        struct pollfd pfds[2] = {
            { .fd = STDIN_FILENO, .events = POLLIN },
            { .fd = sock, .events = POLLIN },
        };
        int rc = poll(pfds, 2, 200);
        if (rc < 0) {
            if (errno == EINTR) { dirty = 1; continue; }
            break;
        }
        if (pfds[0].revents & POLLIN) {
            wint_t wch;
            int r;
            while ((r = get_wch(&wch)) != ERR) handle_key(wch, r == KEY_CODE_YES);
        }
        if (sock < 0 && phase == P_RECONNECT && time(NULL) >= next_reconnect) {
            next_reconnect = time(NULL) + 2;
            sock = net_connect_timeout(srv_host, srv_port, 1000);
            if (sock >= 0) {
                lb.len = 0;
                phase = P_CONNECTING;
                sendf("HELLO %d %s", my_seat, client_id);   /* same seat and client id: the server resumes us */
                logmsg("%s", tr(S_RECONNECTED));
            }
        }
        if (sock >= 0 && (pfds[1].revents & (POLLIN | POLLHUP | POLLERR))) {
            if (linebuf_feed(&lb, sock) < 0) { connection_lost(); continue; }
            char line[LINE_MAX_LEN];
            while (linebuf_next(&lb, line, sizeof line)) handle_line(line);
        }
    }

    if (sock >= 0) { net_sendf(sock, "BYE"); close(sock); }
    endwin();
    return 0;
}
