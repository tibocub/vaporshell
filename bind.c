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
 * sequence ("\e[A"). Chords of several keys and macros are not supported, and
 * neither is ~/.inputrc: put the bind commands in the rc file.
 */

#include <nuttx/config.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vaporshell.h"
#include "exec.h"
#include "ledit.h"

/* The bytes for a key written the readline way: \C-x \M-x \e \t \\ \" \nnn \xHH. */

static int parse_keys(const char *s, size_t n, char *out, int max)
{
  int len = 0;
  size_t i;

  for (i = 0; i < n && len < max; i++)
    {
      int c = (unsigned char)s[i];

      if (c == '\\' && i + 1 < n)
        {
          c = (unsigned char)s[++i];
          if ((c == 'C' || c == 'M') && i + 2 < n && s[i + 1] == '-')
            {
              int k = (unsigned char)s[i + 2];

              i += 2;
              if (c == 'C')
                {
                  c = k == '?' ? 0x7f : k & 0x1f;
                }
              else
                {
                  out[len++] = '\033';          /* meta: ESC first */
                  c = k;
                }
            }
          else if (c == 'e' || c == 'E')
            {
              c = '\033';
            }
          else if (c == 'a') c = '\a';
          else if (c == 'b') c = '\b';
          else if (c == 'd') c = 0x7f;
          else if (c == 'f') c = '\f';
          else if (c == 'n') c = '\n';
          else if (c == 'r') c = '\r';
          else if (c == 't') c = '\t';
          else if (c == 'v') c = '\v';
          else if (c >= '0' && c <= '7')
            {
              int v = c - '0';
              int d;

              for (d = 0; d < 2 && i + 1 < n && s[i + 1] >= '0' && s[i + 1] <= '7'; d++)
                {
                  v = v * 8 + (s[++i] - '0');
                }

              c = v & 0xff;
            }
          else if (c == 'x' && i + 1 < n && isxdigit((unsigned char)s[i + 1]))
            {
              char hex[3] = { s[i + 1], i + 2 < n && isxdigit((unsigned char)s[i + 2]) ? s[i + 2] : '\0', '\0' };

              c = (int)strtol(hex, NULL, 16);
              i += hex[1] != '\0' ? 2 : 1;
            }
        }

      out[len++] = (char)c;
    }

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

      if (a[0] == '-' && a[1] != '\0')
        {
          if (strchr("vVsSfqumu", a[1]) != NULL)
            {
              if (i + 1 < argc && (a[1] == 'f' || a[1] == 'q' || a[1] == 'u' || a[1] == 'm'))
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
          vs_err("bind: `%s': unknown function name", rest);
          status = 1;
        }
      else
        {
          status |= vs_ledit_bind(keys, klen, vs_ledit_cmd_lookup(rest), NULL);
        }
    }

  return status;
}
