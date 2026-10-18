# A command substitution runs in a child that inherits the parameters.
set -- "a b" c
echo "[$(echo "$@")]"
echo "[$(echo "$*")]"
echo "[$(echo $#)]"
v=$(shift; echo "$# [$1]")
echo "sub=[$v] outer=$# [$1]"
