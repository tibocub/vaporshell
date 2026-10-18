/*
 * ledit.c -- the line editor.
 *
 * One editor for every platform: it needs raw terminal input and a way to
 * write, both through platform.h. Everything it draws for one keystroke is
 * composed into a single buffer and written with one write(): a USB serial
 * console is much happier with that than with a byte at a time.
 *
 * A key is read as a complete *sequence* of bytes ("\033[A", "\033f", "a",
 * an entire UTF-8 character) and looked up in one table, so adding a binding
 * is adding a row. The buffer is UTF-8 whatever the platform's locale
 * support: the terminal on the other end speaks it.
 *
 * Drawing is stateless apart from one number: how many rows the cursor sits
 * below the first row of the prompt. Each redraw goes back up that many rows,
 * clears to the end of the screen and prints prompt and buffer again, which
 * is correct for wrapped lines and multi-line prompts alike.
 */

#include <nuttx/config.h>
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "vaporshell.h"
#include "complete.h"
#include "exec.h"
#include "history.h"
#include "ledit.h"
#include "platform.h"
#include "prompt.h"

#ifdef VAPORSHELL_POSIX
#  define ED_OUT STDERR_FILENO          /* like other shells: stdout stays clean */
#else
#  define ED_OUT STDOUT_FILENO
#endif

#define ESC_TIMEOUT_MS 40               /* a lone ESC vs. the start of a sequence */
#define UNDO_DEPTH     8
#define SEQ_MAX        16

/* ---- Commands and the key table -------------------------------------------- */

enum cmd_e
{
  C_NONE = 0, C_SELF, C_ACCEPT, C_BOL, C_EOL, C_BACK, C_FWD, C_BACKWORD,
  C_FWDWORD, C_DELETE, C_DELETE_OR_EOF, C_BACKSPACE, C_KILL_EOL, C_KILL_BOL,
  C_KILL_WORD_BACK_WS, C_KILL_WORD, C_KILL_WORD_BACK, C_YANK, C_TRANSPOSE,
  C_CLEAR, C_PREV, C_NEXT, C_PSEARCH_BACK, C_PSEARCH_FWD, C_RSEARCH,
  C_INTERRUPT, C_COMPLETE, C_UPCASE, C_DOWNCASE, C_CAPITALIZE, C_UNDO,
  C_PASTE, C_NEWLINE, C_HIST_FIRST, C_HIST_LAST, C_COMPLETE_BACK, C_CANCEL,
  C_SHELL, C_NCMD
};

/* readline's names for the commands, by number (NULL: not bindable). */

static const char *const g_cmd_names[C_NCMD] =
{
  [C_SELF] = "self-insert", [C_ACCEPT] = "accept-line", [C_BOL] = "beginning-of-line",
  [C_EOL] = "end-of-line", [C_BACK] = "backward-char", [C_FWD] = "forward-char",
  [C_BACKWORD] = "backward-word", [C_FWDWORD] = "forward-word", [C_DELETE] = "delete-char",
  [C_DELETE_OR_EOF] = "delete-char-or-eof", [C_BACKSPACE] = "backward-delete-char",
  [C_KILL_EOL] = "kill-line", [C_KILL_BOL] = "backward-kill-line",
  [C_KILL_WORD_BACK_WS] = "unix-word-rubout", [C_KILL_WORD] = "kill-word",
  [C_KILL_WORD_BACK] = "backward-kill-word", [C_YANK] = "yank",
  [C_TRANSPOSE] = "transpose-chars", [C_CLEAR] = "clear-screen",
  [C_PREV] = "previous-history", [C_NEXT] = "next-history",
  [C_PSEARCH_BACK] = "history-search-backward", [C_PSEARCH_FWD] = "history-search-forward",
  [C_RSEARCH] = "reverse-search-history", [C_INTERRUPT] = "abort", [C_COMPLETE] = "complete",
  [C_UPCASE] = "upcase-word", [C_DOWNCASE] = "downcase-word", [C_CAPITALIZE] = "capitalize-word",
  [C_UNDO] = "undo", [C_PASTE] = "bracketed-paste-begin", [C_NEWLINE] = "insert-newline",
  [C_HIST_FIRST] = "beginning-of-history", [C_HIST_LAST] = "end-of-history",
  [C_COMPLETE_BACK] = "menu-complete-backward", [C_SHELL] = "shell-command"
};

static const struct
{
  const char *seq;
  unsigned char cmd;
} g_keys[] =
{
  { "\001", C_BOL }, { "\005", C_EOL }, { "\002", C_BACK }, { "\006", C_FWD },
  { "\004", C_DELETE_OR_EOF }, { "\010", C_BACKSPACE }, { "\177", C_BACKSPACE },
  { "\013", C_KILL_EOL }, { "\025", C_KILL_BOL }, { "\027", C_KILL_WORD_BACK_WS },
  { "\031", C_YANK }, { "\024", C_TRANSPOSE }, { "\014", C_CLEAR },
  { "\020", C_PREV }, { "\016", C_NEXT }, { "\022", C_RSEARCH },
  { "\003", C_INTERRUPT }, { "\r", C_ACCEPT }, { "\n", C_ACCEPT },
  { "\t", C_COMPLETE }, { "\037", C_UNDO }, { "\030\025", C_UNDO },
  { "\033b", C_BACKWORD }, { "\033f", C_FWDWORD }, { "\033d", C_KILL_WORD },
  { "\033\177", C_KILL_WORD_BACK }, { "\033\010", C_KILL_WORD_BACK },
  { "\033u", C_UPCASE }, { "\033l", C_DOWNCASE }, { "\033c", C_CAPITALIZE },
  { "\033p", C_PSEARCH_BACK }, { "\033n", C_PSEARCH_FWD },
  { "\033<", C_HIST_FIRST }, { "\033>", C_HIST_LAST },
  { "\033\r", C_NEWLINE }, { "\033/", C_UNDO },
  { "\033[A", C_PREV }, { "\033OA", C_PREV },
  { "\033[B", C_NEXT }, { "\033OB", C_NEXT },
  { "\033[C", C_FWD }, { "\033OC", C_FWD },
  { "\033[D", C_BACK }, { "\033OD", C_BACK },
  { "\033[H", C_BOL }, { "\033OH", C_BOL }, { "\033[1~", C_BOL }, { "\033[7~", C_BOL },
  { "\033[F", C_EOL }, { "\033OF", C_EOL }, { "\033[4~", C_EOL }, { "\033[8~", C_EOL },
  { "\033[3~", C_DELETE },
  { "\033[1;5C", C_FWDWORD }, { "\033[1;5D", C_BACKWORD },
  { "\033[1;3C", C_FWDWORD }, { "\033[1;3D", C_BACKWORD },
  { "\033[5~", C_PSEARCH_BACK }, { "\033[6~", C_PSEARCH_FWD },
  { "\033[200~", C_PASTE }, { "\033[Z", C_COMPLETE_BACK }, { "\033", C_CANCEL },
  { NULL, C_NONE }
};

/* ---- State --------------------------------------------------------------- */

struct snap_s
{
  char *s;
  size_t pos;
};

