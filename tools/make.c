/*
 * This is free and unencumbered software released into the public domain.
 * GEMIOS Custom Make Tool
 * Pure ANSI C90 Makefile parser and build execution engine.
 */

#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#if defined(__STRICT_ANSI__)
extern FILE *popen(const char *command, const char *type);
extern int pclose(FILE *stream);
extern int chdir(const char *path);
#endif

#define MAX_LINE_LEN 16384
#define MAX_PREREQS 1024
#define MAX_COMMANDS 256
#define MAX_RULES 2048
#define MAX_VARS 1024
#define MAX_PHONY 128
#define MAX_CALL_DEPTH 128

/* Dynamic String Buffer */
typedef struct {
    char *data;
    size_t len;
    size_t cap;
} strbuf_t;

static void strbuf_init(strbuf_t *sb) {
    sb->cap = 128;
    sb->len = 0;
    sb->data = (char*)malloc(sb->cap);
    if (!sb->data) {
        fprintf(stderr, "make: fatal out of memory\n");
        exit(1);
    }
    sb->data[0] = '\0';
}

static void strbuf_append_len(strbuf_t *sb, const char *s, size_t len) {
    if (!s || len == 0) return;
    if (sb->len + len + 1 > sb->cap) {
        while (sb->len + len + 1 > sb->cap) sb->cap *= 2;
        sb->data = (char*)realloc(sb->data, sb->cap);
        if (!sb->data) {
            fprintf(stderr, "make: fatal out of memory\n");
            exit(1);
        }
    }
    memcpy(sb->data + sb->len, s, len);
    sb->len += len;
    sb->data[sb->len] = '\0';
}

static void strbuf_append(strbuf_t *sb, const char *s) {
    if (s) strbuf_append_len(sb, s, strlen(s));
}

static void strbuf_append_char(strbuf_t *sb, char c) {
    strbuf_append_len(sb, &c, 1);
}

static void strbuf_free(strbuf_t *sb) {
    if (sb->data) free(sb->data);
    sb->data = NULL;
    sb->len = sb->cap = 0;
}

/* String Helpers */
static char *my_strdup(const char *s) {
    size_t l;
    char *p;
    if (!s) return NULL;
    l = strlen(s) + 1;
    p = (char*)malloc(l);
    if (p) memcpy(p, s, l);
    return p;
}

static char *trim_whitespace(char *s) {
    char *end;
    if (!s) return NULL;
    while (*s && isspace((unsigned char)*s)) s++;
    if (*s == '\0') return s;
    end = s + strlen(s) - 1;
    while (end > s && isspace((unsigned char)*end)) end--;
    end[1] = '\0';
    return s;
}

static const char *skip_ws(const char *s) {
    while (*s && isspace((unsigned char)*s)) s++;
    return s;
}

/* Re-entrant word tokenizer (replaces strtok) */
static char *next_token(const char **p) {
    const char *start;
    size_t len;
    char *res;
    if (!p || !*p) return NULL;
    while (**p && isspace((unsigned char)**p)) (*p)++;
    if (**p == '\0') return NULL;
    start = *p;
    while (**p && !isspace((unsigned char)**p)) (*p)++;
    len = *p - start;
    res = (char*)malloc(len + 1);
    memcpy(res, start, len);
    res[len] = '\0';
    return res;
}

/* Variables */
typedef struct {
    char *name;
    char *val;
    int is_simple;
} var_t;

static var_t g_vars[MAX_VARS];
static int g_var_count = 0;

static const char *get_var_raw(const char *name) {
    int i;
    for (i = g_var_count - 1; i >= 0; i--) {
        if (strcmp(g_vars[i].name, name) == 0) {
            return g_vars[i].val;
        }
    }
    return getenv(name);
}

static void set_var(const char *name, const char *val, int is_simple) {
    int i;
    for (i = 0; i < g_var_count; i++) {
        if (strcmp(g_vars[i].name, name) == 0) {
            free(g_vars[i].val);
            g_vars[i].val = my_strdup(val);
            g_vars[i].is_simple = is_simple;
            return;
        }
    }
    if (g_var_count < MAX_VARS) {
        g_vars[g_var_count].name = my_strdup(name);
        g_vars[g_var_count].val = my_strdup(val);
        g_vars[g_var_count].is_simple = is_simple;
        g_var_count++;
    }
}

