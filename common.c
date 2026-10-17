#include "common.h"

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <fcntl.h>
#include <poll.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

const int   SHIP_LEN[NSHIPS]  = { 5, 4, 3, 3, 2 };

/* ---- Board ------------------------------------------------------------- */

void board_init(Board *b)
{
    memset(b, 0, sizeof *b);
}

int board_can_place(const Board *b, int idx, int r, int c, int horiz)
{
    int len = SHIP_LEN[idx];
    for (int i = 0; i < len; i++) {
        int rr = horiz ? r : r + i;
        int cc = horiz ? c + i : c;
        if (rr < 0 || rr >= BOARD_N || cc < 0 || cc >= BOARD_N) return 0;
        /* no other ship on the cell or on any of its 8 neighbours: ships may not touch */
        for (int dr = -1; dr <= 1; dr++)
            for (int dc = -1; dc <= 1; dc++) {
                int nr = rr + dr, nc = cc + dc;
                if (nr < 0 || nr >= BOARD_N || nc < 0 || nc >= BOARD_N) continue;
                if (b->ship[nr][nc] && b->ship[nr][nc] != idx + 1) return 0;
            }
    }
    return 1;
}

void board_unplace(Board *b, int idx)
{
    if (!b->placed[idx]) return;
    ShipPos *s = &b->ships[idx];
    for (int i = 0; i < s->len; i++) {
        int rr = s->horiz ? s->r : s->r + i;
        int cc = s->horiz ? s->c + i : s->c;
        b->ship[rr][cc] = 0;
    }
    b->placed[idx] = 0;
}

void board_place(Board *b, int idx, int r, int c, int horiz)
{
    board_unplace(b, idx);
    int len = SHIP_LEN[idx];
    for (int i = 0; i < len; i++) {
        int rr = horiz ? r : r + i;
        int cc = horiz ? c + i : c;
        b->ship[rr][cc] = (unsigned char)(idx + 1);
    }
    b->ships[idx]  = (ShipPos){ r, c, len, horiz };
    b->placed[idx] = 1;
}

int board_all_placed(const Board *b)
{
    for (int i = 0; i < NSHIPS; i++)
        if (!b->placed[i]) return 0;
    return 1;
}

void board_auto_place(Board *b)
{
    for (int i = 0; i < NSHIPS; i++) {
        if (b->placed[i]) continue;
        for (int tries = 0; tries < 1000; tries++) {
            int horiz = rand() & 1;
            int r = rand() % BOARD_N, c = rand() % BOARD_N;
            if (board_can_place(b, i, r, c, horiz)) {
                board_place(b, i, r, c, horiz);
                break;
            }
        }
    }
}

int board_fire(Board *b, int r, int c, int *sunk_idx)
{
    if (r < 0 || r >= BOARD_N || c < 0 || c >= BOARD_N) return FIRE_INVALID;
    if (b->shot[r][c]) return FIRE_INVALID;
    if (sunk_idx) *sunk_idx = -1;
    if (!b->ship[r][c]) {
        b->shot[r][c] = 1;
        return FIRE_MISS;
    }
    b->shot[r][c] = 2;
    int idx = b->ship[r][c] - 1;
    b->hits[idx]++;
    if (b->hits[idx] >= SHIP_LEN[idx]) {
        if (sunk_idx) *sunk_idx = idx;
        return FIRE_SUNK;
    }
    return FIRE_HIT;
}

int board_ship_sunk(const Board *b, int idx)
{
    return b->hits[idx] >= SHIP_LEN[idx];
}

int board_all_sunk(const Board *b)
{
    for (int i = 0; i < NSHIPS; i++)
        if (!board_ship_sunk(b, i)) return 0;
    return 1;
}

int fleet_parse(Board *b, const char *s)
{
    board_init(b);
    for (int i = 0; i < NSHIPS; i++) {
        int r, c, h, n = 0;
        if (sscanf(s, "%d,%d,%d%n", &r, &c, &h, &n) != 3) return 0;
        if (!board_can_place(b, i, r, c, h != 0)) return 0;
        board_place(b, i, r, c, h != 0);
        s += n;
        if (*s == ';') s++;
    }
    return 1;
}