/* Answers of the last few path checks (see path_known). */

struct pmemo_s
{
  unsigned long h[8];
  bool exists[8];
  unsigned n;                   /* answers stored so far; slot n % 8 is overwritten next */
};

struct menu_s                   /* the completion menu under the line */
{
  char **name;                  /* what is listed (we own these strings) */
  char **desc;                  /* one line about it, or NULL; any: one column */
  char *suffix;                 /* what follows the name once chosen */
  size_t skip;                  /* of its text, the bytes already typed */
  bool failed;                  /* memory ran out while it was filled */
  bool has_desc;
  int n;
  int cap;
  int maxw;
  int sel;                      /* the chosen one, -1: none yet */
  int top;                      /* first row shown */
  size_t at;                    /* where the tails go in the line */
  size_t inslen;                /* bytes of the chosen tail now in the line */
  size_t del;                   /* typed bytes before 'at' that the first choice replaces */
  char *orig;                   /* ...and what they were, to put back */
  bool replaced;                /* they are gone from the line */
};

struct ed_s
{
  struct menu_s menu;
  struct pmemo_s pm;            /* which words of the line exist as paths */
  struct sbuf_s out;            /* what one redraw writes: kept between redraws, so no malloc per key */
  const char *sug;              /* the suggestion shown by the last refresh (points into the history) */
  struct sbuf_s b;              /* the line; b.s is NULL until something is typed */
  size_t pos;                   /* cursor, a byte offset */
  const char *prompt;
  int cols;
  int cur_row;                  /* rows the cursor is below the prompt's first row */
  struct sbuf_s kill;
  bool last_kill;               /* the previous command was a kill: append to it */
  int hi;                       /* history index being shown; vs_hist_count() = the new line */
  char *hsaved;                 /* the new line, while another entry is shown */
  char *prefix;                 /* prefix search: the text it started from */
  struct snap_s undo[UNDO_DEPTH];
  int nundo;
  int last;                     /* the previous command */
  bool bracketed;               /* terminal paste markers are on */
  bool eof;
};

static const char *text(const struct ed_s *e)
{
  return e->b.s != NULL ? e->b.s : "";
}

/* ---- UTF-8 ---------------------------------------------------------------- */

static size_t u8len(unsigned char c)
{
  return c >= 0xf0 ? 4 : c >= 0xe0 ? 3 : c >= 0xc0 ? 2 : 1;
}

static size_t prev_char(const char *s, size_t pos)
{
  if (pos == 0)
    {
      return 0;
    }

  pos--;
  while (pos > 0 && ((unsigned char)s[pos] & 0xc0) == 0x80)
    {
      pos--;
    }

  return pos;
}

static size_t next_char(const char *s, size_t len, size_t pos)
{
  size_t n = u8len((unsigned char)s[pos]);

  return pos + n > len ? len : pos + n;
}

static long u8decode(const char *s, size_t n)
{
  unsigned char c = (unsigned char)s[0];
  size_t l = u8len(c) < n ? u8len(c) : n;       /* only this character's own bytes */
  long wc;
  size_t i;

  if (c < 0x80 || l < 2)
    {
      return c;
    }

  wc = c >= 0xf0 ? c & 0x07 : c >= 0xe0 ? c & 0x0f : c & 0x1f;
  for (i = 1; i < l; i++)
    {
      wc = (wc << 6) | ((unsigned char)s[i] & 0x3f);
    }

  return wc;
}

/* Display width of a code point: 0 for combining marks, 2 for East Asian
 * wide and emoji, else 1. A small table is enough for editing a command line.
 */

static int wc_width(long wc)
{
  if (wc < 0x300)
    {
      return 1;
    }

  if ((wc >= 0x300 && wc <= 0x36f) || (wc >= 0x200b && wc <= 0x200f) ||
      (wc >= 0xfe00 && wc <= 0xfe0f) || wc == 0x20e3)
    {
      return 0;
    }

  if ((wc >= 0x1100 && wc <= 0x115f) || (wc >= 0x2e80 && wc <= 0xa4cf) ||
      (wc >= 0xac00 && wc <= 0xd7a3) || (wc >= 0xf900 && wc <= 0xfaff) ||
      (wc >= 0xfe30 && wc <= 0xfe6f) || (wc >= 0xff00 && wc <= 0xff60) ||
      (wc >= 0xffe0 && wc <= 0xffe6) || (wc >= 0x1f300 && wc <= 0x1f64f) ||
      (wc >= 0x1f900 && wc <= 0x1f9ff) || (wc >= 0x20000 && wc <= 0x3fffd))
    {
      return 2;
    }

  return 1;
}

/* Columns taken by the UTF-8 character at s, of which 'avail' bytes are there. */

static int char_cols(const char *s, size_t avail)
{
  return (unsigned char)s[0] < 0x80 ? 1 : wc_width(u8decode(s, avail));
}

/* ---- Drawing ----------------------------------------------------------- */

struct cursor_s
{
  int row;
  int col;
};

/* Advance past a character 'w' columns wide. A column equal to the width of
 * the terminal is the "pending wrap" state: the next character goes to the
 * start of the next row.
 */

static void adv(struct cursor_s *c, int w, int cols)
{
  if (w > 0 && c->col + w > cols)
    {
      c->row++;
      c->col = 0;
    }

  c->col += w;
}

static void out_cursor_move(struct sbuf_s *o, int up, int col)
{
  char t[24];

  if (up > 0)
    {
      snprintf(t, sizeof(t), "\033[%dA", up);
      sb_adds(o, t);
    }

  sb_addc(o, '\r');
  if (col > 0)
    {
      snprintf(t, sizeof(t), "\033[%dC", col);
      sb_adds(o, t);
    }
}

/* The prompt: written as is (minus the \[ \] markers); the cursor ends up
 * where its last line ends.
 */

static void draw_prompt(struct sbuf_s *o, const char *p, struct cursor_s *c, int cols)
{
  bool ignore = false;
  bool esc = false;

  for (; *p != '\0'; p++)
    {
      unsigned char ch = (unsigned char)*p;

      if (ch == PROMPT_IGN_START || ch == PROMPT_IGN_END)
        {
          ignore = ch == PROMPT_IGN_START;
          continue;
        }

      if (ignore || esc)
        {
          sb_addc(o, (char)ch);
          if (esc && ((ch >= '@' && ch <= '~' && ch != '[') || ch == '\a'))
            {
              esc = false;
            }

          continue;
        }

      if (ch == '\033')
        {
          esc = true;
          sb_addc(o, (char)ch);
        }
      else if (ch == '\n')
        {
          sb_adds(o, "\r\n");
          c->row++;
          c->col = 0;
        }
      else if (ch == '\r')
        {
          sb_addc(o, '\r');
          c->col = 0;
        }
      else if ((ch & 0xc0) != 0x80)
        {
          int w = char_cols(p, strlen(p));

          adv(c, w, cols);
          sb_addc(o, (char)ch);
        }
      else
        {
          sb_addc(o, (char)ch);
        }
    }
}

/* Draw 'prompt' + 's' and leave the cursor at byte 'cursor' of s. */

