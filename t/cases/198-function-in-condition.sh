ok() { return 0; }
no() { return 1; }
if ok; then echo yes; fi
if no; then echo no-branch; else echo else-branch; fi
ok && echo and
no || echo or
