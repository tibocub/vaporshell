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
   BIT(VF_AMP_REDIR) | BIT(VF_PRINTF_EXT) | BIT(VF_BASH_INFO_FORMATS) | BIT(VF_DOT_ARGS) | BIT(VF_ARITH_EXT) | BIT(VF_BASH_SYNTAX) | \
   BIT(VF_BRACE_EXP) | BIT(VF_ANSI_C_QUOTE) | BIT(VF_PARAM_EXT) | BIT(VF_TRAP_BASH) | \
   BIT(VF_SET_O_BASH) | BIT(VF_RETURN_TOPLEVEL_ERR) | BIT(VF_READ_EXT) | \
   BIT(VF_BASH_VARS))

/* POSIX: the strict reading (dash-like), not bash --posix. Where those two
 * disagree (e.g. `[ a == a ]`) this profile follows POSIX.
 */

#define POSIX_FEATURES \
  (BIT(VF_SPECIAL_ERR_FATAL) | BIT(VF_SPECIAL_ASSIGN_KEEP) | \
   BIT(VF_SPECIAL_BEFORE_FUNC) | BIT(VF_EVAL_SYNTAX_FATAL) | \
   BIT(VF_ERREXIT_IN_CMDSUB) | BIT(VF_EXIT2_ON_ERROR) | BIT(VF_ECHO_XPG) | \
   BIT(VF_LOCAL_INHERITS) | BIT(VF_ALIAS_SCRIPTS) | BIT(VF_OPTARG_EMPTY) | \
   BIT(VF_NOTFOUND_127))

void vash_mode_set(enum vash_profile_e p)
{
  g_sh.profile = (int)p;
  g_sh.features = (p == VASH_PROFILE_POSIX) ? POSIX_FEATURES : BASH_FEATURES;

  if (p == VASH_PROFILE_POSIX)
    {
      /* Like bash: scripts (and child shells) can tell. */

      if (var_get("POSIXLY_CORRECT") == NULL)
        {
          var_set("POSIXLY_CORRECT", "y");
        }
    }
}

enum vash_profile_e vash_mode_get(void)
{
  return (enum vash_profile_e)g_sh.profile;
}

unsigned vash_mode_bit(void)
{
  return g_sh.profile == VASH_PROFILE_POSIX ? VASH_M_POSIX : VASH_M_BASH;
}

const char *vash_mode_name(void)
{
  return g_sh.profile == VASH_PROFILE_POSIX ? "posix" : "bash";
}
