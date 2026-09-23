/*
 * inproc.c -- subshells without fork().
 *
 * NuttX has no fork(), so ( ) and $(...) run in the same task: the shell's
 * state is snapshotted, the body runs against a private copy, and the
 * snapshot is put back afterwards, so the body cannot change anything the
 * parent can see. That covers variables, functions, positional parameters,
 * options, traps, aliases, the hash, the working directory, umask and the
 * file descriptors 0-9 (an `exec 3>file` inside stays inside).
 *
 * $(...) also has to capture stdout. A single task cannot both write and
 * read its own pipe (NuttX pipes hold 1 KiB by default), so a helper thread
 * drains it; that thread touches no shell state.
 *
 * The same code runs on a host OS when VS_INPROC is set in the environment,
 * which is how the test suites exercise it against bash and dash.
 *
 * What is different from a real subshell: it is not concurrent (so a
 * background ( ) & is not possible), `exec cmd` inside it runs cmd and ends
 * the subshell instead of replacing a process, and $BASHPID does not differ.
 */

#include <nuttx/config.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "vaporshell.h"
#include "exec.h"
#include "mode.h"
#include "platform.h"

#if defined(VAPORSHELL_POSIX) || !defined(CONFIG_DISABLE_PTHREAD)
#  define HAVE_INPROC 1
#  include <pthread.h>
#  include <signal.h>
#else
#  define HAVE_INPROC 0
#endif

bool vs_inproc_enabled(void)
{
#if HAVE_INPROC
  return g_sh.force_inproc || !vs_plat_have_fork();
#else
  return false;
#endif
}

#if HAVE_INPROC

#define SNAP_FDS 10                 /* 0-9: what scripts can name portably */
#define DRAIN_STACK 4096            /* the thread only reads and realloc()s */

/* ---- Cloning the parts of the state a subshell may change --------------- */

static struct var_s *clone_vars(const struct var_s *v)
{
  struct var_s *head = NULL;
  struct var_s **tail = &head;

  for (; v != NULL; v = v->next)
    {
      struct var_s *c = vs_xmalloc(sizeof(*c));

      c->name = vs_xstrdup(v->name);
      c->value = v->value != NULL ? vs_xstrdup(v->value) : NULL;
      c->arr = v->arr != NULL ? arr_clone(v->arr) : NULL;
      c->flags = v->flags;
      c->next = NULL;
      *tail = c;
      tail = &c->next;
    }

  return head;
}

static void free_vars(struct var_s *v)
{
  while (v != NULL)
    {
      struct var_s *n = v->next;

      free(v->name);
      free(v->value);
      arr_free(v->arr);
      free(v);
      v = n;
    }
}

static struct func_s *clone_funcs(const struct func_s *f)
{
  struct func_s *head = NULL;
  struct func_s **tail = &head;

  for (; f != NULL; f = f->next)
    {
      struct func_s *c = vs_xmalloc(sizeof(*c));

      c->name = vs_xstrdup(f->name);
      c->src = vs_xstrdup(f->src);
      c->body = f->body;
      c->arena = f->arena;
      arena_retain(c->arena);
      c->next = NULL;
      *tail = c;
      tail = &c->next;
    }

  return head;
}

static void free_funcs(struct func_s *f)
{
  while (f != NULL)
    {
      struct func_s *n = f->next;

      arena_release(f->arena);
      free(f->name);
      free(f->src);
      free(f);
      f = n;
    }
}

static struct alias_s *clone_aliases(const struct alias_s *a)
{
  struct alias_s *head = NULL;
  struct alias_s **tail = &head;

  for (; a != NULL; a = a->next)
    {
      struct alias_s *c = vs_xmalloc(sizeof(*c));

      c->name = vs_xstrdup(a->name);
      c->value = vs_xstrdup(a->value);
      c->next = NULL;
      *tail = c;
      tail = &c->next;
    }

  return head;
}

