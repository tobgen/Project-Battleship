/*
 * Battleship server: fixed seats, admin pairs seats into matches of two or
 * three players from an ncurses console. Keeps a leaderboard on disk.
 * Headless mode logs to stdout instead.
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
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <time.h>
#include <unistd.h>
#include <wchar.h>
#include <wctype.h>

#define MAX_SEATS    32
#define MAX_PENDING  32
#define LOG_LINES    64
#define LB_MAX       512
#define LB_SHOW      10
#define MAX_P        3          /* players per match */

enum SeatState { S_EMPTY, S_LOBBY, S_PLACING, S_READY, S_PLAYING };

typedef struct {
    int     fd;
    char    client[CLIENT_ID_MAX + 1];  /* set by the operator, identifies the machine */
    char    name[NAME_MAX + 1];         /* chosen by the player, may be empty */
    LineBuf lb;
    int     state;
    int     match;      /* index into matches, -1 when none */
    Board   board;
    int     wins, losses;
    int     g_hits, g_shots;   /* current game */
    time_t  connected_at;
    int     gone;         /* connection lost mid-match, seat kept for grace seconds */
    time_t  gone_since;
} Seat;

typedef struct {
    int active;
    int n;              /* 2 or 3 players */
    int p[MAX_P];       /* seat indices */
    int alive[MAX_P];   /* fleet still afloat (3-player elimination) */
    int turn;           /* index into p[] */
    int shots;
    int started;        /* all fleets in */
    int hold;           /* game over while paused: next round waits for the admin */
    int hold_opener;    /* index into p[] who opens that round, -1 = random */
    unsigned char fired[MAX_P][MAX_P][BOARD_N][BOARD_N];   /* [attacker][target]: cells this attacker shot (3-player) */
} Match;

typedef struct {
    int     fd;
    LineBuf lb;
    time_t  since;
} Pending;

typedef struct {
    char name[NAME_MAX + 1];
    int  wins, losses;
    int  hits, shots;          /* all time */
} LBEntry;

typedef struct {
    char name[NAME_MAX + 1];
    int  hits, shots;          /* since the server started */
} SessEntry;

#define MAX_VIEWERS 8
typedef struct {
    int     fd;
    LineBuf lb;
} Viewer;

static int      nseats = 10;
static Seat     seats[MAX_SEATS];
static Match    matches[MAX_SEATS / 2];
static Pending  pending[MAX_PENDING];
static int      npending = 0;
static int      listen_fd = -1;
static int      headless = 0;
static int      autopair = 0;         /* 0 off, else players per auto-paired match */
static int      running = 1;
static int      port = DEFAULT_PORT;
static int      grace = 90;           /* seconds a dropped player may come back */
static int      paused = 0;           /* no new rounds or auto-pairing until the admin resumes */
static int      show_misses = 0;      /* 3-player: show the other attacker's misses to the third player */
static Viewer   viewers[MAX_VIEWERS];
static int      nviewers = 0;

static LBEntry  lb[LB_MAX];
static int      nlb = 0;
static SessEntry sess[LB_MAX];
static int      nsess = 0;
static char     lb_path[512] = "battleship-leaderboard.txt";

static char     logbuf[LOG_LINES][128];
static int      loghead = 0, logcount = 0;

/* admin console state */
enum Focus { F_SEATS, F_LB };
enum Prompt { PR_NONE, PR_RENAME_SEAT, PR_RENAME_LB };
static int      focus = F_SEATS;
static int      cursor = 0;         /* seat cursor */
static int      lb_cursor = 0;
static int      marked[MAX_P] = { -1, -1, -1 };
static int      prompt = PR_NONE;
static int      prompt_target = -1;
static char     prompt_buf[NAME_MAX + 1];
static int      dirty = 1;

static void seat_drop(int s, const char *why);
static void seat_send(int s, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
static void sb_send_fd(int fd);

static const char *state_name(int s)
{
    switch (s) {
    case S_EMPTY:   return tr(S_ST_EMPTY);
    case S_LOBBY:   return tr(S_ST_LOBBY);
    case S_PLACING: return tr(S_ST_PLACING);
    case S_READY:   return tr(S_ST_READY);
    case S_PLAYING: return tr(S_ST_PLAYING);
    }
    return "?";
}

static void logf_(const char *fmt, ...)
{
    char line[128];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(line, sizeof line, fmt, ap);
    va_end(ap);
    if (headless) {
        time_t t = time(NULL);
        struct tm tm;
        localtime_r(&t, &tm);
        printf("%02d:%02d:%02d %s\n", tm.tm_hour, tm.tm_min, tm.tm_sec, line);
        fflush(stdout);
        return;
    }
    memcpy(logbuf[loghead], line, sizeof logbuf[0]);
    loghead = (loghead + 1) % LOG_LINES;
    if (logcount < LOG_LINES) logcount++;
    dirty = 1;
}

/* What to call a seat: the player's name, else the client id. */
static const char *seat_label(int s)
{
    return seats[s].name[0] ? seats[s].name : seats[s].client;
}

/* "1 mot 3 mot 5" / "Maja mot Olle mot Anna" for logs and the console */
static void match_seats_str(const Match *mt, char *out, size_t n)
{
    size_t pos = 0;
    out[0] = 0;
    for (int i = 0; i < mt->n; i++)
        pos += (size_t)snprintf(out + pos, n - pos, "%s%d", i ? tr(S_VS) : "", mt->p[i] + 1);
}

static void match_names_str(const Match *mt, char *out, size_t n)
{
    size_t pos = 0;
    out[0] = 0;
    for (int i = 0; i < mt->n; i++)
        pos += (size_t)snprintf(out + pos, n - pos, "%s%s", i ? tr(S_VS) : "", seat_label(mt->p[i]));
}

/* ---- Projector viewers ------------------------------------------------- */

static void viewers_sendf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void viewers_sendf(const char *fmt, ...)
{
    if (nviewers == 0) return;
    char msg[LINE_MAX_LEN];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof msg, fmt, ap);
    va_end(ap);
    for (int i = 0; i < nviewers; i++) net_sendf(viewers[i].fd, "%s", msg);
}

static void viewer_remove(int i)
{
    close(viewers[i].fd);
    viewers[i] = viewers[--nviewers];
}

/* ---- Leaderboard ------------------------------------------------------- */

static int lb_cmp(const void *pa, const void *pb)
{
    const LBEntry *a = pa, *b = pb;
    if (a->wins != b->wins) return b->wins - a->wins;
    if (a->losses != b->losses) return a->losses - b->losses;
    return utf8_casecmp(a->name, b->name);
}

static void lb_sort(void)
{
    qsort(lb, (size_t)nlb, sizeof lb[0], lb_cmp);
    if (lb_cursor >= nlb) lb_cursor = nlb ? nlb - 1 : 0;
}

static int lb_find(const char *name)
{
    for (int i = 0; i < nlb; i++)
        if (utf8_casecmp(lb[i].name, name) == 0) return i;
    return -1;
}

static int lb_get(const char *name)
{
    int i = lb_find(name);
    if (i >= 0) return i;
    if (nlb >= LB_MAX) return -1;
    memset(&lb[nlb], 0, sizeof lb[nlb]);
    snprintf(lb[nlb].name, sizeof lb[nlb].name, "%s", name);
    return nlb++;
}

