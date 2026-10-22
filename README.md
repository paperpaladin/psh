# psh

A POSIX shell subset in one C file, kept honest by differential testing against
`dash`.

## make test

```
$ make test
gcc -std=c11 -Wall -Wextra -O2 -o psh psh.c
./t/run.sh
288 agree, 0 disagree, 0 skipped
set -e exemptions, agreeing cases each: ifcond 2, elifcond 2, whilecond 2, untilcond 2, andlist 2, orlist 2, bang 2, pipestage 2
```

```
$ make test-asan
gcc -std=c11 -Wall -Wextra -O1 -g -fsanitize=address,undefined \
	-fno-omit-frame-pointer -o psh-asan psh.c
ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 PSH=./psh-asan ./t/run.sh
288 agree, 0 disagree, 0 skipped
set -e exemptions, agreeing cases each: ifcond 2, elifcond 2, whilecond 2, untilcond 2, andlist 2, orlist 2, bang 2, pipestage 2
```

Run a script or a string:

```
$ ./psh -c 'for f in *.c; do echo "found $f"; done'
found psh.c
$ ./psh t/cases/68-while-counter.sh
1
2
3
```

## Why this exists

Writing a shell is easy. Writing a shell that *agrees with another shell* is
not, and the gap between the two is where every shell-scripting bug you have
ever debugged lives. Shell semantics are not hard because they are complicated;
they are hard because they are arbitrary, and the only way to know what is
correct is to ask something that already is.

So psh is organised around that question. There is no spec-reading step and no
"I think this is right" step. Every feature exists because a script in
`t/cases/` compares psh's output against dash's, and every corner that psh
skips is skipped on purpose, named below, and not quietly papered over.

The interesting artefact is therefore not the shell. It is the 279-script
corpus, plus nine checks that psh refuses the expansions and the `set`
options it has not implemented, and the fact that the number on the last line
of `make test` is a real measurement, not a self-assessment. The corpus size
is pinned, because a run over an empty corpus and a run over all of it both
used to print `0 disagree` and exit 0.

The `set -e` exemptions are counted the same way. Each of the eight entries in
the list has to have at least one corpus case behind it that agreed with dash,
and the harness prints the per-entry counts and exits nonzero if any of them is
zero: an exemption with no case is an untested entry, and the list is the whole
of what makes `set -e` hard.

## The oracle

`dash` is the authority. For every script in `t/cases/`, `t/run.sh`:

1. wipes a scratch directory, runs the script there under `dash`, and records
   stdout and exit status;
2. wipes *the same directory path* and runs the script there under `psh`;
3. demands that stdout is byte-identical and the exit status is equal.

The same directory path matters: a script that calls `pwd` or globs `*` would
otherwise disagree for reasons that have nothing to do with the shell.

stderr is not compared. Error wording is not semantics, and pinning dash's
message catalogue would have meant spending the budget on strings.

If dash itself rejects a script (a syntax error, exit status 2) the case is
skipped, not scored. An oracle that refuses to run the program cannot rule
on it, and counting such a case as a pass would be a lie in psh's favour. The
harness prints `N agree, M disagree, K skipped` and exits nonzero whenever M is
not zero.

`DASH=/path/to/dash` and `PSH=/path/to/psh` override either side; `V=1` lists
the passing cases too.

A case that needs positional parameters on its own command line gets them
from a sidecar file next to it, `NAME.args`, one argument per line read
verbatim, so an argument can contain a space, a tab, a glob character or no
bytes at all. Both shells get the same list.

That mechanism cannot be checked by agreement: if the sidecar were never
read, both shells would see zero arguments and agree with each other. So
`209-positional-from-argv.sh` is a canary, and after the corpus run the
harness re-runs it under dash with no arguments and requires the output to
differ from the one the loop produced. The first version of this check read
the sidecar itself and compared two fresh dash runs; it proved the file was
not empty and nothing else, and deleting the sidecar read from the loop left
it green.

### What the sanitizers can and cannot see

