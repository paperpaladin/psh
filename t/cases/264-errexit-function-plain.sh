# A function body is ordinary code when the call is not in a tested position,
# so the body stops at its first failure and the call never returns.
set -e
f() {
	echo in-function
	false
	echo not-reached
}
echo before
f
echo also-not-reached