static void append_var(const char *name, const char *val) {
    int i;
    for (i = 0; i < g_var_count; i++) {
        if (strcmp(g_vars[i].name, name) == 0) {
            size_t new_len = strlen(g_vars[i].val) + 1 + strlen(val) + 1;
            char *new_val = (char*)malloc(new_len);
            sprintf(new_val, "%s %s", g_vars[i].val, val);
            free(g_vars[i].val);
            g_vars[i].val = new_val;
            return;
        }
    }
    set_var(name, val, 0);
}

/* Context for automatic variables */
typedef struct {
    const char *target;
    const char *first_prereq;
    const char *all_prereqs;
    const char *stem;
} auto_vars_t;

/* Forward declaration for variable expansion */
static char *expand_vars_ctx(const char *str, const auto_vars_t *ctx);

static char *eval_shell(const char *cmd) {
    FILE *fp = popen(cmd, "r");
    strbuf_t sb;
    char buf[512];
    size_t n;
    strbuf_init(&sb);
    if (fp) {
        while ((n = fread(buf, 1, sizeof(buf), fp)) > 0) {
            size_t i;
            for (i = 0; i < n; i++) {
                if (buf[i] == '\r' || buf[i] == '\n') buf[i] = ' ';
            }
            strbuf_append_len(&sb, buf, n);
        }
        pclose(fp);
    }
    while (sb.len > 0 && isspace((unsigned char)sb.data[sb.len - 1])) {
        sb.data[--sb.len] = '\0';
    }
    return sb.data;
}

static char *eval_patsubst(const char *pattern, const char *replacement, const char *text) {
    strbuf_t sb;
    const char *p_pct = strchr(pattern, '%');
    const char *r_pct = strchr(replacement, '%');
    const char *ptr = text;
    char *tok;
    strbuf_init(&sb);

    while ((tok = next_token(&ptr)) != NULL) {
        if (sb.len > 0) strbuf_append_char(&sb, ' ');
        if (p_pct && r_pct) {
            size_t p_prefix_len = p_pct - pattern;
            size_t p_suffix_len = strlen(pattern) - p_prefix_len - 1;
            const char *p_suffix = p_pct + 1;
            size_t tok_len = strlen(tok);

            if (tok_len >= p_prefix_len + p_suffix_len &&
                strncmp(tok, pattern, p_prefix_len) == 0 &&
                strcmp(tok + tok_len - p_suffix_len, p_suffix) == 0) {
                size_t stem_len = tok_len - p_prefix_len - p_suffix_len;
                const char *stem = tok + p_prefix_len;
                size_t r_prefix_len = r_pct - replacement;
                const char *r_suffix = r_pct + 1;

                strbuf_append_len(&sb, replacement, r_prefix_len);
                strbuf_append_len(&sb, stem, stem_len);
                strbuf_append(&sb, r_suffix);
            } else {
                strbuf_append(&sb, tok);
            }
        } else if (strcmp(pattern, tok) == 0) {
            strbuf_append(&sb, replacement);
        } else {
            strbuf_append(&sb, tok);
        }
        free(tok);
    }
    return sb.data;
}

static char *eval_dir(const char *text) {
    strbuf_t sb;
    const char *ptr = text;
    char *tok;
    strbuf_init(&sb);

    while ((tok = next_token(&ptr)) != NULL) {
        char *last_slash = strrchr(tok, '/');
        if (sb.len > 0) strbuf_append_char(&sb, ' ');
        if (last_slash) {
            strbuf_append_len(&sb, tok, last_slash - tok + 1);
        } else {
            strbuf_append(&sb, "./");
        }
        free(tok);
    }
    return sb.data;
}

static char *eval_notdir(const char *text) {
    strbuf_t sb;
    const char *ptr = text;
    char *tok;
    strbuf_init(&sb);

    while ((tok = next_token(&ptr)) != NULL) {
        char *last_slash = strrchr(tok, '/');
        if (sb.len > 0) strbuf_append_char(&sb, ' ');
        if (last_slash) {
            strbuf_append(&sb, last_slash + 1);
        } else {
            strbuf_append(&sb, tok);
        }
        free(tok);
    }
    return sb.data;
}

