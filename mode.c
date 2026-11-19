/*
 * mode.c -- the profile presets. This table is the whole definition of
 * what "bash mode" and "POSIX mode" mean; every entry is backed by a
 * measurement (docs/modes.md, "Measured differences").
 */

#include <nuttx/config.h>

#include "vaporshell.h"
#include "mode.h"

void vash_mode_set(enum vash_profile_e p)
{
#ifdef VASH_POSIX_ONLY
  p = VASH_PROFILE_POSIX;                 /* the build has no other mode (config/Kconfig) */
#endif
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
