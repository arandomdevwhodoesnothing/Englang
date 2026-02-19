/*
 * ENGLANG - A Turing-Complete Programming Language with Plain English Syntax
 * 
 * Syntax Examples:
 *   set x to 10
 *   set y to 20
 *   add x and y into result
 *   print result
 *   if x is greater than 5 then ... end if
 *   while x is less than 100 then ... end while
 *   define greeting as ... end define
 *   call greeting
 *   push 5 onto stack
 *   pop from stack into x
 *   store x at address 10
 *   load from address 10 into x
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>

#define MAX_VARS       512
#define MAX_NAME       64
#define MAX_LINE       1024
#define MAX_LINES      8192
#define MAX_STACK      512
#define MAX_CALL_STACK 128
#define MAX_FUNCS      256
#define MAX_MEM        1024
#define MAX_ARRAYS     64
#define MAX_ARRAY_SIZE 1024
#define MAX_STRING_POOL 4096

/* ─── Value types ─── */
typedef enum { TYPE_NUM, TYPE_STR } VType;

typedef struct {
    VType type;
    double num;
    char  str[256];
} Value;

/* ─── Variable store ─── */
typedef struct {
    char  name[MAX_NAME];
    Value val;
    int   used;
} Var;

/* ─── Array store ─── */
typedef struct {
    char   name[MAX_NAME];
    Value  data[MAX_ARRAY_SIZE];
    int    size;
    int    used;
} Array;

/* ─── Function definition ─── */
typedef struct {
    char name[MAX_NAME];
    int  start_line; /* line index of first statement inside */
    int  end_line;   /* line index of "end define" */
    char params[8][MAX_NAME];
    int  param_count;
} FuncDef;

/* ─── Interpreter state ─── */
static Var      vars[MAX_VARS];
static Array    arrays[MAX_ARRAYS];
static FuncDef  funcs[MAX_FUNCS];
static int      func_count = 0;
static double   mem[MAX_MEM]; /* raw memory */
static double   data_stack[MAX_STACK];
static int      stack_top = 0;
static char    *lines[MAX_LINES];
static int      line_count = 0;
static int      call_stack[MAX_CALL_STACK]; /* return line indices */
static int      call_sp = 0;
/* per-call variable scopes aren't implemented; simple globals */

/* ─── String helpers ─── */
static char *trim(char *s) {
    while (isspace((unsigned char)*s)) s++;
    char *e = s + strlen(s) - 1;
    while (e > s && isspace((unsigned char)*e)) *e-- = '\0';
    return s;
}

static int startswith(const char *s, const char *prefix) {
    return strncmp(s, prefix, strlen(prefix)) == 0;
}

/* ─── Variable access ─── */
static Var *find_var(const char *name) {
    for (int i = 0; i < MAX_VARS; i++)
        if (vars[i].used && strcmp(vars[i].name, name) == 0)
            return &vars[i];
    return NULL;
}

static Var *get_or_create_var(const char *name) {
    Var *v = find_var(name);
    if (v) return v;
    for (int i = 0; i < MAX_VARS; i++) {
        if (!vars[i].used) {
            vars[i].used = 1;
            strncpy(vars[i].name, name, MAX_NAME - 1);
            vars[i].val.type = TYPE_NUM;
            vars[i].val.num  = 0;
            vars[i].val.str[0] = '\0';
            return &vars[i];
        }
    }
    fprintf(stderr, "Error: too many variables\n");
    exit(1);
}

/* ─── Array access ─── */
static Array *find_array(const char *name) {
    for (int i = 0; i < MAX_ARRAYS; i++)
        if (arrays[i].used && strcmp(arrays[i].name, name) == 0)
            return &arrays[i];
    return NULL;
}

static Array *get_or_create_array(const char *name) {
    Array *a = find_array(name);
    if (a) return a;
    for (int i = 0; i < MAX_ARRAYS; i++) {
        if (!arrays[i].used) {
            arrays[i].used = 1;
            strncpy(arrays[i].name, name, MAX_NAME - 1);
            arrays[i].size = 0;
            return &arrays[i];
        }
    }
    fprintf(stderr, "Error: too many arrays\n");
    exit(1);
}