/* Expand Variables and Functions */
static char *expand_vars_ctx(const char *str, const auto_vars_t *ctx) {
    strbuf_t sb;
    const char *p = str;
    strbuf_init(&sb);

    while (*p) {
        if (*p == '$') {
            p++;
            if (*p == '$') {
                strbuf_append_char(&sb, '$');
                p++;
            } else if (*p == '@') {
                if (ctx && ctx->target) strbuf_append(&sb, ctx->target);
                p++;
            } else if (*p == '<') {
                if (ctx && ctx->first_prereq) strbuf_append(&sb, ctx->first_prereq);
                p++;
            } else if (*p == '^') {
                if (ctx && ctx->all_prereqs) strbuf_append(&sb, ctx->all_prereqs);
                p++;
            } else if (*p == '*') {
                if (ctx && ctx->stem) strbuf_append(&sb, ctx->stem);
                p++;
            } else if (*p == '(' || *p == '{') {
                char close_ch = (*p == '(') ? ')' : '}';
                int depth = 1;
                const char *vstart = ++p;
                while (*p && depth > 0) {
                    if (*p == '(' || *p == '{') depth++;
                    else if (*p == close_ch) depth--;
                    if (depth > 0) p++;
                }
                if (*p == close_ch) {
                    size_t vlen = p - vstart;
                    char *vname = (char*)malloc(vlen + 1);
                    memcpy(vname, vstart, vlen);
                    vname[vlen] = '\0';
                    p++; /* Skip closing paren */

                    /* Check automatic variables */
                    if (strcmp(vname, "@") == 0) {
                        if (ctx && ctx->target) strbuf_append(&sb, ctx->target);
                    } else if (strcmp(vname, "@D") == 0) {
                        if (ctx && ctx->target) {
                            char *d = eval_dir(ctx->target);
                            size_t dl = strlen(d);
                            if (dl > 1 && d[dl - 1] == '/') d[dl - 1] = '\0';
                            strbuf_append(&sb, d);
                            free(d);
                        }
                    } else if (strcmp(vname, "@F") == 0) {
                        if (ctx && ctx->target) {
                            char *nd = eval_notdir(ctx->target);
                            strbuf_append(&sb, nd);
                            free(nd);
                        }
                    } else if (strcmp(vname, "<") == 0) {
                        if (ctx && ctx->first_prereq) strbuf_append(&sb, ctx->first_prereq);
                    } else if (strcmp(vname, "<D") == 0) {
                        if (ctx && ctx->first_prereq) {
                            char *d = eval_dir(ctx->first_prereq);
                            size_t dl = strlen(d);
                            if (dl > 1 && d[dl - 1] == '/') d[dl - 1] = '\0';
                            strbuf_append(&sb, d);
                            free(d);
                        }
                    } else if (strcmp(vname, "^") == 0) {
                        if (ctx && ctx->all_prereqs) strbuf_append(&sb, ctx->all_prereqs);
                    } else if (strcmp(vname, "*") == 0) {
                        if (ctx && ctx->stem) strbuf_append(&sb, ctx->stem);
                    } else if (strncmp(vname, "shell ", 6) == 0) {
                        char *expanded_cmd = expand_vars_ctx(vname + 6, ctx);
                        char *s_out = eval_shell(expanded_cmd);
                        strbuf_append(&sb, s_out);
                        free(s_out);
                        free(expanded_cmd);
                    } else if (strncmp(vname, "patsubst ", 9) == 0) {
                        char *raw_args = vname + 9;
                        char *comma1 = strchr(raw_args, ',');
                        if (comma1) {
                            char *comma2 = strchr(comma1 + 1, ',');
                            if (comma2) {
                                size_t l1 = comma1 - raw_args;
                                size_t l2 = comma2 - (comma1 + 1);
                                char *p1 = (char*)malloc(l1 + 1);
                                char *p2 = (char*)malloc(l2 + 1);
                                char *p3 = my_strdup(comma2 + 1);
                                char *exp1, *exp2, *exp3;
                                char *pat, *rep, *txt, *res;

                                memcpy(p1, raw_args, l1); p1[l1] = '\0';
                                memcpy(p2, comma1 + 1, l2); p2[l2] = '\0';

                                exp1 = expand_vars_ctx(p1, ctx);
                                exp2 = expand_vars_ctx(p2, ctx);
                                exp3 = expand_vars_ctx(p3, ctx);

                                pat = trim_whitespace(exp1);
                                rep = trim_whitespace(exp2);
                                txt = trim_whitespace(exp3);

                                res = eval_patsubst(pat, rep, txt);
                                strbuf_append(&sb, res);

                                free(res);
                                free(exp1); free(exp2); free(exp3);
                                free(p1); free(p2); free(p3);
                            }
                        }
                    } else if (strncmp(vname, "dir ", 4) == 0) {
                        char *arg = expand_vars_ctx(vname + 4, ctx);
                        char *d = eval_dir(arg);
                        strbuf_append(&sb, d);
                        free(d);
                        free(arg);
                    } else if (strncmp(vname, "notdir ", 7) == 0) {
                        char *arg = expand_vars_ctx(vname + 7, ctx);
                        char *nd = eval_notdir(arg);
                        strbuf_append(&sb, nd);
                        free(nd);
                        free(arg);
                    } else {
                        /* Normal variable */
                        const char *val = get_var_raw(vname);
                        if (val) {
                            char *exp = expand_vars_ctx(val, ctx);
                            strbuf_append(&sb, exp);
                            free(exp);
                        }
                    }
                    free(vname);
                }
            } else {
                /* Single letter variable name */
                char vname[2];
                const char *val;
                vname[0] = *p++;
                vname[1] = '\0';
                val = get_var_raw(vname);
                if (val) {
                    char *exp = expand_vars_ctx(val, ctx);
                    strbuf_append(&sb, exp);
                    free(exp);
                }
            }
        } else {
            strbuf_append_char(&sb, *p++);
        }
    }
    return sb.data;
}