/* Highlighting. One style per byte of the line:
 *   1 green    a command word that runs something       2 red   one that does not
 *   3 yellow   inside quotes (the quotes too)            4 underline   an argument that is an existing path
 * Words with an expansion or an assignment in them are left alone.
 */

enum { HL_NONE, HL_OK, HL_BAD, HL_QUOTE, HL_PATH };

static bool path_exists(const char *word)
{
  char tilde[VS_PATH_MAX];
  struct stat st;

  if (word[0] == '~' && (word[1] == '/' || word[1] == '\0') && var_get("HOME") != NULL)
    {
      if (snprintf(tilde, sizeof(tilde), "%s%s", var_get("HOME"), word + 1) >= (int)sizeof(tilde))
        {
          return false;
        }

      word = tilde;
    }

  return stat(VS_FS(word), &st) == 0;
}

/* The answers of the last few path checks, by hash of the word: the line is redrawn
 * on every key and a stat() on littlefs costs 10 ms or more, so a word that did not
 * change is not looked up again. Kept while one line is edited.
 */


static bool path_known(struct pmemo_s *m, const char *word)
{
  unsigned long h = 5381;
  unsigned i;
  const char *p;

  for (p = word; *p != '\0'; p++)
    {
      h = h * 33 + (unsigned char)*p;
    }

  for (i = 0; i < m->n && i < 8; i++)
    {
      if (m->h[i] == h)
        {
          return m->exists[i];
        }
    }

  m->h[m->n % 8] = h;
  m->exists[m->n % 8] = path_exists(word);
  return m->exists[m->n++ % 8];
}

static unsigned char *highlight(const char *s, size_t len, struct pmemo_s *memo)
{
  unsigned char *st = vs_xmalloc(len + 1);
  bool at_cmd = true;
  char quote = 0;
  size_t i = 0;
  int budget = 16;                /* stat() calls per redraw: a long line must not make typing slow */

  memset(st, HL_NONE, len + 1);
  vs_commands_refresh();
  while (i < len)
    {
      char ch = s[i];
      size_t j;
      char word[96];
      bool plain = true;

      if (quote != 0)
        {
          st[i] = HL_QUOTE;
          if (ch == '\\' && quote == '"' && i + 1 < len)
            {
              st[++i] = HL_QUOTE;
            }
          else if (ch == quote)
            {
              quote = 0;
            }

          i++;
          continue;
        }

      if (ch == '\'' || ch == '"')
        {
          quote = ch;
          st[i++] = HL_QUOTE;
          continue;
        }

      if (strchr("|;&(", ch) != NULL)
        {
          at_cmd = true;
          i++;
          continue;
        }

      if (strchr(" \t\n)<>", ch) != NULL)
        {
          i++;
          continue;
        }

      for (j = i; j < len && strchr(" \t\n|;&()<>'\"", s[j]) == NULL; j++)
        {
          plain = plain && strchr("$`\\=*?[{", s[j]) == NULL && (s[j] != '~' || j == i);
        }

      plain = plain && !(j < len && (s[j] == '\'' || s[j] == '"'));      /* ec"ho": still being written */
      if (at_cmd)
        {
          size_t k = i;

          while (k < j && (isalnum((unsigned char)s[k]) || s[k] == '_'))
            {
              k++;
            }

          if (!(k > i && k < j && s[k] == '='))        /* NAME=value: the command comes after it */
            {
              at_cmd = false;
              if (plain && s[i] != '~' && j - i < sizeof(word))
                {
                  memcpy(word, s + i, j - i);
                  word[j - i] = '\0';
                  memset(st + i, vs_is_command(word) ? HL_OK : HL_BAD, j - i);
                }
            }
        }
      else if (plain && s[i] != '-' && j - i < sizeof(word) && budget-- > 0)
        {
          memcpy(word, s + i, j - i);
          word[j - i] = '\0';
          if (path_known(memo, word))
            {
              memset(st + i, HL_PATH, j - i);
            }
        }

      i = j;
    }

  return st;
}

static void set_style(struct sbuf_s *o, unsigned char from, unsigned char to)
{
  static const char *const sgr[] = { "", "\033[32m", "\033[31m", "\033[33m", "\033[4m" };

  if (from != HL_NONE)
    {
      sb_adds(o, "\033[0m");
    }

  sb_adds(o, sgr[to]);
}

static void draw(struct ed_s *e, const char *prompt, const char *s, size_t len,
                 size_t cursor, const char *ghost, bool hilite, const char *menu)
{
  struct sbuf_s o;
  struct cursor_s c = { 0, 0 };
  struct cursor_s at = { 0, 0 };
  size_t i;
  char t[24];
  const char *hlv = var_get("VS_HIGHLIGHT");
  unsigned char *styles = hilite && (hlv == NULL || strcmp(hlv, "0") != 0) ? highlight(s, len, &e->pm) : NULL;
  unsigned char cur = HL_NONE;

  o = e->out;                       /* the buffer from the last redraw, emptied */
  o.len = 0;
  if (e->cur_row > 0)
    {
      snprintf(t, sizeof(t), "\033[%dA", e->cur_row);
      sb_adds(&o, t);
    }

  sb_adds(&o, "\r\033[J");
  draw_prompt(&o, prompt, &c, e->cols);
  if (cursor == 0)
    {
      at = c;
    }

  for (i = 0; i < len; )
    {
      unsigned char ch = (unsigned char)s[i];

      if (i == cursor)
        {
          at = c;
        }

      if (styles != NULL && styles[i] != cur)
        {
          set_style(&o, cur, styles[i]);
          cur = styles[i];
        }

      if (ch == '\n')
        {
          sb_adds(&o, "\r\n");
          c.row++;
          c.col = 0;
          i++;
        }
      else if (ch < 0x20 || ch == 0x7f)
        {
          adv(&c, 2, e->cols);
          sb_addc(&o, '^');
          sb_addc(&o, ch == 0x7f ? '?' : (char)(ch + '@'));
          i++;
        }
      else
        {
          size_t n = next_char(s, len, i) - i;

          adv(&c, char_cols(s + i, n), e->cols);
          sb_addn(&o, s + i, n);
          i += n;
        }
    }

  if (cur != HL_NONE)
    {
      sb_adds(&o, "\033[0m");
    }

  free(styles);

  if (cursor >= len)
    {
      at = c;
    }

  if (ghost != NULL && *ghost != '\0')            /* grey, after the cursor */
    {
      size_t gl = strlen(ghost);
      size_t k;

      sb_adds(&o, "\033[90m");
      for (k = 0; k < gl; k = next_char(ghost, gl, k))
        {
          size_t n = next_char(ghost, gl, k) - k;

          adv(&c, char_cols(ghost + k, n), e->cols);
          sb_addn(&o, ghost + k, n);
        }

      sb_adds(&o, "\033[0m");
    }

  if (c.col >= e->cols && e->cols > 0)
    {
      sb_adds(&o, "\r\n");          /* leave the pending-wrap state */
      c.row++;
      c.col = 0;
      if (cursor >= len)
        {
          at = c;
        }
    }

  while (menu != NULL && *menu != '\0')
    {
      size_t n = strcspn(menu, "\n");

      sb_adds(&o, "\r\n");          /* one row of the menu */
      sb_addn(&o, menu, n);
      c.row++;
      menu += n + (menu[n] == '\n');
    }

  /* From the end of the text back to the cursor. */

  if (c.row > at.row || c.col != at.col)
    {
      out_cursor_move(&o, c.row - at.row, at.col);
    }

  e->cur_row = at.row;
  if (o.s != NULL)
    {
      ssize_t w = write(ED_OUT, o.s, o.len);

      (void)w;
    }

  e->out = o;
}

