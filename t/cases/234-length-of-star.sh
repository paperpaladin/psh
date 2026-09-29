# ${#*} and ${#@} are the length in bytes of "$*", not the number of
# parameters: three one-letter parameters give 5, not 3.
set -- a b c
echo "${#*} ${#@}"
set --
echo "${#*} ${#@}"
set -- "a b" cd
echo "${#*} ${#@}"
IFS=:
echo "${#*}"
