/* complete_bench.c -- time vs_complete() on a prefix: `make -f posix.mk bench`.
 * usage: complete_bench "line so far" [rounds]   (the first round is reported
 * separately: it pays for whatever is cached afterwards)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "vaporshell.h"
#include "complete.h"

static int g_seen;

static void count(void *ctx, char *name, char *desc, char suffix)
{
  (void)ctx;
  (void)suffix;
  free(name);
  free(desc);
  g_seen++;
}

static double now_ms(void)
{
  struct timespec t;

  clock_gettime(CLOCK_MONOTONIC, &t);
  return t.tv_sec * 1e3 + t.tv_nsec / 1e6;
}

int main(int argc, char **argv)
{
  const char *line = argc > 1 ? argv[1] : "g";
  int rounds = argc > 2 ? atoi(argv[2]) : 200;
  char ins[128];
  double t0;
  double first;
  int n = 0;
  int del;
  size_t skip;
  int i;

  shell_init("bench");
  t0 = now_ms();
  n = vs_complete(line, (int)strlen(line), ins, sizeof(ins), &del, &skip, count, NULL);
  first = now_ms() - t0;
  t0 = now_ms();
  for (i = 0; i < rounds; i++)
    {
      vs_complete(line, (int)strlen(line), ins, sizeof(ins), &del, &skip, count, NULL);
    }

  printf("\"%s\": %d candidates, first %.2f ms, then %.3f ms each (%d rounds)\n",
         line, n, first, (now_ms() - t0) / rounds, rounds);
  return 0;
}
