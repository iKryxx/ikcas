#include "app/lineedit.h"

#include <ctype.h>
#include <errno.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

#define CTRL_KEY(c) ((c) & 0x1f)

// How long to wait for the rest of an escape sequence before taking ESC as a key of its own.
#define ESC_TIMEOUT_MS 50

/** Keys that are not a single byte. Plain bytes are returned as their value. */
enum {
    KEY_NONE = 256, // unrecognized sequence, ignored
    KEY_UP,
    KEY_DOWN,
    KEY_LEFT,
    KEY_RIGHT,
    KEY_WORD_LEFT,
    KEY_WORD_RIGHT,
    KEY_HOME,
    KEY_END,
    KEY_DELETE,
};

/** State of the line being edited. Positions are byte offsets into buf. */
typedef struct {
    const cas_t *cas;
    const char *prompt;
    size_t prompt_cols;
    char *buf;
    size_t cap;
    size_t len;
    size_t pos;
    char *draft;     // the line as typed, kept while a history entry is shown
    size_t hist_pos; // 0 = the typed line, n = the n-th newest history entry
    size_t row;      // screen row (0 = first) the cursor was left on by refresh
} editor_t;

/** True if the terminal can be driven with escapes. */
int lineedit_available(void)
{
    const char *term = getenv("TERM");
    return isatty(STDIN_FILENO) && isatty(STDOUT_FILENO) && term &&
           strcmp(term, "dumb") != 0;
}

/** Reads one byte. Returns -1 on end of input or error, -2 if timeout_ms (when >= 0) runs out first. */
static int read_byte(int timeout_ms)
{
    if (timeout_ms >= 0) {
        struct pollfd p = { STDIN_FILENO, POLLIN, 0 };
        int r;
        do
            r = poll(&p, 1, timeout_ms);
        while (r < 0 && errno == EINTR);
        if (r < 0)
            return -1;
        if (r == 0)
            return -2;
    }

    unsigned char c;
    ssize_t n;
    do
        n = read(STDIN_FILENO, &c, 1);
    while (n < 0 && errno == EINTR);
    return n == 1 ? c : -1;
}

/** True if more input is already waiting, so a redraw can be put off (pasted text). */
static int input_pending(void)
{
    struct pollfd p = { STDIN_FILENO, POLLIN, 0 };
    return poll(&p, 1, 0) > 0;
}

/** Reads one key, decoding the common escape sequences. Returns -1 on end of input. */
static int read_key(void)
{
    int c = read_byte(-1);
    if (c != 0x1b)
        return c;

    int c1 = read_byte(ESC_TIMEOUT_MS);
    if (c1 == 'b')
        return KEY_WORD_LEFT; // Alt-b / Alt-f
    if (c1 == 'f')
        return KEY_WORD_RIGHT;

    if (c1 == 'O') { // application mode cursor keys
        switch (read_byte(ESC_TIMEOUT_MS)) {
        case 'A': return KEY_UP;
        case 'B': return KEY_DOWN;
        case 'C': return KEY_RIGHT;
        case 'D': return KEY_LEFT;
        case 'H': return KEY_HOME;
        case 'F': return KEY_END;
        }
        return KEY_NONE;
    }
    if (c1 != '[')
        return KEY_NONE;

    // CSI: parameter bytes, then one final byte.
    char params[8];
    size_t np = 0;
    int f;
    while ((f = read_byte(ESC_TIMEOUT_MS)) >= 0x20 && f < 0x40) {
        if (np < sizeof(params) - 1)
            params[np++] = (char)f;
    }
    params[np] = '\0';
    int word = strstr(params, ";5") || strstr(params, ";3"); // Ctrl- or Alt-arrow

    switch (f) {
    case 'A': return KEY_UP;
    case 'B': return KEY_DOWN;
    case 'C': return word ? KEY_WORD_RIGHT : KEY_RIGHT;
    case 'D': return word ? KEY_WORD_LEFT : KEY_LEFT;
    case 'H': return KEY_HOME;
    case 'F': return KEY_END;
    case '~':
        switch (atoi(params)) {
        case 1: case 7: return KEY_HOME;
        case 4: case 8: return KEY_END;
        case 3: return KEY_DELETE;
        }
    }
    return KEY_NONE;
}