`make test-asan` is the second oracle, and what it cannot see was measured
rather than reasoned about. Four reads were planted in `exec_simple`, one at a
time, in a copy of `psh.c` outside the repository, each compiled with
`-fsanitize=address,undefined` and run on `psh -c 'echo hi'`:

    past a four-element stack array      stack-buffer-overflow, exit 1
    one past an exactly sized strdup     heap-buffer-overflow, exit 1
    past the live entries of FdSave's
      int fd[16], the array half empty   nothing at all, exit 0
    one past that array, still inside
      the same struct                    UBSan prints "index 16 out of
                                         bounds", exit stays 0

The first two are controls, and they are what makes the other two mean
anything: without them, "nothing was reported" cannot be told apart from "the
compiler deleted the read". Both controls fire, so the instrument works and the
planted reads are real.

The third is the blind case. `FdSave` holds `int fd[16]` and `int saved[16]` in
one struct, so a read inside `fd` past the entries actually saved never leaves
the object, and no sanitizer can see it. The fourth was a surprise: UBSan knows
the declared size of the array from its type and does report the read, but by
default it prints a line to stderr and carries on with exit 0 -- and this
harness compares stdout and exit status only. The diagnostic was there and the
suite was green. `make test-asan` now sets `UBSAN_OPTIONS=halt_on_error=1`,
which turns that class into a nonzero exit the comparison already looks at.

### Where dash and bash disagree

Only dash is in the harness, so these are hand measurements rather than corpus
cases, taken against dash 0.5.12-12 on Debian 13, bash 5.2.37, and that same
bash started with `--posix`. Two shells, three opinions. psh follows dash in both, because
dash is the authority here; the point of writing them down is that neither is a
bug, and a script that relies on either is not portable.

`set -e` inside a command substitution. dash applies it: in
`if [ -n "$(false; echo in)" ]` the substitution stops at `false`, comes back
empty, and the `if` takes its else branch. Plain bash never applies `set -e`
inside `$(...)` at all, so the same line prints `in` and takes the then branch.
`bash --posix` applies it, but carries the enclosing exemption in with it, so
it stops the substitution when the containing command is checked
(`x=$(false; echo in)` gives an empty `x` and exits) and does not when the
containing command is being tested (the `if` above prints `in`). So the
disagreement is three-way and dash is the only one of the three that kills the
substitution regardless of where it sits. `set +e` inside the substitution
gets the other behaviour in all three.

The status of a failed redirection. `set -e; cat < no-such-file` exits 2 under
dash and 1 under bash in both modes, and `cd` to a missing directory is the
same 2-versus-1. psh already answered 2 for both before `set -e` existed here,
since dash is the oracle; what `set -e` adds is that the number becomes the
script's exit status, so the divergence is now visible in a place scripts
notice.

### Things dash does that were genuinely surprising

Each of these was found by the oracle, not by reading POSIX, and each one is now
a numbered case in the corpus:

- `cd` to a missing directory exits 2, not 1. Almost every other failing
  builtin uses 1.
- An assignment-only command resets `$?` to zero. `false; x=1; echo $?`
  prints `0`. But a command substitution inside the assignment leaves its own
  status behind, so `false; x=$(false); echo $?` prints `1`. The status of a
  command with no command word is therefore "zero, unless something inside the
  expansion had an opinion".
- `echo ""` passes an argument; `echo $unset` does not. After expansion both
  words are zero bytes long. The only surviving difference is whether the
  *source* word contained a quote character, which means an implementation has
  to remember that fact specifically.
- dash's `echo` always interprets backslash escapes: no `-e` needed, and
  `\c` truncates the output *including* the trailing newline. It also consumes
  exactly one leading `-n`: `echo -n -n x` prints `-n x`. Matching bash here
  would have been wrong.
- `"$(echo "a b")"`: the inner quotes belong to the substitution, not to
  the outer string. A lexer that scans a double-quoted section by looking for
  the next `"` ends the quote in the wrong place and silently produces
  `echo a b)`. This was psh's first bug and it is why `copy_dquote` and
  `copy_nested` are mutually recursive.
- `break` outside a loop is silently ignored, status 0. Not an error.
- `${#}` is `$#`, i.e. `0`: not the length of a variable named `#`.
- `IFS=` disables field splitting entirely instead of splitting between
  every character, so `IFS=; x="a b"; for w in $x` iterates once.
