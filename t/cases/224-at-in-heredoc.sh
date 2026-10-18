# A here-document body is not field-split either, so "$@" joins the same way
# an assignment does, and a quoted delimiter suppresses it entirely.
set -- "a b" c
cat <<END
at=$@
star=$*
one=$1
count=$#
END
cat <<'END'
at=$@
END