/* The newest history entry that starts with what is typed: the rest of it is
 * shown in grey and Right / End / Ctrl-F / Ctrl-E at the end of the line takes it.
 * VS_AUTOSUGGEST=0 turns it off. NULL if there is none.
 */

static const char *suggestion(const struct ed_s *e)
{
  const char *off = var_get("VS_AUTOSUGGEST");
  int i;

  if (e->pos != e->b.len || e->b.len == 0 || (off != NULL && strcmp(off, "0") == 0) ||
      strchr(text(e), '\n') != NULL)
    {
      return NULL;
    }

  for (i = vs_hist_count() - 1; i >= 0; i--)
    {
      const char *h = vs_hist_get(i);

      if (strlen(h) > e->b.len && strncmp(h, text(e), e->b.len) == 0 &&
          strpbrk(h + e->b.len, "\n\033") == NULL)
        {
          return h + e->b.len;
        }
    }

  return NULL;
}

static void menu_free(struct ed_s *e);
static char *menu_text(const struct ed_s *e);

static void refresh(struct ed_s *e)
{
  char *menu = e->menu.n > 0 ? menu_text(e) : NULL;

  e->sug = e->menu.n > 0 ? NULL : suggestion(e);
  draw(e, e->prompt, text(e), e->b.len, e->pos, e->sug, true, menu);
  free(menu);
}

static void put(const char *s)
{
  ssize_t w = write(ED_OUT, s, strlen(s));

  (void)w;
}

/* ---- Editing primitives -------------------------------------------------- */

static void snap(struct ed_s *e)
{
  if (e->nundo == UNDO_DEPTH)
    {
      free(e->undo[0].s);
      memmove(e->undo, e->undo + 1, sizeof(e->undo[0]) * (UNDO_DEPTH - 1));
      e->nundo--;
    }

  e->undo[e->nundo].s = vs_xstrdup(text(e));
  e->undo[e->nundo].pos = e->pos;
  e->nundo++;
}

static void set_text(struct ed_s *e, const char *s)
{
  sb_free(&e->b);
  sb_init(&e->b);
  sb_adds(&e->b, s);
  e->pos = e->b.len;
}

static void insert(struct ed_s *e, const char *s, size_t n)
{
  size_t tail = e->b.len - e->pos;
  char *keep = NULL;

  if (n == 0)
    {
      return;
    }

  if (tail > 0)
    {
      keep = vs_xmalloc(tail);
      memcpy(keep, e->b.s + e->pos, tail);
    }

  e->b.len = e->pos;
  sb_addn(&e->b, s, n);
  e->pos = e->b.len;
  if (keep != NULL)
    {
      sb_addn(&e->b, keep, tail);
      free(keep);
    }

  e->b.s[e->b.len] = '\0';
}

/* Remove [from, to). 'kill' puts the text in the kill buffer, joined to the
 * previous kill if there was one right before (prepended when 'before').
 */

static void erase(struct ed_s *e, size_t from, size_t to, bool kill, bool before)
{
  if (to <= from)
    {
      return;
    }

  if (kill)
    {
      struct sbuf_s k;

      if (!e->last_kill)
        {
          sb_free(&e->kill);
          sb_init(&e->kill);
        }

      sb_init(&k);
      if (before)
        {
          sb_addn(&k, e->b.s + from, to - from);
          if (e->kill.s != NULL)
            {
              sb_addn(&k, e->kill.s, e->kill.len);
            }
        }
      else
        {
          if (e->kill.s != NULL)
            {
              sb_addn(&k, e->kill.s, e->kill.len);
            }

          sb_addn(&k, e->b.s + from, to - from);
        }

      sb_free(&e->kill);
      e->kill = k;
    }

  memmove(e->b.s + from, e->b.s + to, e->b.len - to);
  e->b.len -= to - from;
  e->b.s[e->b.len] = '\0';
  e->pos = from;
}

static bool is_word(unsigned char c)
{
  return isalnum(c) || c == '_' || c >= 0x80;
}

static size_t word_end(const struct ed_s *e, size_t p)
{
  const char *s = text(e);

  while (p < e->b.len && !is_word((unsigned char)s[p]))
    {
      p = next_char(s, e->b.len, p);
    }

  while (p < e->b.len && is_word((unsigned char)s[p]))
    {
      p = next_char(s, e->b.len, p);
    }

  return p;
}

static size_t word_start(const struct ed_s *e, size_t p)
{
  const char *s = text(e);

  while (p > 0 && !is_word((unsigned char)s[prev_char(s, p)]))
    {
      p = prev_char(s, p);
    }

  while (p > 0 && is_word((unsigned char)s[prev_char(s, p)]))
    {
      p = prev_char(s, p);
    }

  return p;
}

/* Change the case of the word ahead of the cursor. 'mode': 'u', 'l', 'c'. */

static void case_word(struct ed_s *e, char mode)
{
  size_t end = word_end(e, e->pos);
  size_t i;
  bool first = true;

  for (i = e->pos; i < end; i++)
    {
      unsigned char c = (unsigned char)e->b.s[i];

      if (c >= 0x80 || !isalnum(c))
        {
          continue;
        }

      if (mode == 'u' || (mode == 'c' && first))
        {
          e->b.s[i] = (char)toupper(c);
        }
      else
        {
          e->b.s[i] = (char)tolower(c);
        }

      first = false;
    }

  e->pos = end;
}

/* ---- History --------------------------------------------------------------- */

static void hist_show(struct ed_s *e, int idx)
{
  int n = vs_hist_count();

  if (e->hi == n && e->hsaved == NULL)
    {
      e->hsaved = vs_xstrdup(text(e));
    }

  e->hi = idx;
  set_text(e, idx >= n ? (e->hsaved != NULL ? e->hsaved : "") : vs_hist_get(idx));
}

static void hist_move(struct ed_s *e, int dir, bool prefix)
{
  int n = vs_hist_count();
  int i = e->hi + dir;

  if (prefix && e->prefix == NULL)
    {
      e->prefix = vs_xstrdup(text(e));
      e->prefix[e->pos] = '\0';
    }

  for (; i >= 0 && i <= n; i += dir)
    {
      if (!prefix || i == n ||
          strncmp(vs_hist_get(i), e->prefix, strlen(e->prefix)) == 0)
        {
          hist_show(e, i);
          if (prefix)
            {
              e->pos = strlen(e->prefix);
            }

          return;
        }
    }
}

/* ---- Incremental search ----------------------------------------------------- */

static int read_key(char *seq, int *len);

