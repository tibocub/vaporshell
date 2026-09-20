/*
 * mode.h -- language modes ("profiles") as data.
 *
 * There is ONE lexer, parser, expander and executor. Everything that
 * differs between dialects is a named *feature bit* that the core asks
 * about at the exact place the behaviour differs:
 *
 *     if (vs_feat(VF_SPECIAL_ERR_FATAL)) ...
 *
 * A profile (bash, posix, later others) is nothing but a preset of those
 * bits, so `--posix`, `set -o posix`, being invoked as `sh` and
 * POSIXLY_CORRECT all reduce to one call, vs_mode_set(). Individual bits
 * can later be flipped on their own (bash's shopt options are exactly
 * this), so a dialect is never "a second engine".
 *
 * Rules for adding a feature bit (see docs/modes.md):
 *   1. Measure the difference first (bash, bash --posix, dash) and cite it.
 *   2. Name it for the behaviour, not the shell.
 *   3. Ask about it in one place; never test the profile itself.
 */

#ifndef VAPORSHELL_MODE_H
#define VAPORSHELL_MODE_H

#include <stdbool.h>

#include "vaporshell.h"

enum vs_feature_e
{
  /* Semantics that differ between bash's default and POSIX. */

  VF_SPECIAL_ERR_FATAL,     /* a special builtin's error ends a non-interactive shell */
  VF_SPECIAL_ASSIGN_KEEP,   /* NAME=v before a special builtin outlives it */
  VF_SPECIAL_BEFORE_FUNC,   /* special builtins are found before functions */
  VF_EVAL_SYNTAX_FATAL,     /* a syntax error in eval or `.` ends a non-interactive shell */
  VF_DOT_SEARCH_CWD,        /* `.` falls back to the current directory */
  VF_FUNC_NAME_ANY,         /* function names may contain - . and similar */
  VF_ERREXIT_IN_CMDSUB,     /* $(...) inherits set -e */
  VF_TEST_EXT,              /* test / [ accept == */
  VF_EXIT2_ON_ERROR,        /* fatal shell errors exit with status 2 (dash), not 1 */
  VF_ECHO_XPG,              /* echo always interprets escapes and only knows -n (dash) */
  VF_PRINTF_EXT,            /* printf -v, %q and \e \x \u escapes */
  VF_LOCAL_INHERITS,        /* `local x` keeps the outer value instead of unsetting */
  VF_ALIAS_SCRIPTS,         /* aliases expand in non-interactive shells */
  VF_BASH_INFO_FORMATS,     /* alias/hash/times/ulimit use bash's formats and units */
  VF_ARITH_EXT,             /* ** , ++ -- and base#number in $(( )) */
  VF_BASH_SYNTAX,           /* [[ ]], (( )), for (( )), function, time, ;& ;;&, <<< */
  VF_BRACE_EXP,             /* {a,b} and {1..3} */
  VF_ANSI_C_QUOTE,          /* $'...' and $"..." */
  VF_PARAM_EXT,             /* ${x:o:l} ${x/p/r} ${x^^} ${!x} ${x@Q} */
  VF_TRAP_BASH,             /* trap on ERR, DEBUG and RETURN */
  VF_DOT_ARGS,              /* `. file args` sets the positional parameters */
  VF_SET_O_BASH,            /* set -o pipefail / posix exist */
  VF_NOTFOUND_127,          /* type / command -v report "not found" as 127 (dash) */
  VF_RETURN_TOPLEVEL_ERR,   /* `return` outside a function is an error, not an exit */
  VF_READ_EXT,              /* read -p -n -d -t -s -u */
  VF_BASH_VARS,             /* $_, BASH_VERSION, RANDOM, SECONDS, UID, HOSTNAME... */
  VF_OPTARG_EMPTY,          /* getopts sets OPTARG to "" (not unset) for a flag without argument */

  /* Syntax. */

  VF_AMP_REDIR,             /* &> and &>> */

  VF_COUNT                  /* must stay <= 32 */
};

enum vs_profile_e
{
  VS_PROFILE_BASH,          /* the default */
  VS_PROFILE_POSIX
};

/* Bit masks for tagging table entries (builtins, ...) with the profiles
 * they belong to.
 */

#define VS_M_BASH  0x1u
#define VS_M_POSIX 0x2u
#define VS_M_ALL   (VS_M_BASH | VS_M_POSIX)

static inline bool vs_feat(enum vs_feature_e f)
{
  return (g_sh.features & (1UL << (unsigned)f)) != 0;
}

void vs_mode_set(enum vs_profile_e p);
enum vs_profile_e vs_mode_get(void);
unsigned vs_mode_bit(void);                   /* VS_M_BASH or VS_M_POSIX */
const char *vs_mode_name(void);

#endif
