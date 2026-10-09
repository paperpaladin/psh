# The redirection belongs to the definition and applies on every call,
# not once when the function is defined.
f() { echo written; } > fnout
f
f
cat fnout
