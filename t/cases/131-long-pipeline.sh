printf '1\n2\n3\n4\n5\n' | grep -v 3 | sort -r | head -2 | tr -d '\n'
echo