static int find_hist(const char *needle, int from, int dir, const char **hit)
{
  int n = vs_hist_count();
  int i;

  for (i = from; i >= 0 && i < n; i += dir)
    {
      const char *h = vs_hist_get(i);

      *hit = strstr(h, needle);
      if (*hit != NULL)
        {
          return i;
        }
    }

  return -1;
}

/* Returns true if the user accepted the match with Enter. The match is left
 * in the buffer either way unless the search was cancelled.
 */

static bool search(struct ed_s *e)
{
  struct sbuf_s q;
  int idx = vs_hist_count();
  const char *hit = NULL;
  char seq[SEQ_MAX];
  int len;
  bool failed = false;
  char *orig = vs_xstrdup(text(e));
  size_t orig_pos = e->pos;
  bool accepted = false;

  sb_init(&q);
  for (; ; )
    {
      struct sbuf_s pr;

      sb_init(&pr);
      sb_adds(&pr, failed ? "(failed reverse-i-search)`" : "(reverse-i-search)`");
      if (q.s != NULL)
        {
          sb_adds(&pr, q.s);
        }

      sb_adds(&pr, "': ");
      if (idx < vs_hist_count())
        {
          const char *h = vs_hist_get(idx);

          draw(e, pr.s, h, strlen(h), hit != NULL ? (size_t)(hit - h) : 0, NULL, false, NULL);
        }
      else
        {
          draw(e, pr.s, "", 0, 0, NULL, false, NULL);
        }

      sb_free(&pr);
      if (read_key(seq, &len) <= 0)
        {
          break;
        }

      if (len == 1 && (seq[0] == 0x12 || seq[0] == 0x13))     /* Ctrl-R / Ctrl-S */
        {
          int dir = seq[0] == 0x12 ? -1 : 1;
          int from = idx < vs_hist_count() ? idx + dir : vs_hist_count() - 1;
          int found = q.s != NULL ? find_hist(q.s, from, dir, &hit) : -1;

          failed = found < 0 && q.s != NULL;
          if (found >= 0)
            {
              idx = found;
            }
        }
      else if (len == 1 && (seq[0] == 0x7f || seq[0] == 0x08))
        {
          if (q.len > 0)
            {
              q.len = prev_char(q.s, q.len);
              q.s[q.len] = '\0';
              idx = vs_hist_count();
              hit = NULL;
              failed = false;
              if (q.len > 0)
                {
                  int found = find_hist(q.s, vs_hist_count() - 1, -1, &hit);

                  if (found >= 0)
                    {
                      idx = found;
                    }
                }
            }
        }
      else if (len == 1 && (seq[0] == 0x07 || seq[0] == 0x03))  /* Ctrl-G / Ctrl-C */
        {
          set_text(e, orig);
          e->pos = orig_pos;
          idx = vs_hist_count();
          break;
        }
      else if (len >= 1 && (unsigned char)seq[0] >= 0x20 && seq[0] != 0x7f)
        {
          int found;

          sb_addn(&q, seq, (size_t)len);
          found = find_hist(q.s, idx < vs_hist_count() ? idx : vs_hist_count() - 1,
                            -1, &hit);
          failed = found < 0;
          if (found >= 0)
            {
              idx = found;
            }
        }
      else
        {
          /* Any other key ends the search and keeps the match; Enter also
           * runs it.
           */

          if (idx < vs_hist_count())
            {
              set_text(e, vs_hist_get(idx));
              e->pos = hit != NULL ? (size_t)(hit - vs_hist_get(idx)) : e->b.len;
            }

          accepted = len == 1 && (seq[0] == '\r' || seq[0] == '\n');
          e->hi = idx;
          sb_free(&q);
          free(orig);
          return accepted;
        }
    }

  if (idx < vs_hist_count() && hit != NULL)
    {
      set_text(e, vs_hist_get(idx));
      e->pos = (size_t)(hit - vs_hist_get(idx));
      e->hi = idx;
    }

  sb_free(&q);
  free(orig);
  return false;
}

/* ---- Completion --------------------------------------------------------------- */

static int str_width(const char *s)
{
  int w = 0;
  size_t n = strlen(s);
  size_t i;

  for (i = 0; i < n; i = next_char(s, n, i))
    {
      w += char_cols(s + i, n - i);
    }

  return w;
}

static void menu_free(struct ed_s *e)
{
  int i;

  for (i = 0; i < e->menu.n; i++)
    {
      free(e->menu.name[i]);
      free(e->menu.desc[i]);
    }

  free(e->menu.name);
  free(e->menu.suffix);
  free(e->menu.desc);
  free(e->menu.orig);
  memset(&e->menu, 0, sizeof(e->menu));
}

static void cand_cb(void *ctx, char *name, char *desc, char suffix)
{
  struct menu_s *m = ctx;
  int w = str_width(name);

  if (m->n == m->cap && !m->failed)
    {
      int cap = m->cap > 0 ? m->cap * 2 : 16;
      char **nn = realloc(m->name, (size_t)cap * sizeof(char *));
      char **dd = nn != NULL ? realloc(m->desc, (size_t)cap * sizeof(char *)) : NULL;
      char *ss = dd != NULL ? realloc(m->suffix, (size_t)cap) : NULL;

      m->name = nn != NULL ? nn : m->name;                 /* whatever grew stays valid */
      m->desc = dd != NULL ? dd : m->desc;
      m->suffix = ss != NULL ? ss : m->suffix;
      if (ss == NULL)
        {
          m->failed = true;
        }
      else
        {
          m->cap = cap;
        }
    }

  if (m->failed)
    {
      free(name);
      free(desc);
      return;
    }

  m->name[m->n] = name;
  m->desc[m->n] = desc != NULL && *desc != '\0' ? desc : NULL;
  if (m->desc[m->n] != desc)
    {
      free(desc);
    }

  m->has_desc = m->has_desc || m->desc[m->n] != NULL;
  m->suffix[m->n++] = suffix;
  if (w > m->maxw)
    {
      m->maxw = w;
    }
}

/* Columns of the menu, column by column like bash lists them: entry k is on
 * row k % rows. 'rows' is how many rows the whole list takes.
 */

static int menu_rows(const struct ed_s *e)
{
  int per = e->menu.has_desc ? 1 : e->cols / (e->menu.maxw + 2);

  per = per < 1 ? 1 : per;
  return (e->menu.n + per - 1) / per;
}

/* How many rows of the menu fit under the line. */

static int menu_visible(const struct ed_s *e)
{
  int r = vs_plat_rows();
  int vis = (r > 0 ? r : 24) / 2;

  vis = vis < 3 ? 3 : vis;
  return menu_rows(e) < vis ? menu_rows(e) : vis;
}

/* The menu as text for draw(): rows separated by '\n', the chosen entry in
 * reverse video, and a note when rows are scrolled out of view.
 */

