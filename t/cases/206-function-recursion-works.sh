n=0
f() { n=$(expr $n + 1); if [ $n -lt 50 ]; then f; fi; }
f
echo "depth=$n"
