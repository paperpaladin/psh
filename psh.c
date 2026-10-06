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
#include <fcntl.h>
#include <glob.h>
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

/* `break`/`continue` unwind by setting these instead of longjmp: every
 * construct that can contain a loop body checks loopctl after running a child
 * node and stops early. loopcnt is the remaining level count for `break 2`. */
static int loopctl; /* 0 none, 1 break, 2 continue */
static int loopcnt;
static int loopdepth; /* enclosing loops in THIS shell process */

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
typedef struct Var {
	struct Var *next;
	char *name;
	char *val;
	int exported;
} Var;

static Var *vars;
extern char **environ;

static Var *vfind(const char *name) {
	for (Var *v = vars; v; v = v->next)
		if (!strcmp(v->name, name)) return v;
	return NULL;
}

static const char *getvar(const char *name) {
	Var *v = vfind(name);
	return v ? v->val : NULL;
}

static void setvar(const char *name, const char *val) {
	Var *v = vfind(name);
	if (!v) {
		v = xmalloc(sizeof *v);
		v->name = xstrdup(name);
		v->val = NULL;
		v->exported = 0;
		v->next = vars;
		vars = v;
	}
	free(v->val);
	v->val = xstrdup(val);
	/* Exported vars live in environ too, so execvp(3) hands them to the
	 * child without us rebuilding an envp by hand. */
	if (v->exported) setenv(v->name, v->val, 1);
}

static void unsetvar(const char *name) {
	Var **pp = &vars;
	for (Var *v = *pp; v; pp = &v->next, v = v->next)
		if (!strcmp(v->name, name)) {
			*pp = v->next;
			unsetenv(v->name);
			free(v->name);
			free(v->val);
			free(v);
			return;
		}
	unsetenv(name);
}

static void exportvar(const char *name) {
	Var *v = vfind(name);
	if (!v) {
		setvar(name, "");
		v = vfind(name);
	}
	v->exported = 1;
	setenv(v->name, v->val, 1);
}

static void vars_init(void) {
	for (char **e = environ; *e; e++) {
		char *eq = strchr(*e, '=');
		if (!eq) continue;
		char *name = xmalloc((size_t)(eq - *e) + 1);
		memcpy(name, *e, (size_t)(eq - *e));
		name[eq - *e] = 0;
		setvar(name, eq + 1);
		vfind(name)->exported = 1;
		free(name);
	}
}