static void free_aliases(struct alias_s *a)
{
  while (a != NULL)
    {
      struct alias_s *n = a->next;

      free(a->name);
      free(a->value);
      free(a);
      a = n;
    }
}

static struct disabled_s *clone_disabled(const struct disabled_s *d)
{
  struct disabled_s *head = NULL;
  struct disabled_s **tail = &head;

  for (; d != NULL; d = d->next)
    {
      struct disabled_s *c = vs_xmalloc(sizeof(*c));

      c->name = vs_xstrdup(d->name);
      c->next = NULL;
      *tail = c;
      tail = &c->next;
    }

  return head;
}

static void free_disabled(struct disabled_s *d)
{
  while (d != NULL)
    {
      struct disabled_s *n = d->next;

      free(d->name);
      free(d);
      d = n;
    }
}

static struct complete_spec_s *clone_complete_specs(const struct complete_spec_s *s)
{
  struct complete_spec_s *head = NULL;
  struct complete_spec_s **tail = &head;

  for (; s != NULL; s = s->next)
    {
      struct complete_spec_s *c = vs_xmalloc(sizeof(*c));

      c->name = vs_xstrdup(s->name);
      c->opts = vs_xstrdup(s->opts);
      c->next = NULL;
      *tail = c;
      tail = &c->next;
    }

  return head;
}

static void free_complete_specs(struct complete_spec_s *s)
{
  while (s != NULL)
    {
      struct complete_spec_s *n = s->next;

      free(s->name);
      free(s->opts);
      free(s);
      s = n;
    }
}

static struct hash_s *clone_hash(const struct hash_s *h)
{
  struct hash_s *head = NULL;
  struct hash_s **tail = &head;

  for (; h != NULL; h = h->next)
    {
      struct hash_s *c = vs_xmalloc(sizeof(*c));

      c->name = vs_xstrdup(h->name);
      c->path = vs_xstrdup(h->path);
      c->hits = h->hits;
      c->next = NULL;
      *tail = c;
      tail = &c->next;
    }

  return head;
}

static void free_hash(struct hash_s *h)
{
  while (h != NULL)
    {
      struct hash_s *n = h->next;

      free(h->name);
      free(h->path);
      free(h);
      h = n;
    }
}

/* ---- Snapshot ------------------------------------------------------------- */

struct snap_s
{
  struct shell_s saved;             /* a shallow copy: the parent's pointers */
  char cwd[VS_PATH_MAX];
  bool has_cwd;
  mode_t mask;
  int fd[SNAP_FDS];                 /* a high duplicate, or -1 if it was closed */
  int fdflags[SNAP_FDS];
  bool internal[SNAP_FDS];          /* shell plumbing: not snapshotted */
};

static void snap_enter(struct snap_s *s)
{
  int i;

  fflush(NULL);
  s->saved = g_sh;
  s->has_cwd = getcwd(s->cwd, sizeof(s->cwd)) != NULL;
  s->mask = umask(0);
  umask(s->mask);

  for (i = 0; i < SNAP_FDS; i++)
    {
      int fl = fcntl(i, F_GETFD);

      /* A close-on-exec descriptor is the shell's own plumbing (a pipeline's
       * pipe, say), not something the script can name: leave it alone. A
       * copy of a pipe's write end would keep the pipe from ever reaching
       * EOF. Descriptors a script opens (`exec 3>f`) are never close-on-exec.
       */

      s->fdflags[i] = fl;
      s->internal[i] = fl >= 0 && (fl & FD_CLOEXEC) != 0;
      s->fd[i] = (fl >= 0 && !s->internal[i]) ? vs_plat_dup_high(i) : -1;
    }

  /* The subshell works on copies; the originals stay reachable through
   * s->saved and are put back untouched.
   */

  g_sh.vars = clone_vars(s->saved.vars);
  g_sh.funcs = clone_funcs(s->saved.funcs);
  g_sh.aliases = clone_aliases(s->saved.aliases);
  g_sh.hash = clone_hash(s->saved.hash);
  g_sh.disabled_builtins = clone_disabled(s->saved.disabled_builtins);
  g_sh.complete_specs = clone_complete_specs(s->saved.complete_specs);
  if (s->saved.pos != NULL)
    {
      int k;

      /* Same shape as pos_set(): at least one slot, even when empty. */

      g_sh.pos = vs_xmalloc((size_t)(s->saved.npos > 0 ? s->saved.npos : 1) *
                            sizeof(char *));
      for (k = 0; k < s->saved.npos; k++)
        {
          g_sh.pos[k] = vs_xstrdup(s->saved.pos[k]);
        }
    }

  if (s->saved.ndirs > 0)
    {
      int k;

      g_sh.dirstack = vs_xmalloc((size_t)s->saved.ndirs * sizeof(char *));
      for (k = 0; k < s->saved.ndirs; k++)
        {
          g_sh.dirstack[k] = vs_xstrdup(s->saved.dirstack[k]);
        }
    }
  else
    {
      g_sh.dirstack = NULL;
    }

  trap_subshell_enter();
  g_sh.in_subshell++;
  g_sh.interactive = false;
  g_sh.can_exec = false;
  g_sh.unwind = UW_NONE;
}

