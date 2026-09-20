/*
 * assign.c -- applying assignment words: name=v, name+=v, name[i]=v,
 * name[i]+=v, name=(...) and name+=(...).
 *
 * The parser only decides that a word is an assignment (asg_is_word); the
 * pieces are picked apart here, once, so the parser, the executor and the
 * builtins that take assignment arguments cannot disagree about them.
 */

#include <nuttx/config.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vaporshell.h"
#include "mode.h"
#include "expand.h"
#include "parse.h"
#include "exec.h"

/* Where the parts of an assignment word are; nothing is allocated. */

struct asg_pos_s
{
  size_t name_len;
  bool has_sub;
  size_t sub_off;        /* just after [ */
  size_t sub_len;
  bool append;           /* += */
  size_t val_off;        /* just after = */
};

static bool name_start_ch(char c)
{
  return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_';
}

static bool name_ch(char c)
{
  return name_start_ch(c) || (c >= '0' && c <= '9');
}

/* The ] that closes the [ at text[open]; quotes and $(...) inside do not
 * count. (size_t)-1 if there is none.
 */

size_t asg_subscript_end(const char *s, size_t len, size_t open)
{
  size_t i = open + 1;
  int depth = 1;

  while (i < len)
    {
      size_t e;

      if (s[i] == '\\' && i + 1 < len)
        {
          i += 2;
          continue;
        }

      if (s[i] == '\'' ? ws_skip_squote(s, len, i, &e) == WS_OK :
          s[i] == '"'  ? ws_skip_dquote(s, len, i, &e) == WS_OK :
          s[i] == '`'  ? ws_skip_backtick(s, len, i, &e) == WS_OK :
          (s[i] == '$' && i + 1 < len && (s[i + 1] == '(' || s[i + 1] == '{')) ?
                         ws_skip_dollar(s, len, i, &e) == WS_OK : false)
        {
          i = e;
          continue;
        }

      if (s[i] == '[')
        {
          depth++;
        }
      else if (s[i] == ']' && --depth == 0)
        {
          return i;
        }

      i++;
    }

  return (size_t)-1;
}

static bool asg_scan(const char *text, struct asg_pos_s *p)
{
  size_t len = strlen(text);
  size_t i = 0;

  memset(p, 0, sizeof(*p));
  if (!name_start_ch(text[0]))
    {
      return false;
    }

  while (name_ch(text[i]))
    {
      i++;
    }

  p->name_len = i;
  if (text[i] == '[' && vs_feat(VF_BASH_SYNTAX))
    {
      size_t close = asg_subscript_end(text, len, i);

      if (close == (size_t)-1)
        {
          return false;
        }

      p->has_sub = true;
      p->sub_off = i + 1;
      p->sub_len = close - i - 1;
      i = close + 1;
    }

  if (text[i] == '+' && text[i + 1] == '=' && vs_feat(VF_BASH_SYNTAX))
    {
      p->append = true;
      i++;
    }

  if (text[i] != '=')
    {
      return false;
    }

  p->val_off = i + 1;
  return true;
}

bool asg_is_word(const char *text)
{
  struct asg_pos_s p;

  return asg_scan(text, &p);
}

struct asg_s
{
  char *name;            /* malloc'd */
  bool has_sub;
  const char *sub;
  size_t sub_len;
  bool append;
  bool compound;
  const char *val;       /* the text after the = */
};

static bool asg_split(const char *raw, struct asg_s *a)
{
  struct asg_pos_s p;

  if (!asg_scan(raw, &p))
    {
      return false;
    }

  a->name = vs_xstrndup(raw, p.name_len);
  a->has_sub = p.has_sub;
  a->sub = raw + p.sub_off;
  a->sub_len = p.sub_len;
  a->append = p.append;
  a->val = raw + p.val_off;
  a->compound = !p.has_sub && a->val[0] == '(' && vs_feat(VF_BASH_SYNTAX);
  return true;
}

/* ---- name[i]=v ---------------------------------------------------------------- */

static int assign_elem(const struct asg_s *a, const char *val)
{
  long idx;

  if (expand_subscript(a->sub, a->sub_len, a->name, &idx) != 0)
    {
      return 1;
    }

  if (a->append)
    {
      const char *cur = var_elem_get(a->name, idx);

      if (cur != NULL)
        {
          char *both = vs_xmalloc(strlen(cur) + strlen(val) + 1);
          int r;

          strcpy(both, cur);
          strcat(both, val);
          r = var_elem_set(a->name, idx, both);
          free(both);
          return r != 0 ? 1 : 0;
        }
    }

  return var_elem_set(a->name, idx, val) != 0 ? 1 : 0;
}