static char *menu_text(const struct ed_s *e)
{
  const struct menu_s *m = &e->menu;
  int rows = menu_rows(e);
  int vis = menu_visible(e);
  int colw = m->maxw + 2;
  int r;
  int k;
  struct sbuf_s o;

  sb_init(&o);
  for (r = m->top; r < m->top + vis; r++)
    {
      for (k = r; k < m->n; k += rows)
        {
          int pad = colw - str_width(m->name[k]);

          if (k == m->sel)
            {
              sb_adds(&o, "\033[7m");
            }

          sb_adds(&o, m->name[k]);
          if (k == m->sel)
            {
              sb_adds(&o, "\033[0m");
            }

          if (m->desc[k] != NULL)              /* the description, in grey, cut to the line */
            {
              size_t dl = strlen(m->desc[k]);
              size_t j = 0;
              int room = e->cols - colw - 1;

              sb_adds(&o, "\033[90m");
              while (pad-- > 0)
                {
                  sb_addc(&o, ' ');
                }

              while (j < dl && room > 0)
                {
                  size_t n = next_char(m->desc[k], dl, j) - j;
                  int w = char_cols(m->desc[k] + j, n);

                  if (w > room)
                    {
                      break;
                    }

                  sb_addn(&o, m->desc[k] + j, n);
                  room -= w;
                  j += n;
                }

              sb_adds(&o, "\033[0m");
              pad = 0;
            }

          if (k + rows < m->n)
            {
              while (pad-- > 0)
                {
                  sb_addc(&o, ' ');
                }
            }
        }

      sb_addc(&o, '\n');
    }

  if (vis < rows)
    {
      char t[48];

      snprintf(t, sizeof(t), "\033[90mrows %d-%d of %d\033[0m", m->top + 1, m->top + vis, rows);
      sb_adds(&o, t);
    }

  return o.s != NULL ? sb_take(&o) : vs_xstrdup("");
}

/* Choose entry 'sel' (wrapping): its tail replaces the previous choice's. */

static void menu_choose(struct ed_s *e, int sel)
{
  struct menu_s *m = &e->menu;
  int rows = menu_rows(e);
  int vis = menu_visible(e);

  sel = (sel % m->n + m->n) % m->n;
  if (m->del > 0 && !m->replaced)
    {
      m->orig = vs_xmalloc(m->del + 1);               /* a looser match: the typed word goes */
      memcpy(m->orig, e->b.s + m->at - m->del, m->del);
      m->orig[m->del] = '\0';
      erase(e, m->at - m->del, m->at, false, false);
      m->at -= m->del;
      m->replaced = true;
    }

  erase(e, m->at, m->at + m->inslen, false, false);
  e->pos = m->at;
  {
    char *tail = vs_comp_tail(m->name[sel], m->suffix[sel], m->skip);

    m->inslen = tail != NULL ? strlen(tail) : 0;
    if (tail != NULL)
      {
        insert(e, tail, m->inslen);
        free(tail);
      }
  }

  m->sel = sel;
  if (sel % rows < m->top)
    {
      m->top = sel % rows;
    }
  else if (sel % rows >= m->top + vis)
    {
      m->top = sel % rows - vis + 1;
    }
}

static void menu_move(struct ed_s *e, int delta)
{
  menu_choose(e, e->menu.sel < 0 ? (delta > 0 ? 0 : e->menu.n - 1) : e->menu.sel + delta);
}

/* Tab: insert what all candidates share; if that adds nothing, open the menu.
 * With the menu open, Tab / Shift-Tab (dir +1 / -1) walk through it.
 */

static void complete(struct ed_s *e, int dir)
{
  char ins[128];
  int del;
  size_t skip;
  int n;

  if (e->menu.n > 0)
    {
      menu_move(e, dir);
      return;
    }

  n = vs_complete(text(e), (int)e->pos, ins, sizeof(ins), &del, &skip, cand_cb, &e->menu);
  if (e->menu.failed)
    {
      menu_free(e);                    /* too many candidates for the memory there is */
      return;
    }

  if (n == 1 && del > 0)
    {
      snap(e);
      erase(e, e->pos - (size_t)del, e->pos, false, false);   /* a looser match replaces the word */
    }

  if (ins[0] != '\0')
    {
      snap(e);
      insert(e, ins, strlen(ins));
    }

  if (n > 1 && ins[0] == '\0')
    {
      e->menu.at = e->pos;
      e->menu.sel = -1;
      e->menu.del = (size_t)del;
      e->menu.skip = skip;
    }
  else
    {
      menu_free(e);
    }
}

/* ---- Keys ---------------------------------------------------------------- */

/* One key as a byte sequence: a character (all of its UTF-8 bytes), or an
 * escape sequence. Returns 1, or <= 0 at end of input.
 */

static int read_key(char *seq, int *len)
{
  unsigned char c;
  int r;
  int n = 0;

  r = vs_plat_tty_read(&c, -1);
  if (r <= 0)
    {
      return r;
    }

  seq[n++] = (char)c;
  if (c == 0x1b)
    {
      if (vs_plat_tty_read(&c, ESC_TIMEOUT_MS) <= 0)
        {
          *len = n;                         /* a lone ESC */
          return 1;
        }

      seq[n++] = (char)c;
      if (c == '[' || c == 'O')
        {
          /* Parameters and intermediates up to the final byte (0x40-0x7e). */

          while (n < SEQ_MAX - 1 && vs_plat_tty_read(&c, ESC_TIMEOUT_MS) > 0)
            {
              seq[n++] = (char)c;
              if (c >= 0x40 && c <= 0x7e)
                {
                  break;
                }
            }
        }
      else if (c >= 0xc0)
        {
          size_t k = u8len(c);

          while (k-- > 1 && vs_plat_tty_read(&c, ESC_TIMEOUT_MS) > 0 &&
                 n < SEQ_MAX - 1)
            {
              seq[n++] = (char)c;
            }
        }
    }
  else if (c >= 0xc0)
    {
      size_t k = u8len(c);

      while (k-- > 1 && vs_plat_tty_read(&c, ESC_TIMEOUT_MS) > 0 && n < SEQ_MAX - 1)
        {
          seq[n++] = (char)c;
        }
    }

  *len = n;
  return 1;
}

/* Bindings made with `bind`, tried before the table above. */

struct ubind_s
{
  struct ubind_s *next;
  char seq[SEQ_MAX];
  int len;
  int cmd;
  char *shell;                  /* C_SHELL: the code to run (bind -x) */
};

static struct ubind_s *g_ubinds;
static const char *g_shell;     /* the shell code of the binding lookup() found */

int vs_ledit_bind(const char *seq, int len, int cmd, const char *shell)
{
  struct ubind_s *b;

  if (len <= 0 || len >= SEQ_MAX || cmd <= C_NONE || cmd >= C_NCMD)
    {
      return -1;
    }

  vs_ledit_unbind(seq, len);
  b = vs_xmalloc(sizeof(*b));
  memcpy(b->seq, seq, (size_t)len);
  b->len = len;
  b->cmd = cmd;
  b->shell = shell != NULL ? vs_xstrdup(shell) : NULL;
  b->next = g_ubinds;
  g_ubinds = b;
  return 0;
}

int vs_ledit_unbind(const char *seq, int len)
{
  struct ubind_s **p;

  for (p = &g_ubinds; *p != NULL; p = &(*p)->next)
    {
      if ((*p)->len == len && memcmp((*p)->seq, seq, (size_t)len) == 0)
        {
          struct ubind_s *dead = *p;

          *p = dead->next;
          free(dead->shell);
          free(dead);
          return 0;
        }
    }

  return -1;
}

