/*
 * Projector view: `battleship board <host>`. Read-only client that shows the
 * leaderboard, the live matches and the latest results, big enough for a wall.
 */
#include "common.h"
#include "i18n.h"

#include <errno.h>
#include <ncurses.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define LB_SHOW   10
#define MAX_M     16
#define FEED      6

enum { V_GOLD = 1, V_TITLE, V_MINE, V_DIM, V_GREEN };

typedef struct { char name[NAME_MAX + 1]; int wins, losses, hits, shots; } LBEntry;
typedef struct { char name[NAME_MAX + 1]; int hits, shots; } SBEntry;
typedef struct { int n, started, turn, shots, away, seat[3]; char name[3][NAME_MAX + 1]; } MatchRow;

static LBEntry  board[LB_SHOW];
static int      nboard = 0;
static SBEntry  sboard[LB_SHOW];
static int      nsboard = 0;
static MatchRow mrows[MAX_M], mnew[MAX_M];
static int      nm = 0, nmnew = 0;
static char     feed[FEED][LINE_MAX_LEN];
static int      nfeed = 0;
static int      sock = -1, connected = 0, quit = 0, show_logo = 0;

static const char *const BANNER[5] = {
    "##### ##### ##### #   # #####",
    "#     #     #   # #   #   #  ",
    "##### #     #   # #   #   #  ",
    "    # #     #   # #   #   #  ",
    "##### ##### ##### #####   #  ",
};

static void on_sigint(int sig) { (void)sig; quit = 1; }

static void draw_banner(int y, int x, int pw)
{
    attron(COLOR_PAIR(V_GOLD) | A_BOLD);
    for (int i = 0; i < 5; i++) {
        move(y + i, x);
        for (const char *p = BANNER[i]; *p; p++)
            for (int k = 0; k < pw; k++) addstr(*p == '#' ? "\xe2\x96\x88" : " ");
    }
    attroff(COLOR_PAIR(V_GOLD) | A_BOLD);
}