void fleet_format(const Board *b, char *out, size_t n)
{
    size_t pos = 0;
    out[0] = 0;
    for (int i = 0; i < NSHIPS; i++) {
        int w = snprintf(out + pos, n - pos, "%s%d,%d,%d", i ? ";" : "",
                         b->ships[i].r, b->ships[i].c, b->ships[i].horiz);
        if (w < 0 || (size_t)w >= n - pos) break;
        pos += (size_t)w;
    }
}

/* ---- Networking helpers ------------------------------------------------ */

int linebuf_feed(LineBuf *lb, int fd)
{
    if (lb->len >= (int)sizeof lb->buf - 1) lb->len = 0; /* overlong garbage: drop it */
    ssize_t n = read(fd, lb->buf + lb->len, sizeof lb->buf - 1 - (size_t)lb->len);
    if (n <= 0) {
        if (n < 0 && (errno == EAGAIN || errno == EINTR)) return 0;
        return -1;
    }
    lb->len += (int)n;
    return (int)n;
}

int linebuf_next(LineBuf *lb, char *out, size_t n)
{
    char *nl = memchr(lb->buf, '\n', (size_t)lb->len);
    if (!nl) return 0;
    size_t linelen = (size_t)(nl - lb->buf);
    size_t copy = linelen < n - 1 ? linelen : n - 1;
    memcpy(out, lb->buf, copy);
    out[copy] = 0;
    if (copy && out[copy - 1] == '\r') out[copy - 1] = 0;
    size_t rest = (size_t)lb->len - linelen - 1;
    memmove(lb->buf, nl + 1, rest);
    lb->len = (int)rest;
    return 1;
}

int net_sendf(int fd, const char *fmt, ...)
{
    char msg[LINE_MAX_LEN];
    va_list ap;
    va_start(ap, fmt);
    int len = vsnprintf(msg, sizeof msg - 1, fmt, ap);
    va_end(ap);
    if (len < 0) return -1;
    if (len > (int)sizeof msg - 2) len = (int)sizeof msg - 2;
    msg[len++] = '\n';
    int off = 0;
    while (off < len) {
        ssize_t w = send(fd, msg + off, (size_t)(len - off), MSG_NOSIGNAL);
        if (w < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        off += (int)w;
    }
    return 0;
}

int net_connect(const char *host, int port)
{
    struct sockaddr_in sa;
    memset(&sa, 0, sizeof sa);
    sa.sin_family = AF_INET;
    sa.sin_port   = htons((unsigned short)port);
    if (strcmp(host, "localhost") == 0) host = "127.0.0.1";
    if (inet_pton(AF_INET, host, &sa.sin_addr) != 1) {
        errno = EINVAL;
        return -1;
    }
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;
    if (connect(fd, (struct sockaddr *)&sa, sizeof sa) < 0) {
        int e = errno;
        close(fd);
        errno = e;
        return -1;
    }
    int one = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);
    return fd;
}

int net_connect_timeout(const char *host, int port, int timeout_ms)
{
    struct sockaddr_in sa;
    memset(&sa, 0, sizeof sa);
    sa.sin_family = AF_INET;
    sa.sin_port   = htons((unsigned short)port);
    if (strcmp(host, "localhost") == 0) host = "127.0.0.1";
    if (inet_pton(AF_INET, host, &sa.sin_addr) != 1) { errno = EINVAL; return -1; }
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;
    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    int rc = connect(fd, (struct sockaddr *)&sa, sizeof sa);
    if (rc < 0 && errno != EINPROGRESS) { int e = errno; close(fd); errno = e; return -1; }
    if (rc < 0) {
        struct pollfd p = { .fd = fd, .events = POLLOUT };
        if (poll(&p, 1, timeout_ms) <= 0) { close(fd); errno = ETIMEDOUT; return -1; }
        int err = 0;
        socklen_t len = sizeof err;
        if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &len) < 0 || err) { close(fd); errno = err ? err : ECONNREFUSED; return -1; }
    }
    fcntl(fd, F_SETFL, flags);
    int one = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);
    return fd;
}

