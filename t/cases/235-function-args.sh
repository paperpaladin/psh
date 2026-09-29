# The point of the whole item: a function can now take arguments. Before
# this, `f hi` printed a blank line here and `hi` in dash.
f() { echo "[$1] [$2] n=$#"; }
f hi
f "two words" second
f
g() { echo "all: $#"; for a in "$@"; do echo "  [$a]"; done; }
g "a b" "" c
