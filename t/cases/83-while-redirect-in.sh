printf 'p\nq\n' > f1
while read l; do echo "=$l"; done < f1
