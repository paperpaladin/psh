# dash also accepts `set a b` with no --, as long as the first operand does
# not look like an option.
set a "b c"
echo "n=$# [$1] [$2]"
set -- -x -y
echo "n=$# [$1] [$2]"
