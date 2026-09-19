/*
 * posix/self_path.c -- finds this binary's own absolute path.
 *
 * subst.c implements command substitution by spawning vaporshell
 * itself (`vaporshell -c "cmd"`). On NuttX, resolving that by bare
 * name through $PATH is fine because vaporshell is installed as a
 * builtin. A standalone binary run as ./build/vaporshell isn't on
 * $PATH under that name, and the spawn would fail -- silently, since
 * substitution deliberately degrades to an empty string. Resolving
 * once at startup, before anything can chdir(), fixes that.
 *
 * Order: /proc/self/exe where it exists (Linux; immune to a lying
 * argv[0]), then argv[0] itself if it contains a '/', then a $PATH
 * search for it -- the same lookup the invoking shell just did. Only
 * the /proc step is Linux-specific; the other two are what macOS and
 * the BSDs use.
 *
 * Deliberately no realpath(): it's XSI, not base POSIX, and glibc and
 * musl both hide it under strict -D_POSIX_C_SOURCE. A spawn only
 * needs an absolute path, not a canonical one.
 */

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "vaporshell.h"

/****************************************************************************
 * Returns a newly malloc()'d absolute version of 'path' (prefixing the
 * current directory if it's relative), or NULL on failure. Symlinks
 * and ".." components are left as they are.
 ****************************************************************************/

static FAR char *make_absolute(FAR const char *path)
{
    char cwd[PATH_MAX];
    FAR char *absolute;
    size_t len;

    if (path[0] == '/')
    {
        return strdup(path);
    }

    if (getcwd(cwd, sizeof(cwd)) == NULL)
    {
        return NULL;
    }

    len = strlen(cwd) + 1 + strlen(path) + 1;
    absolute = malloc(len);
    if (absolute != NULL)
    {
        snprintf(absolute, len, "%s/%s", cwd, path);
    }

    return absolute;
}

static bool is_executable_file(FAR const char *path)
{
    struct stat st;

    return stat(path, &st) == 0 && S_ISREG(st.st_mode) &&
           access(path, X_OK) == 0;
}

static FAR char *resolve_from_path_env(FAR const char *name)
{
    FAR const char *path = getenv("PATH");
    char candidate[PATH_MAX];

    if (path == NULL)
    {
        return NULL;
    }

    while (*path != '\0')
    {
        size_t len = strcspn(path, ":");

        /* An empty $PATH entry means the current directory. */

        int n = (len > 0) ?
                snprintf(candidate, sizeof(candidate), "%.*s/%s",
                         (int)len, path, name) :
                snprintf(candidate, sizeof(candidate), "./%s", name);

        if (n > 0 && (size_t)n < sizeof(candidate) &&
            is_executable_file(candidate))
        {
            return make_absolute(candidate);
        }

        path += len;
        if (*path == ':')
        {
            path++;
        }
    }

    return NULL;
}

/****************************************************************************
 * Returns a newly malloc()'d absolute path, or NULL if none of the
 * strategies worked (the caller then keeps its NuttX-style default of
 * resolving "vaporshell" through $PATH).
 ****************************************************************************/

FAR char *vs_resolve_self(FAR const char *argv0)
{
#ifdef __linux__
    char exe[PATH_MAX];
    ssize_t n = readlink("/proc/self/exe", exe, sizeof(exe) - 1);

    if (n > 0)
    {
        exe[n] = '\0';
        return strdup(exe);
    }
#endif

    if (argv0 == NULL || argv0[0] == '\0')
    {
        return NULL;
    }

    if (strchr(argv0, '/') != NULL)
    {
        return make_absolute(argv0);
    }

    return resolve_from_path_env(argv0);
}
