echo ${unset1:-fallback}
x=set
echo ${x:-fallback}
y=
echo ${y:-fallback}
echo ${y-fallback}