int vs_ledit_cmd_lookup(const char *name)
{
  int i;

  for (i = 1; i < C_NCMD; i++)
    {
      if (g_cmd_names[i] != NULL && strcmp(g_cmd_names[i], name) == 0)
        {
          return i;
        }
    }

  return -1;
}

int vs_ledit_ncmds(void)
{
  return C_NCMD;
}

const char *vs_ledit_cmd_name(int i)
{
  return i >= 0 && i < C_NCMD ? g_cmd_names[i] : NULL;
}

/* The idx-th binding, user ones first and then the built-in table; false past the end. */

bool vs_ledit_binding(int idx, const char **seq, int *len, const char **name, const char **shell)
{
  const struct ubind_s *b;
  int cmd;

  for (b = g_ubinds; b != NULL; b = b->next, idx--)
    {
      if (idx == 0)
        {
          *seq = b->seq;
          *len = b->len;
          cmd = b->cmd;
          *shell = b->shell;
          *name = cmd == C_SHELL ? NULL : g_cmd_names[cmd];
          return true;
        }
    }

  for (cmd = 0; g_keys[cmd].seq != NULL; cmd++, idx--)
    {
      if (idx == 0 && g_cmd_names[g_keys[cmd].cmd] != NULL)
        {
          *seq = g_keys[cmd].seq;
          *len = (int)strlen(g_keys[cmd].seq);
          *name = g_cmd_names[g_keys[cmd].cmd];
          *shell = NULL;
          return true;
        }
    }

  return false;
}

static int lookup(const char *seq, int len)
{
  const struct ubind_s *b;
  int i;

  g_shell = NULL;
  for (b = g_ubinds; b != NULL; b = b->next)
    {
      if (b->len == len && memcmp(b->seq, seq, (size_t)len) == 0)
        {
          g_shell = b->shell;
          return b->cmd;
        }
    }

  for (i = 0; g_keys[i].seq != NULL; i++)
    {
      if (strlen(g_keys[i].seq) == (size_t)len &&
          memcmp(g_keys[i].seq, seq, (size_t)len) == 0)
        {
          return g_keys[i].cmd;
        }
    }

  return C_NONE;
}

/* After ESC [ 200 ~: take everything up to ESC [ 201 ~ as typed text. */

static void paste(struct ed_s *e)
{
  struct sbuf_s p;
  unsigned char c;
  static const char end[] = "\033[201~";
  size_t m = 0;

  sb_init(&p);
  while (vs_plat_tty_read(&c, 2000) > 0)
    {
      if (c == (unsigned char)end[m])
        {
          if (++m == sizeof(end) - 1)
            {
              break;
            }

          continue;
        }

      if (m > 0)
        {
          sb_addn(&p, end, m);
          m = c == (unsigned char)end[0] ? 1 : 0;
          if (m == 1)
            {
              continue;
            }
        }

      sb_addc(&p, c == '\r' ? '\n' : (char)c);
    }

  if (p.s != NULL)
    {
      while (p.len > 0 && p.s[p.len - 1] == '\n')
        {
          p.len--;                      /* the last newline would run the command */
        }

      snap(e);
      insert(e, p.s, p.len);
    }

  sb_free(&p);
}

/* Run one command. Returns true when the line is finished. */

static bool run(struct ed_s *e, int cmd, const char *seq, int len)
{
  const char *s = text(e);
  bool kill = false;

  if (e->menu.n > 0)
    {
      /* The menu is open: arrows and Tab move in it, Enter takes the choice
       * (without running the line), Esc drops it, anything else closes the
       * menu and acts as usual.
       */

      int rows = menu_rows(e);

      switch (cmd)
        {
          case C_COMPLETE: menu_move(e, 1); return false;
          case C_COMPLETE_BACK: menu_move(e, -1); return false;
          case C_NEXT: menu_move(e, 1); return false;
          case C_PREV: menu_move(e, -1); return false;
          case C_FWD: menu_move(e, rows); return false;
          case C_BACK: menu_move(e, -rows); return false;
          case C_CANCEL:
            erase(e, e->menu.at, e->menu.at + e->menu.inslen, false, false);
            e->pos = e->menu.at;
            if (e->menu.replaced)
              {
                insert(e, e->menu.orig, strlen(e->menu.orig));
              }

            menu_free(e);
            return false;
          case C_ACCEPT:
            if (e->menu.sel >= 0)
              {
                menu_free(e);
                return false;
              }

            /* fall through */

          default:
            menu_free(e);
            s = text(e);
            break;
        }
    }

  switch (cmd)
    {
      case C_SELF:
        if (e->last != C_SELF)
          {
            snap(e);
          }

        insert(e, seq, (size_t)len);
        break;

      case C_ACCEPT:
        return true;

      case C_BOL:
        e->pos = 0;
        break;

      case C_EOL:
        if (e->sug != NULL)
          {
            snap(e);
            insert(e, e->sug, strlen(e->sug));
          }

        e->pos = e->b.len;
        break;

      case C_BACK:
        e->pos = prev_char(s, e->pos);
        break;

      case C_FWD:
        if (e->pos < e->b.len)
          {
            e->pos = next_char(s, e->b.len, e->pos);
          }
        else if (e->sug != NULL)
          {
            snap(e);
            insert(e, e->sug, strlen(e->sug));
          }

        break;

      case C_BACKWORD:
        e->pos = word_start(e, e->pos);
        break;

      case C_FWDWORD:
        if (e->pos == e->b.len && e->sug != NULL)
          {
            const char *sg = e->sug;
            size_t n = 0;

            while (sg[n] == ' ')            /* the next word of the suggestion */
              {
                n++;
              }

            while (sg[n] != '\0' && sg[n] != ' ')
              {
                n++;
              }

            snap(e);
            insert(e, sg, n);
          }

        e->pos = word_end(e, e->pos);
        break;

      case C_DELETE_OR_EOF:
        if (e->b.len == 0)
          {
            e->eof = true;
            return true;
          }

        /* fall through */

      case C_DELETE:
        if (e->pos < e->b.len)
          {
            snap(e);
            erase(e, e->pos, next_char(s, e->b.len, e->pos), false, false);
          }

        break;

      case C_BACKSPACE:
        if (e->pos > 0)
          {
            snap(e);
            erase(e, prev_char(s, e->pos), e->pos, false, false);
          }

        break;

      case C_KILL_EOL:
        snap(e);
        erase(e, e->pos, e->b.len, true, false);
        kill = true;
        break;

      case C_KILL_BOL:
        snap(e);
        erase(e, 0, e->pos, true, true);
        kill = true;
        break;

      case C_KILL_WORD:
        snap(e);
        erase(e, e->pos, word_end(e, e->pos), true, false);
        kill = true;
        break;

      case C_KILL_WORD_BACK:
        snap(e);
        erase(e, word_start(e, e->pos), e->pos, true, true);
        kill = true;
        break;

      case C_KILL_WORD_BACK_WS:
        {
          size_t p = e->pos;

          while (p > 0 && isspace((unsigned char)s[p - 1]))
            {
              p--;
            }

          while (p > 0 && !isspace((unsigned char)s[p - 1]))
            {
              p--;
            }

          snap(e);
          erase(e, p, e->pos, true, true);
          kill = true;
        }

        break;

      case C_YANK:
        if (e->kill.s != NULL)
          {
            snap(e);
            insert(e, e->kill.s, e->kill.len);
          }

        break;

      case C_TRANSPOSE:
        if (e->b.len >= 2)
          {
            size_t b2 = e->pos >= e->b.len ? prev_char(s, e->b.len) : e->pos;
            size_t a2 = prev_char(s, b2);
            size_t end = next_char(s, e->b.len, b2);
            char *x = vs_xmalloc(end - a2);

            if (b2 > 0)
              {
                snap(e);
                memcpy(x, s + b2, end - b2);
                memcpy(x + (end - b2), s + a2, b2 - a2);
                memcpy(e->b.s + a2, x, end - a2);
                e->pos = end;
              }

            free(x);
          }

        break;

      case C_UPCASE:
      case C_DOWNCASE:
      case C_CAPITALIZE:
        snap(e);
        case_word(e, cmd == C_UPCASE ? 'u' : cmd == C_DOWNCASE ? 'l' : 'c');
        break;

      case C_UNDO:
        if (e->nundo > 0)
          {
            struct snap_s *u = &e->undo[--e->nundo];

            set_text(e, u->s);
            e->pos = u->pos;
            free(u->s);
          }

        break;

      case C_CLEAR:
        put("\033[H\033[2J");
        e->cur_row = 0;
        break;

      case C_PREV:
      case C_NEXT:
        hist_move(e, cmd == C_PREV ? -1 : 1, false);
        break;

      case C_PSEARCH_BACK:
      case C_PSEARCH_FWD:
        hist_move(e, cmd == C_PSEARCH_BACK ? -1 : 1, true);
        break;

      case C_HIST_FIRST:
        if (vs_hist_count() > 0)
          {
            hist_show(e, 0);
          }

        break;

      case C_HIST_LAST:
        hist_show(e, vs_hist_count());
        break;

      case C_RSEARCH:
        if (search(e))
          {
            return true;
          }

        break;

      case C_COMPLETE:
      case C_COMPLETE_BACK:
        complete(e, cmd == C_COMPLETE ? 1 : -1);
        break;

      case C_SHELL:                      /* bind -x: the code sees and may change the line */
        if (g_shell != NULL)
          {
            char num[16];
            const char *v;
            char *code = vs_xstrdup(g_shell);

            snap(e);
            var_set("READLINE_LINE", text(e));
            snprintf(num, sizeof(num), "%zu", e->pos);
            var_set("READLINE_POINT", num);
            vs_plat_tty_raw(false);              /* the code may print or read */
            put("\r\n");
            e->cur_row = 0;
            run_string(code, strlen(code));
            vs_plat_tty_raw(true);
            free(code);
            v = var_get("READLINE_LINE");
            set_text(e, v != NULL ? v : "");
            v = var_get("READLINE_POINT");
            e->pos = v != NULL ? (size_t)strtoul(v, NULL, 10) : e->b.len;
            if (e->pos > e->b.len)
              {
                e->pos = e->b.len;
              }
          }

        break;

      case C_PASTE:
        paste(e);
        break;

      case C_NEWLINE:
        snap(e);
        insert(e, "\n", 1);
        break;

      default:
        break;
    }

  e->last_kill = kill;
  if (cmd != C_PSEARCH_BACK && cmd != C_PSEARCH_FWD)
    {
      free(e->prefix);
      e->prefix = NULL;
    }

  return false;
}