static char *expand_vars(const char *str) {
    return expand_vars_ctx(str, NULL);
}

/* Rule Definitions */
typedef struct {
    char *target;
    char **prereqs;
    int prereq_count;
    char **commands;
    int cmd_count;
    int is_phony;
    int is_pattern;
} rule_t;

static rule_t g_rules[MAX_RULES];
static int g_rule_count = 0;

static char *g_phony_targets[MAX_PHONY];
static int g_phony_count = 0;

static char *g_default_target = NULL;

static int is_target_phony(const char *target) {
    int i;
    for (i = 0; i < g_phony_count; i++) {
        if (strcmp(g_phony_targets[i], target) == 0) return 1;
    }
    return 0;
}

static void add_phony(const char *target) {
    if (g_phony_count < MAX_PHONY && !is_target_phony(target)) {
        g_phony_targets[g_phony_count++] = my_strdup(target);
    }
}

static rule_t *find_explicit_rule(const char *target) {
    int i;
    for (i = 0; i < g_rule_count; i++) {
        if (!g_rules[i].is_pattern && strcmp(g_rules[i].target, target) == 0) {
            return &g_rules[i];
        }
    }
    return NULL;
}

static int match_pattern(const char *pattern, const char *str, char *out_stem) {
    const char *pct = strchr(pattern, '%');
    size_t prefix_len, suffix_len, str_len, stem_len;
    const char *suffix;

    if (!pct) return 0;
    prefix_len = pct - pattern;
    suffix = pct + 1;
    suffix_len = strlen(suffix);
    str_len = strlen(str);

    if (str_len < prefix_len + suffix_len) return 0;
    if (strncmp(str, pattern, prefix_len) != 0) return 0;
    if (strcmp(str + str_len - suffix_len, suffix) != 0) return 0;

    stem_len = str_len - prefix_len - suffix_len;
    if (out_stem) {
        memcpy(out_stem, str + prefix_len, stem_len);
        out_stem[stem_len] = '\0';
    }
    return 1;
}

