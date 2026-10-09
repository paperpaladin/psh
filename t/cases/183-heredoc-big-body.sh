# A body that EXPANDS to more than a pipe buffer holds. An implementation that
# fed the body through a pipe without forking a writer would deadlock here
# rather than fail, and a hang is the one result a test suite cannot report.
B=x
i=0
while [ $i -lt 17 ]; do B="$B$B"; i=$(expr $i + 1); done
cat <<EOF | wc -c
$B
EOF
