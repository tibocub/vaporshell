# requires: float
# printf floating-point conversions (on NuttX these need CONFIG_LIBC_FLOATINGPOINT).
printf '%.2f %e %g\n' 3.14159 1234.5 0.0001
printf '%5.1f|%-8.3f|\n' 2.25 1.5
printf '%f\n' 2