/* ---- name=(...) ---------------------------------------------------------------- */

/* The next word of an array literal's text: whitespace, newlines and #
 * comments are skipped, quotes and $(...) nest. Returns a malloc'd copy of
 * the raw word (quotes intact, for the expander), or NULL at the end.
 */

static char *literal_word(const char *s, size_t len, size_t *pos)
{
  size_t i = *pos;
  size_t start;

  for (; ; )
    {
      while (i < len && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n'))
        {
          i++;
        }

      if (i < len && s[i] == '#')
        {
          while (i < len && s[i] != '\n')
            {
              i++;
            }

          continue;
        }

      break;
    }

  if (i >= len)
    {
      *pos = len;
      return NULL;
    }

  start = i;
  while (i < len && s[i] != ' ' && s[i] != '\t' && s[i] != '\n')
    {
      size_t e;

      if (s[i] == '\\' && i + 1 < len)
        {
          i += 2;
        }
      else if ((s[i] == '\'' && ws_skip_squote(s, len, i, &e) == WS_OK) ||
               (s[i] == '"' && ws_skip_dquote(s, len, i, &e) == WS_OK) ||
               (s[i] == '`' && ws_skip_backtick(s, len, i, &e) == WS_OK) ||
               (s[i] == '$' && i + 1 < len && (s[i + 1] == '(' || s[i + 1] == '{') &&
                ws_skip_dollar(s, len, i, &e) == WS_OK))
        {
          i = e;
        }
      else
        {
          i++;
        }
    }

  *pos = i;
  return vs_xstrndup(s + start, i - start);
}

/* `[sub]=value` or `[sub]+=value` as an element of an array literal. */

static bool literal_subscript(const char *w, size_t *sub_len, bool *append,
                              size_t *val_off)
{
  size_t close;

  if (w[0] != '[')
    {
      return false;
    }

  close = asg_subscript_end(w, strlen(w), 0);
  if (close == (size_t)-1)
    {
      return false;
    }

  *sub_len = close - 1;
  *append = false;
  if (w[close + 1] == '+' && w[close + 2] == '=')
    {
      *append = true;
      *val_off = close + 3;
      return true;
    }

  if (w[close + 1] == '=')
    {
      *val_off = close + 2;
      return true;
    }

  return false;
}

static int assign_compound(const struct asg_s *a)
{
  size_t vlen = strlen(a->val);
  size_t len;
  size_t pos = 0;
  const char *inner = a->val + 1;
  struct arr_s *na;
  long next;
  char *w;
  int status = 0;

  if (vlen < 2 || a->val[vlen - 1] != ')')
    {
      vs_err("%s: syntax error in array assignment", a->name);
      return 1;
    }

  len = vlen - 2;
  if (a->append)
    {
      struct arr_s *old = var_array(a->name, false);

      na = old != NULL ? arr_clone(old) : arr_new();
      if (old == NULL && var_get(a->name) != NULL)
        {
          arr_set(na, 0, var_get(a->name));       /* a+=(x) on a scalar keeps its value */
        }
    }
  else
    {
      na = arr_new();
    }

  next = a->append ? arr_max_index(na) + 1 : 0;

  while (status == 0 && (w = literal_word(inner, len, &pos)) != NULL)
    {
      size_t sub_len;
      size_t val_off;
      bool app;

      if (literal_subscript(w, &sub_len, &app, &val_off))
        {
          long idx;
          char *val = expand_assign_str(w + val_off);
          char *sub = vs_xstrndup(w + 1, sub_len);

          if (val == NULL || expand_subscript(sub, sub_len, NULL, &idx) != 0)
            {
              status = 1;
            }
          else
            {
              const char *cur = app ? arr_get(na, idx) : NULL;

              if (cur != NULL)
                {
                  char *both = vs_xmalloc(strlen(cur) + strlen(val) + 1);

                  strcpy(both, cur);
                  strcat(both, val);
                  arr_set(na, idx, both);
                  free(both);
                }
              else
                {
                  arr_set(na, idx, val);
                }

              next = idx + 1;
            }

          free(val);
          free(sub);
        }
      else
        {
          struct word_s word;
          struct fieldv_s f;
          int k;

          word.next = NULL;
          word.text = w;
          fv_init(&f);
          if (expand_words(&word, &f) != 0)
            {
              status = 1;
            }
          else
            {
              for (k = 0; k < f.n; k++)
                {
                  arr_set(na, next++, f.v[k]);
                }
            }

          fv_free(&f);
        }

      free(w);
    }

  if (status == 0 && var_array_replace(a->name, na) != 0)
    {
      status = 1;
      na = NULL;
    }

  if (status != 0)
    {
      arr_free(na);           /* on success the variable owns it */
    }

  return status;
}

/* ---- Entry point ------------------------------------------------------------------ */

int assign_apply(const char *raw)
{
  struct asg_s a;
  int status;

  if (!asg_split(raw, &a))
    {
      vs_err("%s: not an assignment", raw);
      return 1;
    }

  if (a.compound)
    {
      status = assign_compound(&a);
    }
  else
    {
      char *val = expand_assign_str(a.val);

      if (val == NULL)
        {
          status = 1;
        }
      else if (a.has_sub)
        {
          status = assign_elem(&a, val);
        }
      else if (a.append && var_get(a.name) != NULL)
        {
          const char *cur = var_get(a.name);
          char *both = vs_xmalloc(strlen(cur) + strlen(val) + 1);

          strcpy(both, cur);
          strcat(both, val);
          status = var_set(a.name, both) != 0 ? 1 : 0;
          free(both);
        }
      else
        {
          status = var_set(a.name, val) != 0 ? 1 : 0;
        }

      free(val);
    }

  free(a.name);
  return status;
}

/* The name and plain value of a simple assignment word, for the callers that
 * only handle scalars (an assignment in front of a command). Returns false
 * for the array forms.
 */

bool assign_scalar_parts(const char *raw, char **name, const char **value,
                         bool *append)
{
  struct asg_s a;

  if (!asg_split(raw, &a) || a.has_sub || a.compound)
    {
      return false;
    }

  *name = a.name;
  *value = a.val;
  *append = a.append;
  return true;
}

/* ---- name[sub] as a reference (unset, [[ -v ]], test -v) --------------------------- */

/* Splits `name[sub]` (the ] must end the text). Returns false if 'text' is
 * not of that form.
 */

static bool split_ref(const char *text, char **name, const char **sub, size_t *sublen)
{
  size_t len = strlen(text);
  size_t i = 0;

  if (!name_start_ch(text[0]) || !vs_feat(VF_BASH_SYNTAX))
    {
      return false;
    }

  while (name_ch(text[i]))
    {
      i++;
    }

  if (text[i] != '[' || asg_subscript_end(text, len, i) != len - 1)
    {
      return false;
    }

  *name = vs_xstrndup(text, i);
  *sub = text + i + 1;
  *sublen = len - i - 2;
  return true;
}

static bool all_elements(const char *sub, size_t n)
{
  return n == 1 && (sub[0] == '@' || sub[0] == '*');
}

/* Is the variable, or the element, set? `-v a` means a[0]; `-v a[@]` any. */

bool asg_ref_isset(const char *text)
{
  char *name;
  const char *sub;
  size_t sublen;
  bool set;
  long idx;

  if (!split_ref(text, &name, &sub, &sublen))
    {
      return var_get(text) != NULL;
    }

  if (all_elements(sub, sublen))
    {
      struct arr_s *a = var_array(name, false);

      set = a != NULL ? a->n > 0 : var_get(name) != NULL;
    }
  else
    {
      set = expand_subscript(sub, sublen, name, &idx) == 0 &&
            var_elem_get(name, idx) != NULL;
    }

  free(name);
  return set;
}

/* unset name[sub]: 0, or 1 after an error message. name[@] empties the
 * array but keeps it, as bash does.
 */

int asg_unset_ref(const char *text, bool *handled)
{
  char *name;
  const char *sub;
  size_t sublen;
  long idx;
  int status = 0;

  *handled = split_ref(text, &name, &sub, &sublen);
  if (!*handled)
    {
      return 0;
    }

  if (all_elements(sub, sublen))
    {
      struct arr_s *a = var_array(name, false);

      if (a != NULL)
        {
          arr_clear(a);
        }
      else if (var_get(name) != NULL)
        {
          status = var_unset(name) != 0 ? 1 : 0;
        }
    }
  else if (expand_subscript(sub, sublen, name, &idx) != 0)
    {
      status = 1;
    }
  else if (!var_is_array(name) && var_get(name) != NULL && idx != 0)
    {
      vs_err("unset: %s: not an array variable", name);
      status = 1;
    }
  else
    {
      status = var_elem_unset(name, idx) != 0 ? 1 : 0;
    }

  free(name);
  return status;
}