/* ─── Value resolution ─── */
static Value resolve(const char *token) {
    Value v;
    v.type = TYPE_NUM;
    v.num  = 0;
    v.str[0] = '\0';

    /* quoted string literal */
    if (token[0] == '"') {
        v.type = TYPE_STR;
        size_t len = strlen(token);
        size_t copy = (len > 2) ? len - 2 : 0;
        if (copy >= 256) copy = 255;
        strncpy(v.str, token + 1, copy);
        v.str[copy] = '\0';
        return v;
    }

    /* numeric literal */
    char *end;
    double d = strtod(token, &end);
    if (*end == '\0') {
        v.type = TYPE_NUM;
        v.num  = d;
        return v;
    }

    /* variable */
    Var *var = find_var(token);
    if (var) return var->val;

    /* treat as string */
    v.type = TYPE_STR;
    strncpy(v.str, token, 255);
    return v;
}

static double resolve_num(const char *token) {
    Value v = resolve(token);
    if (v.type == TYPE_STR) return 0;
    return v.num;
}

static const char *resolve_str(const char *token, char *buf, int bufsize) {
    Value v = resolve(token);
    if (v.type == TYPE_NUM)
        snprintf(buf, bufsize, "%g", v.num);
    else
        strncpy(buf, v.str, bufsize - 1);
    return buf;
}

/* ─── Function lookup ─── */
static FuncDef *find_func(const char *name) {
    for (int i = 0; i < func_count; i++)
        if (strcmp(funcs[i].name, name) == 0)
            return &funcs[i];
    return NULL;
}

/* ─── Condition evaluation ─── */
/*  "<a> is [not] (greater than|less than|equal to|greater than or equal to|less than or equal to) <b>"
    "<a> is [not] empty"
    "<a> is [not] zero"
*/
static int eval_condition(const char *cond_str) {
    char buf[MAX_LINE];
    strncpy(buf, cond_str, MAX_LINE - 1);
    char *s = trim(buf);

    /* tokenize into words */
    char *words[32];
    int wc = 0;
    char *tok = strtok(s, " \t");
    while (tok && wc < 32) {
        words[wc++] = tok;
        tok = strtok(NULL, " \t");
    }

    if (wc < 3) return 0;

    /* find "is" */
    int is_idx = -1;
    for (int i = 0; i < wc; i++)
        if (strcmp(words[i], "is") == 0) { is_idx = i; break; }
    if (is_idx < 0) return 0;

    /* left operand (could be multi-word but let's take first word(s) before 'is') */
    char lhs[MAX_NAME] = "";
    for (int i = 0; i < is_idx; i++) {
        if (i) strcat(lhs, " ");
        strcat(lhs, words[i]);
    }

    int neg = 0;
    int op_start = is_idx + 1;
    if (op_start < wc && strcmp(words[op_start], "not") == 0) {
        neg = 1;
        op_start++;
    }

    /* rhs is last word(s) after operator */
    /* detect operator */
    char op[64] = "";
    int rhs_start = op_start;
    /* try multi-word operators */
    if (op_start + 3 < wc &&
        strcmp(words[op_start], "greater") == 0 &&
        strcmp(words[op_start+1], "than") == 0 &&
        strcmp(words[op_start+2], "or") == 0 &&
        strcmp(words[op_start+3], "equal") == 0 &&
        op_start+4 < wc && strcmp(words[op_start+4], "to") == 0) {
        strcpy(op, ">="); rhs_start = op_start + 5;
    } else if (op_start + 3 < wc &&
        strcmp(words[op_start], "less") == 0 &&
        strcmp(words[op_start+1], "than") == 0 &&
        strcmp(words[op_start+2], "or") == 0 &&
        strcmp(words[op_start+3], "equal") == 0 &&
        op_start+4 < wc && strcmp(words[op_start+4], "to") == 0) {
        strcpy(op, "<="); rhs_start = op_start + 5;
    } else if (op_start + 2 < wc &&
        strcmp(words[op_start], "greater") == 0 &&
        strcmp(words[op_start+1], "than") == 0) {
        strcpy(op, ">"); rhs_start = op_start + 2;
    } else if (op_start + 2 < wc &&
        strcmp(words[op_start], "less") == 0 &&
        strcmp(words[op_start+1], "than") == 0) {
        strcpy(op, "<"); rhs_start = op_start + 2;
    } else if (op_start + 2 < wc &&
        strcmp(words[op_start], "equal") == 0 &&
        strcmp(words[op_start+1], "to") == 0) {
        strcpy(op, "=="); rhs_start = op_start + 2;
    } else if (op_start < wc && strcmp(words[op_start], "empty") == 0) {
        strcpy(op, "empty"); rhs_start = op_start + 1;
    } else if (op_start < wc && strcmp(words[op_start], "zero") == 0) {
        strcpy(op, "zero"); rhs_start = op_start + 1;
    }

    char rhs[MAX_NAME] = "";
    for (int i = rhs_start; i < wc; i++) {
        if (i > rhs_start) strcat(rhs, " ");
        strcat(rhs, words[i]);
    }

    int result = 0;
    if (strcmp(op, "empty") == 0) {
        Value lv = resolve(lhs);
        if (lv.type == TYPE_STR) result = (lv.str[0] == '\0');
        else result = 0;
    } else if (strcmp(op, "zero") == 0) {
        result = (resolve_num(lhs) == 0);
    } else if (strcmp(op, ">") == 0) {
        result = (resolve_num(lhs) > resolve_num(rhs));
    } else if (strcmp(op, "<") == 0) {
        result = (resolve_num(lhs) < resolve_num(rhs));
    } else if (strcmp(op, ">=") == 0) {
        result = (resolve_num(lhs) >= resolve_num(rhs));
    } else if (strcmp(op, "<=") == 0) {
        result = (resolve_num(lhs) <= resolve_num(rhs));
    } else if (strcmp(op, "==") == 0) {
        Value lv = resolve(lhs), rv = resolve(rhs);
        if (lv.type == TYPE_STR || rv.type == TYPE_STR) {
            char lb[256], rb[256];
            resolve_str(lhs, lb, 256);
            resolve_str(rhs, rb, 256);
            result = (strcmp(lb, rb) == 0);
        } else {
            result = (lv.num == rv.num);
        }
    }

    return neg ? !result : result;
}