static void snap_leave(struct snap_s *s)
{
  int i;

  fflush(NULL);                     /* the subshell's output goes where it was meant to */

  /* Locals the subshell declared that no function return will pop. */

  while (g_sh.locals != NULL && g_sh.locals != s->saved.locals)
    {
      struct local_s *l = g_sh.locals;

      g_sh.locals = l->next;
      free(l->name);
      free(l->old);
      free(l);
    }

  free_vars(g_sh.vars);
  free_funcs(g_sh.funcs);
  free_aliases(g_sh.aliases);
  free_hash(g_sh.hash);
  free_disabled(g_sh.disabled_builtins);
  free_complete_specs(g_sh.complete_specs);
  dirstack_free();
  for (i = 0; i < g_sh.npos; i++)
    {
      free(g_sh.pos[i]);
    }

  free(g_sh.pos);

  trap_subshell_leave(&s->saved);
  g_sh = s->saved;
  vs_plat_locale_update();          /* the parent's LC_ALL etc. are back */

  for (i = 0; i < SNAP_FDS; i++)
    {
      if (s->fd[i] >= 0)
        {
          dup2(s->fd[i], i);
          close(s->fd[i]);
#ifdef FD_CLOEXEC
          if (s->fdflags[i] > 0)
            {
              fcntl(i, F_SETFD, s->fdflags[i]);
            }
#endif
        }
      else if (!s->internal[i])
        {
          close(i);                 /* opened inside: it does not outlive the subshell */
        }
    }

  if (s->has_cwd && chdir(s->cwd) != 0 && chdir(VS_FS(s->cwd)) != 0)
    {
      /* the directory is gone: stay where we are */
    }

  umask(s->mask);
}

/* The status the subshell ends with, after `exit`, a failed `set -e`, or a
 * `return` (which can only leave the subshell, not the caller's function).
 */

static int subshell_finish(int status)
{
  if (g_sh.unwind != UW_NONE)
    {
      status = g_sh.last_status;
      g_sh.unwind = UW_NONE;
    }

  g_sh.last_status = status;
  trap_run_exit();                  /* an EXIT trap set inside the subshell */
  g_sh.unwind = UW_NONE;
  return status & 0xff;
}

/* ---- Capturing stdout ------------------------------------------------------ */

struct capture_s
{
  int save_fd;                      /* the real stdout, parked high */
  int rfd;                          /* read end, owned by the drain thread */
  pthread_t tid;
  char *buf;
  size_t len;
  size_t cap;
  bool failed;
};

