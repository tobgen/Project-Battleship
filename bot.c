/*
 * Robot opponent used by `battleship client --test` / `--test3`. Connects
 * like a normal client, auto-places its fleet and shoots with a simple
 * hunt/target strategy. In three-player games it picks a target itself.
 */
#include "common.h"

#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define THINK_MS 700   /* pause before each shot so the turn change is visible */
#define MAX_EN   2

typedef struct {
    int seat, alive;
    unsigned char tried[BOARD_N][BOARD_N];
    int stack_r[BOARD_N * BOARD_N], stack_c[BOARD_N * BOARD_N], nstack;
} Target;

static int    fd = -1;
static Board  mine;
static Target en[MAX_EN];
static int    nen = 0;

static void push(Target *t, int r, int c)
{
    if (r < 0 || r >= BOARD_N || c < 0 || c >= BOARD_N || t->tried[r][c]) return;
    for (int i = 0; i < t->nstack; i++) if (t->stack_r[i] == r && t->stack_c[i] == c) return;
    t->stack_r[t->nstack] = r;
    t->stack_c[t->nstack] = c;
    t->nstack++;
}

static Target *by_seat(int seat)
{
    for (int i = 0; i < nen; i++) if (en[i].seat == seat) return &en[i];
    return NULL;
}

static void new_game(void)
{
    board_init(&mine);
    board_auto_place(&mine);
    for (int i = 0; i < nen; i++) {
        memset(en[i].tried, 0, sizeof en[i].tried);
        en[i].nstack = 0;
        en[i].alive = 1;
    }
    char f[128];
    fleet_format(&mine, f, sizeof f);
    net_sendf(fd, "FLEET %s", f);
}

static void fire(void)
{
    /* target: an enemy we are already hunting, else a random living one */
    Target *t = NULL;
    for (int i = 0; i < nen; i++) if (en[i].alive && en[i].nstack > 0) { t = &en[i]; break; }
    if (!t) {
        int alive[MAX_EN], n = 0;
        for (int i = 0; i < nen; i++) if (en[i].alive) alive[n++] = i;
        if (n == 0) return;
        t = &en[alive[rand() % n]];
    }
    int r = -1, c = -1;
    while (t->nstack > 0) {
        t->nstack--;
        if (!t->tried[t->stack_r[t->nstack]][t->stack_c[t->nstack]]) { r = t->stack_r[t->nstack]; c = t->stack_c[t->nstack]; break; }
    }
    if (r < 0) {
        /* hunt: random untried cell, preferring a checkerboard since the smallest ship is 2 long */
        for (int tries = 0; tries < 500 && r < 0; tries++) {
            int rr = rand() % BOARD_N, cc = rand() % BOARD_N;
            if (!t->tried[rr][cc] && (tries > 200 || (rr + cc) % 2 == 0)) { r = rr; c = cc; }
        }
        if (r < 0)
            for (int rr = 0; rr < BOARD_N && r < 0; rr++)
                for (int cc = 0; cc < BOARD_N; cc++)
                    if (!t->tried[rr][cc]) { r = rr; c = cc; break; }
    }
    if (r < 0) return;
    t->tried[r][c] = 1;
    struct timespec ts = { THINK_MS / 1000, (THINK_MS % 1000) * 1000000L };
    nanosleep(&ts, NULL);
    if (nen > 1) net_sendf(fd, "FIRE %d %d %d", r, c, t->seat);
    else net_sendf(fd, "FIRE %d %d", r, c);
}

int bot_main(const char *host, int port, const char *name)
{
    srand((unsigned)time(NULL) ^ (unsigned)getpid());
    for (int attempt = 0; attempt < 50 && fd < 0; attempt++) {
        fd = net_connect(host, port);
        if (fd < 0) usleep(100000);
    }
    if (fd < 0) return 1;
    net_sendf(fd, "HELLO 0 robot%d", (int)getpid() % 1000);

    LineBuf lb = { .len = 0 };
    char line[LINE_MAX_LEN];
    for (;;) {
        if (linebuf_feed(&lb, fd) < 0) break;
        while (linebuf_next(&lb, line, sizeof line)) {
            char cmd[32];
            int n = 0;
            if (sscanf(line, "%31s%n", cmd, &n) != 1) continue;
            const char *arg = line + n;
            while (*arg == ' ') arg++;
            if (strcmp(cmd, "WELCOME") == 0) {
                net_sendf(fd, "NAME %s", name);
            } else if (strcmp(cmd, "MATCH") == 0) {
                nen = 1;
                en[0].seat = atoi(arg);
                new_game();
            } else if (strcmp(cmd, "MATCH3") == 0) {
                nen = 2;
                sscanf(arg, "%d %d", &en[0].seat, &en[1].seat);
                new_game();
            } else if (strcmp(cmd, "START") == 0 || strcmp(cmd, "TURN") == 0) {
                if (atoi(arg) == 1) fire();
            } else if (strcmp(cmd, "SHOT") == 0) {
                int r, c, res, idx, ts = 0;
                if (sscanf(arg, "%d %d %d %d %d", &r, &c, &res, &idx, &ts) >= 4) {
                    Target *t = ts ? by_seat(ts) : &en[0];
                    if (t) {
                        if (res == FIRE_HIT) { push(t, r - 1, c); push(t, r + 1, c); push(t, r, c - 1); push(t, r, c + 1); }
                        else if (res == FIRE_SUNK) t->nstack = 0;
                    }
                }
            } else if (strcmp(cmd, "OSHOT") == 0) {
                /* the other attacker's shot on a shared board: that cell is spent, and a hit is a lead */
                int seat, r, c, res, idx, from;
                if (sscanf(arg, "%d %d %d %d %d %d", &seat, &r, &c, &res, &idx, &from) == 6) {
                    Target *t = by_seat(seat);
                    if (t && r >= 0 && r < BOARD_N && c >= 0 && c < BOARD_N) {
                        t->tried[r][c] = 1;
                        if (res == FIRE_HIT) { push(t, r - 1, c); push(t, r + 1, c); push(t, r, c - 1); push(t, r, c + 1); }
                    }
                }
            } else if (strcmp(cmd, "OUT") == 0) {
                Target *t = by_seat(atoi(arg));
                if (t) t->alive = 0;
            } else if (strcmp(cmd, "INCOMING") == 0) {
                int r, c;
                if (sscanf(arg, "%d %d", &r, &c) == 2) board_fire(&mine, r, c, NULL);
            }
            /* GAMEOVER, RESTART, LOBBY, LB...: nothing to do, the next MATCH resets us */
        }
    }
    close(fd);
    return 0;
}