static void lb_save(void)
{
    char tmp[sizeof lb_path + 8];
    snprintf(tmp, sizeof tmp, "%s.tmp", lb_path);
    FILE *f = fopen(tmp, "w");
    if (!f) { logf_(tr(S_LOG_LB_SAVE_FAIL), lb_path, strerror(errno)); return; }
    fprintf(f, "# v2 wins\tlosses\thits\tshots\tname  (edit while the server is stopped, or press R in the console to reload)\n");
    for (int i = 0; i < nlb; i++) fprintf(f, "%d\t%d\t%d\t%d\t%s\n", lb[i].wins, lb[i].losses, lb[i].hits, lb[i].shots, lb[i].name);
    if (fclose(f) != 0 || rename(tmp, lb_path) != 0) logf_(tr(S_LOG_LB_SAVE_FAIL), lb_path, strerror(errno));
}

static void lb_load(void)
{
    nlb = 0;
    FILE *f = fopen(lb_path, "r");
    if (!f) return;
    char line[256];
    int v2 = 0;
    while (fgets(line, sizeof line, f)) {
        if (line[0] == '#') { if (strncmp(line, "# v2", 4) == 0) v2 = 1; continue; }
        if (line[0] == '\n') continue;
        int w, l, h = 0, sh = 0, n = 0;
        if (v2) { if (sscanf(line, "%d %d %d %d %n", &w, &l, &h, &sh, &n) != 4) continue; }
        else if (sscanf(line, "%d %d %n", &w, &l, &n) != 2) continue;
        char *name = line + n;
        name[strcspn(name, "\r\n")] = 0;
        char clean[NAME_MAX + 1];
        sanitize_name(name, clean, sizeof clean);
        if (!clean[0]) continue;
        int i = lb_get(clean);
        if (i < 0) break;
        lb[i].wins += w;
        lb[i].losses += l;
        lb[i].hits += h;
        lb[i].shots += sh;
    }
    fclose(f);
    lb_sort();
    logf_(tr(S_LOG_LB_LOADED), nlb, lb_path);
}

static void lb_send_fd(int fd)
{
    net_sendf(fd, "LBCLEAR");
    for (int i = 0; i < nlb && i < LB_SHOW; i++)
        net_sendf(fd, "LB %d %d %d %d %s", lb[i].wins, lb[i].losses, lb[i].hits, lb[i].shots, lb[i].name);
}

static void lb_broadcast(void)
{
    for (int s = 0; s < nseats; s++)
        if (seats[s].state != S_EMPTY && seats[s].fd >= 0) lb_send_fd(seats[s].fd);
    for (int i = 0; i < nviewers; i++) lb_send_fd(viewers[i].fd);
    dirty = 1;
}

/* ---- Tonight's hit ratio ---------------------------------------------- */

static int sess_cmp(const void *pa, const void *pb)
{
    const SessEntry *a = pa, *b = pb;
    double ra = a->shots ? (double)a->hits / a->shots : 0, rb = b->shots ? (double)b->hits / b->shots : 0;
    if (ra != rb) return ra < rb ? 1 : -1;
    if (a->shots != b->shots) return b->shots - a->shots;
    return utf8_casecmp(a->name, b->name);
}

static void sess_add(const char *name, int hit)
{
    int i;
    for (i = 0; i < nsess; i++) if (utf8_casecmp(sess[i].name, name) == 0) break;
    if (i == nsess) {
        if (nsess >= LB_MAX) return;
        memset(&sess[nsess], 0, sizeof sess[nsess]);
        snprintf(sess[nsess].name, sizeof sess[nsess].name, "%s", name);
        nsess++;
    }
    sess[i].shots++;
    sess[i].hits += hit ? 1 : 0;
    qsort(sess, (size_t)nsess, sizeof sess[0], sess_cmp);
}

static void sb_send_fd(int fd)
{
    net_sendf(fd, "SBCLEAR");
    for (int i = 0; i < nsess && i < LB_SHOW; i++) net_sendf(fd, "SB %d %d %s", sess[i].hits, sess[i].shots, sess[i].name);
}

static void sb_broadcast(void)
{
    for (int s = 0; s < nseats; s++)
        if (seats[s].state != S_EMPTY && seats[s].fd >= 0) sb_send_fd(seats[s].fd);
    for (int i = 0; i < nviewers; i++) sb_send_fd(viewers[i].fd);
}

/* game over: the winner gets a win, everyone else in the match a loss; hit stats for all */
static void lb_record(const Match *mt, int winner_idx)
{
    for (int i = 0; i < mt->n; i++) {
        int s = mt->p[i];
        if (s < 0) continue;
        int k = lb_get(seat_label(s));
        if (k < 0) continue;
        if (i == winner_idx) lb[k].wins++; else lb[k].losses++;
        lb[k].hits  += seats[s].g_hits;
        lb[k].shots += seats[s].g_shots;
    }
    lb_sort();
    lb_save();
    lb_broadcast();
}

static void lb_rename(int i, const char *newname)
{
    if (i < 0 || i >= nlb || !newname[0]) return;
    int j = lb_find(newname);
    if (j >= 0 && j != i) {
        lb[j].wins   += lb[i].wins;
        lb[j].losses += lb[i].losses;
        lb[j].hits   += lb[i].hits;
        lb[j].shots  += lb[i].shots;
        lb[i] = lb[--nlb];
    } else {
        snprintf(lb[i].name, sizeof lb[i].name, "%s", newname);
    }
    lb_sort();
    lb_save();
    lb_broadcast();
}

static void lb_delete(int i)
{
    if (i < 0 || i >= nlb) return;
    logf_(tr(S_LOG_LB_DELETED), lb[i].name);
    lb[i] = lb[--nlb];
    lb_sort();
    lb_save();
    lb_broadcast();
}

/* ---- Viewers: match snapshot --------------------------------------------- */

static void viewers_send_matches(void)
{
    if (nviewers == 0) return;
    viewers_sendf("MCLEAR");
    for (int m = 0; m < (int)(sizeof matches / sizeof matches[0]); m++) {
        if (!matches[m].active) continue;
        Match *mt = &matches[m];
        int away = 0;
        char seatsstr[32] = "", names[LINE_MAX_LEN] = "";
        size_t sp = 0, np = 0;
        for (int i = 0; i < mt->n; i++) {
            int s = mt->p[i];
            if (s >= 0 && seats[s].gone) away = 1;
            sp += (size_t)snprintf(seatsstr + sp, sizeof seatsstr - sp, "%d ", s + 1);
            np += (size_t)snprintf(names + np, sizeof names - np, "%s%s", i ? "|" : "", s >= 0 ? seat_label(s) : "-");
        }
        viewers_sendf("M %d %d %d %d %d %s%s", mt->n, mt->started, mt->p[mt->turn] + 1, mt->shots, away, seatsstr, names);
    }
    viewers_sendf("MEND");
}

static void viewer_add(int fd, const LineBuf *lb_in)
{
    if (nviewers >= MAX_VIEWERS) { net_sendf(fd, "ERR too many viewers"); close(fd); return; }
    viewers[nviewers].fd = fd;
    viewers[nviewers].lb = *lb_in;
    nviewers++;
    lb_send_fd(fd);
    sb_send_fd(fd);
    viewers_send_matches();
    logf_("%s", tr(S_LOG_VIEWER));
}

/* ---- Seat / match helpers ----------------------------------------------- */

