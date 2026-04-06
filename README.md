# psh

A POSIX shell subset in one C file, kept honest by differential testing against
`dash`.

## make test

```
$ make test
gcc -std=c11 -Wall -Wextra -O2 -o psh psh.c
./t/run.sh
164 agree, 0 disagree, 0 skipped
```

```
$ make test-asan
gcc -std=c11 -Wall -Wextra -O1 -g -fsanitize=address,undefined \
	-fno-omit-frame-pointer -o psh-asan psh.c
ASAN_OPTIONS=detect_leaks=0 PSH=./psh-asan ./t/run.sh
164 agree, 0 disagree, 0 skipped
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

The interesting artefact is therefore not the shell. It is the 164-script
corpus and the fact that the number on the last line of `make test` is a real
measurement rather than a self-assessment.

## The oracle

`dash` is the authority. For every script in `t/cases/`, `t/run.sh`:

1. wipes a scratch directory, runs the script there under `dash`, and records
   stdout and exit status;
2. wipes *the same directory path* and runs the script there under `psh`;
3. demands that **stdout is byte-identical and the exit status is equal**.

The same directory path matters: a script that calls `pwd` or globs `*` would
otherwise disagree for reasons that have nothing to do with the shell.

stderr is not compared. Error wording is not semantics, and pinning dash's
message catalogue would have meant spending the budget on strings.

If dash itself rejects a script -- a syntax error, exit status 2 -- the case is
**skipped**, not scored. An oracle that refuses to run the program cannot rule
on it, and counting such a case as a pass would be a lie in psh's favour. The
harness prints `N agree, M disagree, K skipped` and exits nonzero whenever M is
not zero.

`DASH=/path/to/dash` and `PSH=/path/to/psh` override either side; `V=1` lists
the passing cases too.

### Things dash does that were genuinely surprising

Each of these was found by the oracle, not by reading POSIX, and each one is now
a numbered case in the corpus:

- **`cd` to a missing directory exits 2, not 1.** Almost every other failing
  builtin uses 1.
- **An assignment-only command resets `$?` to zero.** `false; x=1; echo $?`
  prints `0`. But a command substitution inside the assignment leaves its own
  status behind, so `false; x=$(false); echo $?` prints `1`. The status of a
  command with no command word is therefore "zero, unless something inside the
  expansion had an opinion".
- **`echo ""` passes an argument; `echo $unset` does not.** After expansion both
  words are zero bytes long. The only surviving difference is whether the
  *source* word contained a quote character, which means an implementation has
  to remember that fact specifically.
- **dash's `echo` always interprets backslash escapes** -- no `-e` needed -- and
  `\c` truncates the output *including* the trailing newline. It also consumes
  exactly one leading `-n`: `echo -n -n x` prints `-n x`. Matching bash here
  would have been wrong.
- **`"$(echo "a b")"`** -- the inner quotes belong to the substitution, not to
  the outer string. A lexer that scans a double-quoted section by looking for
  the next `"` ends the quote in the wrong place and silently produces
  `echo a b)`. This was psh's first bug and it is why `copy_dquote` and
  `copy_nested` are mutually recursive.
- **`break` outside a loop is silently ignored**, status 0. Not an error.
- **`${#}` is `$#`**, i.e. `0` -- not the length of a variable named `#`.
- **`IFS=` disables field splitting entirely** rather than splitting between
  every character, so `IFS=; x="a b"; for w in $x` iterates once.

## Status

### Works

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
`echo`, `true`, `false`, `pwd`, `read`, `unset`, `:`.

### What does not work

Honest list. None of these are "coming soon"; they are absent today.

- **No job control.** `&` is a syntax error. No `jobs`, `fg`, `bg`, `wait`, no
  process groups, no terminal ownership. The shell ignores `SIGINT` outright, so
  Ctrl-C will not interrupt a loop -- it only reaches the foreground child.
- **No functions.** `f() { ...; }` does not parse.
- **No here-documents.** `<<` and `<<-` are syntax errors. This is the biggest
  practical gap.