static void *drain_main(void *arg)
{
  struct capture_s *c = arg;
  char tmp[512];

  for (; ; )
    {
      ssize_t n = read(c->rfd, tmp, sizeof(tmp));

      if (n > 0)
        {
          if (c->len + (size_t)n + 1 > c->cap)
            {
              size_t nc = (c->cap + (size_t)n + 1) * 2;
              char *nb = realloc(c->buf, nc);

              if (nb == NULL)
                {
                  c->failed = true;
                  continue;         /* keep draining so the writer is not stuck */
                }

              c->buf = nb;
              c->cap = nc;
            }

          if (!c->failed)
            {
              memcpy(c->buf + c->len, tmp, (size_t)n);
              c->len += (size_t)n;
            }
        }
      else if (n == 0 || errno != EINTR)
        {
          break;
        }
    }

  return NULL;
}

static int capture_begin(struct capture_s *c)
{
  int fds[2];
  pthread_attr_t at;
  int hi;

  memset(c, 0, sizeof(*c));
  fflush(NULL);
  if (pipe(fds) != 0)
    {
      return -1;
    }

  /* Keep the read end out of 0-9 so a script's `exec 3>f` cannot hit it. */

  hi = vs_plat_dup_high(fds[0]);
  close(fds[0]);
  c->rfd = hi >= 0 ? hi : -1;
  c->save_fd = vs_plat_dup_high(STDOUT_FILENO);
  if (c->rfd < 0 || c->save_fd < 0)
    {
      goto fail;
    }

  pthread_attr_init(&at);
  pthread_attr_setstacksize(&at, DRAIN_STACK);
  if (pthread_create(&c->tid, &at, drain_main, c) != 0)
    {
      pthread_attr_destroy(&at);
      goto fail;
    }

  pthread_attr_destroy(&at);
  dup2(fds[1], STDOUT_FILENO);
  close(fds[1]);
  return 0;

fail:
  close(fds[1]);
  if (c->rfd >= 0) close(c->rfd);
  if (c->save_fd >= 0) close(c->save_fd);
  return -1;
}

/* Restores stdout, waits for the last byte, returns the text (malloc'd). *len,
 * if given, is how many bytes it is: the text may hold NULs, which a string
 * cannot say.
 */

static char *capture_end(struct capture_s *c, size_t *len)
{
  fflush(NULL);
  dup2(c->save_fd, STDOUT_FILENO);  /* drops the last write end: EOF for the thread */
  close(c->save_fd);
  pthread_join(c->tid, NULL);
  close(c->rfd);

  if (len != NULL)
    {
      *len = (c->buf == NULL || c->failed) ? 0 : c->len;
    }

  if (c->buf == NULL || c->failed)
    {
      free(c->buf);
      return vs_xstrdup("");
    }

  c->buf[c->len] = '\0';
  return c->buf;
}

/* ---- The two entry points ---------------------------------------------------- */

int vs_inproc_subshell(struct node_s *body)
{
  struct snap_s s;
  int status;

  snap_enter(&s);
  status = exec_node(body);
  status = subshell_finish(status);
  snap_leave(&s);
  return status;
}

/* Runs 'text' as a subshell and returns everything it wrote to stdout
 * (malloc'd, trailing newlines still there). *status gets its exit status.
 */

char *vs_inproc_cmdsub(const char *text, size_t len, int *status)
{
  struct snap_s s;
  struct capture_s cap;
  char *out;

  snap_enter(&s);
  if (capture_begin(&cap) != 0)
    {
      vs_err("command substitution: %s", strerror(errno));
      snap_leave(&s);
      *status = 1;
      return vs_xstrdup("");
    }

  if (!vs_feat(VF_ERREXIT_IN_CMDSUB))
    {
      g_sh.opt_e = false;           /* bash's default: $(...) does not inherit -e */
    }

  *status = run_string(text, len);
  *status = subshell_finish(*status);
  out = capture_end(&cap, NULL);
  snap_leave(&s);
  return out;
}

/* One stage of a pipeline, run as a subshell. in_fd (or -1) becomes its
 * stdin. If 'out' is not NULL its stdout is captured and returned there
 * (malloc'd, *outlen bytes, NULs included); otherwise it writes where the
 * pipeline writes.
 */