static void draw(void)
{
    erase();
    int rows, cols;
    getmaxyx(stdscr, rows, cols);
    int y = 1;
    if (show_logo && rows >= 30) {
        int pw = cols >= 29 * 2 + 4 ? 2 : 1;
        draw_banner(y, (cols - 29 * pw) / 2, pw);
        y += 6;
    }
    attron(COLOR_PAIR(V_TITLE) | A_BOLD);
    mvprintw(y, 2, " %s ", tr(S_VIEW_TITLE));
    attroff(COLOR_PAIR(V_TITLE) | A_BOLD);
    if (!connected) {
        attron(COLOR_PAIR(V_DIM));
        mvprintw(y, cols - (int)strlen(tr(S_VIEW_WAITING)) - 2, "%s", tr(S_VIEW_WAITING));
        attroff(COLOR_PAIR(V_DIM));
    }
    y += 2;

    /* leaderboard, left half, double spaced when tall enough */
    int step = rows >= 2 * LB_SHOW + 20 ? 2 : 1;
    int lb_w = cols >= 100 ? cols / 2 : cols - 4;
    if (nboard == 0) mvprintw(y, 4, "%s", tr(S_LB_EMPTY));
    for (int i = 0; i < nboard; i++) {
        char name[NAME_MAX * 2];
        pad_utf8(name, sizeof name, board[i].name, lb_w - 22 > NAME_MAX ? NAME_MAX : lb_w - 22);
        attron(i == 0 ? (COLOR_PAIR(V_GOLD) | A_BOLD) : A_BOLD);
        mvprintw(y + i * step, 4, tr(S_LB_ROW), i + 1, name, board[i].wins, board[i].losses,
                 board[i].shots ? board[i].hits * 100 / board[i].shots : 0);
        attroff(COLOR_PAIR(V_GOLD) | A_BOLD);
    }
    /* tonight's hit ratio under the leaderboard when there is room */
    int sy = y + (nboard ? nboard : 1) * step + 1;
    if (cols >= 100 && sy + 2 + nsboard < rows - FEED - 3) {
        attron(COLOR_PAIR(V_TITLE) | A_BOLD);
        mvprintw(sy, 2, " %s ", tr(S_SB_TITLE));
        attroff(COLOR_PAIR(V_TITLE) | A_BOLD);
        if (nsboard == 0) mvprintw(sy + 2, 4, "%s", tr(S_LB_EMPTY));
        for (int i = 0; i < nsboard; i++) {
            char name[NAME_MAX * 2];
            pad_utf8(name, sizeof name, sboard[i].name, NAME_MAX);
            attron(i == 0 ? (COLOR_PAIR(V_GOLD) | A_BOLD) : A_BOLD);
            mvprintw(sy + 2 + i, 4, tr(S_SB_ROW), i + 1, name, sboard[i].shots ? sboard[i].hits * 100 / sboard[i].shots : 0, sboard[i].hits, sboard[i].shots);
            attroff(COLOR_PAIR(V_GOLD) | A_BOLD);
        }
    }

    /* matches: right half on wide screens, else below */
    int mx = cols >= 100 ? cols / 2 + 2 : 2;
    int my = cols >= 100 ? y - 2 : y + (nboard ? nboard : 1) * step + 2;
    attron(COLOR_PAIR(V_TITLE) | A_BOLD);
    mvprintw(my, mx, " %s ", tr(S_VIEW_MATCHES));
    attroff(COLOR_PAIR(V_TITLE) | A_BOLD);
    my += 2;
    if (nm == 0) mvprintw(my, mx + 2, "%s", tr(S_VIEW_NONE));
    for (int i = 0; i < nm && my + 1 < rows - FEED - 3; i++) {
        MatchRow *m = &mrows[i];
        char names[LINE_MAX_LEN];
        size_t pos = 0;
        names[0] = 0;
        for (int k = 0; k < m->n; k++) pos += (size_t)snprintf(names + pos, sizeof names - pos, "%s%s", k ? tr(S_VS) : "", m->name[k]);
        attron(A_BOLD);
        mvprintw(my, mx + 2, "%s", names);
        attroff(A_BOLD);
        const char *turn_name = "?";
        for (int k = 0; k < m->n; k++) if (m->seat[k] == m->turn) turn_name = m->name[k];
        char line[128];
        if (m->away) snprintf(line, sizeof line, "%s", tr(S_VIEW_AWAY));
        else if (!m->started) snprintf(line, sizeof line, "%s", tr(S_VIEW_PLACING));
        else snprintf(line, sizeof line, tr(S_VIEW_SHOOTS), turn_name, m->shots);
        attron(COLOR_PAIR(m->started && !m->away ? V_GREEN : V_DIM));
        mvprintw(my + 1, mx + 4, "%s", line);
        attroff(COLOR_PAIR(m->started && !m->away ? V_GREEN : V_DIM));
        my += step == 2 ? 3 : 2;
    }

    /* feed of recent results at the bottom */
    int fy = rows - FEED - 2;
    mvhline(fy - 1, 0, ACS_HLINE, cols);
    attron(A_BOLD);
    mvprintw(fy - 1, 2, " %s ", tr(S_VIEW_FEED));
    attroff(A_BOLD);
    for (int i = 0; i < nfeed; i++) mvprintw(fy + i, 4, "%.*s", cols - 6, feed[i]);
    refresh();
}

static void handle_line(char *line)
{
    char cmd[32];
    int n = 0;
    if (sscanf(line, "%31s%n", cmd, &n) != 1) return;
    char *arg = line + n;
    while (*arg == ' ') arg++;
    if (strcmp(cmd, "LBCLEAR") == 0) nboard = 0;
    else if (strcmp(cmd, "SBCLEAR") == 0) nsboard = 0;
    else if (strcmp(cmd, "SB") == 0) {
        int h, sh, k = 0;
        if (nsboard < LB_SHOW && sscanf(arg, "%d %d %n", &h, &sh, &k) == 2) {
            sboard[nsboard].hits = h;
            sboard[nsboard].shots = sh;
            snprintf(sboard[nsboard].name, sizeof sboard[nsboard].name, "%s", arg + k);
            nsboard++;
        }
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
    } else if (strcmp(cmd, "MCLEAR") == 0) nmnew = 0;
    else if (strcmp(cmd, "M") == 0) {
        MatchRow m;
        memset(&m, 0, sizeof m);
        int k = 0;
        if (nmnew < MAX_M && sscanf(arg, "%d %d %d %d %d %n", &m.n, &m.started, &m.turn, &m.shots, &m.away, &k) == 5 && m.n >= 2 && m.n <= 3) {
            const char *p = arg + k;
            int ok = 1;
            for (int i = 0; i < m.n && ok; i++) { int q = 0; if (sscanf(p, "%d %n", &m.seat[i], &q) == 1) p += q; else ok = 0; }
            if (ok) {
                char tmp[LINE_MAX_LEN];
                snprintf(tmp, sizeof tmp, "%s", p);
                char *cur = tmp;
                for (int i = 0; i < m.n; i++) {
                    char *bar = strchr(cur, '|');
                    if (bar) *bar = 0;
                    snprintf(m.name[i], sizeof m.name[i], "%.*s", NAME_MAX, cur);
                    if (!bar) break;
                    cur = bar + 1;
                }
                mnew[nmnew++] = m;
            }
        }
    } else if (strcmp(cmd, "MEND") == 0) {
        memcpy(mrows, mnew, sizeof mrows);
        nm = nmnew;
    } else if (strcmp(cmd, "EV") == 0) {
        if (nfeed == FEED) { memmove(feed[0], feed[1], sizeof feed[0] * (FEED - 1)); nfeed--; }
        snprintf(feed[nfeed++], sizeof feed[0], "%s", arg);
    }
}