static void seat_send(int s, const char *fmt, ...)
{
    if (s < 0 || seats[s].fd < 0) return;
    char msg[LINE_MAX_LEN];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof msg, fmt, ap);
    va_end(ap);
    net_sendf(seats[s].fd, "%s", msg);
}

static Match *seat_match(int s)
{
    int m = seats[s].match;
    if (m < 0 || !matches[m].active) return NULL;
    return &matches[m];
}

static int match_index(const Match *mt, int s)
{
    for (int i = 0; i < mt->n; i++) if (mt->p[i] == s) return i;
    return -1;
}

/* sends to every other player of s's match */
static void match_send_others(int s, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
static void match_send_others(int s, const char *fmt, ...)
{
    Match *mt = seat_match(s);
    if (!mt) return;
    char msg[LINE_MAX_LEN];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof msg, fmt, ap);
    va_end(ap);
    for (int i = 0; i < mt->n; i++)
        if (mt->p[i] >= 0 && mt->p[i] != s) seat_send(mt->p[i], "%s", msg);
}

/* MATCH (2 players) or MATCH3 (3 players) describing the opponents of seat s */
static void match_send_intro(const Match *mt, int s)
{
    if (mt->n == 2) {
        int o = mt->p[0] == s ? mt->p[1] : mt->p[0];
        seat_send(s, "MATCH %d %s", o + 1, seat_label(o));
        return;
    }
    int o[2], k = 0;
    for (int i = 0; i < mt->n; i++) if (mt->p[i] != s) o[k++] = mt->p[i];
    seat_send(s, "MATCH3 %d %d %s|%s", o[0] + 1, o[1] + 1, seat_label(o[0]), seat_label(o[1]));
}

static void seat_set_name(int s, const char *raw)
{
    char clean[NAME_MAX + 1];
    sanitize_name(raw, clean, sizeof clean);
    if (!clean[0]) { seat_send(s, "ERR empty name"); return; }
    logf_(tr(S_LOG_NAME), s + 1, seats[s].client, clean);
    strcpy(seats[s].name, clean);
    seat_send(s, "NAMEOK %s", clean);
    match_send_others(s, "OPPNAME %d %s", s + 1, clean);
    dirty = 1;
}

/* everyone back to the lobby, match slot freed */
static void match_end(int m)
{
    if (!matches[m].active) return;
    Match *mt = &matches[m];
    mt->active = 0;
    for (int i = 0; i < mt->n; i++) {
        int s = mt->p[i];
        if (s < 0) continue;
        seats[s].match = -1;
        if (seats[s].state != S_EMPTY) {
            seats[s].state = S_LOBBY;
            board_init(&seats[s].board);
        }
    }
    /* nothing left to come back to */
    for (int i = 0; i < mt->n; i++)
        if (mt->p[i] >= 0 && seats[mt->p[i]].gone) seat_drop(mt->p[i], tr(S_WHY_TIMEOUT));
    dirty = 1;
}

/* fresh boards for a (re)start of the same match */
static void match_new_game(Match *mt, int opener_idx)
{
    mt->started = 0;
    mt->hold    = 0;
    mt->shots   = 0;
    memset(mt->fired, 0, sizeof mt->fired);
    mt->turn    = opener_idx;
    for (int i = 0; i < mt->n; i++) {
        int s = mt->p[i];
        mt->alive[i] = 1;
        seats[s].state = S_PLACING;
        board_init(&seats[s].board);
        seats[s].g_hits = seats[s].g_shots = 0;
    }
    for (int i = 0; i < mt->n; i++) match_send_intro(mt, mt->p[i]);
}

static int match_create(const int *players, int n)
{
    if (n < 2 || n > MAX_P) return -1;
    for (int i = 0; i < n; i++) {
        if (seats[players[i]].state != S_LOBBY) return -1;
        for (int j = 0; j < i; j++) if (players[i] == players[j]) return -1;
    }
    int m = -1;
    for (int i = 0; i < (int)(sizeof matches / sizeof matches[0]); i++)
        if (!matches[i].active) { m = i; break; }
    if (m < 0) return -1;
    Match *mt = &matches[m];
    memset(mt, 0, sizeof *mt);
    mt->active = 1;
    mt->n = n;
    for (int i = 0; i < n; i++) { mt->p[i] = players[i]; seats[players[i]].match = m; }
    match_new_game(mt, rand() % n);
    char names[LINE_MAX_LEN];
    match_names_str(mt, names, sizeof names);
    logf_(tr(S_LOG_PAIRED), names);
    viewers_sendf("EV %s", names);
    dirty = 1;
    return m;
}

/* Full game state for a player that came back: boards, shots, turn. */
static void seat_send_resume(int s)
{
    Match *mt = seat_match(s);
    if (!mt) return;
    int me = match_index(mt, s);
    int ph = seats[s].state == S_PLAYING ? 3 : seats[s].state == S_READY ? 2 : 1;
    int myturn = mt->started && mt->turn == me;
    if (mt->n == 2) {
        int o = mt->p[0] == s ? mt->p[1] : mt->p[0];
        seat_send(s, "RESUME %d %d %d %s", ph, o + 1, myturn, seat_label(o));
    } else {
        int o[2], k = 0;
        for (int i = 0; i < mt->n; i++) if (mt->p[i] != s) o[k++] = mt->p[i];
        seat_send(s, "RESUME3 %d %d %d %d %d %d %d %d %s|%s", ph, myturn, o[0] + 1, o[1] + 1,
                  mt->alive[match_index(mt, o[0])], mt->alive[match_index(mt, o[1])], mt->alive[me],
                  mt->p[mt->turn] + 1, seat_label(o[0]), seat_label(o[1]));
    }
    if (ph >= 2) {
        char f[128];
        fleet_format(&seats[s].board, f, sizeof f);
        seat_send(s, "MYFLEET %s", f);
    }
    char buf[LINE_MAX_LEN];
    size_t pos = 0;
    buf[0] = 0;
    for (int r = 0; r < BOARD_N; r++)
        for (int c = 0; c < BOARD_N; c++)
            if (seats[s].board.shot[r][c])
                pos += (size_t)snprintf(buf + pos, sizeof buf - pos, "%d,%d;", r, c);
    seat_send(s, "MYSHOTS %s", buf);
    for (int i = 0; i < mt->n; i++) {
        int o = mt->p[i];
        if (o == s) continue;
        /* only the shots this player fired at o: we do not track per-shooter, so send the whole shot map
         * (in 3-player games it includes the third player's shots; the client shows them as such) */
        pos = 0;
        buf[0] = 0;
        for (int r = 0; r < BOARD_N; r++)
            for (int c = 0; c < BOARD_N; c++) {
                int sh = seats[o].board.shot[r][c];
                if (!sh) continue;
                /* hidden misses: a miss shows only if this player fired it */
                if (mt->n == 3 && sh == 1 && !show_misses && !mt->fired[me][i][r][c]) continue;
                pos += (size_t)snprintf(buf + pos, sizeof buf - pos, "%d,%d,%d;", r, c, sh);
            }
        seat_send(s, "ENEMYSHOTS %d %s", o + 1, buf);
        pos = 0;
        buf[0] = 0;
        for (int i2 = 0; i2 < NSHIPS; i2++)
            if (board_ship_sunk(&seats[o].board, i2)) pos += (size_t)snprintf(buf + pos, sizeof buf - pos, "%d;", i2);
        seat_send(s, "ENEMYSUNK %d %s", o + 1, buf);
    }
    seat_send(s, "STAT %d %d", seats[s].g_hits, seats[s].g_shots);
}

