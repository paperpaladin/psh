# An expanding body is not a double-quoted word: a bare quote is an ordinary
# character, and a backslash before one keeps the backslash. Only $ ` \ and
# newline are escapable.
X=val
cat <<EOF
bare "double" and 'single' are literal
escaped \$X and \\ and \" and \' and \z
EOF