int board_main(int argc, char **argv)
{
    const char *host = NULL;
    int port = DEFAULT_PORT;
    for (int i = 0; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) { fprintf(stderr, tr(S_BOARD_USAGE), DEFAULT_PORT); return 0; }
        else if (strcmp(argv[i], "--port") == 0 && i + 1 < argc) port = atoi(argv[++i]);
        else if (strcmp(argv[i], "--logo") == 0) show_logo = 1;
        else if (strcmp(argv[i], "--lang") == 0 && i + 1 < argc) i++;
        else if (argv[i][0] != '-' && !host) host = argv[i];
        else { fprintf(stderr, tr(S_BOARD_USAGE), DEFAULT_PORT); return 2; }
    }
    if (!host) { fprintf(stderr, tr(S_BOARD_USAGE), DEFAULT_PORT); return 2; }

    signal(SIGPIPE, SIG_IGN);
    signal(SIGINT, on_sigint);
    signal(SIGTERM, on_sigint);
    initscr();
    cbreak();
    noecho();
    keypad(stdscr, TRUE);
    nodelay(stdscr, TRUE);
    curs_set(0);
    if (has_colors()) {
        start_color();
        use_default_colors();
        init_pair(V_GOLD,  COLOR_YELLOW, -1);
        init_pair(V_TITLE, COLOR_BLACK,  COLOR_CYAN);
        init_pair(V_MINE,  COLOR_GREEN,  -1);
        init_pair(V_DIM,   COLORS >= 16 ? 8 : COLOR_BLUE, -1);
        init_pair(V_GREEN, COLOR_GREEN,  -1);
    }

    LineBuf lb = { .len = 0 };
    time_t next_try = 0;
    int dirty = 1;
    while (!quit) {
        if (sock < 0 && time(NULL) >= next_try) {
            next_try = time(NULL) + 2;
            sock = net_connect_timeout(host, port, 1000);
            if (sock >= 0) { lb.len = 0; net_sendf(sock, "WATCH projector"); connected = 1; dirty = 1; }
        }
        if (dirty) { draw(); dirty = 0; }
        struct pollfd pfds[2] = {
            { .fd = STDIN_FILENO, .events = POLLIN },
            { .fd = sock, .events = POLLIN },
        };
        int rc = poll(pfds, 2, 500);
        if (rc < 0) { if (errno == EINTR) { dirty = 1; continue; } break; }
        if (pfds[0].revents & POLLIN) { int ch; while ((ch = getch()) != ERR) if (ch == KEY_RESIZE) dirty = 1; }
        if (sock >= 0 && (pfds[1].revents & (POLLIN | POLLHUP | POLLERR))) {
            if (linebuf_feed(&lb, sock) < 0) { close(sock); sock = -1; connected = 0; nm = 0; dirty = 1; next_try = time(NULL) + 1; continue; }
            char line[LINE_MAX_LEN];
            while (linebuf_next(&lb, line, sizeof line)) handle_line(line);
            dirty = 1;
        }
    }
    if (sock >= 0) close(sock);
    endwin();
    return 0;
}
