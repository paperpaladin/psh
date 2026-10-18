# Each frame keeps its own parameters while a recursive call runs.
down() {
	echo "depth $1"
	if [ "$1" != 0 ]; then
		down "$(expr "$1" - 1)"
	fi
	echo "back at $1"
}
down 3