static char *subst_stem(const char *pattern, const char *stem) {
    const char *pct = strchr(pattern, '%');
    strbuf_t sb;
    strbuf_init(&sb);
    if (pct) {
        strbuf_append_len(&sb, pattern, pct - pattern);
        strbuf_append(&sb, stem);
        strbuf_append(&sb, pct + 1);
    } else {
        strbuf_append(&sb, pattern);
    }
    return sb.data;
}

/* Check if a file exists on disk */
static int file_exists(const char *path) {
    struct stat st;
    return (stat(path, &st) == 0);
}

/* Get file modification time (or 0 if not exists) */
static time_t get_mtime(const char *path) {
    struct stat st;
    if (stat(path, &st) == 0) {
        return st.st_mtime;
    }
    return 0;
}

/* Check if target can be built or exists */
static int can_build_or_exists(const char *target, int depth) {
    rule_t *exp;
    int i;
    char stem[256];

    if (file_exists(target)) return 1;
    if (is_target_phony(target)) return 1;

    exp = find_explicit_rule(target);
    if (exp) return 1;

    /* Check pattern rules */
    if (depth > 20) return 0;
    for (i = 0; i < g_rule_count; i++) {
        if (g_rules[i].is_pattern) {
            if (match_pattern(g_rules[i].target, target, stem)) {
                if (g_rules[i].prereq_count > 0) {
                    char *first_p = subst_stem(g_rules[i].prereqs[0], stem);
                    int ok = can_build_or_exists(first_p, depth + 1);
                    free(first_p);
                    if (ok) return 1;
                } else {
                    return 1;
                }
            }
        }
    }
    return 0;
}

/* Find matching pattern rule for target */
static rule_t *match_pattern_rule(const char *target, char *out_stem) {
    int i;
    for (i = 0; i < g_rule_count; i++) {
        if (g_rules[i].is_pattern) {
            char stem[256];
            if (match_pattern(g_rules[i].target, target, stem)) {
                if (g_rules[i].prereq_count > 0) {
                    char *first_p = subst_stem(g_rules[i].prereqs[0], stem);
                    if (can_build_or_exists(first_p, 0)) {
                        strcpy(out_stem, stem);
                        free(first_p);
                        return &g_rules[i];
                    }
                    free(first_p);
                } else {
                    strcpy(out_stem, stem);
                    return &g_rules[i];
                }
            }
        }
    }
    return NULL;
}

/* Build Call Stack & Cycle Detection */
static const char *g_call_stack[MAX_CALL_DEPTH];
static int g_call_depth = 0;

static int is_in_stack(const char *target) {
    int i;
    for (i = 0; i < g_call_depth; i++) {
        if (strcmp(g_call_stack[i], target) == 0) return 1;
    }
    return 0;
}

/* Built Target Timestamps Cache */
typedef struct {
    char name[512];
    time_t mtime;
    int built;
} built_target_t;

static built_target_t g_built_targets[MAX_RULES * 2];
static int g_built_target_count = 0;

static built_target_t *find_built_target(const char *name) {
    int i;
    for (i = 0; i < g_built_target_count; i++) {
        if (strcmp(g_built_targets[i].name, name) == 0) {
            return &g_built_targets[i];
        }
    }
    return NULL;
}

static void mark_target_built(const char *name, time_t mtime, int built) {
    built_target_t *bt = find_built_target(name);
    if (!bt && g_built_target_count < (int)(sizeof(g_built_targets) / sizeof(g_built_targets[0]))) {
        bt = &g_built_targets[g_built_target_count++];
        strncpy(bt->name, name, sizeof(bt->name) - 1);
        bt->name[sizeof(bt->name) - 1] = '\0';
    }
    if (bt) {
        bt->mtime = mtime;
        bt->built = built;
    }
}

