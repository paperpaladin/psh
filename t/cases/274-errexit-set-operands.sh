# `set` takes options and operands in one call. `-e` ahead of operands sets
# the option and replaces the parameters; ahead of `--` it does the same; on
# its own it leaves them alone.
set -e a b
echo "n=$# [$1] [$2]"
set -e -- p q r
echo "n=$# [$1] [$3]"
set -e
echo "n=$# [$1]"
set --
echo "n=$# [$1]"
false
echo not-reached
