echo ~ > f1
test "$(cat f1)" = "$HOME" && echo matches
echo a~b
echo "~"