/* Connection lost: inside a match the seat is kept for `grace` seconds so the same client can come back. */
static void seat_disconnect(int s, const char *why)
{
    if (!seat_match(s) || grace <= 0) { seat_drop(s, why); return; }
    if (seats[s].fd >= 0) close(seats[s].fd);
    seats[s].fd = -1;
    seats[s].lb.len = 0;
    seats[s].gone = 1;
    seats[s].gone_since = time(NULL);
    logf_(tr(S_LOG_GONE), s + 1, seat_label(s), grace);
    match_send_others(s, "OPPWAIT %d", s + 1);
    dirty = 1;
}

static void seat_resume(int s, int fd, const LineBuf *lb_in)
{
    seats[s].fd = fd;
    seats[s].lb = *lb_in;
    seats[s].gone = 0;
    seat_send(s, "WELCOME %d %d", s + 1, nseats);
    if (seats[s].name[0]) seat_send(s, "NAMEOK %s", seats[s].name);
    lb_send_fd(fd);
    sb_send_fd(fd);
    if (seat_match(s)) { seat_send_resume(s); match_send_others(s, "OPPBACK %d", s + 1); }
    else seat_send(s, "LOBBY");
    logf_(tr(S_LOG_RESUMED), s + 1, seat_label(s));
    dirty = 1;
}

static void match_try_start(Match *mt);
static void match_check_winner(Match *mt);

static void seat_drop(int s, const char *why)
{
    if (seats[s].state == S_EMPTY) return;
    seats[s].gone = 0;   /* before match_end, which drops gone seats itself */
    Match *mt = seat_match(s);
    logf_(tr(S_LOG_LEFT), s + 1, seat_label(s), why);
    if (mt) {
        int me = match_index(mt, s);
        int others_alive = 0;
        for (int i = 0; i < mt->n; i++) if (i != me && mt->p[i] >= 0 && mt->alive[i]) others_alive++;
        if (mt->n == 3 && mt->started && others_alive >= 2 && mt->alive[me]) {
            /* three-player game in progress: the leaver is out, the other two play on */
            mt->alive[me] = 0;
            mt->p[me] = -1;
            seats[s].match = -1;
            match_send_others(s, "OPPLEFT %d", s + 1);
            for (int i = 0; i < mt->n; i++) if (mt->p[i] >= 0) seat_send(mt->p[i], "OUT %d", s + 1);
            if (mt->turn == me) {
                do mt->turn = (mt->turn + 1) % mt->n; while (mt->p[mt->turn] < 0 || !mt->alive[mt->turn]);
                for (int i = 0; i < mt->n; i++)
                    if (mt->p[i] >= 0) seat_send(mt->p[i], "TURN %d %d", i == mt->turn, mt->p[mt->turn] + 1);
            }
            match_check_winner(mt);
        } else {
            int m = (int)(mt - matches);
            match_send_others(s, "OPPLEFT %d", s + 1);
            match_end(m);
            for (int i = 0; i < mt->n; i++) if (mt->p[i] >= 0 && mt->p[i] != s) seat_send(mt->p[i], "LOBBY");
        }
    }
    if (seats[s].fd >= 0) close(seats[s].fd);
    seats[s].fd    = -1;
    seats[s].state = S_EMPTY;
    seats[s].match = -1;
    seats[s].name[0] = 0;
    seats[s].client[0] = 0;
    seats[s].lb.len = 0;
    for (int i = 0; i < MAX_P; i++) if (marked[i] == s) marked[i] = -1;
    dirty = 1;
}

static void seat_kick(int s)
{
    if (seats[s].state == S_EMPTY) return;
    seat_send(s, "ERR kicked by admin");
    seat_drop(s, tr(S_WHY_KICKED));
}

static void match_abort(int m)
{
    if (!matches[m].active) return;
    Match *mt = &matches[m];
    char str[32];
    match_seats_str(mt, str, sizeof str);
    logf_(tr(S_LOG_ABORTED), str);
    int players[MAX_P], n = mt->n;
    memcpy(players, mt->p, sizeof players);
    match_end(m);
    for (int i = 0; i < n; i++) if (players[i] >= 0) { seat_send(players[i], "ABORT"); seat_send(players[i], "LOBBY"); }
}

/* Same players, new game. The loser (or a random player) opens. Players stay together until the admin changes it. */
static void match_rematch(Match *mt, int opener_idx)
{
    /* a player who left a three-player game mid-way is gone: shrink to the remaining players */
    int keep[MAX_P], n = 0;
    for (int i = 0; i < mt->n; i++) if (mt->p[i] >= 0) keep[n++] = mt->p[i];
    if (n < 2) { match_end((int)(mt - matches)); return; }
    int opener_seat = opener_idx >= 0 ? mt->p[opener_idx] : -1;
    mt->n = n;
    for (int i = 0; i < n; i++) mt->p[i] = keep[i];
    int op = 0;
    for (int i = 0; i < n; i++) if (mt->p[i] == opener_seat) op = i;
    match_new_game(mt, opener_seat >= 0 ? op : rand() % n);
    char str[32];
    match_seats_str(mt, str, sizeof str);
    logf_(tr(S_LOG_REMATCH), str);
    dirty = 1;
}

/* Admin restarts the current round: same players, boards cleared, nobody scores. */
static void match_restart(int m)
{
    Match *mt = &matches[m];
    if (!mt->active) return;
    for (int i = 0; i < mt->n; i++) if (mt->p[i] >= 0) seat_send(mt->p[i], "RESTART");
    match_rematch(mt, -1);
    char str[32];
    match_seats_str(mt, str, sizeof str);
    logf_(tr(S_LOG_RESTART), str);
}

static void match_try_start(Match *mt)
{
    if (!mt->active || mt->started || mt->hold) return;
    for (int i = 0; i < mt->n; i++) if (seats[mt->p[i]].state != S_READY) return;
    mt->started = 1;
    for (int i = 0; i < mt->n; i++) {
        seats[mt->p[i]].state = S_PLAYING;
        seat_send(mt->p[i], "START %d %d", i == mt->turn, mt->p[mt->turn] + 1);
    }
    char str[32];
    match_seats_str(mt, str, sizeof str);
    logf_(tr(S_LOG_STARTED), str, mt->p[mt->turn] + 1);
    dirty = 1;
}

/* one fleet left afloat: game over */
static void match_check_winner(Match *mt)
{
    int alive = 0, w = -1;
    for (int i = 0; i < mt->n; i++) if (mt->p[i] >= 0 && mt->alive[i]) { alive++; w = i; }
    if (alive > 1 || w < 0) return;
    int ws = mt->p[w];
    seats[ws].wins++;
    for (int i = 0; i < mt->n; i++) if (i != w && mt->p[i] >= 0) seats[mt->p[i]].losses++;
    char names[LINE_MAX_LEN];
    match_names_str(mt, names, sizeof names);
    logf_(tr(S_LOG_WON), seat_label(ws), names, mt->shots);
    for (int i = 0; i < mt->n; i++)
        if (mt->p[i] >= 0) seat_send(mt->p[i], "GAMEOVER %s %s", i == w ? "WIN" : "LOSE", seat_label(ws));
    {
        char ev[LINE_MAX_LEN];
        snprintf(ev, sizeof ev, tr(S_EV_WIN), seat_label(ws), names, mt->shots);
        viewers_sendf("EV %s", ev);
    }
    lb_record(mt, w);
    /* next game: in 2-player the loser opens, in 3-player a random player */
    int opener = -1;
    if (mt->n == 2) for (int i = 0; i < mt->n; i++) if (i != w) opener = i;
    if (paused) {
        mt->hold = 1;
        mt->hold_opener = opener;
        mt->started = 0;
        for (int i = 0; i < mt->n; i++)
            if (mt->p[i] >= 0) { seats[mt->p[i]].state = S_PLACING; seat_send(mt->p[i], "HOLD"); }
        return;
    }
    match_rematch(mt, opener);
}

