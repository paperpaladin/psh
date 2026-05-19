# No scoping: POSIX functions share the shell's variables.
v=outer
s() { v=inner; }
s
echo "v=$v"
