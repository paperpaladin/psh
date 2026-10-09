X=val
cat <<EOF
x is $X and ${X} and ${NOPE:-fallback}
status was $?
sub is $(echo inner)
EOF