/* ---- Entry points ------------------------------------------------------------- */

bool vs_ledit_usable(void)
{
  const char *term = var_get("TERM");

  if (!vs_plat_isatty(STDIN_FILENO))
    {
      return false;
    }

  return term == NULL || strcmp(term, "dumb") != 0;
}

static void ed_free(struct ed_s *e)
{
  int i;

  menu_free(e);
  sb_free(&e->out);
  sb_free(&e->b);
  sb_free(&e->kill);
  free(e->hsaved);
  free(e->prefix);
  for (i = 0; i < e->nundo; i++)
    {
      free(e->undo[i].s);
    }
}

char *vs_ledit_read(const char *prompt, bool continuation)
{
  struct ed_s e;
  char seq[SEQ_MAX];
  char *result = NULL;
  const char *bp;
  int len;

  memset(&e, 0, sizeof(e));
  sb_init(&e.b);
  sb_init(&e.kill);
  e.prompt = prompt;
  e.hi = vs_hist_count();
  e.last = C_NONE;

  if (!vs_plat_tty_raw(true))
    {
      return NULL;
    }

  bp = var_get("VS_BRACKETED_PASTE");
#ifdef VAPORSHELL_POSIX
  e.bracketed = bp == NULL || strcmp(bp, "0") != 0;
#else
  e.bracketed = bp != NULL && strcmp(bp, "1") == 0;
#endif
  if (e.bracketed)
    {
      put("\033[?2004h");
    }

  for (; ; )
    {
      int cmd;
      int cols = vs_plat_columns();
      const char *cv = var_get("COLUMNS");

      e.cols = cols > 0 ? cols : (cv != NULL && atoi(cv) > 0) ? atoi(cv) : 80;
      refresh(&e);
      if (read_key(seq, &len) <= 0)
        {
          e.eof = e.b.len == 0;
          break;
        }

      cmd = lookup(seq, len);
      if (cmd == C_NONE && ((unsigned char)seq[0] >= 0x20 && seq[0] != 0x7f) &&
          seq[0] != 0x1b)
        {
          cmd = C_SELF;
        }

      if (cmd == C_INTERRUPT)
        {
          draw(&e, e.prompt, text(&e), e.b.len, e.b.len, NULL, false, NULL);
          put("^C\r\n");
          g_sh.last_status = 130;
          g_sh.input_aborted = continuation;
          result = continuation ? NULL : vs_xstrdup("\n");   /* NULL: the parser drops the half-typed command */
          goto done;
        }

      if (run(&e, cmd, seq, len))
        {
          break;
        }

      e.last = cmd;
    }

  /* Leave the line on screen, the cursor on the next row. */

  if (e.eof && continuation)
    {
      put("\r\n");                       /* the parser's error goes on its own line */
    }

  if (!e.eof)
    {
      draw(&e, e.prompt, text(&e), e.b.len, e.b.len, NULL, true, NULL);
      put("\r\n");
      size_t n = e.b.len;

      result = vs_xmalloc(n + 2);
      memcpy(result, text(&e), n);
      result[n] = '\n';
      result[n + 1] = '\0';
      if (!continuation)
        {
          vs_hist_add(text(&e));
          vs_hist_save();                 /* now, not at exit: a kill or a reset keeps the session */
        }
    }

done:
  if (e.bracketed)
    {
      put("\033[?2004l");
    }

  vs_plat_tty_raw(false);
  ed_free(&e);
  return result;
}