/* ─── Forward declarations ─── */
static int execute(int start, int end_excl);

/* ─── Find matching "end X" for block starting at line `from` ─── */
static int find_end(int from, const char *end_keyword) {
    int depth = 1;
    for (int i = from + 1; i < line_count; i++) {
        char *l = trim(lines[i]);
        if (startswith(l, "if ") || startswith(l, "while ") || startswith(l, "repeat ") || startswith(l, "define "))
            depth++;
        if (startswith(l, end_keyword) || startswith(l, "end "))
            depth--;
        if (depth == 0) return i;
    }
    return line_count;
}

/* ─── Parse tokens from line (space-separated, respecting quotes) ─── */
static int tokenize(const char *line, char tokens[][MAX_NAME], int max_tok) {
    int count = 0;
    const char *p = line;
    while (*p && count < max_tok) {
        while (isspace((unsigned char)*p)) p++;
        if (!*p) break;
        if (*p == '"') {
            /* quoted string */
            const char *start = p++;
            while (*p && *p != '"') p++;
            if (*p == '"') p++;
            int len = (int)(p - start);
            if (len >= MAX_NAME) len = MAX_NAME - 1;
            strncpy(tokens[count], start, len);
            tokens[count][len] = '\0';
            count++;
        } else {
            const char *start = p;
            while (*p && !isspace((unsigned char)*p)) p++;
            int len = (int)(p - start);
            if (len >= MAX_NAME) len = MAX_NAME - 1;
            strncpy(tokens[count], start, len);
            tokens[count][len] = '\0';
            count++;
        }
    }
    return count;
}

