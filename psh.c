/*
 * psh -- a POSIX shell subset.
 *
 * Hand-written lexer + recursive-descent parser. No flex/bison: shell
 * tokenisation is context-sensitive (a '(' is an operator in a command
 * position and an ordinary character inside a word; "do" is a keyword only
 * when unquoted and in command position), and expressing that in a generated
 * scanner costs more than writing the 200 lines by hand.
 *
 * Correctness authority is dash: see t/run.sh. Every feature here exists
 * because a corpus script compares psh's stdout + exit status against dash's.
 *
 * shortcut: the whole program leaks parse trees and expansion buffers on
 * purpose. Ceiling: a long-running interactive session or a script with a
 * multi-million-iteration loop will grow without bound. Upgrade path: an arena
 * per parsed command, reset after exec_node returns. Not worth it while the
 * only consumer is "run a script and exit".
 */
#define _GNU_SOURCE
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

static const char *progname = "psh";
static int status = 0; /* $? */

static void fatal(const char *fmt, ...) {
	va_list ap;
	va_start(ap, fmt);
	fflush(stdout);
	fprintf(stderr, "%s: ", progname);
	vfprintf(stderr, fmt, ap);
	fputc('\n', stderr);
	va_end(ap);
	exit(2);
}

static void *xmalloc(size_t n) {
	void *p = malloc(n);
	if (!p) fatal("out of memory");
	return p;
}

static void *xrealloc(void *p, size_t n) {
	p = realloc(p, n);
	if (!p) fatal("out of memory");
	return p;
}

static char *xstrdup(const char *s) {
	char *p = strdup(s);
	if (!p) fatal("out of memory");
	return p;
}

/* ------------------------------------------------------------------ Str */

typedef struct {
	char *p;
	size_t n, cap;
} Str;

static void sput(Str *s, int c) {
	if (s->n + 2 > s->cap) {
		s->cap = s->cap ? s->cap * 2 : 32;
		s->p = xrealloc(s->p, s->cap);
	}
	s->p[s->n++] = (char)c;
	s->p[s->n] = 0;
}

/* ------------------------------------------------------------------ Vec */

typedef struct {
	char **v;
	int n, cap;
} Vec;

static void vpush(Vec *a, char *s) {
	if (a->n + 2 > a->cap) {
		a->cap = a->cap ? a->cap * 2 : 8;
		a->v = xrealloc(a->v, (size_t)a->cap * sizeof *a->v);
	}
	a->v[a->n++] = s;
	a->v[a->n] = NULL;
}

/* ------------------------------------------------------------- variables */

/* ---------------------------------------------------------------- lexer */

enum { TEOF, TWORD, TIONUM, TOP };

typedef struct {
	int type;
	char *text;
	int quoted; /* word contained a quote or an expansion -> never a keyword */
} Tok;

static Tok *toks;
static int ntoks, tcap, ti;

static void tpush(int type, const char *text, int quoted) {
	if (ntoks + 1 > tcap) {
		tcap = tcap ? tcap * 2 : 64;
		toks = xrealloc(toks, (size_t)tcap * sizeof *toks);
	}
	toks[ntoks].type = type;
	toks[ntoks].text = xstrdup(text);
	toks[ntoks].quoted = quoted;
	ntoks++;
}

static const char *WORDBREAK = " \t\n|&;()<>";

/*
 * Copy a $(...) or ${...} construct verbatim, including nesting, so that the
 * word scanner does not stop at the spaces and operators inside it. Quoting
 * inside the construct must be tracked, otherwise $(echo ")") ends the copy
 * at the wrong paren.
 */
static const char *copy_nested(const char *p, Str *w, char open, char close);

/*
 * Copy a double-quoted section including both quotes. $(...) and ${...} inside
 * it must be copied as units: in `"$(echo "a b")"` the inner quotes belong to
 * the substitution, so a scanner that just looks for the next '"' ends the
 * section in the wrong place. This is the bug the oracle caught first.
 */
static const char *copy_dquote(const char *p, Str *w) {
	sput(w, *p++); /* opening " */
	while (*p && *p != '"') {
		if (*p == '\\' && p[1]) {
			sput(w, *p);
			sput(w, p[1]);
			p += 2;
			continue;
		}
		if (*p == '$' && (p[1] == '(' || p[1] == '{')) {
			char o = p[1], c = (o == '(') ? ')' : '}';
			sput(w, '$');
			p++;
			p = copy_nested(p, w, o, c);
			if (!p) return NULL;
			continue;
		}
		sput(w, *p++);
	}
	if (!*p) return NULL;
	sput(w, *p++); /* closing " */
	return p;
}