static void handle_fire(int s, int r, int c, int target_seat)
{
    Match *mt = seat_match(s);
    if (!mt || !mt->started) { seat_send(s, "ERR no game"); return; }
    int me = match_index(mt, s);
    if (mt->turn != me) { seat_send(s, "ERR not your turn"); return; }
    if (!mt->alive[me]) { seat_send(s, "ERR you are out"); return; }
    /* pick the target: the only opponent in 2-player, else the requested seat */
    int t = -1;
    if (mt->n == 2) t = me == 0 ? 1 : 0;
    else t = match_index(mt, target_seat - 1);
    if (t < 0 || t == me || mt->p[t] < 0 || !mt->alive[t]) { seat_send(s, "ERR bad target"); return; }
    int o = mt->p[t];
    int sunk = -1;
    int res;
    if (mt->n == 3) {
        if (r < 0 || r >= BOARD_N || c < 0 || c >= BOARD_N || mt->fired[me][t][r][c]) { seat_send(s, "ERR invalid shot"); return; }
        int prior = seats[o].board.shot[r][c];
        if (prior == 2) { seat_send(s, "ERR invalid shot"); return; }   /* hits are visible to all */
        if (prior == 1) {
            /* the other attacker already missed here: a legal shot, and a miss for this player too */
            mt->fired[me][t][r][c] = 1;
            mt->shots++;
            seats[s].g_shots++;
            seat_send(s, "SHOT %d %d %d %d %d", r, c, FIRE_MISS, -1, o + 1);
            seat_send(s, "STAT %d %d", seats[s].g_hits, seats[s].g_shots);
            sess_add(seat_label(s), 0);
            sb_broadcast();
            goto next_turn;
        }
    }
    res = board_fire(&seats[o].board, r, c, &sunk);
    if (res == FIRE_INVALID) { seat_send(s, "ERR invalid shot"); return; }
    if (mt->n == 3) mt->fired[me][t][r][c] = 1;
    mt->shots++;
    seats[s].g_shots++;
    if (res != FIRE_MISS) seats[s].g_hits++;
    seat_send(s, "SHOT %d %d %d %d %d", r, c, res, sunk, o + 1);
    seat_send(o, "INCOMING %d %d %d %d %d", r, c, res, sunk, s + 1);
    /* three players share one board per target: the third player sees hits (and misses when the admin shows them) */
    if (mt->n == 3 && (res != FIRE_MISS || show_misses))
        for (int i = 0; i < mt->n; i++)
            if (mt->p[i] >= 0 && mt->p[i] != s && mt->p[i] != o)
                seat_send(mt->p[i], "OSHOT %d %d %d %d %d %d", o + 1, r, c, res, sunk, s + 1);
    seat_send(s, "STAT %d %d", seats[s].g_hits, seats[s].g_shots);
    sess_add(seat_label(s), res != FIRE_MISS);
    sb_broadcast();
    if (board_all_sunk(&seats[o].board)) {
        mt->alive[t] = 0;
        if (mt->n == 3) {
            for (int i = 0; i < mt->n; i++) if (mt->p[i] >= 0) seat_send(mt->p[i], "OUT %d", o + 1);
            logf_(tr(S_LOG_OUT), o + 1, seat_label(o));
        }
        match_check_winner(mt);
        if (!mt->started) return;   /* game over, rematch set up */
    }
next_turn:
    /* next alive player */
    do mt->turn = (mt->turn + 1) % mt->n; while (mt->p[mt->turn] < 0 || !mt->alive[mt->turn]);
    for (int i = 0; i < mt->n; i++)
        if (mt->p[i] >= 0) seat_send(mt->p[i], "TURN %d %d", i == mt->turn, mt->p[mt->turn] + 1);
    dirty = 1;
}

static void handle_seat_line(int s, char *line)
{
    char cmd[32];
    int n = 0;
    if (sscanf(line, "%31s%n", cmd, &n) != 1) return;
    char *arg = line + n;
    while (*arg == ' ') arg++;

    if (strcmp(cmd, "FIRE") == 0) {
        int r, c, t = 0;
        if (sscanf(arg, "%d %d %d", &r, &c, &t) >= 2) handle_fire(s, r, c, t);
        else seat_send(s, "ERR bad FIRE");
    } else if (strcmp(cmd, "FLEET") == 0) {
        if (seats[s].state != S_PLACING) { seat_send(s, "ERR not placing"); return; }
        Board b;
        if (!fleet_parse(&b, arg)) { seat_send(s, "ERR invalid fleet"); return; }
        seats[s].board = b;
        seats[s].state = S_READY;
        seat_send(s, "FLEETOK");
        logf_(tr(S_LOG_FLEET_READY), s + 1, seat_label(s));
        Match *mt = seat_match(s);
        if (mt) match_try_start(mt);
    } else if (strcmp(cmd, "NAME") == 0) {
        if (seats[s].state == S_PLAYING || seats[s].state == S_READY) { seat_send(s, "ERR not now"); return; }
        seat_set_name(s, arg);
    } else if (strcmp(cmd, "BYE") == 0) {
        seat_drop(s, tr(S_WHY_BYE));
    } else if (strcmp(cmd, "PING") == 0) {
        seat_send(s, "PONG");
    } else {
        seat_send(s, "ERR unknown command");
    }
}

/* ---- Pending connections (before HELLO) --------------------------------- */

static void pending_remove(int i)
{
    pending[i] = pending[--npending];
}