/* ─── Execute a single line, return next line index ─── */
static int exec_line(int idx) {
    char buf[MAX_LINE];
    strncpy(buf, lines[idx], MAX_LINE - 1);
    char *line = trim(buf);

    if (line[0] == '\0' || startswith(line, "#") || startswith(line, "//"))
        return idx + 1;

    char tok[32][MAX_NAME];
    int tc = tokenize(line, tok, 32);
    if (tc == 0) return idx + 1;

    /* ── set <var> to <value/expr> ── */
    if (strcmp(tok[0], "set") == 0 && tc >= 4 && strcmp(tok[2], "to") == 0) {
        Var *v = get_or_create_var(tok[1]);
        /* check for expressions: set x to a plus b */
        if (tc >= 6 && (strcmp(tok[4], "plus") == 0 || strcmp(tok[4], "minus") == 0 ||
                        strcmp(tok[4], "times") == 0 || strcmp(tok[4], "divided") == 0 ||
                        strcmp(tok[4], "modulo") == 0 || strcmp(tok[4], "to") == 0)) {
            /* handled below via add/subtract etc. but let's do inline */
        }
        /* simple: set x to <val> */
        Value val = resolve(tok[3]);
        if (tc > 4) {
            /* set x to a plus/minus/times/divided by/modulo b */
            if (strcmp(tok[4], "plus") == 0 && tc >= 6) {
                val.type = TYPE_NUM;
                val.num = resolve_num(tok[3]) + resolve_num(tok[5]);
            } else if (strcmp(tok[4], "minus") == 0 && tc >= 6) {
                val.type = TYPE_NUM;
                val.num = resolve_num(tok[3]) - resolve_num(tok[5]);
            } else if (strcmp(tok[4], "times") == 0 && tc >= 6) {
                val.type = TYPE_NUM;
                val.num = resolve_num(tok[3]) * resolve_num(tok[5]);
            } else if (strcmp(tok[4], "divided") == 0 && tc >= 7 && strcmp(tok[5], "by") == 0) {
                double d = resolve_num(tok[6]);
                val.type = TYPE_NUM;
                val.num = (d != 0) ? resolve_num(tok[3]) / d : 0;
            } else if (strcmp(tok[4], "modulo") == 0 && tc >= 6) {
                long a = (long)resolve_num(tok[3]);
                long b = (long)resolve_num(tok[5]);
                val.type = TYPE_NUM;
                val.num = (b != 0) ? (double)(a % b) : 0;
            } else if (strcmp(tok[4], "power") == 0 && tc >= 6) {
                val.type = TYPE_NUM;
                val.num = pow(resolve_num(tok[3]), resolve_num(tok[5]));
            } else if (strcmp(tok[4], "concatenated") == 0 && tc >= 7 && strcmp(tok[5], "with") == 0) {
                char lb[256], rb[256];
                resolve_str(tok[3], lb, 256);
                resolve_str(tok[6], rb, 256);
                val.type = TYPE_STR;
                snprintf(val.str, 256, "%s%s", lb, rb);
            }
        }
        v->val = val;
        return idx + 1;
    }

    /* ── add <a> and <b> into <result> ── */
    if (strcmp(tok[0], "add") == 0 && tc >= 5 && strcmp(tok[2], "and") == 0 && strcmp(tok[4], "into") == 0 && tc >= 6) {
        Var *r = get_or_create_var(tok[5]);
        r->val.type = TYPE_NUM;
        r->val.num  = resolve_num(tok[1]) + resolve_num(tok[3]);
        return idx + 1;
    }

    /* ── subtract <a> from <b> into <result> ── */
    if (strcmp(tok[0], "subtract") == 0 && tc >= 6 && strcmp(tok[2], "from") == 0 && strcmp(tok[4], "into") == 0) {
        Var *r = get_or_create_var(tok[5]);
        r->val.type = TYPE_NUM;
        r->val.num  = resolve_num(tok[3]) - resolve_num(tok[1]);
        return idx + 1;
    }

    /* ── multiply <a> by <b> into <result> ── */
    if (strcmp(tok[0], "multiply") == 0 && tc >= 6 && strcmp(tok[2], "by") == 0 && strcmp(tok[4], "into") == 0) {
        Var *r = get_or_create_var(tok[5]);
        r->val.type = TYPE_NUM;
        r->val.num  = resolve_num(tok[1]) * resolve_num(tok[3]);
        return idx + 1;
    }

    /* ── divide <a> by <b> into <result> ── */
    if (strcmp(tok[0], "divide") == 0 && tc >= 6 && strcmp(tok[2], "by") == 0 && strcmp(tok[4], "into") == 0) {
        Var *r = get_or_create_var(tok[5]);
        double d = resolve_num(tok[3]);
        r->val.type = TYPE_NUM;
        r->val.num  = (d != 0) ? resolve_num(tok[1]) / d : 0;
        return idx + 1;
    }

    /* ── increment <var> ── */
    if (strcmp(tok[0], "increment") == 0 && tc >= 2) {
        Var *v = get_or_create_var(tok[1]);
        double by = (tc >= 4 && strcmp(tok[2], "by") == 0) ? resolve_num(tok[3]) : 1;
        v->val.type = TYPE_NUM;
        v->val.num += by;
        return idx + 1;
    }

    /* ── decrement <var> ── */
    if (strcmp(tok[0], "decrement") == 0 && tc >= 2) {
        Var *v = get_or_create_var(tok[1]);
        double by = (tc >= 4 && strcmp(tok[2], "by") == 0) ? resolve_num(tok[3]) : 1;
        v->val.type = TYPE_NUM;
        v->val.num -= by;
        return idx + 1;
    }

    /* ── print <val> [and <val2> ...] ── */
    if (strcmp(tok[0], "print") == 0) {
        char sb[256];
        /* collect all tokens after "print", skip "and" */
        int first = 1;
        for (int i = 1; i < tc; i++) {
            if (strcmp(tok[i], "and") == 0) continue;
            if (!first) printf(" ");
            printf("%s", resolve_str(tok[i], sb, 256));
            first = 0;
        }
        printf("\n");
        return idx + 1;
    }

    /* ── say <val> ── (alias for print) */
    if (strcmp(tok[0], "say") == 0) {
        char sb[256];
        for (int i = 1; i < tc; i++) {
            if (strcmp(tok[i], "and") == 0) continue;
            printf("%s ", resolve_str(tok[i], sb, 256));
        }
        printf("\n");
        return idx + 1;
    }

    /* ── ask <prompt> into <var> ── */
    if (strcmp(tok[0], "ask") == 0 && tc >= 4) {
        /* find "into" */
        int into_idx = -1;
        for (int i = 1; i < tc; i++)
            if (strcmp(tok[i], "into") == 0) { into_idx = i; break; }
        if (into_idx > 0 && into_idx + 1 < tc) {
            char sb[256];
            printf("%s ", resolve_str(tok[1], sb, 256));
            fflush(stdout);
            char input[256];
            if (fgets(input, 256, stdin)) {
                char *nl = strchr(input, '\n');
                if (nl) *nl = '\0';
                Var *v = get_or_create_var(tok[into_idx + 1]);
                char *end;
                double d = strtod(input, &end);
                if (*end == '\0' && end != input) {
                    v->val.type = TYPE_NUM;
                    v->val.num  = d;
                } else {
                    v->val.type = TYPE_STR;
                    strncpy(v->val.str, input, 255);
                }
            }
        }
        return idx + 1;
    }

    /* ── if <condition> then ... [otherwise ...] end if ── */
    if (strcmp(tok[0], "if") == 0) {
        /* find "then" */
        int then_idx = -1;
        for (int i = 1; i < tc; i++)
            if (strcmp(tok[i], "then") == 0) { then_idx = i; break; }
        if (then_idx < 0) return idx + 1;

        /* build condition string */
        char cond[MAX_LINE] = "";
        for (int i = 1; i < then_idx; i++) {
            if (i > 1) strcat(cond, " ");
            strcat(cond, tok[i]);
        }

        int end_if = find_end(idx, "end if");
        /* find optional "otherwise" at same depth */
        int otherwise = -1;
        int depth = 1;
        for (int i = idx + 1; i < end_if; i++) {
            char *l = trim(lines[i]);
            if (startswith(l, "if ") || startswith(l, "while ") || startswith(l, "repeat ") || startswith(l, "define "))
                depth++;
            if (startswith(l, "end ")) depth--;
            if (depth == 1 && startswith(l, "otherwise")) { otherwise = i; break; }
        }

        int cond_true = eval_condition(cond);
        if (cond_true) {
            int block_end = (otherwise >= 0) ? otherwise : end_if;
            execute(idx + 1, block_end);
        } else if (otherwise >= 0) {
            execute(otherwise + 1, end_if);
        }
        return end_if + 1;
    }

    /* ── while <condition> then ... end while ── */
    if (strcmp(tok[0], "while") == 0) {
        int then_idx = -1;
        for (int i = 1; i < tc; i++)
            if (strcmp(tok[i], "then") == 0) { then_idx = i; break; }
        if (then_idx < 0) return idx + 1;

        char cond[MAX_LINE] = "";
        for (int i = 1; i < then_idx; i++) {
            if (i > 1) strcat(cond, " ");
            strcat(cond, tok[i]);
        }

        int end_while = find_end(idx, "end while");
        while (eval_condition(cond)) {
            execute(idx + 1, end_while);
        }
        return end_while + 1;
    }

    /* ── repeat <n> times then ... end repeat ── */
    if (strcmp(tok[0], "repeat") == 0 && tc >= 3 && strcmp(tok[2], "times") == 0) {
        int n = (int)resolve_num(tok[1]);
        int end_rep = find_end(idx, "end repeat");
        for (int i = 0; i < n; i++)
            execute(idx + 1, end_rep);
        return end_rep + 1;
    }

    /* ── for <var> from <a> to <b> [step <s>] then ... end for ── */
    if (strcmp(tok[0], "for") == 0 && tc >= 6 && strcmp(tok[2], "from") == 0 && strcmp(tok[4], "to") == 0) {
        char *varname = tok[1];
        double from = resolve_num(tok[3]);
        double to   = resolve_num(tok[5]);
        double step = 1;
        if (tc >= 9 && strcmp(tok[6], "step") == 0)
            step = resolve_num(tok[7]);
        int end_for = find_end(idx, "end for");
        Var *v = get_or_create_var(varname);
        v->val.type = TYPE_NUM;
        if (step > 0) {
            for (double d = from; d <= to; d += step) {
                v->val.num = d;
                execute(idx + 1, end_for);
            }
        } else {
            for (double d = from; d >= to; d += step) {
                v->val.num = d;
                execute(idx + 1, end_for);
            }
        }
        return end_for + 1;
    }

    /* ── define <name> [with <p1> <p2> ...] as ... end define ── */
    if (strcmp(tok[0], "define") == 0 && tc >= 3) {
        int end_def = find_end(idx, "end define");
        /* find "as" */
        int as_idx = -1;
        for (int i = 2; i < tc; i++)
            if (strcmp(tok[i], "as") == 0) { as_idx = i; break; }

        FuncDef *f = &funcs[func_count++];
        strncpy(f->name, tok[1], MAX_NAME - 1);
        f->start_line = idx + 1;
        f->end_line   = end_def;
        f->param_count = 0;

        /* parameters between "with" and "as" */
        if (as_idx > 0) {
            int param_start = 2;
            if (tc > 2 && strcmp(tok[2], "with") == 0) param_start = 3;
            for (int i = param_start; i < as_idx && f->param_count < 8; i++)
                strncpy(f->params[f->param_count++], tok[i], MAX_NAME - 1);
        }

        return end_def + 1;
    }

    /* ── call <name> [with <a> <b> ...] ── */
    if (strcmp(tok[0], "call") == 0 && tc >= 2) {
        FuncDef *f = find_func(tok[1]);
        if (!f) {
            fprintf(stderr, "Error: undefined function '%s'\n", tok[1]);
            return idx + 1;
        }
        /* bind parameters */
        int arg_start = 2;
        if (tc > 2 && strcmp(tok[2], "with") == 0) arg_start = 3;
        for (int i = 0; i < f->param_count && (arg_start + i) < tc; i++) {
            Var *pv = get_or_create_var(f->params[i]);
            pv->val = resolve(tok[arg_start + i]);
        }
        execute(f->start_line, f->end_line);
        return idx + 1;
    }

    /* ── return <value> ── (set special "return" variable) */
    if (strcmp(tok[0], "return") == 0 && tc >= 2) {
        Var *rv = get_or_create_var("return");
        rv->val = resolve(tok[1]);
        return idx + 1;
    }

    /* ── push <val> onto stack ── */
    if (strcmp(tok[0], "push") == 0 && tc >= 4 && strcmp(tok[2], "onto") == 0 && strcmp(tok[3], "stack") == 0) {
        if (stack_top < MAX_STACK)
            data_stack[stack_top++] = resolve_num(tok[1]);
        return idx + 1;
    }

    /* ── pop from stack into <var> ── */
    if (strcmp(tok[0], "pop") == 0 && tc >= 5 && strcmp(tok[1], "from") == 0 &&
        strcmp(tok[2], "stack") == 0 && strcmp(tok[3], "into") == 0) {
        Var *v = get_or_create_var(tok[4]);
        v->val.type = TYPE_NUM;
        v->val.num  = (stack_top > 0) ? data_stack[--stack_top] : 0;
        return idx + 1;
    }

    /* ── store <val> at address <n> ── */
    if (strcmp(tok[0], "store") == 0 && tc >= 5 && strcmp(tok[2], "at") == 0 &&
        strcmp(tok[3], "address") == 0) {
        int addr = (int)resolve_num(tok[4]);
        if (addr >= 0 && addr < MAX_MEM)
            mem[addr] = resolve_num(tok[1]);
        return idx + 1;
    }

    /* ── load from address <n> into <var> ── */
    if (strcmp(tok[0], "load") == 0 && tc >= 6 && strcmp(tok[1], "from") == 0 &&
        strcmp(tok[2], "address") == 0 && strcmp(tok[4], "into") == 0) {
        int addr = (int)resolve_num(tok[3]);
        Var *v = get_or_create_var(tok[5]);
        v->val.type = TYPE_NUM;
        v->val.num  = (addr >= 0 && addr < MAX_MEM) ? mem[addr] : 0;
        return idx + 1;
    }

    /* ── create array <name> ── */
    if (strcmp(tok[0], "create") == 0 && tc >= 3 && strcmp(tok[1], "array") == 0) {
        get_or_create_array(tok[2]);
        return idx + 1;
    }

    /* ── append <val> to array <name> ── */
    if (strcmp(tok[0], "append") == 0 && tc >= 5 && strcmp(tok[2], "to") == 0 &&
        strcmp(tok[3], "array") == 0) {
        Array *a = get_or_create_array(tok[4]);
        if (a->size < MAX_ARRAY_SIZE) {
            a->data[a->size++] = resolve(tok[1]);
        }
        return idx + 1;
    }

    /* ── get element <i> of array <name> into <var> ── */
    if (strcmp(tok[0], "get") == 0 && tc >= 7 && strcmp(tok[1], "element") == 0 &&
        strcmp(tok[3], "of") == 0 && strcmp(tok[4], "array") == 0 && strcmp(tok[6], "into") == 0) {
        int i = (int)resolve_num(tok[2]);
        Array *a = find_array(tok[5]);
        Var *v = get_or_create_var(tok[7]);
        if (a && i >= 0 && i < a->size)
            v->val = a->data[i];
        else { v->val.type = TYPE_NUM; v->val.num = 0; }
        return idx + 1;
    }

    /* ── set element <i> of array <name> to <val> ── */
    if (strcmp(tok[0], "set") == 0 && tc >= 7 && strcmp(tok[1], "element") == 0 &&
        strcmp(tok[3], "of") == 0 && strcmp(tok[4], "array") == 0 && strcmp(tok[6], "to") == 0) {
        int i = (int)resolve_num(tok[2]);
        Array *a = get_or_create_array(tok[5]);
        if (i >= 0 && i < MAX_ARRAY_SIZE) {
            a->data[i] = resolve(tok[7]);
            if (i >= a->size) a->size = i + 1;
        }
        return idx + 1;
    }

    /* ── size of array <name> into <var> ── */
    if (strcmp(tok[0], "size") == 0 && tc >= 5 && strcmp(tok[1], "of") == 0 &&
        strcmp(tok[2], "array") == 0 && strcmp(tok[4], "into") == 0) {
        Array *a = find_array(tok[3]);
        Var *v = get_or_create_var(tok[5]);
        v->val.type = TYPE_NUM;
        v->val.num  = a ? a->size : 0;
        return idx + 1;
    }

    /* ── square root of <val> into <var> ── */
    if (strcmp(tok[0], "square") == 0 && tc >= 6 && strcmp(tok[1], "root") == 0 &&
        strcmp(tok[2], "of") == 0 && strcmp(tok[4], "into") == 0) {
        Var *v = get_or_create_var(tok[5]);
        v->val.type = TYPE_NUM;
        v->val.num  = sqrt(resolve_num(tok[3]));
        return idx + 1;
    }

    /* ── absolute value of <val> into <var> ── */
    if (strcmp(tok[0], "absolute") == 0 && tc >= 6 && strcmp(tok[1], "value") == 0 &&
        strcmp(tok[2], "of") == 0 && strcmp(tok[4], "into") == 0) {
        Var *v = get_or_create_var(tok[5]);
        v->val.type = TYPE_NUM;
        v->val.num  = fabs(resolve_num(tok[3]));
        return idx + 1;
    }

    /* ── length of <str_var> into <var> ── */
    if (strcmp(tok[0], "length") == 0 && tc >= 5 && strcmp(tok[1], "of") == 0 &&
        strcmp(tok[3], "into") == 0) {
        char sb[256];
        resolve_str(tok[2], sb, 256);
        Var *v = get_or_create_var(tok[4]);
        v->val.type = TYPE_NUM;
        v->val.num  = strlen(sb);
        return idx + 1;
    }

    /* ── convert <var> to number ── */
    if (strcmp(tok[0], "convert") == 0 && tc >= 4 && strcmp(tok[2], "to") == 0 &&
        strcmp(tok[3], "number") == 0) {
        Var *v = get_or_create_var(tok[1]);
        if (v->val.type == TYPE_STR) {
            v->val.num  = atof(v->val.str);
            v->val.type = TYPE_NUM;
        }
        return idx + 1;
    }

    /* ── convert <var> to string ── */
    if (strcmp(tok[0], "convert") == 0 && tc >= 4 && strcmp(tok[2], "to") == 0 &&
        strcmp(tok[3], "string") == 0) {
        Var *v = get_or_create_var(tok[1]);
        if (v->val.type == TYPE_NUM) {
            snprintf(v->val.str, 256, "%g", v->val.num);
            v->val.type = TYPE_STR;
        }
        return idx + 1;
    }

    /* ── stop ── / ── exit ── */
    if (strcmp(tok[0], "stop") == 0 || strcmp(tok[0], "exit") == 0) {
        exit(0);
    }

    /* ── skip / otherwise / end X (handled by parent) ── */
    if (strcmp(tok[0], "otherwise") == 0 || startswith(tok[0], "end"))
        return idx + 1;

    /* Unknown instruction */
    fprintf(stderr, "Warning: unknown instruction on line %d: '%s'\n", idx + 1, line);
    return idx + 1;
}

