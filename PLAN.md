# psh

## Contract

`psh FILE`, `psh -c STRING`, or `psh` reading a script from stdin runs a subset
of the POSIX shell command language and exits with the status of the last
command it ran.

In scope: simple commands with arguments; single-quote, double-quote and
backslash quoting; pipelines (`|`); redirections (`<`, `>`, `>>`, `N>`, `N>&M`);
`&&`, `||`, `;`, `!`; subshells `( )` and brace groups `{ }`; variable
assignment, `$VAR`, `${VAR}`, `${VAR:-w}`, `${VAR:+w}`, `${#VAR}`, `$?`;
pathname expansion through `glob(3)`; command substitution `$(...)`; the
builtins `cd`, `exit`, `export`, `echo`, `true`, `false`, `pwd`, `read`, `:`,
`break`, `continue`, `unset`; and the compound commands `if/elif/else/fi`,
`while/do/done`, `until/do/done`, `for/in/do/done`.

Out of scope, and deliberately so: everything in **Non-goals** below.

## Hard invariant

For every script in `t/cases/`, psh's **stdout** and **exit status** are
byte-for-byte identical to dash's. `make test` is the enforcement: it reports
"N agree, M disagree, K skipped" and exits nonzero if M is not zero.

stderr is explicitly *not* part of the invariant. Diagnostic wording is not
something a shell subset owes anyone, and pinning it would mean reimplementing
dash's message catalogue instead of its semantics.

Corollary invariants that fall out of the above and are worth naming, because
each one is a bug people ship:

- The order of expansions is parameter/command-substitution -> field splitting
  -> pathname expansion -> quote removal, and splitting and globbing apply only
  to bytes that came out of substitution *unquoted*. psh tracks a quoted flag
  per output byte to make this representable at all.
- A pipeline's status is its **last** command's status, not its first failure.
- A command with no command word (`x=1`, `> f`) succeeds, unless a command
  substitution in its assignments left a status behind.

## Oracle

Ranked, strongest first:

1. **dash** (`/bin/dash`), run on the same script in the same scratch directory
   path, compared on stdout and exit status. This is the authority. If dash and
   psh disagree, psh is wrong by definition. If dash *rejects* a script
   (syntax error, exit 2), the case is **skipped** rather than scored: an oracle
   that will not run the program cannot rule on it.
2. **ASAN + UBSAN** over the whole corpus (`make test-asan`). Memory errors in
   the expansion buffers and the parallel quote-flag arrays are exactly the
   class of bug that produces *intermittently* correct output, which the
   differential test alone would miss.
3. **gcc `-Wall -Wextra`** at zero warnings. Cheapest signal available.
4. Hand probing against dash while developing, promoted into `t/cases/` the
   moment it finds a divergence. Six of the first-draft bugs were found this
   way and every one of them is now a numbered case.

## Non-goals

- Job control: no `&`, no `jobs`/`fg`/`bg`, no process groups, no terminal
  ownership handoff. `&` is a syntax error.
- Shell functions (`name() { ... }`).
- Here-documents (`<<`, `<<-`) and here-strings.
- `trap`, `set -e`/`set -u`/`set -x`/`set --`, `shift`, positional parameters
  and `$@`/`$*`/`$1` (they expand to empty, which is what dash does when a
  script is given no arguments).
- Arithmetic expansion `$(( ))`. psh refuses it loudly rather than silently
  mis-evaluating it.
- `case`/`esac`.
- Backquote command substitution. `$()` only; a backquote is a hard error.
- `${VAR%pat}`, `${VAR#pat}`, `${VAR:=w}`, `${VAR:?w}`.
- Aliases, `eval`, `exec`, `getopts`, `hash`, `times`, `ulimit`, `umask`, `readonly`.
- Interactive editing, history, prompts, `$PS1`.
- Locale-aware collation in globbing: whatever `glob(3)` does is what psh does.

## Commit sequence

- [x] Lexer, recursive-descent parser, fork/exec of simple commands, with
      quotes kept in the token and removed only at expansion time.
      why: nothing else can be tested until a word can become an argv, and
      eagerly stripping quotes destroys the information every later stage needs.
- [x] GPL-3 text, vendored.
      why: 674 lines of legal text in a behavioural diff makes both the diff and
      the licence audit harder; it belongs on its own.
- [x] Variables, `$VAR`/`${VAR}`/`$?` expansion, assignments, IFS field
      splitting with per-byte quote flags.
      why: the expansion *order* is the single most commonly botched part of a
      shell and it is unrepresentable without tracking which bytes were quoted.
      Retrofitting that information is impossible.
- [x] Pathname expansion via `glob(3)`, with quoted metacharacters escaped.
      why: libc already implements POSIX patterns and sort order; the only part
      libc cannot know is which metacharacters the user quoted.
- [x] Redirections `<`, `>`, `>>`, `N>`, `N>&M`, left to right, with
      save/restore so a parent-run builtin can be undone.
      why: `cmd >f 2>&1` and `cmd 2>&1 >f` differ only in order, and a builtin
      whose redirection is not undone leaves the shell's own stdout in a file.
- [x] Pipelines, `&&`, `||`, `!`.
      why: control flow is where exit-status propagation goes wrong, and exit
      status is half of what the oracle compares.
- [x] Subshells `( )` and brace groups `{ }`.
      why: they differ in exactly one observable way -- whether assignments
      survive -- so having only one of them is not a subset, it is a wrong answer.
