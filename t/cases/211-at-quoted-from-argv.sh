# "$@" over arguments that came from argv. Every parameter here contains
# something that a wrong implementation loses: a space, a tab, an empty
# string. Joining them (the $* behaviour) gives one field instead of five.
show() { echo "count=$#"; for a in "$@"; do echo "[$a]"; done; }
show "$@"