static const char *copy_nested(const char *p, Str *w, char open, char close) {
	int depth = 0;
	while (*p) {
		char c = *p;
		if (c == '\\' && p[1]) {
			sput(w, c);
			sput(w, p[1]);
			p += 2;
			continue;
		}
		if (c == '\'') {
			sput(w, c);
			p++;
			while (*p && *p != '\'') sput(w, *p++);
			if (!*p) return NULL;
			sput(w, *p++);
			continue;
		}
		if (c == '"') {
			p = copy_dquote(p, w);
			if (!p) return NULL;
			continue;
		}
		if (c == open) depth++;
		if (c == close) {
			depth--;
			sput(w, c);
			p++;
			if (depth == 0) return p;
			continue;
		}
		sput(w, c);
		p++;
	}
	return NULL;
}

static int lex(const char *p) {
	ntoks = ti = 0;
	for (;;) {
		while (*p == ' ' || *p == '\t') p++;
		if (*p == '\\' && p[1] == '\n') { /* line continuation */
			p += 2;
			continue;
		}
		if (*p == '#') {
			while (*p && *p != '\n') p++;
			continue;
		}
		if (!*p) {
			tpush(TEOF, "", 0);
			return 0;
		}
		if (*p == '\n') {
			tpush(TOP, "\n", 0);
			p++;
			continue;
		}
		if (!strncmp(p, "&&", 2) || !strncmp(p, "||", 2) || !strncmp(p, ">>", 2) ||
		    !strncmp(p, ">&", 2) || !strncmp(p, "<&", 2)) {
			char op[3] = {p[0], p[1], 0};
			tpush(TOP, op, 0);
			p += 2;
			continue;
		}
		if (strchr("|;()<>&", *p)) {
			char op[2] = {*p, 0};
			tpush(TOP, op, 0);
			p++;
			continue;
		}

		Str w = {0};
		int quoted = 0;
		while (*p) {
			char c = *p;
			if (c == '\\') {
				if (p[1] == '\n') {
					p += 2;
					continue;
				}
				sput(&w, c);
				if (p[1]) sput(&w, p[1]), p += 2;
				else p++;
				quoted = 1;
				continue;
			}
			if (c == '\'') {
				sput(&w, c);
				p++;
				while (*p && *p != '\'') sput(&w, *p++);
				if (!*p) {
					fprintf(stderr, "%s: unterminated '\n", progname);
					return -1;
				}
				sput(&w, *p++);
				quoted = 1;
				continue;
			}
			if (c == '"') {
				p = copy_dquote(p, &w);
				if (!p) {
					fprintf(stderr, "%s: unterminated \"\n", progname);
					return -1;
				}
				quoted = 1;
				continue;
			}
			if (strchr(WORDBREAK, c)) break;
			sput(&w, c);
			p++;
		}
		/* "2>file": the digits are an IO_NUMBER, a token of their own, but
		 * only when unquoted and glued to the redirection operator. */
		int alldig = w.n > 0 && !quoted;
		for (size_t i = 0; alldig && i < w.n; i++)
			if (!isdigit((unsigned char)w.p[i])) alldig = 0;
		tpush(alldig && (*p == '>' || *p == '<') ? TIONUM : TWORD, w.p ? w.p : "", quoted);
		free(w.p);
	}
}

/* --------------------------------------------------------------- parser */

enum { N_SIMPLE, N_SEQ, N_AND, N_OR, N_PIPE, N_NOT, N_SUBSHELL, N_GROUP, N_IF, N_WHILE, N_FOR };

typedef struct Node Node;
struct Node {
	int type;
	Vec words; /* SIMPLE: argv, raw (unexpanded). FOR: the "in" list. */
	Node *left, *right; /* SEQ / AND / OR / PIPE */
	Node *cond, *body, *els;
	char *var; /* FOR */
};

static Node *nnew(int type) {
	Node *n = xmalloc(sizeof *n);
	memset(n, 0, sizeof *n);
	n->type = type;
	return n;
}

static int perr(const char *what) {
	fflush(stdout);
	fprintf(stderr, "%s: syntax error near unexpected token `%s'\n", progname, what);
	return -1;
}

static Tok *cur(void) { return &toks[ti]; }
static void adv(void) {
	if (toks[ti].type != TEOF) ti++;
}
static int isop(const char *o) { return cur()->type == TOP && !strcmp(cur()->text, o); }
static int iskw(const char *k) {
	return cur()->type == TWORD && !cur()->quoted && !strcmp(cur()->text, k);
}
static int at_terminator(void) {
	if (cur()->type == TEOF || isop(")")) return 1;
	static const char *kw[] = {"then", "else", "elif", "fi", "do", "done", "}", NULL};
	for (int i = 0; kw[i]; i++)
		if (iskw(kw[i])) return 1;
	return 0;
}