- `shift` past the end is not a status-1 failure. It is an error in a POSIX
  special builtin, so dash prints a diagnostic and the script STOPS, with
  status 2. `set -- a b; shift 3; echo hi` prints nothing.
- Unquoted `$*` is not the join-then-split that POSIX describes. With
  `IFS=:` and the parameters `a`, `""`, `b`, joining gives `a::b` and
  splitting that gives three fields; dash gives two, the same two as `$@`.
  Unquoted `$@` and unquoted `$*` are the same thing in dash.
- `${#*}` and `${#@}` are the length in bytes of `"$*"`, not the number of
  parameters: with `a b c` they are 5.
- The quotes around `"$@"` disappear with it when there are no parameters,
  so `f "$@"` passes zero arguments where `f ""` passes one. Only those
  quotes: `f ""$@` still passes one empty argument.
- `$10` is `$1` followed by a literal `0`. The tenth parameter is `${10}`.
- `set -e; ! true` does not exit. POSIX exempts the whole pipeline beginning
  with `!`, not only its operand, so the script carries on with `$?` of 1.
- `set -e; false && true` does not exit either, with status 1 at the end of
  it. The last operand is the only one whose status is checked and the
  short circuit means it never ran, so there is nothing to check.
- `{ false && true; }` does not exit and `(false && true)` does. A brace group
  hands back the status of the list inside it and is not a command that `set
  -e` checks; a subshell is one. Same list, same status, different answer.
- The exemption is inherited by a function body, so one function behaves two
  ways. With `set -e` and `f() { false; echo in; }`, plain `f` prints nothing
  and `if f; then` prints `in`. bash does the same and documents it.
- `local x=$(false)` masks the failure in dash and in bash: the command is
  `local`, whose own status is 0, so the substitution's 1 is discarded and
  `set -e` sees nothing. `local x; x=$(false)` exits. psh has no `local`,
  POSIX has none either, so the shape cannot arise here, but the plain
  `x=$(false)` half of it does and is case 270.

## Works

