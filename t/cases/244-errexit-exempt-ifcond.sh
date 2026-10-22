# The condition of `if` is tested, so `set -e` is ignored there. The whole
# compound list counts, not just its last command, and an `if` that takes no
# branch at all is still successful.
set -e
if false; then echo taken; fi
echo past-simple
if false; true; then echo taken-list; fi
echo past-list
if false; then echo t; else echo else-branch; fi
echo past-else
if false; then :; fi
echo "status=$?"
