# A pipeline forks every stage, so a shift in one is invisible afterwards,
# and "$@" still expands correctly inside the child.
set -- "a b" c d
echo "x" | { shift; echo "in pipe: $# [$1]"; }
echo "after: $# [$1]"
for a in "$@"; do echo "[$a]"; done | cat