/* ─── Execute lines [start, end_excl) ─── */
static int execute(int start, int end_excl) {
    int i = start;
    while (i < end_excl && i < line_count) {
        i = exec_line(i);
    }
    return i;
}

/* ─── First pass: collect function definitions ─── */
static void collect_funcs(void) {
    for (int i = 0; i < line_count; i++) {
        char buf[MAX_LINE];
        strncpy(buf, lines[i], MAX_LINE - 1);
        char *line = trim(buf);
        char tok[32][MAX_NAME];
        int tc = tokenize(line, tok, 32);
        if (tc >= 3 && strcmp(tok[0], "define") == 0) {
            int end_def = find_end(i, "end define");
            int as_idx = -1;
            for (int j = 2; j < tc; j++)
                if (strcmp(tok[j], "as") == 0) { as_idx = j; break; }
            FuncDef *f = &funcs[func_count++];
            strncpy(f->name, tok[1], MAX_NAME - 1);
            f->start_line = i + 1;
            f->end_line   = end_def;
            f->param_count = 0;
            if (as_idx > 0) {
                int ps = 2;
                if (tc > 2 && strcmp(tok[2], "with") == 0) ps = 3;
                for (int k = ps; k < as_idx && f->param_count < 8; k++)
                    strncpy(f->params[f->param_count++], tok[k], MAX_NAME - 1);
            }
            i = end_def;
        }
    }
}

