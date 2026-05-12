( cat <<EOF
subshell
EOF
)
{ cat <<EOF
brace group
EOF
}
if cat <<EOF | grep -q yes
yes
EOF
then echo matched; fi