/* HELLO <seat|0> <client id>  or  WATCH <id> */
static void handle_pending_line(int i, char *line)
{
    char cmd[32], rawid[64] = "";
    int want = 0;
    if (sscanf(line, "%31s", cmd) == 1 && strcmp(cmd, "WATCH") == 0) {
        int fd = pending[i].fd;
        LineBuf lb_copy = pending[i].lb;
        pending_remove(i);
        viewer_add(fd, &lb_copy);
        return;
    }
    if (sscanf(line, "%31s %d %63s", cmd, &want, rawid) < 2 || strcmp(cmd, "HELLO") != 0) {
        net_sendf(pending[i].fd, "ERR expected HELLO");
        close(pending[i].fd);
        pending_remove(i);
        return;
    }
    char id[CLIENT_ID_MAX + 1];
    sanitize_id(rawid, id, sizeof id);
    int s = -1;
    /* same client id = same machine: a player coming back, whether or not we noticed the old connection die */
    for (int k = 0; k < nseats; k++)
        if (seats[k].state != S_EMPTY && strcmp(seats[k].client, id) == 0) {
            int fd = pending[i].fd;
            LineBuf lb_copy = pending[i].lb;
            pending_remove(i);
            if (!seats[k].gone && seats[k].fd >= 0) {
                net_sendf(seats[k].fd, "ERR taken over: another client connected with the same client id (%s)", id);
                close(seats[k].fd);
                seats[k].fd = -1;
                logf_(tr(S_LOG_REPLACED), k + 1, seat_label(k));
            }
            seat_resume(k, fd, &lb_copy);
            return;
        }
    if (want > 0) {
        if (want > nseats) { net_sendf(pending[i].fd, "ERR no such seat (1-%d)", nseats); goto reject; }
        if (seats[want - 1].state != S_EMPTY) { net_sendf(pending[i].fd, "ERR seat %d is taken", want); goto reject; }
        s = want - 1;
    } else {
        for (int k = 0; k < nseats; k++) if (seats[k].state == S_EMPTY) { s = k; break; }
        if (s < 0) { net_sendf(pending[i].fd, "ERR server full"); goto reject; }
    }
    seats[s].fd    = pending[i].fd;
    seats[s].lb    = pending[i].lb;
    seats[s].state = S_LOBBY;
    seats[s].match = -1;
    seats[s].connected_at = time(NULL);
    seats[s].name[0] = 0;
    strcpy(seats[s].client, id);
    board_init(&seats[s].board);
    pending_remove(i);
    seat_send(s, "WELCOME %d %d", s + 1, nseats);
    seat_send(s, "LOBBY");
    lb_send_fd(seats[s].fd);
    sb_send_fd(seats[s].fd);
    logf_(tr(S_LOG_SEAT_TAKEN), s + 1, id);
    dirty = 1;
    return;
reject:
    close(pending[i].fd);
    pending_remove(i);
}

static void accept_client(void)
{
    int fd = accept(listen_fd, NULL, NULL);
    if (fd < 0) return;
    int one = 1, idle = 10, intvl = 5, cnt = 4;
    setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &one, sizeof one);
    setsockopt(fd, IPPROTO_TCP, TCP_KEEPIDLE, &idle, sizeof idle);
    setsockopt(fd, IPPROTO_TCP, TCP_KEEPINTVL, &intvl, sizeof intvl);
    setsockopt(fd, IPPROTO_TCP, TCP_KEEPCNT, &cnt, sizeof cnt);
    if (npending >= MAX_PENDING) {
        net_sendf(fd, "ERR server busy");
        close(fd);
        return;
    }
    pending[npending++] = (Pending){ .fd = fd, .since = time(NULL) };
}

static void set_paused(int on)
{
    paused = on;
    logf_("%s", tr(on ? S_LOG_PAUSE_ON : S_LOG_PAUSE_OFF));
    if (!on)
        for (int m = 0; m < (int)(sizeof matches / sizeof matches[0]); m++)
            if (matches[m].active && matches[m].hold) match_rematch(&matches[m], matches[m].hold_opener);
    dirty = 1;
}

/* ---- Auto pairing (optional) -------------------------------------------- */

static void do_autopair(void)
{
    if (paused) return;
    int group[MAX_P], n = 0;
    for (int s = 0; s < nseats; s++) {
        if (seats[s].state != S_LOBBY) continue;
        group[n++] = s;
        if (n == autopair) { match_create(group, n); n = 0; }
    }
}

/* ---- Admin console ------------------------------------------------------- */

#define SEAT_TABLE_W 64

static int is_marked(int s)
{
    for (int i = 0; i < MAX_P; i++) if (marked[i] == s) return 1;
    return 0;
}

static void draw_leaderboard(int y, int x, int maxrows)
{
    attron(A_BOLD | (focus == F_LB ? A_UNDERLINE : 0));
    mvprintw(y, x, "%s", tr(S_SRV_LB));
    attroff(A_BOLD | A_UNDERLINE);
    if (nlb == 0) { mvprintw(y + 1, x, "%s", tr(S_SRV_NONE)); return; }
    int first = 0;
    if (focus == F_LB && lb_cursor >= maxrows) first = lb_cursor - maxrows + 1;
    for (int i = first; i < nlb && i - first < maxrows; i++) {
        char name[NAME_MAX * 2];
        pad_utf8(name, sizeof name, lb[i].name, 16);
        int attr = (focus == F_LB && i == lb_cursor) ? A_REVERSE : 0;
        attron(attr);
        mvprintw(y + 1 + i - first, x, tr(S_SRV_LB_ROW), i + 1, name, lb[i].wins, lb[i].losses, lb[i].shots ? lb[i].hits * 100 / lb[i].shots : 0);
        attroff(attr);
    }
}

static void console_draw(void)
{
    erase();
    int rows, cols;
    getmaxyx(stdscr, rows, cols);
    attron(A_BOLD);
    mvprintw(0, 0, tr(S_SRV_TITLE), port, nseats, autopair ? tr(S_SRV_AUTOPAIR) : "");
    attroff(A_BOLD);
    if (paused) {
        attron(A_BOLD | A_REVERSE);
        printw("  %s  ", tr(S_SRV_PAUSED));
        attroff(A_BOLD | A_REVERSE);
    }
    printw("  %s", tr(show_misses ? S_SRV_MISSES_ON : S_SRV_MISSES_OFF));
    attron(A_DIM);
    mvprintw(0, cols - (int)strlen(BUILD_VERSION) - 2, "v%s", BUILD_VERSION);
    attroff(A_DIM);

    /* seat table */
    attron(focus == F_SEATS ? A_UNDERLINE : 0);
    mvprintw(2, 1, "%s", tr(S_SRV_HEADER));
    attroff(A_UNDERLINE);
    int table_rows = nseats;
    for (int s = 0; s < nseats && 4 + s < rows - 7; s++) {
        int y = 3 + s;
        int attr = 0;
        if (s == cursor && focus == F_SEATS) attr |= A_REVERSE;
        if (seats[s].state == S_EMPTY) attr |= A_DIM;
        char mt[16] = "-";
        Match *m = seat_match(s);
        if (m) {
            size_t pos = 0;
            mt[0] = 0;
            for (int i = 0; i < m->n; i++)
                if (m->p[i] >= 0 && m->p[i] != s) pos += (size_t)snprintf(mt + pos, sizeof mt - pos, "%s%d", pos ? "+" : "vs ", m->p[i] + 1);
        }
        char name[NAME_MAX * 2];
        pad_utf8(name, sizeof name, seats[s].state == S_EMPTY ? "" : seats[s].name, 16);
        char st[24];
        if (seats[s].gone) snprintf(st, sizeof st, "%s %lds", tr(S_ST_GONE), (long)(time(NULL) - seats[s].gone_since));
        else snprintf(st, sizeof st, "%s", state_name(seats[s].state));
        attron(attr);
        mvprintw(y, 1, " %2d  %-12s %s %-8s %-7s %2d/%-2d %s ",
                 s + 1, seats[s].state == S_EMPTY ? "" : seats[s].client, name,
                 st, mt, seats[s].wins, seats[s].losses, is_marked(s) ? "*" : " ");
        attroff(attr);
    }

    int logy = rows - 6;

    /* right pane: matches + leaderboard, or below the seat table on narrow screens */
    int mx = SEAT_TABLE_W;
    int wide = cols >= mx + 34;
    int py = wide ? 2 : 3 + table_rows + 1;
    int px = wide ? mx : 1;
    if (wide || py + 4 < logy - 1) {
        mvprintw(py, px, "%s", tr(S_SRV_MATCHES));
        int y = py + 1;
        for (int m = 0; m < (int)(sizeof matches / sizeof matches[0]); m++) {
            if (!matches[m].active) continue;
            Match *mt = &matches[m];
            char str[32];
            match_seats_str(mt, str, sizeof str);
            if (mt->hold)
                mvprintw(y++, px, tr(S_SRV_HELD), str);
            else if (mt->started)
                mvprintw(y++, px, tr(S_SRV_TURN), str, mt->p[mt->turn] + 1, mt->shots);
            else
                mvprintw(y++, px, tr(S_SRV_PLACING), str);
        }
        if (y == py + 1) mvprintw(y++, px, "%s", tr(S_SRV_NONE));
        y++;
        int maxrows = logy - 2 - (y + 1);
        if (maxrows > 0) draw_leaderboard(y, px, maxrows > LB_SHOW ? LB_SHOW : maxrows);
    }

    /* log */
    mvhline(logy - 1, 0, ACS_HLINE, cols);
    int show = 4;
    for (int i = 0; i < show; i++) {
        int idx = logcount - show + i;
        if (idx < 0) continue;
        int k = (loghead - logcount + idx + LOG_LINES) % LOG_LINES;
        mvprintw(logy + i, 1, "%.*s", cols - 2, logbuf[k]);
    }

    if (prompt != PR_NONE) {
        attron(A_BOLD);
        mvprintw(rows - 1, 0, "%s %s_", tr(prompt == PR_RENAME_SEAT ? S_SRV_PROMPT_SEAT : S_SRV_PROMPT_LB), prompt_buf);
        attroff(A_BOLD);
        clrtoeol();
    } else {
        mvprintw(rows - 1, 0, "%.*s", cols - 1, tr(focus == F_SEATS ? S_SRV_KEYS : S_SRV_KEYS_LB));
    }
    refresh();
    dirty = 0;
}