- [x] Command substitution `$(...)`; hard errors for backquotes and `$(( ))`.
      why: `$((1+1))` silently parsed as a subshell running a command named
      `1+1` and printed nothing. A loud refusal costs a diagnostic; a silent
      wrong answer costs an afternoon.
- [x] `${V:-w}`, `${V:+w}`, `${#V}`, `${?}`, `${#}`, leading-tilde expansion.
      why: `echo ~` printing a literal tilde was the dangerous shape of bug --
      plausible output, no diagnostic, wrong answer.
- [x] Builtins `echo`, `true`, `false`, `pwd`, `:`.
      why: dash's `echo` interprets backslash escapes with no `-e` and consumes
      exactly one `-n`. Copying bash here would have made psh fail its own oracle.
- [x] Builtins `cd` (incl. `cd -`), `exit`, `export`, `unset`, `read`.
      why: these change the shell's own state, which is what forces the
      parent-vs-child builtin split; and `read` must not buffer past the newline.
- [x] `if`/`elif`/`else`/`fi`.
      why: an `if` with no taken branch is successful, which is the rule that
      makes conditionals safe, and parsing `elif` as a nested `if` with a single
      shared `fi` is the detail every first attempt gets wrong.
- [x] `while`/`until`, `break [n]`, `continue [n]`.
      why: without `break`, a missing keyword inside `while true` is an infinite
      loop rather than an error message.
- [x] `for ... in`.
      why: its item list exercises substitution, splitting and globbing together,
      so it is the construct where every earlier decision becomes visible at once.
- [x] `t/run.sh` differential oracle and the first 50 corpus scripts.
      why: the corpus is the deliverable. The shell is the thing the corpus
      happens to be about.
- [x] Corpus 51-99: builtins, control flow, and the statuses nobody checks.
      why: exit status produces no visible output, so it is the half of the
      comparison that rots silently.
- [x] Corpus 100-164 and a clean ASAN/UBSAN run.
      why: most of these were written in response to a disagreement, so each is a
      regression test for a bug that actually shipped.
- [x] README and PLAN.
      why: documenting exactly which corner was skipped is the valuable part; a
      list of omissions is worth more than an unmeasured claim of completeness.

### Milestone 1

- [ ] Here-documents: `<<WORD`, `<<-WORD`, and the quoted-delimiter form that
      suppresses expansion of the body.
      why: the single most-used feature psh lacks; every real script that emits
      a config file needs it, and it is the only redirection whose operand is
      lexed out of the *following* lines rather than the current token stream.
- [ ] Shell functions, including `return` and the fact that a function body is
      a compound command that can carry redirections.
      why: without functions a script cannot factor anything, so the corpus
      cannot grow past straight-line tests.
- [ ] Positional parameters: `$1`..`$9`, `$#`, `"$@"` versus `$*`, `shift`,
      `set --`.
      why: `"$@"` is the one expansion that produces a *different number of
      fields* depending only on its quoting, which no other construct does, so
      it needs the quote-flag machinery extended from bytes to field boundaries.
- [ ] `set -e` semantics, exactly: which commands are exempt (conditions of
      `if`/`while`/`until`, operands of `&&`/`||`, the body of `!`), and how it
      propagates into subshells.
      why: `set -e` is notorious precisely because the exemption list is long
      and unobvious; implementing it against dash is the only way to get it
      right, and getting it wrong is worse than not having it.
- [ ] `trap`, at least `EXIT`, `INT` and `TERM`, with correct ordering against
      the exit status.
      why: cleanup handlers are how scripts avoid leaving temp files behind,
      and `trap ... EXIT` interacts with `exit`'s status in a way worth pinning.
- [ ] Job control: `&`, `wait`, `jobs`, process groups and `tcsetpgrp`.
      why: psh currently ignores SIGINT shell-wide, so an interactive user
      cannot interrupt a loop. This is the fix, and it is the largest single
      piece of remaining work.
- [ ] `case`/`esac` with `fnmatch(3)` patterns and `|` alternatives.
      why: the only pattern-matching control structure, and it shares the glob
      engine already present, so the cost is parsing rather than semantics.
- [ ] Arithmetic expansion `$(( ))` with the full POSIX operator set and
      assignment side effects.
      why: psh errors out on it today; every counting loop in the corpus has to
      call `expr`, which makes the loop tests measure `expr` as much as psh.
- [ ] `${VAR%pat}`, `${VAR#pat}`, `${VAR:=w}`, `${VAR:?w}`, and `${#*}`.
      why: the trimming forms are how portable scripts do basename/dirname
      without forking, and they need the glob matcher applied to strings rather
      than paths.
- [ ] `eval`, `exec`, and `.`/`source`.
      why: each one re-enters the parser with different state-ownership rules;
      they are the test of whether `exec_string`'s save/restore of global lexer
      state is actually sound or merely sufficient for command substitution.
- [ ] Run the `modernish` compliance suite against psh and record, per failing
      test, whether the gap is a bug or a documented non-goal.
      why: a self-written corpus tests what its author already thought of.
      modernish was written by someone else to break shells, so it is the first
      honest measurement of how much POSIX is actually missing.
- [ ] Replace the leak-everything allocation strategy with a per-command arena.
      why: required before psh can be a login shell or run a long loop, and it
      is what lets `make test-asan` turn LeakSanitizer back on, which would
      have caught two of the bugs found by hand instead.