static int build_target_internal(const char *target, int depth) {
    rule_t *rule = NULL;
    char stem[256];
    char **actual_prereqs = NULL;
    int actual_prereq_count = 0;
    int is_phony = is_target_phony(target);
    int target_exists = file_exists(target);
    time_t target_mtime = target_exists ? get_mtime(target) : 0;
    int needs_rebuild = 0;
    int p;
    int c;
    built_target_t *cached;

    stem[0] = '\0';

    if (is_in_stack(target)) {
        fprintf(stderr, "make: Circular dependency dropped for target '%s'\n", target);
        return 0;
    }

    cached = find_built_target(target);
    if (cached && !is_phony) {
        return 0;
    }

    rule = find_explicit_rule(target);
    if (rule) {
        actual_prereqs = rule->prereqs;
        actual_prereq_count = rule->prereq_count;
    } else {
        rule_t *p_rule = match_pattern_rule(target, stem);
        if (p_rule) {
            rule = p_rule;
            actual_prereqs = (char**)malloc(p_rule->prereq_count * sizeof(char*));
            actual_prereq_count = p_rule->prereq_count;
            for (p = 0; p < p_rule->prereq_count; p++) {
                actual_prereqs[p] = subst_stem(p_rule->prereqs[p], stem);
            }
        }
    }

    if (!rule && !target_exists && !is_phony) {
        fprintf(stderr, "make: *** No rule to make target '%s'.  Stop.\n", target);
        return -1;
    }

    /* Push call stack */
    if (g_call_depth < MAX_CALL_DEPTH) {
        g_call_stack[g_call_depth++] = target;
    }

    /* Build prerequisites */
    for (p = 0; p < actual_prereq_count; p++) {
        const char *prereq = actual_prereqs[p];
        if (build_target_internal(prereq, depth + 1) != 0) {
            g_call_depth--;
            return -1;
        }

        if (!is_phony) {
            time_t p_mtime = get_mtime(prereq);
            built_target_t *p_bt = find_built_target(prereq);
            if (p_bt && p_bt->built) {
                needs_rebuild = 1;
            }
            if (p_mtime > target_mtime) {
                needs_rebuild = 1;
            }
        }
    }

    /* Pop call stack */
    g_call_depth--;

    if (is_phony || !target_exists || (actual_prereq_count == 0 && rule && rule->cmd_count > 0)) {
        needs_rebuild = 1;
    }

    if (!needs_rebuild) {
        mark_target_built(target, target_mtime, 0);
        if (actual_prereqs && rule && actual_prereqs != rule->prereqs) {
            for (p = 0; p < actual_prereq_count; p++) free(actual_prereqs[p]);
            free(actual_prereqs);
        }
        return 0;
    }

    if (rule && rule->cmd_count > 0) {
        auto_vars_t ctx;
        strbuf_t all_p_sb;
        strbuf_init(&all_p_sb);
        for (p = 0; p < actual_prereq_count; p++) {
            if (p > 0) strbuf_append_char(&all_p_sb, ' ');
            strbuf_append(&all_p_sb, actual_prereqs[p]);
        }

        ctx.target = target;
        ctx.first_prereq = (actual_prereq_count > 0) ? actual_prereqs[0] : "";
        ctx.all_prereqs = all_p_sb.data;
        ctx.stem = stem;

        for (c = 0; c < rule->cmd_count; c++) {
            char *raw_cmd = rule->commands[c];
            char *expanded_cmd = expand_vars_ctx(raw_cmd, &ctx);
            char *cmd_to_run = trim_whitespace(expanded_cmd);
            int silent = 0;
            int ignore_err = 0;
            int res;

            while (*cmd_to_run == '@' || *cmd_to_run == '-') {
                if (*cmd_to_run == '@') silent = 1;
                if (*cmd_to_run == '-') ignore_err = 1;
                cmd_to_run = trim_whitespace(cmd_to_run + 1);
            }

            if (!silent && *cmd_to_run) {
                printf("%s\n", cmd_to_run);
                fflush(stdout);
            }

            if (*cmd_to_run) {
                res = system(cmd_to_run);
                if (res != 0 && !ignore_err) {
                    fprintf(stderr, "make: *** [%s] Error %d\n", target, res);
                    free(expanded_cmd);
                    strbuf_free(&all_p_sb);
                    if (actual_prereqs && actual_prereqs != rule->prereqs) {
                        for (p = 0; p < actual_prereq_count; p++) free(actual_prereqs[p]);
                        free(actual_prereqs);
                    }
                    return -1;
                }
            }
            free(expanded_cmd);
        }
        strbuf_free(&all_p_sb);
    }

    mark_target_built(target, time(NULL), 1);

    if (actual_prereqs && rule && actual_prereqs != rule->prereqs) {
        for (p = 0; p < actual_prereq_count; p++) free(actual_prereqs[p]);
        free(actual_prereqs);
    }

    return 0;
}

