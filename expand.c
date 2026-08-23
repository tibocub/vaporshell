/*
 * expand.c -- variable assignment ("NAME=VALUE") detection, $NAME /
 * ${NAME} expansion against the current environment, and command
 * substitution ($(...) and `...`, via subst.c -- this file only
 * dispatches to it once find_command_subst() has located one; the
 * actual parsing/running lives there). No arithmetic expansion, no
 * arrays -- those are their own, separate pieces of work (see the
 * project's own roadmap).
 */

#include <nuttx/config.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include "expand.h"
#include "subst.h"

static bool is_ident_start(char c)
{
    return isalpha((unsigned char)c) || c == '_';
}

static bool is_ident_char(char c)
{
    return isalnum((unsigned char)c) || c == '_';
}

bool is_assignment(FAR const char *token, FAR char **name, FAR char **value)
{
    FAR const char *p = token;
    size_t namelen;

    if (!is_ident_start(*p))
    {
        return false;
    }

    p++;
    while (is_ident_char(*p))
    {
        p++;
    }

    if (*p != '=')
    {
        return false;
    }

    namelen = (size_t)(p - token);

    *name = malloc(namelen + 1);
    if (*name == NULL)
    {
        return false;
    }

    memcpy(*name, token, namelen);
    (*name)[namelen] = '\0';

    *value = strdup(p + 1);
    if (*value == NULL)
    {
        free(*name);
        *name = NULL;
        return false;
    }

    return true;
}

/****************************************************************************
 * Grows *out (realloc, doubling) if appending 'extra' more bytes plus
 * a trailing NUL wouldn't fit in *cap -- shared by every append below
 * rather than repeating the same growth check at each call site.
 ****************************************************************************/

static bool ensure_capacity(FAR char **out, size_t *cap, size_t len,
                             size_t extra)
{
    FAR char *grown;

    while (len + extra + 1 > *cap)
    {
        *cap *= 2;
    }

    grown = realloc(*out, *cap);
    if (grown == NULL)
    {
        return false;
    }

    *out = grown;
    return true;
}

FAR char *expand_token(FAR const char *token, bool in_single_quotes)
{
    size_t cap = strlen(token) * 2 + 64;
    FAR char *out = malloc(cap);
    size_t len = 0;
    FAR const char *p = token;

    if (out == NULL)
    {
        return NULL;
    }

    if (in_single_quotes)
    {
        strcpy(out, token);
        return out;
    }

    while (*p != '\0')
    {
        FAR const char *name_start;
        size_t namelen;
        FAR const char *value;
        size_t valuelen;
        char namebuf[128];
        FAR const char *cmd_start;
        FAR const char *cmd_end;
        FAR const char *after;

        if (find_command_subst(p, &cmd_start, &cmd_end, &after))
        {
            size_t cmdlen = (size_t)(cmd_end - cmd_start);
            FAR char *cmd_text = malloc(cmdlen + 1);
            FAR char *captured;
            size_t capturedlen;

            if (cmd_text == NULL)
            {
                free(out);
                return NULL;
            }

            memcpy(cmd_text, cmd_start, cmdlen);
            cmd_text[cmdlen] = '\0';

            captured = capture_command_output(cmd_text);
            free(cmd_text);

            if (captured == NULL)
            {
                free(out);
                return NULL;
            }

            capturedlen = strlen(captured);

            if (!ensure_capacity(&out, &cap, len, capturedlen))
            {
                free(captured);
                free(out);
                return NULL;
            }

            memcpy(out + len, captured, capturedlen);
            len += capturedlen;
            free(captured);

            p = after;
            continue;
        }

        if (*p != '$')
        {
            /* Not a command substitution, and not '$' either -- an
             * ordinary character (this also correctly covers a lone
             * '`' with no matching close: find_command_subst()
             * already returned false for it above, so it falls
             * through to here and gets copied literally, same "don't
             * guess at an unterminated construct" choice its own doc
             * comment describes).
             */

            if (!ensure_capacity(&out, &cap, len, 1))
            {
                free(out);
                return NULL;
            }

            out[len++] = *p++;
            continue;
        }

        p++; /* skip '$' */

        if (*p == '{')
        {
            p++;
            name_start = p;
            while (*p != '\0' && *p != '}')
            {
                p++;
            }

            namelen = (size_t)(p - name_start);
            if (*p == '}')
            {
                p++;
            }
        }
        else if (is_ident_start(*p))
        {
            name_start = p;
            while (is_ident_char(*p))
            {
                p++;
            }

            namelen = (size_t)(p - name_start);
        }
        else
        {
            /* '$' not followed by a valid identifier/brace (and not
             * '(' either -- that was already handled by
             * find_command_subst() above, including the unterminated
             * case) -- a literal '$' (e.g. a lone "$" or "$5" --
             * positional parameters aren't implemented yet).
             */

            if (!ensure_capacity(&out, &cap, len, 1))
            {
                free(out);
                return NULL;
            }

            out[len++] = '$';
            continue;
        }

        if (namelen >= sizeof(namebuf))
        {
            namelen = sizeof(namebuf) - 1;
        }

        memcpy(namebuf, name_start, namelen);
        namebuf[namelen] = '\0';

        value = getenv(namebuf);
        if (value == NULL)
        {
            value = "";
        }

        valuelen = strlen(value);

        if (!ensure_capacity(&out, &cap, len, valuelen))
        {
            free(out);
            return NULL;
        }

        memcpy(out + len, value, valuelen);
        len += valuelen;
    }

    out[len] = '\0';
    return out;
}
