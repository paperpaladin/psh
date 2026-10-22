# A pipeline's status is its last command's, so a failing earlier stage is not
# a failing command: `false | true` succeeds. The last stage is not exempt and
# ends the script, which is why the output stops here.
set -e
false | cat
echo "first-stage status=$?"
echo a | false | cat
echo "middle-stage status=$?"
echo b | cat
echo "all-ok status=$?"
true | false
echo not-reached
