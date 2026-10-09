# A return out of a while loop must not evaluate the condition one more time
# on the way out. Only visible when the condition has a side effect, which is
# why it is its own case: with the loop's condition being `:` the difference
# is unobservable and a mutation that removed the check survived the corpus.
f() { while echo tick; do return 1; done; }
f
echo "s=$?"
g() { i=0; while echo "cond $i"; do i=$(expr $i + 1); if [ $i -gt 1 ]; then return 9; fi; done; }
g
echo "s=$?"
