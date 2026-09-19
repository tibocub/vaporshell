/*
 * Stand-in for NuttX's generated <nuttx/config.h>, only ever on the
 * include path for the standalone build (posix.mk). Every source file
 * includes this first; on a real NuttX build the genuine header is
 * found instead, since posix/ is never on that build's include path.
 * Deliberately empty: nothing in vaporshell reads a CONFIG_* value
 * outside the NuttX-only readline calls that #if-guard themselves.
 */