Simple commands and arguments. Single-quote, double-quote and backslash
quoting, including the rule that a backslash inside double quotes is only
special before `$ ` ` " \` and newline. Pipelines of arbitrary length with
correct last-command exit status. Redirections `<`, `>`, `>>`, `N>`, `N>>`,
`N<`, `N>&M`, `N<&M`, `N>&-`, applied left to right, and restored afterwards
when the command is a builtin. `&&`, `||`, `;`, `!`, newline-separated lists,
comments, backslash-newline continuation. Subshells `( )` and brace groups
`{ }`, both able to carry their own redirections. Variable assignment,
assignment prefixes scoped to the command (`X=1 echo` does not leak `X`),
`$VAR`, `${VAR}`, `${VAR:-w}`, `${VAR-w}`, `${VAR:+w}`, `${VAR+w}`, `${#VAR}`,
`$?`, `${?}`. Field splitting on `IFS` with the POSIX whitespace-run rules.
Pathname expansion through `glob(3)`, suppressed by quoting at the level of
individual bytes. Command substitution `$(...)`, nested, with trailing newlines
stripped and its exit status visible to an assignment. Tilde expansion for a
leading `~` or `~/`. `if`/`elif`/`else`/`fi`, `while`, `until`, `for ... in`,
`break [n]`, `continue [n]`. Builtins `cd` (including `cd -`), `exit`, `export`,
`echo`, `true`, `false`, `pwd`, `read`, `unset`, `:`, `set --`, `set -e`,
`shift`.

Positional parameters. `$0` is the script path as it was given on the command
line, or the shell's own `argv[0]` under `-c`; `$1`..`$9` and `${10}` upwards
are the arguments, `$#` is how many, and `${1:-w}`, `${1:+w}`, `${#1}` work on
them like any other parameter. `"$@"` is one field per parameter whatever IFS
says, `"$*"` is one field with the parameters joined by the first byte of IFS,
and the unquoted `$@` and `$*` are one field per parameter with ordinary IFS
splitting applied to each and empty parameters dropped. `set --` replaces the
whole list, `set a b` does too, and `shift [n]` drops from the front. A
function call replaces the parameters for the duration and restores them on
return, so `shift` inside a function shifts the function's arguments; `$0` is
not replaced, which is what dash does.

The field boundary that `"$@"` needs is carried in the per-byte quote-flag
array described under Design, as a flag value of its own: 2 for a break from
a quoted `"$@"`, where an empty field either side of it is a real empty
argument, and 3 for a break from the unquoted forms, where it is nothing.
That is the whole of the difference between `f "$@"` and `f $@` on the
parameters `a`, `""`, `b`: three arguments and two.

Shell functions: `name() compound-command`, with the body able to carry its own
redirections: `f() { echo x; } > file` redirects on every call, not once at
definition. `return [n]`, where a bare `return` keeps the current status and a
`return` outside a function ends the script, as it does in dash. Functions beat
regular builtins and lose to special ones, which is POSIX 2.9.1's search order:
a function named `true` shadows the builtin, and one named `export` is refused
at parse time the way dash refuses it. `unset -f` removes one. Variables are
shared, not scoped, because POSIX functions have no locals. Recursion is capped
at 1000 frames, which is dash's number and dash's exit status. The number is
checked and not just asserted: `208-function-recursion-500-deep.sh` recurses
500 deep and must complete, so a cap below 500 fails the suite. That case alone could not tell, it trips any cap, and
`MAXFUNCDEPTH=50` passed all 207 of the old corpus.

`set -e` and `set +e`. A failing command ends the script with that command's
status: 3 for `(exit 3)`, 127 for a command that was not found, 137 for a child
killed by SIGKILL. The exemption list is the part worth writing out, because it
is the whole of what makes the option hard. Exempt, since something else is
already testing the status:

- the condition of `if` and of each `elif`, the whole compound list and not
  only the command that decides it, so `if false; true; then` is fine;
- the condition of `while` and of `until`;
- every operand of `&&` and `||` except the last, which is what makes
  `cmd || handler` the portable way to handle a failure;
- the operand of `!`, and the result of the `!` along with it.

A pipeline is not on that list and does not need to be: its status is its last
command's, so `false | true` is a successful command and only the last stage
can end the script. The stages are separate processes that inherit the
exemption, so `{ false; echo in; } | cat` prints nothing on its own and prints
`in` when it is the condition of an `if`.

A function body and a subshell inherit the exemption too, all the way down:
with `g` running `false` and `f` calling `g`, `if f; then` runs every command
in both bodies. A command substitution does not inherit it, which is the one
place dash and bash part company.

psh checks the status where one is produced rather than where it is passed
along: a simple command, a pipeline, a subshell, and a function call. A brace
group, an `if`, a `while` and a `for` only hand back the status of the list
inside them, which has already been checked or exempted, and that is the whole
reason `{ false && true; }` does not end the script while `(false && true)`
does.

`set -e` with no operands leaves the positional parameters alone, while
`set -e a b` and `set -e -- a b` replace them. An assignment-only command takes
its status from the command substitution in it, so `x=$(false)` is a failing
command and `v=$(false) true` and `echo $(false)` are not.

Here-documents: `<<WORD`, `<<-WORD` with leading tabs stripped from both the
body and the delimiter line, and the quoted forms `<<'W'`, `<<"W"` and `<<\W`
that suppress expansion of the body entirely. An unquoted delimiter means the
body expands as if double-quoted, except that a quote character in the body is
ordinary and a backslash before one keeps the backslash, only `$`, `` ` ``,
`\` and newline are escapable there. Multiple here-documents on one line are
read in order. The body is stored unexpanded and re-expanded every time the
redirection is applied, so a here-document inside a loop sees the current value
of a variable on each iteration.

## What does not work

Honest list. None of these are "coming soon". They are absent today.

- No job control. `&` is a syntax error. No `jobs`, `fg`, `bg`, `wait`, no
  process groups, no terminal ownership. The shell ignores `SIGINT` outright, so
  Ctrl-C will not interrupt a loop, it only reaches the foreground child.
- No here-strings. `<<<` is refused with a diagnostic instead of being lexed as `<<` with an empty delimiter, which would silently swallow the rest of the
  file. dash has no `<<<` either.
- A here-document inside `$(...)` breaks on an unbalanced `)` in the body.
  `v=$(cat <<EOF` / `has ) paren` / `EOF` / `)` works in dash and is a syntax
  error in psh, because the scanner that copies a `$(...)` construct verbatim
  counts parentheses and tracks quoting but knows nothing about here-document
  bodies, so the stray `)` ends the copy early. The leftover text goes to the
  outer parser, so both shapes tried so far come out as a syntax error and exit 2 instead of a wrong string, but that is an observation about those two and no guarantee about every body.
- Backquotes in a here-document body are refused, not substituted. dash
  expands `` ` `` there. psh rejects a backquote everywhere else, and an
  expanding body is the one place where passing one through as a literal would
  have been silently wrong, so it is a hard error instead. With a quoted
  delimiter nothing expands and a backquote really is a literal, so there it is
  left alone.
