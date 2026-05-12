X=val
cat <<EOF | tr a-z A-Z
piped $X
EOF
