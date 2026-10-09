# POSIX 2.9.1: functions beat regular builtins.
true() { echo shadowed; }
true
unset -f true
true
echo "s=$?"