int net_listen(int port)
{
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;
    int one = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
    struct sockaddr_in sa;
    memset(&sa, 0, sizeof sa);
    sa.sin_family      = AF_INET;
    sa.sin_addr.s_addr = htonl(INADDR_ANY);
    sa.sin_port        = htons((unsigned short)port);
    if (bind(fd, (struct sockaddr *)&sa, sizeof sa) < 0 || listen(fd, 16) < 0) {
        int e = errno;
        close(fd);
        errno = e;
        return -1;
    }
    return fd;
}

void sanitize_name(const char *in, char *out, size_t n)
{
    size_t j = 0;
    int last_space = 1; /* also strips leading spaces */
    for (size_t i = 0; in[i] && j < n - 1; i++) {
        unsigned char ch = (unsigned char)in[i];
        if (ch == '\t') ch = ' ';
        if (ch == '|') ch = '/';   /* '|' separates names in viewer messages */
        if (ch < 0x20 || ch == 0x7f) continue;
        if (ch == ' ') {
            if (last_space) continue;
            last_space = 1;
        } else last_space = 0;
        if (j >= NAME_MAX || j >= n - 1) break;
        out[j++] = (char)ch;
    }
    /* if truncation cut a multi-byte character in half, drop the fragment */
    size_t start = j;
    while (start > 0 && ((unsigned char)out[start - 1] & 0xC0) == 0x80) start--;
    if (start > 0) {
        unsigned char lead = (unsigned char)out[start - 1];
        size_t need = lead >= 0xF0 ? 4 : lead >= 0xE0 ? 3 : lead >= 0xC0 ? 2 : 1;
        if (j - (start - 1) < need) j = start - 1;
    }
    while (j > 0 && out[j - 1] == ' ') j--;
    out[j] = 0;
}

void sanitize_id(const char *in, char *out, size_t n)
{
    size_t j = 0;
    for (size_t i = 0; in[i] && j < n - 1 && j < CLIENT_ID_MAX; i++) {
        char ch = in[i];
        if ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
            (ch >= '0' && ch <= '9') || ch == '_' || ch == '-')
            out[j++] = ch;
    }
    if (j == 0) { strncpy(out, "client", n - 1); out[n - 1] = 0; return; }
    out[j] = 0;
}

int utf8_width(const char *s)
{
    int w = 0;
    for (; *s; s++)
        if (((unsigned char)*s & 0xC0) != 0x80) w++;
    return w;
}

/* Decodes one UTF-8 code point, folded to lower case for ASCII and U+00C0..U+00DE. */
static unsigned fold_next(const char **p)
{
    const unsigned char *s = (const unsigned char *)*p;
    unsigned cp;
    int len;
    if (s[0] < 0x80)      { cp = s[0]; len = 1; }
    else if (s[0] < 0xE0 && (s[1] & 0xC0) == 0x80) { cp = ((s[0] & 0x1Fu) << 6) | (s[1] & 0x3Fu); len = 2; }
    else if (s[0] < 0xF0 && (s[1] & 0xC0) == 0x80 && (s[2] & 0xC0) == 0x80) {
        cp = ((s[0] & 0x0Fu) << 12) | ((s[1] & 0x3Fu) << 6) | (s[2] & 0x3Fu); len = 3;
    } else { cp = s[0]; len = 1; }
    *p += len;
    if (cp >= 'A' && cp <= 'Z') cp += 32;
    else if (cp >= 0xC0 && cp <= 0xDE && cp != 0xD7) cp += 32;
    return cp;
}

int utf8_casecmp(const char *a, const char *b)
{
    for (;;) {
        unsigned ca = fold_next(&a), cb = fold_next(&b);
        if (ca != cb) return ca < cb ? -1 : 1;
        if (!ca) return 0;
    }
}

void pad_utf8(char *out, size_t n, const char *s, int width)
{
    size_t j = 0;
    int w = 0;
    for (; *s && j < n - 1; s++) {
        int cont = ((unsigned char)*s & 0xC0) == 0x80;
        if (!cont) {
            if (w >= width) break;
            w++;
        }
        out[j++] = *s;
    }
    while (w < width && j < n - 1) { out[j++] = ' '; w++; }
    out[j] = 0;
}