static Node *parse_list(void);

static Node *parse_command(void) {

	Node *n = nnew(N_SIMPLE);
	int got = 0;
	for (;;) {
		if (cur()->type != TWORD) break;
		{
			if (n->words.n == 0 && at_terminator()) break; /* keyword, not a word */
			vpush(&n->words, xstrdup(cur()->text));
		}
		got = 1;
		adv();
	}
	if (!got) return perr(cur()->type == TEOF ? "end of input" : cur()->text), NULL;
	return n;
}

static Node *parse_pipeline(void) {
	Node *n = parse_command();
	if (!n) return NULL;
	return n;
}

static Node *parse_and_or(void) {
	Node *n = parse_pipeline();
	if (!n) return NULL;
	return n;
}

static Node *parse_list(void) {
	while (isop(";") || isop("\n")) adv();
	if (at_terminator()) { /* an empty list, e.g. "if ; then" -> error upstream */
		return perr(cur()->type == TEOF ? "end of input" : cur()->text), NULL;
	}
	Node *n = parse_and_or();
	if (!n) return NULL;
	for (;;) {
		if (!isop(";") && !isop("\n")) break;
		while (isop(";") || isop("\n")) adv();
		if (at_terminator()) break;
		Node *s = nnew(N_SEQ);
		s->left = n;
		if (!(s->right = parse_and_or())) return NULL;
		n = s;
	}
	return n;
}

static Node *parse_program(void) {
	while (isop(";") || isop("\n")) adv();
	if (cur()->type == TEOF) return nnew(N_SEQ); /* empty input: a no-op */
	Node *n = parse_list();
	if (!n) return NULL;
	if (cur()->type != TEOF) return perr(cur()->text), NULL;
	return n;
}

/* ------------------------------------------------------------ expansion */

static int exec_string(const char *s);

/*
 * THE ORDER OF EXPANSIONS IS THE THING EVERYONE GETS WRONG.
 *
 * POSIX 2.6 mandates, for each word, in this order:
 *   1. tilde expansion, parameter expansion, command substitution,
 *      arithmetic expansion  -- all in ONE left-to-right pass
 *   2. field splitting (on IFS), but ONLY on characters that came out of
 *      step 1 unquoted
 *   3. pathname expansion (globbing), again only on unquoted characters
 *   4. quote removal
 *
 * The consequence people trip over: `x="a b"; cmd $x` passes TWO arguments,
 * because splitting happens AFTER substitution; but `cmd "$x"` passes one,
 * and `x='*'; echo $x` globs while `echo "$x"` does not. Getting this right
 * requires tracking, per output byte, whether it was quoted. That is what the
 * parallel `flg` buffer below is for: flg[i] == 1 means "byte i is literal,
 * exempt from splitting and from globbing".
 *
 * Anything that re-scans an already-expanded string (the naive "expand to a
 * char* then split it" approach) loses that information and cannot be fixed
 * later.
 */
typedef struct {
	Str text;
	Str flg;
} Exp;

static void expand_into(const char *raw, Exp *e, int dq);

/* Set by expand_into when it consumes an actual quote character. `echo "" x`
 * must pass two arguments while `echo $unset x` passes one, and after
 * substitution both words are zero bytes long -- the only surviving difference
 * is "were there quotes in the source word". shortcut: a file-static flag
 * rather than threading an out-parameter through the recursion. Ceiling: not
 * reentrant, which is fine because expansion never runs concurrently with
 * itself (command substitution forks). */
static int exp_sawquote;

static void eput(Exp *e, int c, int quoted) {
	sput(&e->text, c);
	sput(&e->flg, quoted ? 1 : 0);
}

/* Step 1: substitution pass. Fills e->text and the per-byte quoted flags. */
static void expand_into(const char *p, Exp *e, int dq_init) {
	int dq = dq_init;
	while (*p) {
		char c = *p;
		if (!dq && c == '\'') {
			exp_sawquote = 1;
			p++;
			while (*p && *p != '\'') eput(e, *p++, 1);
			if (*p) p++;
			continue;
		}
		if (c == '"') {
			exp_sawquote = 1;
			dq = !dq;
			p++;
			continue;
		}
		if (c == '\\') {
			if (!dq) {
				if (p[1]) eput(e, p[1], 1), p += 2;
				else eput(e, '\\', 1), p++;
				continue;
			}
			/* Inside double quotes a backslash is only special before
			 * $ ` " \ and newline; elsewhere it stays literal. */
			if (p[1] == '$' || p[1] == '`' || p[1] == '"' || p[1] == '\\') {
				eput(e, p[1], 1);
				p += 2;
			} else if (p[1] == '\n') {
				p += 2;
			} else {
				eput(e, '\\', 1);
				p++;
			}
			continue;
		}
		/* Inside double quotes, glob metacharacters are literal: hence the
		 * quoted flag follows dq for ordinary bytes too. */
		eput(e, c, dq);
		p++;
	}
}

