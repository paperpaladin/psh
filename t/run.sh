#!/bin/sh
# Differential oracle. dash is the authority: for every corpus script, psh's
# stdout AND exit status must be byte-identical to dash's. stderr is not
# compared -- error wording is not something a shell subset owes anyone.
#
# Each script runs twice in the SAME scratch directory path (wiped in between)
# so that `pwd` and glob results are identical for both shells. If dash itself
# rejects a script we SKIP it rather than scoring it: the oracle cannot rule on
# something it will not run.
set -u

here=$(cd "$(dirname "$0")" && pwd)
psh=${PSH:-$here/../psh}
dash=${DASH:-/bin/dash}

[ -x "$psh" ]  || { echo "run.sh: no $psh -- run make first" >&2; exit 1; }
psh=$(cd "$(dirname "$psh")" && pwd)/$(basename "$psh")  # absolute: tests cd away
[ -x "$dash" ] || { echo "run.sh: no oracle at $dash" >&2; exit 1; }

agree=0
disagree=0
skipped=0

tmp=$(mktemp -d) || exit 1
trap 'rm -rf "$tmp"' EXIT INT TERM
work=$tmp/w
out=$tmp/o
mkdir -p "$out"

for f in "$here"/cases/*.sh; do
	name=$(basename "$f")

	rm -rf "$work"; mkdir -p "$work"
	( cd "$work" && "$dash" "$f" ) >"$out/d.out" 2>"$out/d.err"
	ds=$?

	# dash's own parse/feature refusal -> not a verdict we can use.
	if [ "$ds" -eq 2 ] && grep -qi 'syntax error\|unexpected' "$out/d.err"; then
		skipped=$((skipped + 1))
		echo "SKIP     $name (dash rejects it)"
		continue
	fi

	rm -rf "$work"; mkdir -p "$work"
	( cd "$work" && "$psh" "$f" ) >"$out/p.out" 2>"$out/p.err"
	ps=$?

	if [ "$ds" -eq "$ps" ] && cmp -s "$out/d.out" "$out/p.out"; then
		agree=$((agree + 1))
		[ -n "${V:-}" ] && echo "ok       $name"
	else
		disagree=$((disagree + 1))
		echo "DISAGREE $name  (dash status=$ds, psh status=$ps)"
		echo "  --- dash stdout ---"; sed -n '1,20p' "$out/d.out" | sed 's/^/  /'
		echo "  --- psh  stdout ---"; sed -n '1,20p' "$out/p.out" | sed 's/^/  /'
		if [ -s "$out/p.err" ]; then
			echo "  --- psh  stderr ---"; sed -n '1,10p' "$out/p.err" | sed 's/^/  /'
		fi
	fi
done

echo "$agree agree, $disagree disagree, $skipped skipped"
[ "$disagree" -eq 0 ]
