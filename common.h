#ifndef BATTLESHIP_COMMON_H
#define BATTLESHIP_COMMON_H

#include <stddef.h>

#define DEFAULT_PORT   4242

#ifndef BUILD_VERSION
#define BUILD_VERSION "dev"
#endif
#ifndef BUILD_REV
#define BUILD_REV "nogit"
#endif
#ifndef BUILD_DATE
#define BUILD_DATE "unknown"
#endif
#define VERSION_STRING "battleship " BUILD_VERSION " (" BUILD_REV ", built " BUILD_DATE ")"
#define BOARD_N        10
#define NSHIPS         5
#define NAME_MAX       24   /* bytes of UTF-8 */
#define CLIENT_ID_MAX  15
#define LINE_MAX_LEN   1024

extern const int   SHIP_LEN[NSHIPS];

/* ---- Board ------------------------------------------------------------- */

typedef struct { int r, c, len, horiz; } ShipPos;

typedef struct {
    unsigned char ship[BOARD_N][BOARD_N];  /* 0 = water, else ship index + 1 */
    unsigned char shot[BOARD_N][BOARD_N];  /* 0 = untouched, 1 = miss, 2 = hit */
    ShipPos       ships[NSHIPS];
    int           placed[NSHIPS];
    int           hits[NSHIPS];
} Board;

enum { FIRE_INVALID = -1, FIRE_MISS = 0, FIRE_HIT = 1, FIRE_SUNK = 2 };

void board_init(Board *b);
int  board_can_place(const Board *b, int idx, int r, int c, int horiz);
void board_place(Board *b, int idx, int r, int c, int horiz);
void board_unplace(Board *b, int idx);
int  board_all_placed(const Board *b);
void board_auto_place(Board *b);                       /* places every unplaced ship */
int  board_fire(Board *b, int r, int c, int *sunk_idx); /* FIRE_* */
int  board_all_sunk(const Board *b);
int  board_ship_sunk(const Board *b, int idx);

/* "r,c,h;r,c,h;..." in ship order. Returns 1 when the fleet is valid. */
int  fleet_parse(Board *b, const char *s);
void fleet_format(const Board *b, char *out, size_t n);

/* ---- Networking helpers ------------------------------------------------ */

typedef struct {
    char buf[2048];
    int  len;
} LineBuf;

/* Reads whatever is available on fd. Returns -1 on EOF/error, else bytes read. */
int  linebuf_feed(LineBuf *lb, int fd);
/* Extracts the next complete line (without newline). Returns 1 if a line was produced. */
int  linebuf_next(LineBuf *lb, char *out, size_t n);

/* Sends a newline-terminated formatted message. Returns -1 on error. */
int  net_sendf(int fd, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
/* Connects to host:port. host may be an IPv4 dotted address or "localhost". */
int  net_connect(const char *host, int port);
/* Same, but gives up after timeout_ms (non-blocking connect + poll). */
int  net_connect_timeout(const char *host, int port, int timeout_ms);
int  net_listen(int port);

/* Player name: printable UTF-8, single spaces, trimmed, at most NAME_MAX bytes
 * cut on a character boundary. May come out empty. */
void sanitize_name(const char *in, char *out, size_t n);
/* Client id: keeps [A-Za-z0-9_-], at most CLIENT_ID_MAX. Never empty. */
void sanitize_id(const char *in, char *out, size_t n);
/* Display width of a UTF-8 string (counts code points; fine for Latin text). */
int  utf8_width(const char *s);
/* Case-insensitive compare that also folds Latin-1 letters such as ÅÄÖ/åäö. */
int  utf8_casecmp(const char *a, const char *b);
/* Copies s into out padded with spaces (or truncated) to exactly width columns. */
void pad_utf8(char *out, size_t n, const char *s, int width);

#endif