/* Makefile Parsing */
static void parse_makefile(const char *filename) {
    FILE *fp = fopen(filename, "r");
    char line_buf[MAX_LINE_LEN];
    strbuf_t cur_line;
    rule_t *cur_rule = NULL;

    if (!fp) {
        fprintf(stderr, "make: *** %s: No such file or directory.  Stop.\n", filename);
        exit(2);
    }

    strbuf_init(&cur_line);

    while (fgets(line_buf, sizeof(line_buf), fp)) {
        size_t len = strlen(line_buf);
        int is_continued = 0;

        /* Check for line continuation */
        while (len > 0 && (line_buf[len - 1] == '\r' || line_buf[len - 1] == '\n')) {
            line_buf[--len] = '\0';
        }

        if (len > 0 && line_buf[len - 1] == '\\') {
            line_buf[--len] = '\0';
            is_continued = 1;
        }

        strbuf_append(&cur_line, line_buf);
        if (is_continued) {
            strbuf_append_char(&cur_line, ' ');
            continue;
        }

        /* Process complete line */
        {
            char *line = cur_line.data;
            char *line_copy = my_strdup(line);
            char *trimmed = trim_whitespace(line_copy);

            if (line[0] == '\t' || (line[0] == ' ' && cur_rule && strchr(line, '=') == NULL && strchr(line, ':') == NULL)) {
                /* Recipe line */
                if (cur_rule && trimmed[0] != '#' && trimmed[0] != '\0') {
                    if (cur_rule->cmd_count < MAX_COMMANDS) {
                        const char *cmd_start = line;
                        if (*cmd_start == '\t') cmd_start++;
                        else cmd_start = skip_ws(cmd_start);
                        cur_rule->commands[cur_rule->cmd_count++] = my_strdup(cmd_start);
                    }
                }
            } else if (trimmed[0] == '#' || trimmed[0] == '\0') {
                /* Comment or empty line */
            } else {
                /* Check for assignment vs target */
                char *colon = strchr(line, ':');
                char *eq = strchr(line, '=');

                if (colon && (!eq || colon < eq || (eq && colon[1] == '='))) {
                    /* Rule definition: targets : prereqs */
                    char *targets_str;
                    char *prereqs_str;
                    char *expanded_targets;
                    char *expanded_prereqs;
                    const char *t_ptr;
                    char *t_tok;

                    if (colon[1] == '=') {
                        /* := assignment */
                        char *var_name;
                        char *var_val;
                        char *exp_val;
                        *colon = '\0';
                        var_name = trim_whitespace(line);
                        var_val = trim_whitespace(colon + 2);
                        exp_val = expand_vars(var_val);
                        set_var(var_name, exp_val, 1);
                        free(exp_val);
                        cur_rule = NULL;
                        goto line_done;
                    }

                    *colon = '\0';
                    targets_str = trim_whitespace(line);
                    prereqs_str = trim_whitespace(colon + 1);

                    /* Check special targets */
                    if (strcmp(targets_str, ".PHONY") == 0) {
                        const char *p_ptr;
                        char *p_tok;
                        expanded_prereqs = expand_vars(prereqs_str);
                        p_ptr = expanded_prereqs;
                        while ((p_tok = next_token(&p_ptr)) != NULL) {
                            add_phony(p_tok);
                            free(p_tok);
                        }
                        free(expanded_prereqs);
                        cur_rule = NULL;
                        goto line_done;
                    }
                    if (strcmp(targets_str, ".SECONDARY") == 0 || strcmp(targets_str, ".PRECIOUS") == 0) {
                        cur_rule = NULL;
                        goto line_done;
                    }

                    expanded_targets = expand_vars(targets_str);
                    expanded_prereqs = expand_vars(prereqs_str);

                    t_ptr = expanded_targets;
                    while ((t_tok = next_token(&t_ptr)) != NULL) {
                        int is_pat = (strchr(t_tok, '%') != NULL);
                        rule_t *r = NULL;

                        if (!is_pat) {
                            r = find_explicit_rule(t_tok);
                        }

                        if (!r && g_rule_count < MAX_RULES) {
                            r = &g_rules[g_rule_count++];
                            r->target = my_strdup(t_tok);
                            r->prereqs = (char**)malloc(MAX_PREREQS * sizeof(char*));
                            r->prereq_count = 0;
                            r->commands = (char**)malloc(MAX_COMMANDS * sizeof(char*));
                            r->cmd_count = 0;
                            r->is_phony = is_target_phony(t_tok);
                            r->is_pattern = is_pat;

                            if (!g_default_target && !is_pat && t_tok[0] != '.') {
                                g_default_target = my_strdup(t_tok);
                            }
                        }

                        if (r) {
                            const char *p_ptr = expanded_prereqs;
                            char *p_tok;
                            while ((p_tok = next_token(&p_ptr)) != NULL) {
                                if (r->prereq_count < MAX_PREREQS) {
                                    r->prereqs[r->prereq_count++] = p_tok;
                                } else {
                                    free(p_tok);
                                }
                            }
                            cur_rule = r;
                        }

                        free(t_tok);
                    }

                    free(expanded_targets);
                    free(expanded_prereqs);
                } else if (eq) {
                    /* Variable assignment */
                    char *var_name;
                    char *var_val;
                    int is_append = 0;
                    int is_simple = 0;

                    if (eq > line && eq[-1] == '+') {
                        is_append = 1;
                        eq[-1] = '\0';
                    } else if (eq > line && eq[-1] == ':') {
                        is_simple = 1;
                        eq[-1] = '\0';
                    } else if (eq > line && eq[-1] == '?') {
                        eq[-1] = '\0';
                        if (get_var_raw(trim_whitespace(line)) != NULL) {
                            cur_rule = NULL;
                            goto line_done;
                        }
                    } else {
                        *eq = '\0';
                    }

                    var_name = trim_whitespace(line);
                    var_val = trim_whitespace(eq + 1);

                    if (is_append) {
                        char *exp_val = expand_vars(var_val);
                        append_var(var_name, exp_val);
                        free(exp_val);
                    } else if (is_simple) {
                        char *exp_val = expand_vars(var_val);
                        set_var(var_name, exp_val, 1);
                        free(exp_val);
                    } else {
                        set_var(var_name, var_val, 0);
                    }

                    cur_rule = NULL;
                }
            }

        line_done:
            free(line_copy);
            strbuf_free(&cur_line);
            strbuf_init(&cur_line);
        }
    }

    strbuf_free(&cur_line);
    fclose(fp);
}