enum { EX_FULL, EX_NOSPLIT };

static void expand_word(const char *raw, Vec *out, int mode) {
	Exp e = {0};
	exp_sawquote = 0;
	expand_into(raw, &e, 0);
	size_t n = e.text.n;
	const char *t = e.text.p ? e.text.p : "";
	const char *f = e.flg.p ? e.flg.p : "";

	/* Before field splitting exists, a word is exactly one field: expansion
	 * does quote removal and nothing else. */
	(void)mode;
	Str lit = {0};
	for (size_t i = 0; i < n; i++) sput(&lit, t[i]);
	vpush(out, lit.p ? lit.p : xstrdup(""));
	(void)f;
	free(e.text.p);
	free(e.flg.p);
}

/* ------------------------------------------------------------- builtins */

/* ------------------------------------------------------------- execution */

static int exec_node(Node *n);

static void exec_external(char **argv) {
	execvp(argv[0], argv);
	int code = (errno == ENOENT) ? 127 : 126;
	fflush(stdout);
	fprintf(stderr, "%s: %s: %s\n", progname, argv[0],
	        code == 127 ? "not found" : strerror(errno));
	_exit(code);
}

/* Run a simple command. in_child != 0 means we are already in a forked
 * process, so builtins may run here and redirections need no restoring. */
static int exec_simple(Node *n, int in_child) {
	Vec argv = {0};
	for (int i = 0; i < n->words.n; i++) expand_word(n->words.v[i], &argv, EX_FULL);

	if (argv.n == 0) {
		/* No command word. Redirections still happen (`> f` creates f) and
		 * assignments apply to the current shell. */
		return status;
	}

	if (in_child) {
		exec_external(argv.v);
	}

	fflush(NULL);
	pid_t pid = fork();
	if (pid < 0) fatal("fork: %s", strerror(errno));
	if (pid == 0) {
		signal(SIGINT, SIG_DFL);
		exec_external(argv.v);
	}
	int st = 0;
	while (waitpid(pid, &st, 0) < 0 && errno == EINTR) {}
	return WIFEXITED(st) ? WEXITSTATUS(st) : 128 + WTERMSIG(st);
}

static int exec_compound_body(Node *n) {
	switch (n->type) {
	}
	return 2;
}

static int exec_node(Node *n) {
	if (!n) return 0;
	switch (n->type) {
	case N_SEQ: {
		if (!n->left && !n->right) return status; /* empty program */
		int s = exec_node(n->left);
		(void)s;
		return exec_node(n->right);
	}
	case N_SIMPLE:
		return status = exec_simple(n, 0);
	default: {
		int st = exec_compound_body(n);
		return status = st;
	}
	}
}

/* ------------------------------------------------------------------ main */

static int exec_string(const char *s) {
	/* The token array and parse state are global, so a nested parse (command
	 * substitution) must save and restore them. */
	Tok *otoks = toks;
	int ont = ntoks, oc = tcap, oi = ti;
	toks = NULL;
	ntoks = tcap = ti = 0;

	int rc;
	if (lex(s) < 0) {
		rc = 2;
	} else {
		Node *prog = parse_program();
		rc = prog ? exec_node(prog) : 2;
	}
	free(toks);
	toks = otoks;
	ntoks = ont;
	tcap = oc;
	ti = oi;
	return rc;
}

static char *slurp(FILE *f) {
	Str s = {0};
	int c;
	while ((c = fgetc(f)) != EOF) sput(&s, c);
	return s.p ? s.p : xstrdup("");
}

int main(int argc, char **argv) {
	signal(SIGINT, SIG_IGN); /* shortcut: no job control, no trap; the shell
	                          * simply ignores SIGINT and children get the
	                          * default. Ceiling: Ctrl-C in an interactive
	                          * session does not interrupt a loop. Upgrade
	                          * path: process groups + tcsetpgrp. */

	char *text = NULL;
	if (argc >= 3 && !strcmp(argv[1], "-c")) {
		text = xstrdup(argv[2]);
	} else if (argc >= 2 && strcmp(argv[1], "-") != 0) {
		FILE *f = fopen(argv[1], "r");
		if (!f) {
			fprintf(stderr, "%s: %s: %s\n", progname, argv[1], strerror(errno));
			return 127;
		}
		text = slurp(f);
		fclose(f);
	} else {
		text = slurp(stdin);
	}

	int rc = exec_string(text);
	fflush(NULL);
	return rc;
}
