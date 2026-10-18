# An assignment is not field-split, so $@ joins there -- with the first byte
# of IFS, which is a space by default and nothing when IFS is empty.
set -- "a b" c d
x=$@;  echo "x=[$x]"
y="$@"; echo "y=[$y]"
z="$*"; echo "z=[$z]"
IFS=:
x=$@;  echo "x=[$x]"
y="$@"; echo "y=[$y]"
IFS=
x=$@;  echo "x=[$x]"