/* ─── Load source file ─── */
static void load_file(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) { perror(path); exit(1); }
    char buf[MAX_LINE];
    while (fgets(buf, MAX_LINE, f) && line_count < MAX_LINES) {
        /* strip trailing newline */
        buf[strcspn(buf, "\r\n")] = '\0';
        lines[line_count] = strdup(buf);
        line_count++;
    }
    fclose(f);
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "ENGLANG Interpreter v1.0\nUsage: %s <script.eng>\n", argv[0]);
        fprintf(stderr, "\nLanguage Quick Reference:\n");
        fprintf(stderr, "  set x to 42\n");
        fprintf(stderr, "  set greeting to \"Hello, World!\"\n");
        fprintf(stderr, "  add x and y into result\n");
        fprintf(stderr, "  subtract a from b into diff\n");
        fprintf(stderr, "  multiply x by y into product\n");
        fprintf(stderr, "  divide a by b into quotient\n");
        fprintf(stderr, "  increment counter\n");
        fprintf(stderr, "  decrement counter by 5\n");
        fprintf(stderr, "  print x and y\n");
        fprintf(stderr, "  ask \"Enter a number:\" into num\n");
        fprintf(stderr, "  if x is greater than 5 then\n");
        fprintf(stderr, "    print x\n");
        fprintf(stderr, "  otherwise\n");
        fprintf(stderr, "    print \"small\"\n");
        fprintf(stderr, "  end if\n");
        fprintf(stderr, "  while x is less than 100 then\n");
        fprintf(stderr, "    increment x\n");
        fprintf(stderr, "  end while\n");
        fprintf(stderr, "  repeat 10 times\n");
        fprintf(stderr, "    print x\n");
        fprintf(stderr, "  end repeat\n");
        fprintf(stderr, "  for i from 1 to 10 step 1 then\n");
        fprintf(stderr, "    print i\n");
        fprintf(stderr, "  end for\n");
        fprintf(stderr, "  define factorial with n as\n");
        fprintf(stderr, "    ...\n");
        fprintf(stderr, "  end define\n");
        fprintf(stderr, "  call factorial with 5\n");
        fprintf(stderr, "  push 42 onto stack\n");
        fprintf(stderr, "  pop from stack into x\n");
        fprintf(stderr, "  store x at address 0\n");
        fprintf(stderr, "  load from address 0 into y\n");
        fprintf(stderr, "  create array nums\n");
        fprintf(stderr, "  append 10 to array nums\n");
        fprintf(stderr, "  get element 0 of array nums into val\n");
        fprintf(stderr, "  square root of x into root\n");
        fprintf(stderr, "  length of mystring into len\n");
        return 1;
    }

    memset(vars, 0, sizeof(vars));
    memset(arrays, 0, sizeof(arrays));
    memset(mem, 0, sizeof(mem));

    load_file(argv[1]);
    collect_funcs();
    execute(0, line_count);
    return 0;
}