- No `trap`.
- `set -u`, `set -x`, `set -o` and bare `set` (which lists every variable in
  dash) are refused with a diagnostic and status 2, not accepted and ignored.
  `set -e`, `set +e`, `set --`, `set a b` and combinations of those are
  implemented. `set -eu` in one word is refused, since it is not `-e`, and so
  is `set -o errexit`. An unsupported option is refused before anything in the
  same `set` is applied, so `set -e -u` leaves errexit off rather than
  half-obeying a line dash obeys completely. Five checks in the harness assert
  the refusals.

  With `set -e` on, a refused option is itself a failing command, so the script
  stops at the refusal with status 2 rather than running on with the option
  silently absent. dash accepts `-u` and stops later at the first real failure
  with status 1, so the corpus cannot compare this: it is psh being stricter
  than its oracle, deliberately.
- `${VAR#pat}` and `${VAR%pat}` are refused, and neither quietly expands to nothing. They are
  not implemented (PLAN item 201), and until recently the name scan in
  `expand_brace` stopped only at `:`, `-` and `+`, so `${n#x}` was read as a
  variable *named* `n#x`, found unset, and expanded to nothing while dash
  gives `xx`. No diagnostic at all. psh already failed loudly on `$((...))`
  for exactly this reason. Four checks in the harness now assert the refusal,
  since an unimplemented expansion can never agree with dash and so cannot
  live in the corpus.
- `$0` under `-c` is not oracle-tested. psh uses its own `argv[0]` when no
  operand follows the string and the operand when one does, which is dash's
  rule, but `t/run.sh` only ever runs script files, so nothing in the corpus
  checks it. The script case is checked: both shells are handed the same
  path and `210-positional-dollar-zero.sh` compares what they print.
- `$*` and `$@` are not settable or unsettable, and `${#*}` is the only
  `${#...}` form over them. `${*%pat}` and friends are refused along with
  every other trimming form.
- The quote cancellation around an empty `"$@"` is matched as the two literal
  spellings `"$@"` and `"${@}"`, not tracked through the expansion, so
  `"${1+"$@"}"` with no parameters leaves one empty argument here and none in
  dash. Nothing in the corpus covers that shape, which makes it an untested
  divergence rather than a measured one.
- `unset 1` diverges, and did before this item: dash calls it a bad variable
  name and, `unset` being a special builtin, ENDS the script with status 2.
  psh prints its own diagnostic and carries on with status 0. The same is
  true of every other bad name handed to `unset`. It is left alone here
  because it is a property of `unset`, not of positional parameters, and
  fixing it means deciding how every builtin reports a special-builtin error.
- No local variables. POSIX has none and dash's `local` is an extension, so
  a function that sets a variable changes the caller's. Asserted explicitly: `191-function-globals.sh` compares that against dash.
- `break` inside a function can reach the caller's loops. `loopdepth` is
  per-process and is not saved across a call, so `f() { break; }` called from
  inside a loop breaks that loop. dash has the same wart, so the corpus cannot
  catch it, which means it is untested, not verified.
- No arithmetic expansion. `$(( ))` is refused with a diagnostic and exit
  status 2, instead of being silently mis-evaluated as a subshell running a
  command called `1+1`. Counting loops in the corpus call `expr`.
