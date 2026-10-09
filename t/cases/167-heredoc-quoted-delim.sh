X=val
cat <<'EOF'
nothing expands: $X ${X} $(echo inner) "dq" 'sq' \$X \\ \"
EOF
cat <<"EOF"
also nothing: $X
EOF
