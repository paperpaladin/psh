# `for i in "$@"` is the idiom the whole feature exists for: it must iterate
# once per parameter, with whitespace intact, and not at all when there are
# none.
set -- "a b" "" "c	d"
for i in "$@"; do echo "q[$i]"; done
for i in $@; do echo "u[$i]"; done
set --
for i in "$@"; do echo "never"; done
echo "done"
