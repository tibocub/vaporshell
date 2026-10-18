/*
 * bind.c -- readline key bindings for the line editor (ledit.c).
 *
 *   bind '"\C-o": kill-line'        a key runs an editor command
 *   bind -x '"\C-g": some-code'     a key runs shell code; READLINE_LINE and
 *                                   READLINE_POINT are the line and cursor, and
 *                                   what the code leaves in them is the new line
 *   bind -r '\C-o'                  unbind
 *   bind -l                         the command names
 *   bind -p                         the bindings, in the form bind takes back
 *
 * A key is the exact bytes a terminal sends for it: one key or one escape
 * sequence ("\e[A"). Chords of several keys and macros are not supported.
 * `bind -f FILE` reads an inputrc: only the "keys": command lines count
 * (set, $if and comments are skipped). ~/.inputrc ($INPUTRC) is read at startup.
 */

#include <nuttx/config.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vaporshell.h"
#include "exec.h"
#include "ledit.h"
#include "platform.h"

/* The bytes for a key written the readline way: \C-x \M-x \d, and the usual
 * backslash escapes (\e \t \nnn \xHH ...), which the interpreter decodes.
 */

static int parse_keys(const char *s, size_t n, char *out, int max)
{
  struct sbuf_s b;
  bool stop = false;
  size_t i = 0;
  int len;

  sb_init(&b);
  while (i < n)
    {
      if (s[i] != '\\' || i + 1 >= n)
        {
          sb_addc(&b, s[i++]);
          continue;
        }

      i++;
      if ((s[i] == 'C' || s[i] == 'M') && i + 2 < n && s[i + 1] == '-')
        {
          int k = (unsigned char)s[i + 2];

          if (s[i] == 'M')
            {
              sb_addc(&b, '\033');                  /* meta: ESC first */
            }

          sb_addc(&b, s[i] == 'M' ? (char)k : (char)(k == '?' ? 0x7f : k & 0x1f));
          i += 3;
        }
      else if (s[i] == 'd')
        {
          sb_addc(&b, 0x7f);
          i++;
        }
      else
        {
          i += vs_esc_one(s + i, ESC_OCT_PLAIN | ESC_HEXU | ESC_E, &b, &stop);
        }
    }

  len = b.s == NULL ? 0 : (int)b.len < max ? (int)b.len : max;
  if (len > 0)
    {
      memcpy(out, b.s, (size_t)len);
    }

  sb_free(&b);
  return len;
}

/* The inverse, for `bind -p`. */

static void print_keys(const char *seq, int len)
{
  int i;

  putchar('"');
  for (i = 0; i < len; i++)
    {
      unsigned char c = (unsigned char)seq[i];

      if (c == 0x1b)
        {
          fputs("\\e", stdout);
        }
      else if (c == 0x7f)
        {
          fputs("\\C-?", stdout);
        }
      else if (c < 0x20)
        {
          printf("\\C-%c", c + '`');
        }
      else
        {
          if (c == '"' || c == '\\')
            {
              putchar('\\');
            }

          putchar(c);
        }
    }

  putchar('"');
}

static bool g_in_file;             /* reading an inputrc: it is shared with readline, so names we lack are skipped silently */

static int read_inputrc(const char *file)
{
  FILE *f = fopen(VS_FS(file), "r");
  char *line = NULL;
  size_t cap = 0;
  int status = 0;

  if (f == NULL)
    {
      vs_err("bind: %s: cannot read", file);
      return 1;
    }

  while (getline(&line, &cap, f) >= 0)
    {
      char *p = line + strspn(line, " \t");
      char *argv[3] = { "bind", p, NULL };

      p[strcspn(p, "\n")] = '\0';
      if (*p == '"')
        {
          g_in_file = true;
          status |= bi_bind(2, argv);
          g_in_file = false;
        }
    }

  free(line);
  fclose(f);
  return status;
}

int bi_bind(int argc, char **argv)
{
  bool shell = false;
  bool remove = false;
  int status = 0;
  int i;

  for (i = 1; i < argc; i++)
    {
      const char *a = argv[i];
      char keys[16];
      int klen;
      const char *rest;
      const char *close;

      if (strcmp(a, "-l") == 0)
        {
          int k;

          for (k = 1; k < vs_ledit_ncmds(); k++)
            {
              if (vs_ledit_cmd_name(k) != NULL)
                {
                  puts(vs_ledit_cmd_name(k));
                }
            }

          continue;
        }

      if (strcmp(a, "-p") == 0 || strcmp(a, "-P") == 0)
        {
          const char *seq;
          const char *name;
          const char *code;
          int len;
          int k;

          for (k = 0; vs_ledit_binding(k, &seq, &len, &name, &code); k++)
            {
              print_keys(seq, len);
              if (name != NULL)
                {
                  printf(": %s\n", name);
                }
              else
                {
                  printf(": %s\n", code != NULL ? code : "");
                }
            }

          continue;
        }

      if (strcmp(a, "-x") == 0)
        {
          shell = true;
          continue;
        }

      if (strcmp(a, "-r") == 0)
        {
          remove = true;
          continue;
        }

      if (strcmp(a, "-f") == 0 && i + 1 < argc)
        {
          status |= read_inputrc(argv[++i]);
          continue;
        }

      if (a[0] == '-' && a[1] != '\0')
        {
          if (strchr("vVsSfqumu", a[1]) != NULL)
            {
              if (i + 1 < argc && (a[1] == 'q' || a[1] == 'u' || a[1] == 'm'))
                {
                  i++;                 /* accepted and ignored: takes a file or name */
                }

              continue;
            }

          vs_err("bind: %s: invalid option", a);
          return 2;
        }

      /* A binding: "keys": command, or with -r just the keys. */

      if (a[0] == '"')
        {
          close = strchr(a + 1, '"');
          while (close != NULL && close[-1] == '\\')
            {
              close = strchr(close + 1, '"');
            }

          if (close == NULL)
            {
              vs_err("bind: %s: missing closing quote", a);
              status = 1;
              continue;
            }

          klen = parse_keys(a + 1, (size_t)(close - a - 1), keys, (int)sizeof(keys));
          rest = close + 1;
        }
      else
        {
          const char *colon = strchr(a, ':');          /* keys:command, unquoted */

          klen = parse_keys(a, colon != NULL ? (size_t)(colon - a) : strlen(a), keys, (int)sizeof(keys));
          rest = colon != NULL ? colon : "";
        }

      if (remove)
        {
          status |= vs_ledit_unbind(keys, klen) == 0 ? 0 : 1;
          remove = false;
          continue;
        }

      while (*rest == ':' || *rest == ' ')
        {
          rest++;
        }

      if (*rest == '\0')
        {
          vs_err("bind: %s: no command", a);
          status = 1;
        }
      else if (shell)
        {
          status |= vs_ledit_bind(keys, klen, vs_ledit_cmd_lookup("shell-command"), rest);
          shell = false;
        }
      else if (vs_ledit_cmd_lookup(rest) < 0)
        {
          if (!g_in_file)
            {
              vs_err("bind: `%s': unknown function name", rest);
            }

          status = 1;
        }
      else
        {
          status |= vs_ledit_bind(keys, klen, vs_ledit_cmd_lookup(rest), NULL);
        }
    }

  return status;
}
