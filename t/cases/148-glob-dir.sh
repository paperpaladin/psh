mkdir -p sub
: > sub/a
: > sub/b
echo sub/*
for f in sub/*; do echo "[$f]"; done