/** True for the continuation bytes of a UTF-8 sequence. */
static int is_cont(char c)
{
    return ((unsigned char)c & 0xc0) == 0x80;
}

/** Offset of the character before pos. */
static size_t prev_char(const char *s, size_t pos)
{
    if (pos > 0)
        pos--;
    while (pos > 0 && is_cont(s[pos]))
        pos--;
    return pos;
}

/** Offset of the character after pos. */
static size_t next_char(const char *s, size_t len, size_t pos)
{
    if (pos < len)
        pos++;
    while (pos < len && is_cont(s[pos]))
        pos++;
    return pos;
}

/** Screen columns taken by the first n bytes of s, counting one per UTF-8 character (wide glyphs are not handled). */
static size_t cols_of(const char *s, size_t n)
{
    size_t cols = 0;
    for (size_t i = 0; i < n; i++)
        cols += !is_cont(s[i]);
    return cols;
}

/** Characters that make up a word for Ctrl-W and word movement. */
static int is_word(char c)
{
    return isalnum((unsigned char)c) || c == '_' || (unsigned char)c >= 0x80;
}

/** Start of the word before the cursor. */
static size_t word_left(const editor_t *e)
{
    size_t p = e->pos;
    while (p > 0 && !is_word(e->buf[p - 1]))
        p--;
    while (p > 0 && is_word(e->buf[p - 1]))
        p--;
    return p;
}

/** End of the word after the cursor. */
static size_t word_right(const editor_t *e)
{
    size_t p = e->pos;
    while (p < e->len && !is_word(e->buf[p]))
        p++;
    while (p < e->len && is_word(e->buf[p]))
        p++;
    return p;
}

/** Terminal width in columns, 80 if unknown. */
static size_t term_cols(void)
{
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0)
        return ws.ws_col;
    return 80;
}

/**
 * Redraws prompt and line, and puts the cursor where it belongs. Lines wider
 * than the terminal wrap, so the cursor first goes back up to the line's first
 * row, then everything below it is cleared and written again.
 */
static void refresh(editor_t *e)
{
    size_t cols = term_cols();
    size_t total = e->prompt_cols + cols_of(e->buf, e->len);
    size_t cursor = e->prompt_cols + cols_of(e->buf, e->pos);

    if (e->row > 0)
        printf("\x1b[%zuA", e->row);
    fputs("\r\x1b[J", stdout);
    fputs(e->prompt, stdout);
    fwrite(e->buf, 1, e->len, stdout);
    // After a full row the terminal holds the cursor at the margin instead of
    // wrapping. Force the wrap so the row arithmetic below holds.
    if (total > 0 && total % cols == 0)
        fputs("\n\r", stdout);

    size_t end_row = total / cols;
    size_t row = cursor / cols;
    size_t col = cursor % cols;
    if (end_row > row)
        printf("\x1b[%zuA", end_row - row);
    putchar('\r');
    if (col > 0)
        printf("\x1b[%zuC", col);
    fflush(stdout);
    e->row = row;
}

/** Inserts n bytes at the cursor. Returns -1 if they do not fit. */
static int insert(editor_t *e, const char *s, size_t n)
{
    if (e->len + n >= e->cap)
        return -1;
    memmove(e->buf + e->pos + n, e->buf + e->pos, e->len - e->pos);
    memcpy(e->buf + e->pos, s, n);
    e->pos += n;
    e->len += n;
    e->buf[e->len] = '\0';
    return 0;
}

/** Removes bytes [from, to) and leaves the cursor at from. */
static void erase(editor_t *e, size_t from, size_t to)
{
    memmove(e->buf + from, e->buf + to, e->len - to);
    e->len -= to - from;
    e->buf[e->len] = '\0';
    e->pos = from;
}

/** Replaces the line with s, cursor at the end. */
static void set_line(editor_t *e, const char *s)
{
    size_t n = strlen(s);
    if (n >= e->cap)
        n = e->cap - 1;
    memcpy(e->buf, s, n);
    e->buf[n] = '\0';
    e->len = e->pos = n;
}

/**
 * Moves through the history, towards older entries if older is set. Whatever
 * was typed is kept in the draft and comes back after the newest entry.
 * Changes made to a recalled entry are not kept when moving off it.
 */