- **No `trap`.**
- **No `set`.** No `set -e`, `-u`, `-x`, and no `set --`.
- **No positional parameters.** `$1`..`$9` expand to empty and `$#` expands to
  `0`, which is what dash does for a script invoked without arguments -- so the
  corpus agrees, but psh would diverge the moment arguments were passed. There
  is no `$@`, no `$*`, no `shift`.
- **No arithmetic expansion.** `$(( ))` is refused with a diagnostic and exit
  status 2, rather than being silently mis-evaluated as a subshell running a
  command called `1+1`. Counting loops in the corpus call `expr`.
- **No `case`/`esac`.**
- **No backquotes.** `` ` `` is a hard error; use `$()`.
- **No `${VAR%pat}`, `${VAR#pat}`, `${VAR:=w}`, `${VAR:?w}`.**
- **No `eval`, `exec`, `.`/`source`, `alias`, `getopts`, `shift`, `umask`,
  `hash`, `times`, `ulimit`, `readonly`.**
- **No interactive features.** No prompt, no line editing, no history. Reading
  from a tty works only because stdin happens to be a file descriptor.
- **Tilde expansion is partial.** `~user` is not supported, and a tilde after a
  `:` in an assignment (`PATH=~/bin:~/sbin`) is left literal.
- **psh leaks memory by design.** Parse trees and expansion buffers are never
  freed, which is why `make test-asan` disables LeakSanitizer. Fine for "run a
  script and exit"; not fine for a login shell.
- **Fixed limits**: 32 stages per pipeline, 16 saved file descriptors per
  command.

`PLAN.md` ranks these; the ones in Milestone 1 are the ones worth doing next.

## Design

One file, ~1700 lines, six stages that each hand a single data structure to the
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

**No flex/bison.** Shell tokenisation is context-sensitive in ways a generated
scanner fights: `(` is an operator in command position and an ordinary byte
inside a word; `do` is a keyword only when unquoted and in command position;
`2` is an `IO_NUMBER` only when it is unquoted digits glued to `<` or `>`. Each
of those is two lines by hand and a lexer-state fight in a generator. They are
also not installed.

**The lexer keeps quotes.** A `WORD` token holds the source text verbatim,
quotes and all, plus one bit recording "this word contained a quote or an
expansion, so it is not a keyword". Quote removal happens at expansion time
because it has to: you cannot decide whether a `*` globs until you know whether
the quote that contained it was in the source or came out of a variable.

**Expansion carries a quote flag per byte.** This is the one design decision
that everything else follows from. POSIX 2.6 mandates, per word, in this order:

1. tilde / parameter / command substitution / arithmetic -- one left-to-right
   pass;
2. field splitting on `IFS`, on the bytes from step 1 **that were unquoted**;
3. pathname expansion, again **only on unquoted bytes**;
4. quote removal.

Everyone gets this wrong, and they get it wrong the same way: they expand into
a `char *` and then split and glob the result. That loses the information step 2
and step 3 need. `x="a b"; cmd $x` must pass two arguments while `cmd "$x"`
passes one; `x='*'; echo $x` must glob while `echo "$x"` must not. Both pairs
are byte-identical after step 1. So `expand_into()` writes two parallel buffers,
`text` and `flg`, where `flg[i] == 1` means "byte `i` is literal, exempt from
splitting and from globbing", and `glob_field()` backslash-escapes exactly the
flagged metacharacters before handing the pattern to `glob(3)`.

The mutual recursion between `copy_dquote()` and `copy_nested()` exists for the
same reason one level up: the lexer has to know that the `"` inside
`"$(echo "a b")"` belongs to the substitution.

**Pipelines fork every stage**, builtins included, so a builtin in a pipeline
cannot touch the parent shell -- which is exactly the surprise dash gives you
with `echo x | read v`. Builtins outside a pipeline run in the parent (otherwise
`cd` would be useless), with their redirections saved through `F_DUPFD_CLOEXEC`
and restored afterwards.

**`break`/`continue` unwind by flag**, not `longjmp`: `loopctl` plus a level
count, checked by every node type that can contain a loop body. `exec_string()`
saves and restores that state along with the global lexer arrays, because
command substitution re-enters the parser.

## License

GPL-3.0. See `LICENSE`.