int vs_inproc_stage(struct node_s *stage, int in_fd, char **out, size_t *outlen)
{
  struct snap_s s;
  struct capture_s cap;
  int status;

  snap_enter(&s);
  if (in_fd >= 0)
    {
      dup2(in_fd, STDIN_FILENO);
    }

  if (out != NULL)
    {
      if (capture_begin(&cap) != 0)
        {
          vs_err("pipeline: %s", strerror(errno));
          snap_leave(&s);
          *out = NULL;
          return 1;
        }
    }

  status = exec_node(stage);
  status = subshell_finish(status);
  if (out != NULL)
    {
      *out = capture_end(&cap, outlen);
    }

  snap_leave(&s);
  return status;
}

/* Feeds 'buf' into fd from a helper thread, then closes fd and frees buf, so
 * the next stage can read while this one is already finished. The thread
 * blocks SIGPIPE: a reader that quits early is not an error.
 *
 * The thread must not outlive the pipeline. It owns a descriptor number, and a
 * subshell's cleanup closes every descriptor opened inside it: were the
 * thread still running by then, that number would be free for the next
 * pipeline to reuse, and this thread's own close() would land on the new
 * pipe. So the caller gets a handle and waits for it (vs_inproc_feed_wait)
 * once the stages are done and their read ends are closed.
 */

struct feed_s
{
  int fd;
  char *buf;
  size_t len;
  pthread_t tid;
};

static void *feed_main(void *arg)
{
  struct feed_s *f = arg;
  size_t off = 0;

#ifdef SIGPIPE
  sigset_t set;

  sigemptyset(&set);
  sigaddset(&set, SIGPIPE);
  pthread_sigmask(SIG_BLOCK, &set, NULL);
#endif

  while (off < f->len)
    {
      ssize_t n = write(f->fd, f->buf + off, f->len - off);

      if (n > 0)
        {
          off += (size_t)n;
        }
      else if (n < 0 && errno == EINTR)
        {
          continue;
        }
      else
        {
          break;
        }
    }

  close(f->fd);
  free(f->buf);
  f->buf = NULL;
  return NULL;
}

/* Returns a handle for vs_inproc_feed_wait(), or NULL if there was nothing to
 * feed (fd is closed and buf freed either way).
 */

void *vs_inproc_feed(int fd, char *buf, size_t len)
{
  struct feed_s *f;
  pthread_attr_t at;
  int ret;

  if (len == 0)
    {
      close(fd);
      free(buf);
      return NULL;
    }

  f = vs_xmalloc(sizeof(*f));
  f->fd = fd;
  f->buf = buf;
  f->len = len;
  pthread_attr_init(&at);
  pthread_attr_setstacksize(&at, DRAIN_STACK);
  ret = pthread_create(&f->tid, &at, feed_main, f);
  pthread_attr_destroy(&at);
  if (ret != 0)
    {
      close(fd);
      free(buf);
      free(f);
      return NULL;
    }

  return f;
}

/* Waits for the feeder to have written what it could and closed its end. */

void vs_inproc_feed_wait(void *handle)
{
  struct feed_s *f = handle;

  if (f != NULL)
    {
      pthread_join(f->tid, NULL);
      free(f);
    }
}

#else /* !HAVE_INPROC */

int vs_inproc_subshell(struct node_s *body)
{
  (void)body;
  return 1;
}

char *vs_inproc_cmdsub(const char *text, size_t len, int *status)
{
  (void)text;
  (void)len;
  *status = 1;
  return NULL;
}

int vs_inproc_stage(struct node_s *stage, int in_fd, char **out, size_t *outlen)
{
  (void)stage;
  (void)in_fd;
  (void)outlen;
  *out = NULL;
  return 1;
}

void *vs_inproc_feed(int fd, char *buf, size_t len)
{
  (void)buf;
  (void)len;
  close(fd);
  return NULL;
}

void vs_inproc_feed_wait(void *handle)
{
  (void)handle;
}

#endif
