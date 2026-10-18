# `set --` replaces the parameters, and with no operands it clears them.
set -- a "b c" ""
echo "n=$# [$1] [$2] [$3]"
set -- x
echo "n=$# [$1] [$2]"
set --
echo "n=$# [$1]"
set -- "$@" added
echo "n=$# [$1]"
echo "status=$?"
