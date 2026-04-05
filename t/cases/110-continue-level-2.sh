for a in 1 2; do for b in x y; do if [ $b = y ]; then continue 2; fi; echo $a$b; done; echo never; done