int main(int argc, char **argv) {
    const char *makefile = "Makefile";
    const char *target = NULL;
    int i;

    /* Parse CLI options */
    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-f") == 0 && i + 1 < argc) {
            makefile = argv[++i];
        } else if (strncmp(argv[i], "-f", 2) == 0) {
            makefile = argv[i] + 2;
        } else if (strcmp(argv[i], "-C") == 0 && i + 1 < argc) {
            if (chdir(argv[++i]) != 0) {
                fprintf(stderr, "make: chdir to %s failed\n", argv[i]);
                return 2;
            }
        } else if (argv[i][0] == '-') {
            /* Ignore unsupported flags like -j, -k, etc. */
        } else if (strchr(argv[i], '=')) {
            /* CLI variable override: VAR=val */
            char *eq = strchr(argv[i], '=');
            char *name = (char*)malloc(eq - argv[i] + 1);
            memcpy(name, argv[i], eq - argv[i]);
            name[eq - argv[i]] = '\0';
            set_var(name, eq + 1, 1);
            free(name);
        } else {
            target = argv[i];
        }
    }

    parse_makefile(makefile);

    if (!target) {
        target = g_default_target;
    }

    if (!target) {
        fprintf(stderr, "make: *** No targets specified and no makefile found.  Stop.\n");
        return 2;
    }

    if (build_target_internal(target, 0) != 0) {
        return 2;
    }

    return 0;
}