static void history_step(editor_t *e, int older)
{
    if (older) {
        if (e->hist_pos == cas_history_count(e->cas))
            return;
        if (e->hist_pos == 0)
            memcpy(e->draft, e->buf, e->len + 1);
        e->hist_pos++;
    } else {
        if (e->hist_pos == 0)
            return;
        e->hist_pos--;
    }
    set_line(e, e->hist_pos ? cas_history_get(e->cas, e->hist_pos - 1) : e->draft);
}

/** Handles one key. Returns 1 when the line is finished, 0 to go on, -1 for end of input. */
static int handle_key(editor_t *e, int key)
{
    switch (key) {
    case '\r':
    case '\n':
        e->pos = e->len;
        refresh(e);
        fputs("\r\n", stdout);
        return 1;
    case CTRL_KEY('C'):
        e->pos = e->len;
        refresh(e);
        fputs("^C\r\n", stdout);
        e->len = e->pos = 0;
        e->buf[0] = '\0';
        return 1;
    case CTRL_KEY('D'):
        if (e->len == 0)
            return -1;
        /* fall through */
    case KEY_DELETE:
        erase(e, e->pos, next_char(e->buf, e->len, e->pos));
        break;
    case 0x7f:
    case CTRL_KEY('H'):
        erase(e, prev_char(e->buf, e->pos), e->pos);
        break;
    case KEY_LEFT:
    case CTRL_KEY('B'):
        e->pos = prev_char(e->buf, e->pos);
        break;
    case KEY_RIGHT:
    case CTRL_KEY('F'):
        e->pos = next_char(e->buf, e->len, e->pos);
        break;
    case KEY_WORD_LEFT:
        e->pos = word_left(e);
        break;
    case KEY_WORD_RIGHT:
        e->pos = word_right(e);
        break;
    case KEY_HOME:
    case CTRL_KEY('A'):
        e->pos = 0;
        break;
    case KEY_END:
    case CTRL_KEY('E'):
        e->pos = e->len;
        break;
    case KEY_UP:
    case CTRL_KEY('P'):
        history_step(e, 1);
        break;
    case KEY_DOWN:
    case CTRL_KEY('N'):
        history_step(e, 0);
        break;
    case CTRL_KEY('U'):
        erase(e, 0, e->pos);
        break;
    case CTRL_KEY('K'):
        erase(e, e->pos, e->len);
        break;
    case CTRL_KEY('W'):
        erase(e, word_left(e), e->pos);
        break;
    case CTRL_KEY('L'):
        fputs("\x1b[H\x1b[2J", stdout);
        e->row = 0;
        break;
    default:
        if (key >= 0x20 && key < 0x100 && key != 0x7f) {
            char c = (char)key;
            insert(e, &c, 1); // a full buffer just drops the key
        }
        break;
    }
    return 0;
}

/** Reads a line in raw mode, see the header. */
int lineedit_read(const cas_t *cas, const char *prompt, char *buf, size_t cap,
                  size_t *len)
{
    struct termios orig, raw;

    if (cap == 0 || tcgetattr(STDIN_FILENO, &orig) != 0)
        return -1;

    raw = orig;
    raw.c_iflag &= ~(tcflag_t)(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    raw.c_lflag &= ~(tcflag_t)(ECHO | ICANON | IEXTEN | ISIG);
    raw.c_cflag |= CS8;
    raw.c_cc[VMIN] = 1;
    raw.c_cc[VTIME] = 0;
    // TCSADRAIN, not TCSAFLUSH: input typed ahead while a line was evaluating must survive.
    if (tcsetattr(STDIN_FILENO, TCSADRAIN, &raw) != 0)
        return -1;

    editor_t e = { .cas = cas, .prompt = prompt, .buf = buf, .cap = cap };
    e.prompt_cols = cols_of(prompt, strlen(prompt));
    e.draft = malloc(cap);
    int rc = -1;
    if (e.draft) {
        buf[0] = '\0';
        refresh(&e);
        for (;;) {
            int key = read_key();
            int r = key < 0 ? -1 : handle_key(&e, key);
            if (r != 0) {
                rc = r > 0 ? 1 : 0;
                break;
            }
            if (!input_pending())
                refresh(&e);
        }
        *len = e.len;
    }

    free(e.draft);
    tcsetattr(STDIN_FILENO, TCSADRAIN, &orig);
    return rc;
}
