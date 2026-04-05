export UV=1
env | grep -c '^UV=' 
unset UV
env | grep -c '^UV='
echo status=$?
