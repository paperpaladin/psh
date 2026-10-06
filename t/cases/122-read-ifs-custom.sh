IFS=,
printf 'a,b,c\n' > f1
read p q < f1
echo "[$p][$q]"
