# Unquoted $@. Each parameter becomes a field and is then split on IFS, so
# "a b" turns into two. The empty parameter contributes nothing at all --
# that is the half of the rule that quoted "$@" gets the other way round.
show() { echo "count=$#"; for a in "$@"; do echo "[$a]"; done; }
set -- "a b" "c	d" "" e "f g h"
show $@
