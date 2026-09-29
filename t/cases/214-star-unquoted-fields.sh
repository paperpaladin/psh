# Unquoted $* behaves like unquoted $@ in dash, not like a join followed by a
# split: with IFS=: and the parameters a "" b a join would give three fields
# and dash gives two.
show() { echo "count=$#"; for a in "$@"; do echo "[$a]"; done; }
set -- "a b" "c	d" "" e "f g h"
show $*
IFS=:
set -- a "" b
show $*
show $@