- No `case`/`esac`.
- No backquotes. `` ` `` is a hard error; use `$()`.
- No `${VAR%pat}`, `${VAR#pat}`, `${VAR:=w}`, `${VAR:?w}`.
- No `eval`, `exec`, `.`/`source`, `alias`, `getopts`, `umask`,
  `hash`, `times`, `ulimit`, `readonly`.
- No interactive features. No prompt, no line editing, no history. Reading
  from a tty works only because stdin happens to be a file descriptor.
- Tilde expansion is partial. `~user` is not supported, and a tilde after a
  `:` in an assignment (`PATH=~/bin:~/sbin`) is left literal.
- psh leaks memory by design. Parse trees and expansion buffers are never
  freed, and `make test-asan` disables LeakSanitizer for that reason. Fine for "run a
  script and exit"; not fine for a login shell.
- Fixed limits: 32 stages per pipeline, 16 saved file descriptors per
  command.

`PLAN.md` ranks these. The ones in Milestone 1 are the ones worth doing next.

## Design

One file, ~2460 lines, six stages that each hand a single data structure to the
next:

```
input text
   |  lex()            quote-aware, context-sensitive tokeniser
   v
Tok[]                  WORD / IONUM / OP / EOF, each WORD still holding its
   |                   raw quotes -- quote removal is deliberately deferred
   |  parse_program()  recursive descent
   v
Node tree              SIMPLE / SEQ / AND / OR / PIPE / NOT / SUBSHELL /
   |                   GROUP / IF / WHILE / FOR
   |  exec_node()      fork, dup2, waitpid
   v
exit status
```

No flex/bison. Shell tokenisation is context-sensitive in ways a generated
scanner fights: `(` is an operator in command position and an ordinary byte
inside a word; `do` is a keyword only when unquoted and in command position;
`2` is an `IO_NUMBER` only when it is unquoted digits glued to `<` or `>`. Each
of those is two lines by hand and a lexer-state fight in a generator. They are
also not installed.

The lexer keeps quotes. A `WORD` token holds the source text verbatim,
quotes and all, plus one bit recording "this word contained a quote or an
expansion, so it is not a keyword". Quote removal happens at expansion time
because it has to: you cannot decide whether a `*` globs until you know whether
the quote that contained it was in the source or came out of a variable.

Expansion carries a quote flag per byte. This is the one design decision
that everything else follows from. POSIX 2.6 mandates, per word, in this order:

1. tilde / parameter / command substitution / arithmetic: one left-to-right
   pass;
2. field splitting on `IFS`, on the bytes from step 1 **that were unquoted**;
3. pathname expansion, again **only on unquoted bytes**;
4. quote removal.

Everyone gets this wrong, and they get it wrong the same way: they expand into
a `char *` and then split and glob the result. That loses the information step 2
and step 3 need. `x="a b"; cmd $x` must pass two arguments while `cmd "$x"`
passes one; `x='*'; echo $x` must glob while `echo "$x"` must not. Both pairs
are byte-identical after step 1. `"$@"` is the sharpest case of the same
thing: `set -- "a b" c` makes `f "$@"` two arguments and `f "$*"` one, and
after step 1 those differ by nothing but a flag byte. So `expand_into()` writes two parallel buffers,
`text` and `flg`, where `flg[i] == 1` means "byte `i` is literal, exempt from
splitting and from globbing", and `glob_field()` backslash-escapes exactly the
flagged metacharacters before handing the pattern to `glob(3)`.

The mutual recursion between `copy_dquote()` and `copy_nested()` exists for the
same reason one level up: the lexer has to know that the `"` inside
`"$(echo "a b")"` belongs to the substitution.

Pipelines fork every stage, builtins included, so a builtin in a pipeline
cannot touch the parent shell, exactly the surprise dash gives you
with `echo x | read v`. Builtins outside a pipeline run in the parent (otherwise
`cd` would be useless), with their redirections saved through `F_DUPFD_CLOEXEC`
and restored afterwards.

`break`/`continue` unwind by flag, not `longjmp`: `loopctl` plus a level
count, checked by every node type that can contain a loop body. `exec_string()`
saves and restores that state along with the global lexer arrays, because
command substitution re-enters the parser.

## License

GPL-3.0. See `LICENSE`.
