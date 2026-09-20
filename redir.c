/*
 * redir.c -- redirections. Applied in the shell process itself and undone
 * afterwards, so the same mechanism serves builtins, functions, compound
 * commands and external programs (which simply inherit the descriptors).
 */

#include <nuttx/config.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "vaporshell.h"
#include "expand.h"
#include "exec.h"
#include "mode.h"
#include "platform.h"

/* Larger bodies than this do not go through a pipe (it could fill up
 * with nobody reading yet).
 */

#define HEREDOC_PIPE_MAX 4096

static void save_add(struct redir_saved_s *sv, int fd, int saved)
{
  sv->e = vs_xrealloc(sv->e, (size_t)(sv->n + 1) * sizeof(sv->e[0]));
  sv->e[sv->n].fd = fd;
  sv->e[sv->n].saved = saved;
  sv->n++;
}

void redir_restore(struct redir_saved_s *sv)
{
  int i;

  fflush(NULL);
  for (i = sv->n - 1; i >= 0; i--)
    {
      if (sv->e[i].saved >= 0)
        {
          dup2(sv->e[i].saved, sv->e[i].fd);
          close(sv->e[i].saved);
        }
      else
        {
          close(sv->e[i].fd);
        }
    }

  free(sv->e);
  sv->e = NULL;
  sv->n = 0;
}

/* A readable descriptor holding the here-document text, or -1. */

static int heredoc_fd(const char *text)
{
  size_t len = strlen(text);
  int p[2];

#ifdef VAPORSHELL_POSIX
  if (len > HEREDOC_PIPE_MAX)
    {
      char path[] = "/tmp/vaporshell-hd-XXXXXX";
      int fd = mkstemp(path);

      if (fd < 0)
        {
          return -1;
        }

      unlink(path);
      if (write(fd, text, len) != (ssize_t)len || lseek(fd, 0, SEEK_SET) < 0)
        {
          close(fd);
          return -1;
        }

      return fd;
    }
#else
  if (len > HEREDOC_PIPE_MAX)
    {
      errno = EFBIG;
      return -1;
    }
#endif

  if (pipe(p) != 0)
    {
      return -1;
    }

  if (len > 0 && write(p[1], text, len) != (ssize_t)len)
    {
      close(p[0]);
      close(p[1]);
      return -1;
    }

  close(p[1]);
  return p[0];
}

static bool is_number(const char *s)
{
  size_t i;

  for (i = 0; s[i] != '\0'; i++)
    {
      if (s[i] < '0' || s[i] > '9')
        {
          return false;
        }
    }

  return i > 0;
}

/* Makes 'fd' refer to what 'src' refers to, remembering how to undo it. */

static int install_fd(struct redir_saved_s *sv, bool persist, int fd, int src)
{
  if (!persist)
    {
      /* A negative result just means fd was not open: undoing closes it. */

      save_add(sv, fd, vs_plat_dup_high(fd));
    }

  if (src != fd && dup2(src, fd) < 0)
    {
      vs_err("%d: %s", fd, strerror(errno));
      return -1;
    }

  return 0;
}

/* Performs one redirection. Returns 0, or -1 after printing why. */

static int redir_one(struct redir_s *r, struct redir_saved_s *sv,
                     bool persist)
{
  int fd = r->fd;
  int src = -1;
  bool own = true;              /* close src after dup2 */
  bool close_only = false;
  char *word = NULL;
  int flags;

  bool both = (r->op == R_OUT_ERR || r->op == R_APPEND_ERR);

  if (fd < 0)
    {
      fd = (r->op == R_IN || r->op == R_DUPIN || r->op == R_RDWR ||
            r->op == R_HEREDOC) ? 0 : 1;
    }

  if (r->op == R_HEREDOC)
    {
      char *text = r->hd_quoted ? vs_xstrdup(r->hd_body != NULL ? r->hd_body : "")
                                : expand_heredoc(r->hd_body != NULL ? r->hd_body : "");

      if (text == NULL)
        {
          return -1;
        }

      src = heredoc_fd(text);
      free(text);
      if (src < 0)
        {
          vs_err("here-document: %s", strerror(errno));
          return -1;
        }
    }
  else
    {
      word = expand_word_str(r->target);
      if (word == NULL)
        {
          return -1;
        }

      if (r->op == R_DUPIN || r->op == R_DUPOUT)
        {
          if (strcmp(word, "-") == 0)
            {
              close_only = true;
            }
          else if (is_number(word))
            {
              int probe;

              src = atoi(word);
              own = false;
              probe = dup(src);
              if (probe < 0)
                {
                  vs_err("%s: bad file descriptor", word);
                  free(word);
                  return -1;
                }

              close(probe);
            }
          else
            {
              vs_err("%s: ambiguous redirect", word);
              free(word);
              return -1;
            }
        }
      else
        {
          flags = O_RDONLY;
          switch (r->op)
            {
              case R_OUT:
              case R_OUT_ERR:
                flags = O_WRONLY | O_CREAT | O_TRUNC;
                if (g_sh.opt_C)
                  {
                    struct stat st;

                    if (stat(VS_FS(word), &st) == 0 && S_ISREG(st.st_mode))
                      {
                        vs_err("%s: cannot overwrite existing file", word);
                        free(word);
                        return -1;
                      }
                  }

                break;
              case R_CLOBBER: flags = O_WRONLY | O_CREAT | O_TRUNC; break;
              case R_APPEND:
              case R_APPEND_ERR:
                flags = O_WRONLY | O_CREAT | O_APPEND;
                break;
              case R_RDWR:    flags = O_RDWR | O_CREAT; break;
              default:        break;
            }

          src = open(VS_FS(word), flags, 0666);
          if (src < 0)
            {
              vs_err("%s: %s", word, strerror(errno));
              free(word);
              return -1;
            }
        }
    }

  free(word);

  if (close_only)
    {
      if (!persist)
        {
          save_add(sv, fd, vs_plat_dup_high(fd));
        }

      close(fd);
      return 0;
    }

  if (install_fd(sv, persist, fd, src) != 0 ||
      (both && install_fd(sv, persist, STDERR_FILENO, src) != 0))
    {
      if (own && src != fd)
        {
          close(src);
        }

      return -1;
    }

  if (own && src != fd && !(both && src == STDERR_FILENO))
    {
      close(src);
    }

  return 0;
}

int redir_apply(struct redir_s *list, struct redir_saved_s *sv, bool persist)
{
  struct redir_s *r;

  sv->e = NULL;
  sv->n = 0;
  fflush(NULL);

  for (r = list; r != NULL; r = r->next)
    {
      if (redir_one(r, sv, persist) != 0)
        {
          redir_restore(sv);
          return -1;
        }
    }

  return 0;
}
