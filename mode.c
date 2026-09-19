/*
 * mode.c -- the profile presets. This table is the whole definition of
 * what "bash mode" and "POSIX mode" mean; every entry is backed by a
 * measurement (docs/modes.md, "Measured differences").
 */

#include <nuttx/config.h>

#include "vaporshell.h"
#include "mode.h"

#define BIT(f) (1UL << (unsigned)(f))

/* Bash's default. */

#define BASH_FEATURES \
  (BIT(VF_DOT_SEARCH_CWD) | BIT(VF_FUNC_NAME_ANY) | BIT(VF_TEST_EXT) | \
   BIT(VF_AMP_REDIR))

/* POSIX: the strict reading (dash-like), not bash --posix. Where those two
 * disagree (e.g. `[ a == a ]`) this profile follows POSIX.
 */

#define POSIX_FEATURES \
  (BIT(VF_SPECIAL_ERR_FATAL) | BIT(VF_SPECIAL_ASSIGN_KEEP) | \
   BIT(VF_SPECIAL_BEFORE_FUNC) | BIT(VF_EVAL_SYNTAX_FATAL) | \
   BIT(VF_ERREXIT_IN_CMDSUB))

unsigned long g_vs_features = BASH_FEATURES;
static enum vs_profile_e g_profile = VS_PROFILE_BASH;

void vs_mode_set(enum vs_profile_e p)
{
  g_profile = p;
  g_vs_features = (p == VS_PROFILE_POSIX) ? POSIX_FEATURES : BASH_FEATURES;

  if (p == VS_PROFILE_POSIX)
    {
      /* Like bash: scripts (and child shells) can tell. */

      if (var_get("POSIXLY_CORRECT") == NULL)
        {
          var_set("POSIXLY_CORRECT", "y");
        }
    }
}

enum vs_profile_e vs_mode_get(void)
{
  return g_profile;
}

unsigned vs_mode_bit(void)
{
  return g_profile == VS_PROFILE_POSIX ? VS_M_POSIX : VS_M_BASH;
}

const char *vs_mode_name(void)
{
  return g_profile == VS_PROFILE_POSIX ? "posix" : "bash";
}