static void prompt_start(int kind, int target, const char *initial)
{
    prompt = kind;
    prompt_target = target;
    strncpy(prompt_buf, initial, NAME_MAX);
    prompt_buf[NAME_MAX] = 0;
}

static void prompt_finish(void)
{
    char clean[NAME_MAX + 1];
    sanitize_name(prompt_buf, clean, sizeof clean);
    if (clean[0]) {
        if (prompt == PR_RENAME_SEAT && prompt_target >= 0 && seats[prompt_target].state != S_EMPTY)
            seat_set_name(prompt_target, clean);
        else if (prompt == PR_RENAME_LB && prompt_target >= 0 && prompt_target < nlb) {
            logf_(tr(S_LOG_LB_RENAMED), lb[prompt_target].name, clean);
            lb_rename(prompt_target, clean);
        }
    }
    prompt = PR_NONE;
}

static void prompt_key(wint_t wch, int is_key)
{
    if (is_key) {
        if (wch == KEY_BACKSPACE) goto backspace;
        return;
    }
    if (wch == 27) { prompt = PR_NONE; return; }
    if (wch == '\n' || wch == '\r') { prompt_finish(); return; }
    if (wch == 127 || wch == 8) goto backspace;
    if (!iswprint(wch)) return;
    {
        char mb[MB_CUR_MAX + 1];
        mbstate_t st;
        memset(&st, 0, sizeof st);
        size_t n = wcrtomb(mb, (wchar_t)wch, &st);
        if (n == (size_t)-1) return;
        size_t len = strlen(prompt_buf);
        if (len + n > NAME_MAX) return;
        memcpy(prompt_buf + len, mb, n);
        prompt_buf[len + n] = 0;
    }
    return;
backspace:
    {
        size_t len = strlen(prompt_buf);
        while (len > 0 && ((unsigned char)prompt_buf[len - 1] & 0xC0) == 0x80) len--;
        if (len > 0) len--;
        prompt_buf[len] = 0;
    }
}

static void console_key(wint_t wch, int is_key)
{
    dirty = 1;
    if (prompt != PR_NONE) { prompt_key(wch, is_key); return; }

    if (is_key) {
        switch (wch) {
        case KEY_UP:
            if (focus == F_SEATS) { if (cursor > 0) cursor--; }
            else if (lb_cursor > 0) lb_cursor--;
            break;
        case KEY_DOWN:
            if (focus == F_SEATS) { if (cursor < nseats - 1) cursor++; }
            else if (lb_cursor < nlb - 1) lb_cursor++;
            break;
        case KEY_BTAB:
            focus = focus == F_SEATS ? F_LB : F_SEATS;
            break;
        }
        return;
    }

    switch (wch) {
    case '\t':
        focus = focus == F_SEATS ? F_LB : F_SEATS;
        break;
    case 'q':
        running = 0;
        break;
    case 'R':
        lb_load();
        lb_broadcast();
        break;
    case 'P':
        set_paused(!paused);
        break;
    case 'm':
        show_misses = !show_misses;
        logf_("%s", tr(show_misses ? S_LOG_MISSES_ON : S_LOG_MISSES_OFF));
        break;
    }

    if (focus == F_SEATS) {
        switch (wch) {
        case 'j': if (cursor < nseats - 1) cursor++; break;
        case 'k': if (cursor > 0) cursor--; break;
        case ' ':
            if (seats[cursor].state != S_LOBBY) { logf_(tr(S_LOG_NOT_LOBBY), cursor + 1); break; }
            if (is_marked(cursor)) {
                for (int i = 0; i < MAX_P; i++) if (marked[i] == cursor) marked[i] = -1;
            } else {
                int placed = 0;
                for (int i = 0; i < MAX_P && !placed; i++) if (marked[i] < 0) { marked[i] = cursor; placed = 1; }
                if (!placed) { marked[0] = marked[1]; marked[1] = marked[2]; marked[2] = cursor; }
            }
            break;
        case 'p': case '\n': case '\r': {
            int players[MAX_P], n = 0;
            for (int i = 0; i < MAX_P; i++) if (marked[i] >= 0) players[n++] = marked[i];
            if (n < 2) { logf_("%s", tr(S_LOG_MARK_TWO)); break; }
            if (match_create(players, n) < 0) logf_("%s", tr(S_LOG_CANT_PAIR));
            for (int i = 0; i < MAX_P; i++) marked[i] = -1;
            break;
        }
        case 'u':
            if (seat_match(cursor)) match_abort(seats[cursor].match);
            else logf_(tr(S_LOG_NOT_IN_MATCH), cursor + 1);
            break;
        case 'r':
            if (seat_match(cursor)) match_restart(seats[cursor].match);
            else logf_(tr(S_LOG_NOT_IN_MATCH), cursor + 1);
            break;
        case 'x':
            seat_kick(cursor);
            break;
        case 'e':
            if (seats[cursor].state == S_EMPTY) break;
            if (seats[cursor].state == S_PLAYING || seats[cursor].state == S_READY) { logf_("%s", tr(S_LOG_NOT_NOW)); break; }
            prompt_start(PR_RENAME_SEAT, cursor, seats[cursor].name);
            break;
        }
    } else {
        switch (wch) {
        case 'j': if (lb_cursor < nlb - 1) lb_cursor++; break;
        case 'k': if (lb_cursor > 0) lb_cursor--; break;
        case 'e': case '\n': case '\r':
            if (nlb) prompt_start(PR_RENAME_LB, lb_cursor, lb[lb_cursor].name);
            break;
        case 'd':
            lb_delete(lb_cursor);
            break;
        }
    }
}

