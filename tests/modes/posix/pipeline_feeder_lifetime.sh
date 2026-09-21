# A pipeline inside a subshell, run many times in a row, must never lose its data.
#
# In-process pipelines (NuttX, and VS_INPROC=1 on a host) hand a stage's output to the next stage through a
# helper thread that owns a descriptor. The subshell's cleanup closes every descriptor opened inside it, so
# a helper still running then left its number free for the next pipeline, and its own close() later landed
# on that pipeline's pipe: the reader saw end-of-file with nothing in it. `read` stops at its newline
# without waiting for end-of-file, which is what made it likely. The helper is now waited for.
i=0
while [ $i -lt 400 ]
do
  ( printf '%s\n' 'a b' | { read x y; [ "$x$y" = ab ] || echo "lost data in pipeline 1, round $i"; } )
  ( printf 'p q' | { read x y; [ "$x$y" = pq ] || echo "lost data in pipeline 2, round $i"; } )
  ( printf '%s\n' 'c d' | { read x y; [ "$x$y" = cd ] || echo "lost data in pipeline 3, round $i"; } )
  i=$((i + 1))
done
echo done