static int valid_name(const char *s, size_t n) {
	if (!n || (!isalpha((unsigned char)s[0]) && s[0] != '_')) return 0;
	for (size_t i = 1; i < n; i++)
		if (!isalnum((unsigned char)s[i]) && s[i] != '_') return 0;
	return 1;
}

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
			if (c == '`') {
				fprintf(stderr, "%s: backquotes not supported, use $()\n", progname);
				return -1;
			}
			if (c == '$' && p[1] == '(' && p[2] == '(') {
				/* Fail loudly rather than silently evaluating $((1+1)) as a
				 * subshell running the command `1+1`. */
				fprintf(stderr, "%s: arithmetic expansion is not implemented\n", progname);
				return -1;
			}
			if (c == '$' && p[1] == '(') {
				sput(&w, '$');
				p++;
				p = copy_nested(p, &w, '(', ')');
				if (!p) {
					fprintf(stderr, "%s: unterminated $(\n", progname);
					return -1;
				}
				quoted = 1;
				continue;
			}
			if (c == '$' && p[1] == '{') {
				sput(&w, '$');
				p++;
				p = copy_nested(p, &w, '{', '}');
				if (!p) {
					fprintf(stderr, "%s: unterminated ${\n", progname);
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

enum { R_IN, R_OUT, R_APPEND, R_DUP };

typedef struct {
	int fd;
	int how;
	char *word;
} Redir;

enum { N_SIMPLE, N_SEQ, N_AND, N_OR, N_PIPE, N_NOT, N_SUBSHELL, N_GROUP, N_IF, N_WHILE, N_FOR };

typedef struct Node Node;
struct Node {
	int type;
	Vec words; /* SIMPLE: argv, raw (unexpanded). FOR: the "in" list. */
	Vec assigns; /* SIMPLE: NAME=raw-value prefixes */
	Redir *redirs;
	int nredirs;
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

static void push_redir(Node *n, int fd, int how, const char *word) {
	n->redirs = xrealloc(n->redirs, (size_t)(n->nredirs + 1) * sizeof *n->redirs);
	n->redirs[n->nredirs].fd = fd;
	n->redirs[n->nredirs].how = how;
	n->redirs[n->nredirs].word = xstrdup(word);
	n->nredirs++;
}

/* Returns 1 if a redirection was consumed, 0 if none present, -1 on error. */
static int parse_redir(Node *n) {
	int fd = -1;
	if (cur()->type == TIONUM) {
		fd = atoi(cur()->text);
		adv();
	} else if (!isop("<") && !isop(">") && !isop(">>") && !isop(">&") && !isop("<&")) {
		return 0;
	}
	int how;
	if (isop("<")) how = R_IN, fd = fd < 0 ? 0 : fd;
	else if (isop(">")) how = R_OUT, fd = fd < 0 ? 1 : fd;
	else if (isop(">>")) how = R_APPEND, fd = fd < 0 ? 1 : fd;
	else if (isop(">&")) how = R_DUP, fd = fd < 0 ? 1 : fd;
	else if (isop("<&")) how = R_DUP, fd = fd < 0 ? 0 : fd;
	else return perr(cur()->text);
	adv();
	if (cur()->type != TWORD) return perr(cur()->text);
	push_redir(n, fd, how, cur()->text);
	adv();
	return 1;
}

static int parse_trailing_redirs(Node *n) {
	for (;;) {
		int r = parse_redir(n);
		if (r < 0) return -1;
		if (!r) return 0;
	}
}

static Node *parse_if_body(void) { /* 'if' or 'elif' already consumed */
	Node *n = nnew(N_IF);
	if (!(n->cond = parse_list())) return NULL;
	if (!iskw("then")) return perr(cur()->text), NULL;
	adv();
	if (!(n->body = parse_list())) return NULL;
	if (iskw("elif")) {
		adv();
		if (!(n->els = parse_if_body())) return NULL;
		return n; /* the innermost elif consumed the single 'fi' */
	}
	if (iskw("else")) {
		adv();
		if (!(n->els = parse_list())) return NULL;
	}
	if (!iskw("fi")) return perr(cur()->text), NULL;
	adv();
	return n;
}

static Node *parse_command(void) {
	if (isop("(")) {
		adv();
		Node *n = nnew(N_SUBSHELL);
		if (!(n->body = parse_list())) return NULL;
		if (!isop(")")) return perr(cur()->text), NULL;
		adv();
		if (parse_trailing_redirs(n) < 0) return NULL;
		return n;
	}
	if (iskw("{")) {
		adv();
		Node *n = nnew(N_GROUP);
		if (!(n->body = parse_list())) return NULL;
		if (!iskw("}")) return perr(cur()->text), NULL;
		adv();
		if (parse_trailing_redirs(n) < 0) return NULL;
		return n;
	}
	if (iskw("if")) {
		adv();
		Node *n = parse_if_body();
		if (n && parse_trailing_redirs(n) < 0) return NULL;
		return n;
	}
	if (iskw("while") || iskw("until")) {
		int until = iskw("until");
		adv();
		Node *n = nnew(N_WHILE);
		n->var = until ? xstrdup("until") : NULL; /* reuse: flags inversion */
		if (!(n->cond = parse_list())) return NULL;
		if (!iskw("do")) return perr(cur()->text), NULL;
		adv();
		if (!(n->body = parse_list())) return NULL;
		if (!iskw("done")) return perr(cur()->text), NULL;
		adv();
		if (parse_trailing_redirs(n) < 0) return NULL;
		return n;
	}

	Node *n = nnew(N_SIMPLE);
	int got = 0;
	for (;;) {
		int r = parse_redir(n);
		if (r < 0) return NULL;
		if (r) {
			got = 1;
			continue;
		}
		if (cur()->type != TWORD) break;
		/* NAME=value is an assignment only in the prefix position. */
		char *eq = strchr(cur()->text, '=');
		if (n->words.n == 0 && eq && valid_name(cur()->text, (size_t)(eq - cur()->text))) {
			vpush(&n->assigns, xstrdup(cur()->text));
		} else {
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
	if (iskw("!")) { /* `! cmd` inverts the pipeline's status to exactly 0 or 1 */
		adv();
		Node *p = nnew(N_NOT);
		if (!(p->body = parse_pipeline())) return NULL;
		return p;
	}
	Node *n = parse_command();
	if (!n) return NULL;
	while (isop("|")) {
		adv();
		while (isop("\n")) adv();
		Node *p = nnew(N_PIPE);
		p->left = n;
		if (!(p->right = parse_command())) return NULL;
		n = p;
	}
	return n;
}

static Node *parse_and_or(void) {
	Node *n = parse_pipeline();
	if (!n) return NULL;
	while (isop("&&") || isop("||")) {
		int type = isop("&&") ? N_AND : N_OR;
		adv();
		while (isop("\n")) adv();
		Node *p = nnew(type);
		p->left = n;
		if (!(p->right = parse_pipeline())) return NULL;
		n = p;
	}
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

static void eputs(Exp *e, const char *s, int quoted) {
	while (*s) eput(e, *s++, quoted);
}

/* Run `cmd` in a child, capture its stdout, strip trailing newlines. */
static char *command_subst(const char *cmd) {
	int fds[2];
	if (pipe(fds) < 0) fatal("pipe: %s", strerror(errno));
	fflush(NULL);
	pid_t pid = fork();
	if (pid < 0) fatal("fork: %s", strerror(errno));
	if (pid == 0) {
		close(fds[0]);
		if (fds[1] != 1) {
			dup2(fds[1], 1);
			close(fds[1]);
		}
		int s = exec_string(cmd);
		fflush(NULL);
		_exit(s);
	}
	close(fds[1]);
	Str out = {0};
	char buf[4096];
	ssize_t r;
	while ((r = read(fds[0], buf, sizeof buf)) > 0)
		for (ssize_t i = 0; i < r; i++) sput(&out, buf[i]);
	close(fds[0]);
	int st = 0;
	while (waitpid(pid, &st, 0) < 0 && errno == EINTR) {}
	/* The substitution's status is visible to `x=$(false)`; a real command in
	 * the same simple command overwrites it afterwards. */
	status = WIFEXITED(st) ? WEXITSTATUS(st) : 128 + WTERMSIG(st);
	while (out.n && out.p[out.n - 1] == '\n') out.p[--out.n] = 0;
	return out.p ? out.p : xstrdup("");
}

/* Handle one ${...} body (without the braces). */
static void expand_braced(const char *b, Exp *e, int dq) {
	if (!strcmp(b, "#")) { /* ${#} is $#, not a length */
		eputs(e, "0", dq);
		return;
	}
	if (!strcmp(b, "?")) {
		char num[16];
		snprintf(num, sizeof num, "%d", status);
		eputs(e, num, dq);
		return;
	}
	if (*b == '#' && b[1]) { /* ${#NAME} -> length */
		const char *v = getvar(b + 1);
		char num[32];
		snprintf(num, sizeof num, "%zu", v ? strlen(v) : (size_t)0);
		eputs(e, num, dq);
		return;
	}
	size_t nlen = 0;
	while (b[nlen] && b[nlen] != ':' && b[nlen] != '-' && b[nlen] != '+') nlen++;
	char *name = xmalloc(nlen + 1);
	memcpy(name, b, nlen);
	name[nlen] = 0;
	const char *v = getvar(name);
	const char *op = b + nlen;
	int colon = *op == ':';
	if (colon) op++;
	int unset = !v || (colon && !*v);
	if (*op == '-') {
		if (unset) {
			Exp sub = {0};
			/* The default word is itself expanded, recursively. */
			expand_into(op + 1, &sub, dq);
			for (size_t i = 0; i < sub.text.n; i++) eput(e, sub.text.p[i], sub.flg.p[i]);
		} else {
			eputs(e, v, dq);
		}
	} else if (*op == '+') {
		if (!unset) {
			Exp sub = {0};
			expand_into(op + 1, &sub, dq);
			for (size_t i = 0; i < sub.text.n; i++) eput(e, sub.text.p[i], sub.flg.p[i]);
		}
	} else if (!*op) {
		if (v) eputs(e, v, dq);
	} else {
		fprintf(stderr, "%s: ${%s}: unsupported expansion\n", progname, b);
	}
	free(name);
}

/* Step 1: substitution pass. Fills e->text and the per-byte quoted flags. */
static void expand_into(const char *p, Exp *e, int dq_init) {
	int dq = dq_init;
	const char *begin = p;
	while (*p) {
		char c = *p;
		/* Tilde expansion is step 1a: only an unquoted leading ~ followed by
		 * '/' or end-of-word, and the result is NOT field-split or globbed,
		 * hence the quoted flag. shortcut: no ~user and no expansion after
		 * ':' in assignments. Ceiling: PATH=~/bin:~/sbin keeps the second
		 * tilde literal. Upgrade path: getpwnam(3) plus a colon-aware scan of
		 * assignment right-hand sides. */
		if (c == '~' && p == begin && !dq && (p[1] == '/' || p[1] == 0)) {
			const char *h = getvar("HOME");
			if (h) {
				while (*h) eput(e, *h++, 1);
				p++;
				continue;
			}
		}
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
		if (c == '$' && p[1]) {
			char n = p[1];
			if (n == '?') {
				char num[16];
				snprintf(num, sizeof num, "%d", status);
				eputs(e, num, dq);
				p += 2;
				continue;
			}
			if (n == '#') { /* no positional parameters -> always 0 */
				eputs(e, "0", dq);
				p += 2;
				continue;
			}
			if (isdigit((unsigned char)n)) { /* $1.. unset -> empty, as in dash */
				p += 2;
				continue;
			}
			if (n == '(') {
				Str raw = {0};
				const char *q = copy_nested(p + 1, &raw, '(', ')');
				if (!q) {
					fprintf(stderr, "%s: unterminated $(\n", progname);
					p += 2;
					continue;
				}
				raw.p[raw.n - 1] = 0; /* drop trailing ')' */
				char *out = command_subst(raw.p + 1);
				eputs(e, out, dq);
				free(raw.p);
				free(out);
				p = q;
				continue;
			}
			if (n == '{') {
				Str raw = {0};
				const char *q = copy_nested(p + 1, &raw, '{', '}');
				if (!q) {
					fprintf(stderr, "%s: unterminated ${\n", progname);
					p += 2;
					continue;
				}
				raw.p[raw.n - 1] = 0;
				expand_braced(raw.p + 1, e, dq);
				free(raw.p);
				p = q;
				continue;
			}
			if (isalpha((unsigned char)n) || n == '_') {
				const char *s = p + 1;
				size_t k = 0;
				while (s[k] && (isalnum((unsigned char)s[k]) || s[k] == '_')) k++;
				char *name = xmalloc(k + 1);
				memcpy(name, s, k);
				name[k] = 0;
				const char *v = getvar(name);
				if (v) eputs(e, v, dq);
				free(name);
				p = s + k;
				continue;
			}
			eput(e, '$', dq);
			p++;
			continue;
		}
		/* Inside double quotes, glob metacharacters are literal: hence the
		 * quoted flag follows dq for ordinary bytes too. */
		eput(e, c, dq);
		p++;
	}
}

static const char *ifs_value(void) {
	const char *v = getvar("IFS");
	return v ? v : " \t\n";
}

/* Step 3: glob a single field. Quoted metacharacters are backslash-escaped so
 * glob(3) treats them literally; glob(3) honours backslash unless
 * GLOB_NOESCAPE. No match -> the field is used as written (POSIX). */
static void glob_field(const char *text, const char *flg, size_t n, Vec *out) {
	int special = 0;
	for (size_t i = 0; i < n; i++)
		if (!flg[i] && (text[i] == '*' || text[i] == '?' || text[i] == '[')) special = 1;

	Str lit = {0};
	for (size_t i = 0; i < n; i++) sput(&lit, text[i]);

	if (!special) {
		vpush(out, lit.p ? lit.p : xstrdup(""));
		return;
	}
	Str pat = {0};
	for (size_t i = 0; i < n; i++) {
		if (flg[i] && strchr("*?[]\\", text[i])) sput(&pat, '\\');
		sput(&pat, text[i]);
	}
	glob_t g;
	memset(&g, 0, sizeof g);
	int rc = glob(pat.p ? pat.p : "", 0, NULL, &g);
	if (rc == 0 && g.gl_pathc > 0) {
		for (size_t i = 0; i < g.gl_pathc; i++) vpush(out, xstrdup(g.gl_pathv[i]));
		free(lit.p);
	} else {
		vpush(out, lit.p ? lit.p : xstrdup(""));
	}
	globfree(&g);
	free(pat.p);
}

enum { EX_FULL, EX_NOSPLIT };

static void expand_word(const char *raw, Vec *out, int mode) {
	Exp e = {0};
	exp_sawquote = 0;
	expand_into(raw, &e, 0);
	size_t n = e.text.n;
	const char *t = e.text.p ? e.text.p : "";
	const char *f = e.flg.p ? e.flg.p : "";

	if (mode == EX_NOSPLIT) {
		Str s = {0};
		for (size_t i = 0; i < n; i++) sput(&s, t[i]);
		vpush(out, s.p ? s.p : xstrdup(""));
		free(e.text.p);
		free(e.flg.p);
		return;
	}

	/* Step 2: field splitting. IFS whitespace runs collapse and leading or
	 * trailing runs produce no empty fields; a non-whitespace IFS byte is a
	 * hard delimiter. Only unquoted bytes delimit. */
	if (n == 0) {
		if (exp_sawquote) vpush(out, xstrdup(""));
		free(e.text.p);
		free(e.flg.p);
		return;
	}
	const char *ifs = ifs_value();
	size_t i = 0;
	int any = 0;
	while (i < n) {
		while (i < n && !f[i] && strchr(ifs, t[i]) && isspace((unsigned char)t[i])) i++;
		if (i >= n) break;
		size_t start = i;
		while (i < n && !(!f[i] && strchr(ifs, t[i]))) i++;
		glob_field(t + start, f + start, i - start, out);
		any = 1;
		if (i < n && !f[i] && !isspace((unsigned char)t[i])) {
			i++; /* consume the hard delimiter; an empty field may follow */
			if (i == n) {
				vpush(out, xstrdup(""));
			}
		}
	}
	/* A word that expands to nothing at all contributes no field -- this is
	 * why `cmd $unset` runs cmd with zero arguments, not with "". */
	(void)any;
	free(e.text.p);
	free(e.flg.p);
}

/* ------------------------------------------------------------- builtins */

static int b_echo(char **av) {
	int nflag = 0, first = 1;
	int i = 1;
	if (av[1] && !strcmp(av[1], "-n")) nflag = 1, i = 2;
	for (; av[i]; i++) {
		if (!first) putchar(' ');
		first = 0;
		/* dash's echo always interprets backslash escapes (no -e needed) and
		 * \c aborts output including the trailing newline. We match dash,
		 * not bash, because dash is the oracle. */
		for (char *s = av[i]; *s; s++) {
			if (*s != '\\') {
				putchar(*s);
				continue;
			}
			switch (s[1]) {
			case 'a': putchar('\a'); s++; break;
			case 'b': putchar('\b'); s++; break;
			case 'c': fflush(stdout); return 0;
			case 'f': putchar('\f'); s++; break;
			case 'n': putchar('\n'); s++; break;
			case 'r': putchar('\r'); s++; break;
			case 't': putchar('\t'); s++; break;
			case 'v': putchar('\v'); s++; break;
			case '\\': putchar('\\'); s++; break;
			case '0': {
				int v = 0, k = 0;
				s++;
				while (k < 3 && s[1] >= '0' && s[1] <= '7') v = v * 8 + (s[1] - '0'), s++, k++;
				putchar(v);
				break;
			}
			default: putchar('\\'); break;
			}
		}
	}
	if (!nflag) putchar('\n');
	fflush(stdout);
	return 0;
}

static int b_cd(char **av) {
	const char *d = av[1];
	int print = 0;
	char *target = NULL;
	if (!d) {
		d = getvar("HOME");
		if (!d) {
			fprintf(stderr, "%s: cd: HOME not set\n", progname);
			return 2;
		}
	} else if (!strcmp(d, "-")) {
		const char *o = getvar("OLDPWD");
		if (!o) {
			fflush(stdout);
			fprintf(stderr, "%s: cd: OLDPWD not set\n", progname);
			return 2;
		}
		target = xstrdup(o);
		d = target;
		print = 1; /* `cd -` echoes the directory it moved to */
	}
	char old[PATH_MAX];
	int haveold = getcwd(old, sizeof old) != NULL;
	if (chdir(d) < 0) {
		fflush(stdout);
		fprintf(stderr, "%s: cd: %s: %s\n", progname, d, strerror(errno));
		free(target);
		return 2; /* dash reports 2 here, not 1 */
	}
	if (haveold) setvar("OLDPWD", old);
	char buf[PATH_MAX];
	if (getcwd(buf, sizeof buf)) setvar("PWD", buf);
	if (print) {
		printf("%s\n", getvar("PWD") ? getvar("PWD") : d);
		fflush(stdout);
	}
	free(target);
	return 0;
}

static int b_pwd(char **av) {
	(void)av;
	char buf[PATH_MAX];
	if (!getcwd(buf, sizeof buf)) {
		fprintf(stderr, "%s: pwd: %s\n", progname, strerror(errno));
		return 1;
	}
	printf("%s\n", buf);
	fflush(stdout);
	return 0;
}

static int b_export(char **av) {
	if (!av[1]) return 0;
	for (int i = 1; av[i]; i++) {
		char *eq = strchr(av[i], '=');
		if (eq) {
			*eq = 0;
			if (!valid_name(av[i], strlen(av[i]))) {
				fprintf(stderr, "%s: export: %s: bad name\n", progname, av[i]);
				*eq = '=';
				return 1;
			}
			setvar(av[i], eq + 1);
			exportvar(av[i]);
			*eq = '=';
		} else {
			if (!valid_name(av[i], strlen(av[i]))) {
				fprintf(stderr, "%s: export: %s: bad name\n", progname, av[i]);
				return 1;
			}
			exportvar(av[i]);
		}
	}
	return 0;
}

static int b_exit(char **av) {
	int s = av[1] ? atoi(av[1]) & 0xff : status;
	fflush(NULL);
	exit(s);
}

static int b_read(char **av) {
	int nv = 0;
	while (av[nv + 1]) nv++;
	if (nv == 0) {
		fprintf(stderr, "%s: read: arg expected\n", progname);
		return 2;
	}
	/* Byte-at-a-time: `read` must not consume past the newline, or the next
	 * command in a pipeline loses its input. */
	Str line = {0};
	int got = 0, eof = 0;
	for (;;) {
		char c;
		ssize_t r = read(0, &c, 1);
		if (r <= 0) {
			eof = 1;
			break;
		}
		got = 1;
		if (c == '\\') {
			char d;
			if (read(0, &d, 1) <= 0) break;
			if (d == '\n') continue; /* line continuation */
			sput(&line, d);
			continue;
		}
		if (c == '\n') break;
		sput(&line, c);
	}
	const char *t = line.p ? line.p : "";
	size_t n = line.n;
	const char *ifs = ifs_value();
	size_t i = 0;
	for (int k = 0; k < nv; k++) {
		while (i < n && strchr(ifs, t[i]) && isspace((unsigned char)t[i])) i++;
		size_t start = i;
		size_t end;
		if (k == nv - 1) {
			end = n;
			while (end > start && strchr(ifs, t[end - 1]) && isspace((unsigned char)t[end - 1]))
				end--;
			i = n;
		} else {
			while (i < n && !strchr(ifs, t[i])) i++;
			end = i;
			if (i < n && !isspace((unsigned char)t[i])) i++;
		}
		char *val = xmalloc(end - start + 1);
		memcpy(val, t + start, end - start);
		val[end - start] = 0;
		setvar(av[k + 1], val);
		free(val);
	}
	free(line.p);
	return (eof && !got) ? 1 : 0;
}

static int b_unset(char **av) {
	/* dash does not complain about unsetting something that was never set. */
	for (int i = 1; av[i]; i++) {
		if (!valid_name(av[i], strlen(av[i]))) {
			fflush(stdout);
			fprintf(stderr, "%s: unset: %s: bad name\n", progname, av[i]);
			return 1;
		}
		unsetvar(av[i]);
	}
	return 0;
}

static int b_loopctl(char **av, int which) {
	int n = av[1] ? atoi(av[1]) : 1;
	if (n < 1) {
		fprintf(stderr, "%s: %s: bad count\n", progname, av[0]);
		return 2;
	}
	/* dash silently ignores break outside a loop; match that. */
	if (loopdepth == 0) return 0;
	loopctl = which;
	loopcnt = n > loopdepth ? loopdepth : n;
	return 0;
}

static int b_break(char **av) { return b_loopctl(av, 1); }
static int b_continue(char **av) { return b_loopctl(av, 2); }

static int b_true(char **av) {
	(void)av;
	return 0;
}
static int b_false(char **av) {
	(void)av;
	return 1;
}
static int b_colon(char **av) {
	(void)av;
	return 0;
}

static struct {
	const char *name;
	int (*fn)(char **);
} builtins[] = {
    {"echo", b_echo}, {"true", b_true}, {"false", b_false},
    {"pwd", b_pwd},   {":", b_colon},
    {"cd", b_cd},     {"exit", b_exit}, {"export", b_export},
    {"read", b_read}, {"unset", b_unset},
    {"break", b_break}, {"continue", b_continue},
};

static int builtin_index(const char *name) {
	for (size_t i = 0; i < sizeof builtins / sizeof *builtins; i++)
		if (!strcmp(builtins[i].name, name)) return (int)i;
	return -1;
}

/* ------------------------------------------------------------- execution */

typedef struct {
	int fd[16];
	int saved[16];
	int n;
} FdSave;

static int apply_redirs(Redir *r, int n, FdSave *sv) {
	if (sv) sv->n = 0;
	for (int i = 0; i < n; i++) {
		Vec w = {0};
		expand_word(r[i].word, &w, EX_NOSPLIT);
		if (w.n != 1) {
			fflush(stdout);
			fprintf(stderr, "%s: ambiguous redirect\n", progname);
			return -1;
		}
		if (r[i].how == R_DUP) {
			/* `2>&1` and friends. Only a digit or "-" is accepted; psh has no
			 * named-fd tricks. Saving happens before the dup2 so a builtin's
			 * redirection can be undone. */
			int src;
			if (!strcmp(w.v[0], "-")) src = -1;
			else {
				char *end;
				long v = strtol(w.v[0], &end, 10);
				if (*end || end == w.v[0]) {
					fflush(stdout);
					fprintf(stderr, "%s: %s: bad fd\n", progname, w.v[0]);
					return -1;
				}
				src = (int)v;
			}
			if (sv && sv->n < 16) {
				sv->fd[sv->n] = r[i].fd;
				sv->saved[sv->n] = fcntl(r[i].fd, F_DUPFD_CLOEXEC, 10);
				sv->n++;
			}
			fflush(NULL);
			if (src < 0) close(r[i].fd);
			else if (dup2(src, r[i].fd) < 0) {
				fprintf(stderr, "%s: %d: %s\n", progname, src, strerror(errno));
				return -1;
			}
			continue;
		}
		int fd;
		if (r[i].how == R_IN) fd = open(w.v[0], O_RDONLY);
		else if (r[i].how == R_OUT) fd = open(w.v[0], O_WRONLY | O_CREAT | O_TRUNC, 0666);
		else fd = open(w.v[0], O_WRONLY | O_CREAT | O_APPEND, 0666);
		if (fd < 0) {
			fflush(stdout);
			fprintf(stderr, "%s: %s: %s\n", progname, w.v[0], strerror(errno));
			return -1;
		}
		if (sv && sv->n < 16) {
			sv->fd[sv->n] = r[i].fd;
			sv->saved[sv->n] = fcntl(r[i].fd, F_DUPFD_CLOEXEC, 10);
			sv->n++;
		}
		if (fd != r[i].fd) {
			fflush(r[i].fd == 1 ? stdout : NULL);
			dup2(fd, r[i].fd);
			close(fd);
		}
	}
	return 0;
}

static void restore_fds(FdSave *sv) {
	fflush(NULL);
	for (int i = sv->n - 1; i >= 0; i--) {
		if (sv->saved[i] >= 0) {
			dup2(sv->saved[i], sv->fd[i]);
			close(sv->saved[i]);
		} else {
			close(sv->fd[i]);
		}
	}
	sv->n = 0;
}

static int exec_node(Node *n);

static void do_assigns(Node *n, int export_them) {
	for (int i = 0; i < n->assigns.n; i++) {
		char *eq = strchr(n->assigns.v[i], '=');
		*eq = 0;
		Vec w = {0};
		expand_word(eq + 1, &w, EX_NOSPLIT); /* no splitting, no globbing */
		setvar(n->assigns.v[i], w.n ? w.v[0] : "");
		if (export_them) exportvar(n->assigns.v[i]);
		*eq = '=';
	}
}

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
		FdSave sv = {0};
		if (n->nredirs) {
			if (apply_redirs(n->redirs, n->nredirs, &sv) < 0) {
				restore_fds(&sv);
				return 2;
			}
			restore_fds(&sv);
		}
		/* A command with no command word succeeds (status 0) -- EXCEPT that a
		 * command substitution in the assignment leaves its own status behind,
		 * which is the only way `x=$(false)` can be detected. Hence: zero
		 * first, then let do_assigns overwrite it. */
		status = 0;
		do_assigns(n, 0);
		return status;
	}

	int bi = builtin_index(argv.v[0]);
	if (bi >= 0 && !in_child) {
		FdSave sv = {0};
		if (n->nredirs && apply_redirs(n->redirs, n->nredirs, &sv) < 0) {
			restore_fds(&sv);
			return 2;
		}
		/* `X=1 echo hi` must NOT leave X set afterwards, so snapshot the
		 * named variables (including "was unset") and put them back. The
		 * exception is POSIX's special builtins, where the assignment
		 * persists -- but psh has none of those that take prefixes, so we
		 * restore unconditionally. */
		Vec old = {0};
		for (int i = 0; i < n->assigns.n; i++) {
			char *eq = strchr(n->assigns.v[i], '=');
			*eq = 0;
			const char *v = getvar(n->assigns.v[i]);
			vpush(&old, v ? xstrdup(v) : NULL);
			*eq = '=';
		}
		do_assigns(n, 1); /* prefixes are exported to the command's env */
		int st = builtins[bi].fn(argv.v);
		fflush(stdout);
		for (int i = 0; i < n->assigns.n; i++) {
			char *eq = strchr(n->assigns.v[i], '=');
			*eq = 0;
			if (old.v[i]) setvar(n->assigns.v[i], old.v[i]);
			else unsetvar(n->assigns.v[i]);
			*eq = '=';
		}
		if (n->nredirs) restore_fds(&sv);
		return st;
	}

	if (in_child) {
		if (apply_redirs(n->redirs, n->nredirs, NULL) < 0) _exit(2);
		do_assigns(n, 1);
		if (bi >= 0) {
			int st = builtins[bi].fn(argv.v);
			fflush(NULL);
			_exit(st);
		}
		exec_external(argv.v);
	}

	fflush(NULL);
	pid_t pid = fork();
	if (pid < 0) fatal("fork: %s", strerror(errno));
	if (pid == 0) {
		signal(SIGINT, SIG_DFL);
		if (apply_redirs(n->redirs, n->nredirs, NULL) < 0) _exit(2);
		do_assigns(n, 1);
		exec_external(argv.v);
	}
	int st = 0;
	while (waitpid(pid, &st, 0) < 0 && errno == EINTR) {}
	return WIFEXITED(st) ? WEXITSTATUS(st) : 128 + WTERMSIG(st);
}

/* Flatten the left-leaning N_PIPE tree into execution order. */
static int flatten_pipe(Node *n, Node **out, int max) {
	if (n->type != N_PIPE) {
		if (max < 1) return -1;
		out[0] = n;
		return 1;
	}
	int k = flatten_pipe(n->left, out, max);
	if (k < 0 || k >= max) return -1;
	out[k] = n->right;
	return k + 1;
}

static int exec_pipeline(Node *n) {
	Node *cmds[32];
	int nc = flatten_pipe(n, cmds, 32);
	if (nc < 0) {
		fprintf(stderr, "%s: pipeline too long\n", progname);
		return 2;
	}
	pid_t pids[32];
	int prev_read = -1;
	fflush(NULL);
	for (int i = 0; i < nc; i++) {
		int fds[2] = {-1, -1};
		if (i < nc - 1 && pipe(fds) < 0) fatal("pipe: %s", strerror(errno));
		pid_t pid = fork();
		if (pid < 0) fatal("fork: %s", strerror(errno));
		if (pid == 0) {
			signal(SIGINT, SIG_DFL);
			if (prev_read >= 0) {
				dup2(prev_read, 0);
				close(prev_read);
			}
			if (fds[1] >= 0) {
				close(fds[0]);
				dup2(fds[1], 1);
				close(fds[1]);
			}
			/* Every stage runs in its own process, so a builtin in a
			 * pipeline cannot affect the parent shell -- the same surprise
			 * dash gives you with `echo x | read v`. */
			int st = (cmds[i]->type == N_SIMPLE) ? exec_simple(cmds[i], 1) : exec_node(cmds[i]);
			fflush(NULL);
			_exit(st);
		}
		pids[i] = pid;
		if (prev_read >= 0) close(prev_read);
		if (fds[1] >= 0) close(fds[1]);
		prev_read = fds[0];
	}
	if (prev_read >= 0) close(prev_read);
	int last = 0;
	for (int i = 0; i < nc; i++) {
		int st = 0;
		while (waitpid(pids[i], &st, 0) < 0 && errno == EINTR) {}
		/* A pipeline's status is the LAST command's status, not the first
		 * failure: `false | true` is 0. */
		if (i == nc - 1) last = WIFEXITED(st) ? WEXITSTATUS(st) : 128 + WTERMSIG(st);
	}
	return last;
}

/* One loop level consumes one pending break/continue. Returns 1 if this loop
 * should keep iterating (a `continue` aimed at us), 0 if it must stop (a
 * `break` aimed at us, or anything aimed at an outer loop). */
static int loop_absorb(void) {
	if (--loopcnt > 0) return 0; /* still unwinding towards an outer loop */
	int which = loopctl;
	loopctl = 0;
	loopcnt = 0;
	return which == 2;
}

static int exec_compound_body(Node *n) {
	switch (n->type) {
	case N_SUBSHELL: {
		fflush(NULL);
		pid_t pid = fork();
		if (pid < 0) fatal("fork: %s", strerror(errno));
		if (pid == 0) {
			signal(SIGINT, SIG_DFL);
			int st = exec_node(n->body);
			fflush(NULL);
			_exit(st);
		}
		int st = 0;
		while (waitpid(pid, &st, 0) < 0 && errno == EINTR) {}
		return WIFEXITED(st) ? WEXITSTATUS(st) : 128 + WTERMSIG(st);
	}
	case N_GROUP:
		/* A brace group runs in the CURRENT shell -- unlike ( ), assignments
		 * and cd inside it are visible afterwards. */
		return exec_node(n->body);
	case N_IF: {
		int c = exec_node(n->cond);
		status = c;
		if (c == 0) return exec_node(n->body);
		if (n->els) return exec_node(n->els);
		return 0; /* an if with no taken branch is successful */
	}
	case N_WHILE: {
		int invert = n->var != NULL; /* `until` */
		int st = 0;
		loopdepth++;
		for (;;) {
			int c = exec_node(n->cond);
			status = c;
			if (loopctl) break;
			if (invert ? (c == 0) : (c != 0)) break;
			st = exec_node(n->body);
			if (loopctl && !loop_absorb()) break;
		}
		loopdepth--;
		return st; /* zero iterations -> 0, as POSIX requires */
	}
	}
	return 2;
}

static int exec_node(Node *n) {
	if (!n) return 0;
	switch (n->type) {
	case N_SEQ: {
		if (!n->left && !n->right) return status; /* empty program */
		int s = exec_node(n->left);
		if (loopctl) return s; /* break/continue aborts the rest of the list */
		return exec_node(n->right);
	}
	case N_AND: {
		int s = exec_node(n->left);
		status = s;
		if (loopctl) return s;
		return s == 0 ? exec_node(n->right) : s;
	}
	case N_OR: {
		int s = exec_node(n->left);
		status = s;
		if (loopctl) return s;
		return s != 0 ? exec_node(n->right) : s;
	}
	case N_NOT: {
		int s = exec_node(n->body);
		return status = (s == 0) ? 1 : 0;
	}
	case N_PIPE:
		return status = exec_pipeline(n);
	case N_SIMPLE:
		return status = exec_simple(n, 0);
	default: {
		/* A compound command may carry its own redirections
		 * (`while ...; done > f`); apply them around the whole body. */
		FdSave sv = {0};
		if (n->nredirs && apply_redirs(n->redirs, n->nredirs, &sv) < 0) {
			restore_fds(&sv);
			return status = 2;
		}
		int st = exec_compound_body(n);
		if (n->nredirs) restore_fds(&sv);
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

	int oloopctl = loopctl, oloopcnt = loopcnt, oloopdepth = loopdepth;
	loopctl = loopcnt = loopdepth = 0;

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
	loopctl = oloopctl;
	loopcnt = oloopcnt;
	loopdepth = oloopdepth;
	return rc;
}

static char *slurp(FILE *f) {
	Str s = {0};
	int c;
	while ((c = fgetc(f)) != EOF) sput(&s, c);
	return s.p ? s.p : xstrdup("");
}

int main(int argc, char **argv) {
	vars_init();
	char cwd[PATH_MAX];
	if (getcwd(cwd, sizeof cwd)) setvar("PWD", cwd);
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