/* ---- Main loop ------------------------------------------------------------ */

static void on_signal(int sig)
{
    (void)sig;
    running = 0;
}

/* SIGUSR1 toggles the pause, so a leader can do it from a script: kill -USR1 <pid> */
static volatile sig_atomic_t toggle_pause_req = 0;
static void on_usr1(int sig)
{
    (void)sig;
    toggle_pause_req = 1;
}

static void usage(void)
{
    fprintf(stderr, tr(S_SRV_USAGE), DEFAULT_PORT, MAX_SEATS, lb_path, 90);
}

int server_main(int argc, char **argv)
{
    for (int i = 0; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) { usage(); return 0; }
        if (strcmp(argv[i], "--headless") == 0) headless = 1;
        else if (strcmp(argv[i], "--paused") == 0) paused = 1;
        else if (strcmp(argv[i], "--show-misses") == 0) show_misses = 1;
        else if (strcmp(argv[i], "--autopair") == 0) {
            autopair = 2;
            if (i + 1 < argc && (strcmp(argv[i + 1], "2") == 0 || strcmp(argv[i + 1], "3") == 0)) autopair = atoi(argv[++i]);
        }
        else if (strcmp(argv[i], "--port") == 0 && i + 1 < argc) port = atoi(argv[++i]);
        else if (strcmp(argv[i], "--seats") == 0 && i + 1 < argc) nseats = atoi(argv[++i]);
        else if (strcmp(argv[i], "--grace") == 0 && i + 1 < argc) grace = atoi(argv[++i]);
        else if (strcmp(argv[i], "--leaderboard") == 0 && i + 1 < argc) { strncpy(lb_path, argv[++i], sizeof lb_path - 1); lb_path[sizeof lb_path - 1] = 0; }
        else if (strcmp(argv[i], "--lang") == 0 && i + 1 < argc) i++; /* handled in main */
        else { usage(); return 2; }
    }
    if (nseats < 2 || nseats > MAX_SEATS) {
        fprintf(stderr, tr(S_SRV_SEATS_RANGE), MAX_SEATS);
        return 2;
    }
    if (!isatty(STDIN_FILENO)) headless = 1;

    srand((unsigned)time(NULL) ^ (unsigned)getpid());
    for (int s = 0; s < MAX_SEATS; s++) { seats[s].fd = -1; seats[s].match = -1; }

    listen_fd = net_listen(port);
    if (listen_fd < 0) {
        perror("listen");
        return 1;
    }
    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);
    signal(SIGUSR1, on_usr1);
    signal(SIGPIPE, SIG_IGN);

    if (!headless) {
        initscr();
        cbreak();
        noecho();
        keypad(stdscr, TRUE);
        nodelay(stdscr, TRUE);
        curs_set(0);
    }
    logf_(tr(S_LOG_LISTENING), port, nseats);
    lb_load();

    struct pollfd pfds[1 + 1 + MAX_SEATS + MAX_PENDING + MAX_VIEWERS];
    while (running) {
        int n = 0;
        pfds[n++] = (struct pollfd){ .fd = listen_fd, .events = POLLIN };
        pfds[n++] = (struct pollfd){ .fd = headless ? -1 : STDIN_FILENO, .events = POLLIN };
        int seat_base = n;
        for (int s = 0; s < nseats; s++)
            pfds[n++] = (struct pollfd){ .fd = seats[s].fd, .events = POLLIN };
        /* only the entries polled in THIS iteration may be serviced below: connections
         * accepted or promoted to viewer mid-iteration have no (valid) revents yet */
        int pend_base = n, np_polled = npending;
        for (int i = 0; i < npending; i++)
            pfds[n++] = (struct pollfd){ .fd = pending[i].fd, .events = POLLIN };
        int view_base = n, nv_polled = nviewers;
        for (int i = 0; i < nviewers; i++)
            pfds[n++] = (struct pollfd){ .fd = viewers[i].fd, .events = POLLIN };

        int rc = poll(pfds, (nfds_t)n, 250);
        if (rc < 0) {
            if (errno == EINTR) continue;
            break;
        }

        if (pfds[0].revents & POLLIN) accept_client();

        if (!headless && (pfds[1].revents & POLLIN)) {
            wint_t wch;
            int r;
            while ((r = get_wch(&wch)) != ERR) {
                if (r == KEY_CODE_YES && wch == KEY_RESIZE) { dirty = 1; continue; }
                console_key(wch, r == KEY_CODE_YES);
            }
        }

        for (int s = 0; s < nseats; s++) {
            if (seats[s].fd < 0 || !(pfds[seat_base + s].revents & (POLLIN | POLLHUP | POLLERR))) continue;
            if (linebuf_feed(&seats[s].lb, seats[s].fd) < 0) { seat_disconnect(s, tr(S_WHY_DISCONNECTED)); continue; }
            char line[LINE_MAX_LEN];
            while (seats[s].fd >= 0 && linebuf_next(&seats[s].lb, line, sizeof line))
                handle_seat_line(s, line);
        }

        /* iterate backwards: handlers may remove entries by swapping with the last */
        for (int i = np_polled - 1; i >= 0; i--) {
            if (i >= npending) continue;
            if (!(pfds[pend_base + i].revents & (POLLIN | POLLHUP | POLLERR))) continue;
            if (linebuf_feed(&pending[i].lb, pending[i].fd) < 0) {
                close(pending[i].fd);
                pending_remove(i);
                continue;
            }
            char line[LINE_MAX_LEN];
            if (linebuf_next(&pending[i].lb, line, sizeof line)) handle_pending_line(i, line);
        }
        /* viewers only ever hang up */
        for (int i = nv_polled - 1; i >= 0; i--) {
            if (i >= nviewers) continue;
            if (!(pfds[view_base + i].revents & (POLLIN | POLLHUP | POLLERR))) continue;
            if (linebuf_feed(&viewers[i].lb, viewers[i].fd) < 0) viewer_remove(i);
            else viewers[i].lb.len = 0;
        }
        /* drop pending connections that never say HELLO */
        time_t now = time(NULL);
        for (int i = npending - 1; i >= 0; i--)
            if (now - pending[i].since > 10) { close(pending[i].fd); pending_remove(i); }
        /* players that did not come back in time */
        for (int s = 0; s < nseats; s++)
            if (seats[s].gone) {
                if (now - seats[s].gone_since > grace) seat_drop(s, tr(S_WHY_TIMEOUT));
                else dirty = 1; /* keep the "away Ns" counter ticking */
            }

        if (toggle_pause_req) { toggle_pause_req = 0; set_paused(!paused); }
        if (autopair) do_autopair();
        if (dirty) {
            viewers_send_matches();
            if (!headless) console_draw();
            dirty = 0;
        }
    }

    for (int s = 0; s < nseats; s++)
        if (seats[s].fd >= 0) { net_sendf(seats[s].fd, "ERR server shutting down"); close(seats[s].fd); }
    for (int i = 0; i < npending; i++) close(pending[i].fd);
    for (int i = 0; i < nviewers; i++) close(viewers[i].fd);
    close(listen_fd);
    if (!headless) endwin();
    return 0;
}
