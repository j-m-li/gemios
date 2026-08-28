/*
 * This is free and unencumbered software released into the public domain.
 * GEMIOS - Custom Host Tool: Complete 32-bit x86 Assembler (as replacement)
 *
 * Full support for clang -S generated assembly (.s / .S) and handwritten assembly.
 * Assembles into standard ELF32 relocatable object (.o).
 * Standard C90 compliant.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#if defined(_MSC_VER)
typedef unsigned char uint8_t;
typedef unsigned short uint16_t;
typedef unsigned int uint32_t;
typedef int int32_t;
#else
#include <stdint.h>
#endif

#define EI_NIDENT 16

#define ET_REL  1
#define EM_386 3
#define EV_CURRENT 1

#define SHT_NULL     0
#define SHT_PROGBITS 1
#define SHT_SYMTAB   2
#define SHT_STRTAB   3
#define SHT_NOBITS   8
#define SHT_REL      9

#define SHF_WRITE     (1 << 0)
#define SHF_ALLOC     (1 << 1)
#define SHF_EXECINSTR (1 << 2)

#define SHN_UNDEF  0
#define SHN_ABS    0xFFF1
#define SHN_COMMON 0xFFF2

#define STB_LOCAL  0
#define STB_GLOBAL 1

#define STT_NOTYPE  0
#define STT_OBJECT  1
#define STT_FUNC    2
#define STT_SECTION 3

#define ELF32_ST_BIND(i)   ((i) >> 4)
#define ELF32_ST_TYPE(i)   ((i) & 0x0F)
#define ELF32_ST_INFO(b,t) (((b) << 4) + ((t) & 0x0F))

#define ELF32_R_SYM(i)     ((i) >> 8)
#define ELF32_R_TYPE(i)    ((uint8_t)(i))
#define ELF32_R_INFO(s,t)  (((s) << 8) + (uint8_t)(t))

#define R_386_32   1
#define R_386_PC32 2

#pragma pack(push, 1)
typedef struct {
    uint8_t  e_ident[EI_NIDENT];
    uint16_t e_type;
    uint16_t e_machine;
    uint32_t e_version;
    uint32_t e_entry;
    uint32_t e_phoff;
    uint32_t e_shoff;
    uint32_t e_flags;
    uint16_t e_ehsize;
    uint16_t e_phentsize;
    uint16_t e_phnum;
    uint16_t e_shentsize;
    uint16_t e_shnum;
    uint16_t e_shstrndx;
} Elf32_Ehdr;

typedef struct {
    uint32_t sh_name;
    uint32_t sh_type;
    uint32_t sh_flags;
    uint32_t sh_addr;
    uint32_t sh_offset;
    uint32_t sh_size;
    uint32_t sh_link;
    uint32_t sh_info;
    uint32_t sh_addralign;
    uint32_t sh_entsize;
} Elf32_Shdr;

typedef struct {
    uint32_t st_name;
    uint32_t st_value;
    uint32_t st_size;
    uint8_t  st_info;
    uint8_t  st_other;
    uint16_t st_shndx;
} Elf32_Sym;

typedef struct {
    uint32_t r_offset;
    uint32_t r_info;
} Elf32_Rel;
#pragma pack(pop)

#define ALIGN_UP(val, align) (((val) + (align) - 1) & ~((align) - 1))

#define MAX_SECTIONS 32
#define MAX_SYMBOLS 4096
#define MAX_RELOCS 8192
#define MAX_MACROS 64
#define MAX_MACRO_LINES 128
#define MAX_LOCAL_LABELS 2048

typedef struct {
    char     name[64];
    uint32_t type;
    uint32_t flags;
    uint32_t align;
    uint8_t *data;
    size_t   size;
    size_t   capacity;
} section_t;

typedef struct {
    char     name[128];
    uint32_t value;
    uint32_t size;
    uint8_t  type;
    uint8_t  binding;
    int      section_idx;
    int      defined;
} symbol_t;

typedef struct {
    int      section_idx;
    uint32_t offset;
    int      sym_idx;
    uint32_t type;
} reloc_t;

typedef struct {
    char name[64];
    char params[4][32];
    int  param_count;
    char lines[MAX_MACRO_LINES][256];
    int  line_count;
} macro_def_t;

typedef struct {
    char     name[64];
    int      section_idx;
    uint32_t offset;
    int      line_idx;
} label_pos_t;

static section_t g_sections[MAX_SECTIONS];
static int g_section_count = 0;
static int g_cur_section = 1;

static symbol_t g_symbols[MAX_SYMBOLS];
static int g_symbol_count = 0;

static reloc_t g_relocs[MAX_RELOCS];
static int g_reloc_count = 0;

static macro_def_t g_macros[MAX_MACROS];
static int g_macro_count = 0;

static label_pos_t g_local_labels[MAX_LOCAL_LABELS];
static int g_local_label_count = 0;

static void clean_string(char *s) {
    size_t len = strlen(s);
    while (len > 0 && (s[len-1] == ' ' || s[len-1] == '\t' || s[len-1] == '\r' || s[len-1] == '\n')) {
        s[--len] = '\0';
    }
}

static const char *skip_ws(const char *p) {
    while (*p && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')) p++;
    return p;
}

static const char *find_label_colon(const char *p) {
    const char *s = skip_ws(p);
    if (!*s || *s == '"' || *s == '#' || *s == ';') return NULL;

    while (*s && (isalnum((unsigned char)*s) || *s == '_' || *s == '.' || *s == '$')) {
        s++;
    }
    s = skip_ws(s);
    if (*s == ':') {
        return s;
    }
    return NULL;
}

static void assemble_line(const char *line, int line_idx, int pass);

static int get_or_create_section(const char *name, uint32_t type, uint32_t flags, uint32_t align) {
    int i;
    for (i = 1; i <= g_section_count; i++) {
        if (strcmp(g_sections[i].name, name) == 0) {
            return i;
        }
    }
    if (g_section_count + 1 >= MAX_SECTIONS) {
        fprintf(stderr, "Error: Too many sections\n");
        exit(1);
    }
    g_section_count++;
    memset(&g_sections[g_section_count], 0, sizeof(section_t));
    strncpy(g_sections[g_section_count].name, name, sizeof(g_sections[g_section_count].name) - 1);
    g_sections[g_section_count].type = type;
    g_sections[g_section_count].flags = flags;
    g_sections[g_section_count].align = align;
    return g_section_count;
}

static void sec_emit_byte(int sec_idx, uint8_t b) {
    section_t *sec = &g_sections[sec_idx];
    if (sec->type != SHT_NOBITS) {
        if (sec->size + 1 > sec->capacity) {
            size_t new_cap = (sec->capacity == 0) ? 8192 : sec->capacity * 2;
            sec->data = (uint8_t*)realloc(sec->data, new_cap);
            if (!sec->data) {
                fprintf(stderr, "Out of memory\n");
                exit(1);
            }
            sec->capacity = new_cap;
        }
        sec->data[sec->size] = b;
    }
    sec->size++;
}

static void sec_emit_word(int sec_idx, uint16_t w) {
    sec_emit_byte(sec_idx, (uint8_t)(w & 0xFF));
    sec_emit_byte(sec_idx, (uint8_t)((w >> 8) & 0xFF));
}

static void sec_emit_dword(int sec_idx, uint32_t dw) {
    sec_emit_byte(sec_idx, (uint8_t)(dw & 0xFF));
    sec_emit_byte(sec_idx, (uint8_t)((dw >> 8) & 0xFF));
    sec_emit_byte(sec_idx, (uint8_t)((dw >> 16) & 0xFF));
    sec_emit_byte(sec_idx, (uint8_t)((dw >> 24) & 0xFF));
}

static int find_or_add_symbol(const char *name) {
    int i;
    char clean_name[128];
    strncpy(clean_name, name, 127);
    clean_name[127] = '\0';
    clean_string(clean_name);

    for (i = 1; i <= g_symbol_count; i++) {
        if (strcmp(g_symbols[i].name, clean_name) == 0) {
            return i;
        }
    }
    if (g_symbol_count + 1 >= MAX_SYMBOLS) {
        fprintf(stderr, "Error: Exceeded MAX_SYMBOLS (%d)\n", MAX_SYMBOLS);
        exit(1);
    }
    g_symbol_count++;
    memset(&g_symbols[g_symbol_count], 0, sizeof(symbol_t));
    strncpy(g_symbols[g_symbol_count].name, clean_name, sizeof(g_symbols[g_symbol_count].name) - 1);
    g_symbols[g_symbol_count].binding = STB_LOCAL;
    return g_symbol_count;
}

static void add_relocation(int sec_idx, uint32_t offset, int sym_idx, uint32_t type) {
    if (g_reloc_count >= MAX_RELOCS) {
        fprintf(stderr, "Error: Exceeded MAX_RELOCS (%d)\n", MAX_RELOCS);
        exit(1);
    }
    g_relocs[g_reloc_count].section_idx = sec_idx;
    g_relocs[g_reloc_count].offset = offset;
    g_relocs[g_reloc_count].sym_idx = sym_idx;
    g_relocs[g_reloc_count].type = type;
    g_reloc_count++;
}

/* Register Types */
enum {
    REG_NONE = 0,
    REG_R8,
    REG_R16,
    REG_R32,
    REG_SREG
};

static int parse_reg(const char *p, int *type, int *num) {
    p = skip_ws(p);
    if (*p != '%') return 0;
    p++;

    /* 32-bit registers */
    if (strcmp(p, "eax") == 0) { *type = REG_R32; *num = 0; return 1; }
    if (strcmp(p, "ecx") == 0) { *type = REG_R32; *num = 1; return 1; }
    if (strcmp(p, "edx") == 0) { *type = REG_R32; *num = 2; return 1; }
    if (strcmp(p, "ebx") == 0) { *type = REG_R32; *num = 3; return 1; }
    if (strcmp(p, "esp") == 0) { *type = REG_R32; *num = 4; return 1; }
    if (strcmp(p, "ebp") == 0) { *type = REG_R32; *num = 5; return 1; }
    if (strcmp(p, "esi") == 0) { *type = REG_R32; *num = 6; return 1; }
    if (strcmp(p, "edi") == 0) { *type = REG_R32; *num = 7; return 1; }

    /* 16-bit registers */
    if (strcmp(p, "ax") == 0) { *type = REG_R16; *num = 0; return 1; }
    if (strcmp(p, "cx") == 0) { *type = REG_R16; *num = 1; return 1; }
    if (strcmp(p, "dx") == 0) { *type = REG_R16; *num = 2; return 1; }
    if (strcmp(p, "bx") == 0) { *type = REG_R16; *num = 3; return 1; }
    if (strcmp(p, "sp") == 0) { *type = REG_R16; *num = 4; return 1; }
    if (strcmp(p, "bp") == 0) { *type = REG_R16; *num = 5; return 1; }
    if (strcmp(p, "si") == 0) { *type = REG_R16; *num = 6; return 1; }
    if (strcmp(p, "di") == 0) { *type = REG_R16; *num = 7; return 1; }

    /* 8-bit registers */
    if (strcmp(p, "al") == 0) { *type = REG_R8; *num = 0; return 1; }
    if (strcmp(p, "cl") == 0) { *type = REG_R8; *num = 1; return 1; }
    if (strcmp(p, "dl") == 0) { *type = REG_R8; *num = 2; return 1; }
    if (strcmp(p, "bl") == 0) { *type = REG_R8; *num = 3; return 1; }
    if (strcmp(p, "ah") == 0) { *type = REG_R8; *num = 4; return 1; }
    if (strcmp(p, "ch") == 0) { *type = REG_R8; *num = 5; return 1; }
    if (strcmp(p, "dh") == 0) { *type = REG_R8; *num = 6; return 1; }
    if (strcmp(p, "bh") == 0) { *type = REG_R8; *num = 7; return 1; }

    /* Segment registers */
    if (strcmp(p, "es") == 0) { *type = REG_SREG; *num = 0; return 1; }
    if (strcmp(p, "cs") == 0) { *type = REG_SREG; *num = 1; return 1; }
    if (strcmp(p, "ss") == 0) { *type = REG_SREG; *num = 2; return 1; }
    if (strcmp(p, "ds") == 0) { *type = REG_SREG; *num = 3; return 1; }
    if (strcmp(p, "fs") == 0) { *type = REG_SREG; *num = 4; return 1; }
    if (strcmp(p, "gs") == 0) { *type = REG_SREG; *num = 5; return 1; }

    return 0;
}

static int32_t eval_expr(const char **expr_ptr) {
    const char *p = *expr_ptr;
    int32_t val = 0;
    int neg = 0;

    p = skip_ws(p);
    if (*p == '-') {
        neg = 1;
        p++;
    } else if (*p == '~') {
        p++;
        val = ~eval_expr(&p);
        *expr_ptr = p;
        return val;
    }

    p = skip_ws(p);
    if (*p == '(') {
        p++;
        val = eval_expr(&p);
        p = skip_ws(p);
        if (*p == ')') p++;
    } else if (*p == '$') {
        p++;
        val = eval_expr(&p);
    } else if (*p == '\'') {
        p++;
        if (*p == '\\') {
            p++;
            if (*p == 'n') val = '\n';
            else if (*p == 't') val = '\t';
            else if (*p == 'r') val = '\r';
            else if (*p == 'b') val = '\b';
            else if (*p == 'a') val = '\a';
            else if (*p == 'f') val = '\f';
            else if (*p == 'v') val = '\v';
            else if (*p == '0') val = '\0';
            else if (*p == '\\') val = '\\';
            else if (*p == '\'') val = '\'';
            else val = (unsigned char)*p;
            p++;
        } else {
            val = (unsigned char)*p++;
        }
        if (*p == '\'') p++;
    } else if (isdigit((unsigned char)*p)) {
        if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) {
            val = (int32_t)strtoul(p, (char**)&p, 16);
        } else {
            val = (int32_t)strtoul(p, (char**)&p, 10);
        }
    } else if (isalpha((unsigned char)*p) || *p == '_' || *p == '.') {
        char sym_name[128];
        int len = 0;
        int i;

        while (isalnum((unsigned char)*p) || *p == '_' || *p == '.') {
            if (len < 127) sym_name[len++] = *p;
            p++;
        }
        sym_name[len] = '\0';

        for (i = 1; i <= g_symbol_count; i++) {
            if (strcmp(g_symbols[i].name, sym_name) == 0 && g_symbols[i].defined) {
                val = (int32_t)g_symbols[i].value;
                break;
            }
        }
    }

    if (neg) val = -val;

    p = skip_ws(p);
    while (*p == '+' || *p == '-' || *p == '|' || *p == '&' || (*p == '<' && p[1] == '<') || (*p == '>' && p[1] == '>')) {
        char op = *p++;
        int32_t rhs;

        if (op == '<') { p++; op = 'L'; }
        if (op == '>') { p++; op = 'R'; }

        rhs = eval_expr(&p);
        if (op == '+') val += rhs;
        else if (op == '-') val -= rhs;
        else if (op == '|') val |= rhs;
        else if (op == '&') val &= rhs;
        else if (op == 'L') val <<= rhs;
        else if (op == 'R') val >>= rhs;

        p = skip_ws(p);
    }

    *expr_ptr = p;
    return val;
}

/* Operand Representation */
enum {
    OP_NONE = 0,
    OP_REG,
    OP_IMM,
    OP_MEM
};

typedef struct {
    int      kind;
    int      reg_type;
    int      reg_num;
    int32_t  imm_val;
    char     sym_name[128];
    int      has_sym;

    /* Memory Addressing */
    int      base_reg;   /* -1 if none */
    int      index_reg;  /* -1 if none */
    int      scale;      /* 1, 2, 4, 8 */
    int32_t  disp;
} operand_t;

static int parse_operand(const char *s, operand_t *op) {
    memset(op, 0, sizeof(operand_t));
    op->base_reg = -1;
    op->index_reg = -1;
    op->scale = 1;

    s = skip_ws(s);
    if (!*s) return 0;

    /* Register */
    if (*s == '%' && parse_reg(s, &op->reg_type, &op->reg_num)) {
        op->kind = OP_REG;
        return 1;
    }

    /* Immediate: $imm or $sym or $sym+disp */
    if (*s == '$') {
        const char *p = s + 1;
        p = skip_ws(p);
        op->kind = OP_IMM;

        if (isalpha((unsigned char)*p) || *p == '_' || *p == '.') {
            int len = 0;
            while (isalnum((unsigned char)*p) || *p == '_' || *p == '.') {
                if (len < 127) op->sym_name[len++] = *p;
                p++;
            }
            op->sym_name[len] = '\0';
            op->has_sym = 1;

            p = skip_ws(p);
            if (*p == '+' || *p == '-') {
                op->imm_val = eval_expr(&p);
            }
        } else {
            op->imm_val = eval_expr(&p);
        }
        return 1;
    }

    /* Memory operand */
    op->kind = OP_MEM;
    {
        const char *p = s;
        const char *lparen = strchr(p, '(');

        if (lparen) {
            /* Displacement / Symbol before '(' */
            if (lparen > p) {
                char prefix[128];
                size_t plen = lparen - p;
                if (plen > 127) plen = 127;
                memcpy(prefix, p, plen);
                prefix[plen] = '\0';
                clean_string(prefix);

                if (isalpha((unsigned char)prefix[0]) || prefix[0] == '_' || prefix[0] == '.') {
                    const char *pp = prefix;
                    int len = 0;
                    while (isalnum((unsigned char)*pp) || *pp == '_' || *pp == '.') {
                        if (len < 127) op->sym_name[len++] = *pp;
                        pp++;
                    }
                    op->sym_name[len] = '\0';
                    op->has_sym = 1;

                    pp = skip_ws(pp);
                    if (*pp == '+' || *pp == '-') {
                        op->disp = eval_expr(&pp);
                    }
                } else {
                    const char *pp = prefix;
                    op->disp = eval_expr(&pp);
                }
            }

            /* Inside '(' ... ')' */
            {
                char inside[128];
                const char *rparen = strchr(lparen, ')');
                if (rparen) {
                    char parts[3][32];
                    int pcount = 0;
                    const char *ip;
                    size_t ilen = rparen - (lparen + 1);
                    if (ilen > 127) ilen = 127;
                    memcpy(inside, lparen + 1, ilen);
                    inside[ilen] = '\0';

                    ip = inside;
                    while (*ip) {
                        int len = 0;
                        while (*ip && *ip != ',') {
                            if (len < 31) parts[pcount][len++] = *ip;
                            ip++;
                        }
                        parts[pcount][len] = '\0';
                        clean_string(parts[pcount]);
                        pcount++;
                        if (*ip == ',') ip++;
                        ip = skip_ws(ip);
                    }

                    if (pcount == 1) {
                        int rt, rn;
                        if (parse_reg(parts[0], &rt, &rn)) op->base_reg = rn;
                    } else if (pcount == 2) {
                        int rt, rn;
                        if (parts[0][0] && parse_reg(parts[0], &rt, &rn)) op->base_reg = rn;
                        if (parts[1][0] && parse_reg(parts[1], &rt, &rn)) op->index_reg = rn;
                    } else if (pcount == 3) {
                        int rt, rn;
                        if (parts[0][0] && parse_reg(parts[0], &rt, &rn)) op->base_reg = rn;
                        if (parts[1][0] && parse_reg(parts[1], &rt, &rn)) op->index_reg = rn;
                        if (parts[2][0]) op->scale = atoi(parts[2]);
                    }
                }
            }
        } else {
            /* Direct symbol or absolute address */
            if (isalpha((unsigned char)*p) || *p == '_' || *p == '.') {
                int len = 0;
                while (isalnum((unsigned char)*p) || *p == '_' || *p == '.') {
                    if (len < 127) op->sym_name[len++] = *p;
                    p++;
                }
                op->sym_name[len] = '\0';
                op->has_sym = 1;

                p = skip_ws(p);
                if (*p == '+' || *p == '-') {
                    op->disp = eval_expr(&p);
                }
            } else {
                op->disp = eval_expr(&p);
            }
        }
    }
    return 1;
}

/* ModR/M & SIB Machine Code Emitter */
static void emit_modrm_sib(int sec_idx, int reg_field, operand_t *rm, int pass) {
    if (rm->kind == OP_REG) {
        sec_emit_byte(sec_idx, (uint8_t)(0xC0 | (reg_field << 3) | rm->reg_num));
        return;
    }

    /* Memory operand */
    if (rm->base_reg < 0 && rm->index_reg < 0) {
        /* disp32 or symbol absolute address */
        sec_emit_byte(sec_idx, (uint8_t)(0x05 | (reg_field << 3)));
        if (rm->has_sym) {
            int s_idx = find_or_add_symbol(rm->sym_name);
            if (pass == 2) {
                add_relocation(sec_idx, (uint32_t)g_sections[sec_idx].size, s_idx, R_386_32);
            }
            sec_emit_dword(sec_idx, (uint32_t)rm->disp);
        } else {
            sec_emit_dword(sec_idx, (uint32_t)rm->disp);
        }
        return;
    }

    if (rm->index_reg >= 0) {
        int scale_code = 0;
        if (rm->scale == 2) scale_code = 1;
        else if (rm->scale == 4) scale_code = 2;
        else if (rm->scale == 8) scale_code = 3;

        if (rm->base_reg < 0) {
            /* (,%index,scale) + disp32 */
            sec_emit_byte(sec_idx, (uint8_t)(0x04 | (reg_field << 3)));
            sec_emit_byte(sec_idx, (uint8_t)((scale_code << 6) | (rm->index_reg << 3) | 5));
            if (rm->has_sym) {
                int s_idx = find_or_add_symbol(rm->sym_name);
                if (pass == 2) {
                    add_relocation(sec_idx, (uint32_t)g_sections[sec_idx].size, s_idx, R_386_32);
                }
                sec_emit_dword(sec_idx, (uint32_t)rm->disp);
            } else {
                sec_emit_dword(sec_idx, (uint32_t)rm->disp);
            }
        } else if (rm->disp == 0 && !rm->has_sym && rm->base_reg != 5) {
            /* (%base, %index, scale) */
            sec_emit_byte(sec_idx, (uint8_t)(0x04 | (reg_field << 3)));
            sec_emit_byte(sec_idx, (uint8_t)((scale_code << 6) | (rm->index_reg << 3) | rm->base_reg));
        } else if (rm->disp >= -128 && rm->disp <= 127 && !rm->has_sym) {
            /* disp8(%base, %index, scale) */
            sec_emit_byte(sec_idx, (uint8_t)(0x44 | (reg_field << 3)));
            sec_emit_byte(sec_idx, (uint8_t)((scale_code << 6) | (rm->index_reg << 3) | rm->base_reg));
            sec_emit_byte(sec_idx, (uint8_t)rm->disp);
        } else {
            /* disp32(%base, %index, scale) */
            sec_emit_byte(sec_idx, (uint8_t)(0x84 | (reg_field << 3)));
            sec_emit_byte(sec_idx, (uint8_t)((scale_code << 6) | (rm->index_reg << 3) | rm->base_reg));
            if (rm->has_sym) {
                int s_idx = find_or_add_symbol(rm->sym_name);
                if (pass == 2) {
                    add_relocation(sec_idx, (uint32_t)g_sections[sec_idx].size, s_idx, R_386_32);
                }
                sec_emit_dword(sec_idx, (uint32_t)rm->disp);
            } else {
                sec_emit_dword(sec_idx, (uint32_t)rm->disp);
            }
        }
        return;
    }

    /* Base register without index */
    if (rm->base_reg == 4) {
        /* %esp needs SIB */
        if (rm->disp == 0 && !rm->has_sym) {
            sec_emit_byte(sec_idx, (uint8_t)(0x04 | (reg_field << 3)));
            sec_emit_byte(sec_idx, 0x24);
        } else if (rm->disp >= -128 && rm->disp <= 127 && !rm->has_sym) {
            sec_emit_byte(sec_idx, (uint8_t)(0x44 | (reg_field << 3)));
            sec_emit_byte(sec_idx, 0x24);
            sec_emit_byte(sec_idx, (uint8_t)rm->disp);
        } else {
            sec_emit_byte(sec_idx, (uint8_t)(0x84 | (reg_field << 3)));
            sec_emit_byte(sec_idx, 0x24);
            if (rm->has_sym) {
                int s_idx = find_or_add_symbol(rm->sym_name);
                if (pass == 2) {
                    add_relocation(sec_idx, (uint32_t)g_sections[sec_idx].size, s_idx, R_386_32);
                }
                sec_emit_dword(sec_idx, (uint32_t)rm->disp);
            } else {
                sec_emit_dword(sec_idx, (uint32_t)rm->disp);
            }
        }
    } else {
        if (rm->disp == 0 && !rm->has_sym && rm->base_reg != 5) {
            sec_emit_byte(sec_idx, (uint8_t)((reg_field << 3) | rm->base_reg));
        } else if (rm->disp >= -128 && rm->disp <= 127 && !rm->has_sym) {
            sec_emit_byte(sec_idx, (uint8_t)(0x40 | (reg_field << 3) | rm->base_reg));
            sec_emit_byte(sec_idx, (uint8_t)rm->disp);
        } else {
            sec_emit_byte(sec_idx, (uint8_t)(0x80 | (reg_field << 3) | rm->base_reg));
            if (rm->has_sym) {
                int s_idx = find_or_add_symbol(rm->sym_name);
                if (pass == 2) {
                    add_relocation(sec_idx, (uint32_t)g_sections[sec_idx].size, s_idx, R_386_32);
                }
                sec_emit_dword(sec_idx, (uint32_t)rm->disp);
            } else {
                sec_emit_dword(sec_idx, (uint32_t)rm->disp);
            }
        }
    }
}

static void parse_operands_raw(const char *p, char ops[8][512], int *op_count) {
    int i = 0;
    *op_count = 0;
    p = skip_ws(p);

    while (*p) {
        int len = 0;
        int in_paren = 0;
        int in_quote = 0;

        if (*p == '#' || *p == ';') break;

        while (*p) {
            if (!in_quote && (*p == '#' || *p == ';')) break;
            if (!in_quote && !in_paren && *p == ',') break;

            if (*p == '"') {
                in_quote = !in_quote;
            } else if (*p == '\\' && in_quote) {
                if (len < 510) ops[i][len++] = *p++;
                if (*p) {
                    if (len < 510) ops[i][len++] = *p;
                }
                p++;
                continue;
            } else if (*p == '(' && !in_quote) {
                in_paren++;
            } else if (*p == ')' && !in_quote) {
                in_paren--;
            }

            if (len < 510) ops[i][len++] = *p;
            p++;
        }
        ops[i][len] = '\0';
        clean_string(ops[i]);
        /* Trim leading spaces */
        {
            char *s = ops[i];
            while (*s == ' ' || *s == '\t') s++;
            if (s != ops[i]) memmove(ops[i], s, strlen(s) + 1);
        }

        i++;
        if (i >= 8) break;
        if (*p == ',') p++;
        p = skip_ws(p);
    }
    *op_count = i;
}

static void emit_string_literal(int sec_idx, const char *str, int is_null_terminated) {
    const char *p = skip_ws(str);
    if (*p == '"') p++;

    while (*p) {
        if (*p == '"') {
            break;
        }
        if (*p == '\\') {
            p++;
            if (*p == 'n') sec_emit_byte(sec_idx, '\n');
            else if (*p == 't') sec_emit_byte(sec_idx, '\t');
            else if (*p == 'r') sec_emit_byte(sec_idx, '\r');
            else if (*p == 'b') sec_emit_byte(sec_idx, '\b');
            else if (*p == 'a') sec_emit_byte(sec_idx, '\a');
            else if (*p == 'f') sec_emit_byte(sec_idx, '\f');
            else if (*p == 'v') sec_emit_byte(sec_idx, '\v');
            else if (*p == '0' && (p[1] < '0' || p[1] > '7')) sec_emit_byte(sec_idx, 0);
            else if (*p >= '0' && *p <= '7') {
                uint32_t oct = 0;
                int count = 0;
                while (count < 3 && *p >= '0' && *p <= '7') {
                    oct = (oct << 3) + (*p - '0');
                    p++;
                    count++;
                }
                p--;
                sec_emit_byte(sec_idx, (uint8_t)oct);
            } else if (*p == 'x') {
                char hex[3];
                p++;
                hex[0] = *p++;
                hex[1] = *p;
                hex[2] = '\0';
                sec_emit_byte(sec_idx, (uint8_t)strtoul(hex, NULL, 16));
            } else if (*p == '\\') sec_emit_byte(sec_idx, '\\');
            else if (*p == '"') sec_emit_byte(sec_idx, '"');
            else if (*p == '\'') sec_emit_byte(sec_idx, '\'');
            else if (*p == '?') sec_emit_byte(sec_idx, '?');
            else sec_emit_byte(sec_idx, (uint8_t)*p);
        } else {
            sec_emit_byte(sec_idx, (uint8_t)*p);
        }
        p++;
    }
    if (is_null_terminated) {
        sec_emit_byte(sec_idx, 0);
    }
}

static void assemble_line(const char *line, int line_idx, int pass) {
    const char *p;
    char token[64];
    int len;
    char raw_ops[8][512];
    operand_t ops[8];
    int op_count;
    int i;

    p = skip_ws(line);
    if (!*p || *p == '#' || *p == ';') return;
    if (p[0] == '/' && p[1] == '*') return;

    /* Check for Label Definition (colon) */
    {
        const char *colon = find_label_colon(p);
        if (colon) {
            char label_name[128];
            size_t llen = colon - p;
            if (llen < 127) {
                int s_idx;
                memcpy(label_name, p, llen);
                label_name[llen] = '\0';
                clean_string(label_name);

                /* Local or Global Label */
                s_idx = find_or_add_symbol(label_name);
                g_symbols[s_idx].value = (uint32_t)g_sections[g_cur_section].size;
                g_symbols[s_idx].section_idx = g_cur_section;
                g_symbols[s_idx].defined = 1;

                if (pass == 1) {
                    if (g_local_label_count < MAX_LOCAL_LABELS) {
                        strncpy(g_local_labels[g_local_label_count].name, label_name, 63);
                        g_local_labels[g_local_label_count].section_idx = g_cur_section;
                        g_local_labels[g_local_label_count].offset = (uint32_t)g_sections[g_cur_section].size;
                        g_local_labels[g_local_label_count].line_idx = line_idx;
                        g_local_label_count++;
                    }
                } else if (pass == 2) {
                    int k;
                    for (k = 0; k < g_local_label_count; k++) {
                        if (g_local_labels[k].line_idx == line_idx) {
                            g_local_labels[k].offset = (uint32_t)g_sections[g_cur_section].size;
                            break;
                        }
                    }
                }

                p = colon + 1;
                p = skip_ws(p);
                if (!*p || *p == '#' || *p == ';') return;
            }
        }
    }

    /* Extract Mnemonic / Directive */
    len = 0;
    while (*p && !isspace((unsigned char)*p) && *p != '#' && *p != ';') {
        if (len < 63) token[len++] = *p;
        p++;
    }
    token[len] = '\0';
    p = skip_ws(p);

    parse_operands_raw(p, raw_ops, &op_count);
    for (i = 0; i < op_count; i++) {
        parse_operand(raw_ops[i], &ops[i]);
    }

    /* 1. Directives */
    if (strcmp(token, ".text") == 0) {
        g_cur_section = get_or_create_section(".text", SHT_PROGBITS, SHF_ALLOC | SHF_EXECINSTR, 4);
        return;
    }
    if (strcmp(token, ".data") == 0) {
        g_cur_section = get_or_create_section(".data", SHT_PROGBITS, SHF_ALLOC | SHF_WRITE, 4);
        return;
    }
    if (strcmp(token, ".bss") == 0) {
        g_cur_section = get_or_create_section(".bss", SHT_NOBITS, SHF_ALLOC | SHF_WRITE, 4);
        return;
    }
    if (strcmp(token, ".section") == 0 && op_count >= 1) {
        uint32_t type = SHT_PROGBITS;
        uint32_t flags = SHF_ALLOC;
        if (strstr(raw_ops[0], ".text")) flags |= SHF_EXECINSTR;
        if (strstr(raw_ops[0], ".bss")) { type = SHT_NOBITS; flags |= SHF_WRITE; }
        if (strstr(raw_ops[0], ".data")) flags |= SHF_WRITE;
        g_cur_section = get_or_create_section(raw_ops[0], type, flags, 4);
        return;
    }
    if (strcmp(token, ".globl") == 0 || strcmp(token, ".global") == 0) {
        if (op_count >= 1) {
            int s_idx = find_or_add_symbol(raw_ops[0]);
            g_symbols[s_idx].binding = STB_GLOBAL;
        }
        return;
    }
    if (strcmp(token, ".local") == 0) {
        if (op_count >= 1) {
            int s_idx = find_or_add_symbol(raw_ops[0]);
            g_symbols[s_idx].binding = STB_LOCAL;
        }
        return;
    }
    if (strcmp(token, ".comm") == 0 && op_count >= 2) {
        int s_idx = find_or_add_symbol(raw_ops[0]);
        uint32_t sz = (uint32_t)atoi(raw_ops[1]);
        uint32_t al = (op_count >= 3) ? (uint32_t)atoi(raw_ops[2]) : 4;
        g_symbols[s_idx].binding = STB_GLOBAL;
        g_symbols[s_idx].size = sz;
        g_symbols[s_idx].value = al;
        g_symbols[s_idx].section_idx = SHN_COMMON;
        g_symbols[s_idx].defined = 1;
        return;
    }
    if (strcmp(token, ".p2align") == 0 || strcmp(token, ".align") == 0) {
        if (op_count >= 1) {
            uint32_t a = (uint32_t)atoi(raw_ops[0]);
            uint32_t align_bytes = (strcmp(token, ".p2align") == 0) ? (1U << a) : a;
            uint32_t aligned = ALIGN_UP(g_sections[g_cur_section].size, align_bytes);
            while (g_sections[g_cur_section].size < aligned) {
                sec_emit_byte(g_cur_section, 0);
            }
            if (align_bytes > g_sections[g_cur_section].align) {
                g_sections[g_cur_section].align = align_bytes;
            }
        }
        return;
    }
    if (strcmp(token, ".asciz") == 0 || strcmp(token, ".string") == 0) {
        emit_string_literal(g_cur_section, p, 1);
        return;
    }
    if (strcmp(token, ".ascii") == 0) {
        emit_string_literal(g_cur_section, p, 0);
        return;
    }
    if (strcmp(token, ".byte") == 0 || strcmp(token, ".1byte") == 0) {
        for (i = 0; i < op_count; i++) {
            const char *ep = raw_ops[i];
            sec_emit_byte(g_cur_section, (uint8_t)eval_expr(&ep));
        }
        return;
    }
    if (strcmp(token, ".short") == 0 || strcmp(token, ".word") == 0 || strcmp(token, ".value") == 0 ||
        strcmp(token, ".2byte") == 0 || strcmp(token, ".half") == 0 || strcmp(token, ".hword") == 0) {
        for (i = 0; i < op_count; i++) {
            const char *ep = raw_ops[i];
            sec_emit_word(g_cur_section, (uint16_t)eval_expr(&ep));
        }
        return;
    }
    if (strcmp(token, ".long") == 0 || strcmp(token, ".quad") == 0 || strcmp(token, ".int") == 0 || strcmp(token, ".4byte") == 0) {
        for (i = 0; i < op_count; i++) {
            if (ops[i].has_sym) {
                int s_idx = find_or_add_symbol(ops[i].sym_name);
                if (pass == 2) {
                    add_relocation(g_cur_section, (uint32_t)g_sections[g_cur_section].size, s_idx, R_386_32);
                }
                sec_emit_dword(g_cur_section, (uint32_t)ops[i].disp);
            } else {
                const char *ep = raw_ops[i];
                sec_emit_dword(g_cur_section, (uint32_t)eval_expr(&ep));
            }
            if (strcmp(token, ".quad") == 0) sec_emit_dword(g_cur_section, 0);
        }
        return;
    }
    if (strcmp(token, ".zero") == 0 || strcmp(token, ".space") == 0 || strcmp(token, ".skip") == 0) {
        if (op_count >= 1) {
            const char *ep = raw_ops[0];
            uint32_t count = (uint32_t)eval_expr(&ep);
            size_t c;
            for (c = 0; c < count; c++) sec_emit_byte(g_cur_section, 0);
        }
        return;
    }
    if (strcmp(token, ".set") == 0) {
        if (op_count >= 2) {
            int s_idx = find_or_add_symbol(raw_ops[0]);
            const char *ep = raw_ops[1];
            g_symbols[s_idx].value = (uint32_t)eval_expr(&ep);
            g_symbols[s_idx].section_idx = SHN_ABS;
            g_symbols[s_idx].defined = 1;
        }
        return;
    }
    if (token[0] == '.') {
        /* Ignore other compiler metadata (.file, .ident, .type, .size, .cfi_*, etc.) */
        return;
    }

    /* 2. Instructions */

    /* Zero-operand / Stack instructions */
    if (strcmp(token, "ret") == 0 || strcmp(token, "retl") == 0 || strcmp(token, "retw") == 0) {
        if (op_count >= 1 && ops[0].kind == OP_IMM) {
            sec_emit_byte(g_cur_section, 0xC2);
            sec_emit_word(g_cur_section, (uint16_t)ops[0].imm_val);
        } else {
            sec_emit_byte(g_cur_section, 0xC3);
        }
        return;
    }
    if (strcmp(token, "iret") == 0 || strcmp(token, "iretl") == 0) { sec_emit_byte(g_cur_section, 0xCF); return; }
    if (strcmp(token, "cli") == 0) { sec_emit_byte(g_cur_section, 0xFA); return; }
    if (strcmp(token, "sti") == 0) { sec_emit_byte(g_cur_section, 0xFB); return; }
    if (strcmp(token, "hlt") == 0) { sec_emit_byte(g_cur_section, 0xF4); return; }
    if (strcmp(token, "cld") == 0) { sec_emit_byte(g_cur_section, 0xFC); return; }
    if (strcmp(token, "std") == 0) { sec_emit_byte(g_cur_section, 0xFD); return; }
    if (strcmp(token, "movsb") == 0) { sec_emit_byte(g_cur_section, 0xA4); return; }
    if (strcmp(token, "movsw") == 0) { sec_emit_byte(g_cur_section, 0x66); sec_emit_byte(g_cur_section, 0xA5); return; }
    if (strcmp(token, "movsl") == 0 || strcmp(token, "movsd") == 0) { sec_emit_byte(g_cur_section, 0xA5); return; }
    if (strcmp(token, "stosb") == 0) { sec_emit_byte(g_cur_section, 0xAA); return; }
    if (strcmp(token, "stosw") == 0) { sec_emit_byte(g_cur_section, 0x66); sec_emit_byte(g_cur_section, 0xAB); return; }
    if (strcmp(token, "stosl") == 0 || strcmp(token, "stosd") == 0) { sec_emit_byte(g_cur_section, 0xAB); return; }
    if (strcmp(token, "rep") == 0 || strcmp(token, "repz") == 0 || strcmp(token, "repe") == 0) {
        sec_emit_byte(g_cur_section, 0xF3);
        if (*p) {
            assemble_line(p, line_idx, pass);
        }
        return;
    }
    if (strcmp(token, "repnz") == 0 || strcmp(token, "repne") == 0) {
        sec_emit_byte(g_cur_section, 0xF2);
        if (*p) {
            assemble_line(p, line_idx, pass);
        }
        return;
    }
    if (strcmp(token, "pusha") == 0 || strcmp(token, "pushal") == 0) { sec_emit_byte(g_cur_section, 0x60); return; }
    if (strcmp(token, "popa") == 0 || strcmp(token, "popal") == 0) { sec_emit_byte(g_cur_section, 0x61); return; }
    if (strcmp(token, "pushfl") == 0 || strcmp(token, "pushf") == 0) { sec_emit_byte(g_cur_section, 0x9C); return; }
    if (strcmp(token, "popfl") == 0 || strcmp(token, "popf") == 0) { sec_emit_byte(g_cur_section, 0x9D); return; }
    if (strcmp(token, "cltd") == 0 || strcmp(token, "cdq") == 0) { sec_emit_byte(g_cur_section, 0x99); return; }
    if (strcmp(token, "cpuid") == 0) { sec_emit_byte(g_cur_section, 0x0F); sec_emit_byte(g_cur_section, 0xA2); return; }
    if (strcmp(token, "rdmsr") == 0) { sec_emit_byte(g_cur_section, 0x0F); sec_emit_byte(g_cur_section, 0x32); return; }
    if (strcmp(token, "wrmsr") == 0) { sec_emit_byte(g_cur_section, 0x0F); sec_emit_byte(g_cur_section, 0x30); return; }

    /* Single operand: push / pop */
    if ((strcmp(token, "pushl") == 0 || strcmp(token, "push") == 0) && op_count == 1) {
        if (ops[0].kind == OP_REG && ops[0].reg_type == REG_R32) {
            sec_emit_byte(g_cur_section, (uint8_t)(0x50 + ops[0].reg_num));
        } else if (ops[0].kind == OP_IMM) {
            if (ops[0].has_sym) {
                int s_idx = find_or_add_symbol(ops[0].sym_name);
                sec_emit_byte(g_cur_section, 0x68);
                if (pass == 2) {
                    add_relocation(g_cur_section, (uint32_t)g_sections[g_cur_section].size, s_idx, R_386_32);
                }
                sec_emit_dword(g_cur_section, (uint32_t)ops[0].imm_val);
            } else if (ops[0].imm_val >= -128 && ops[0].imm_val <= 127) {
                sec_emit_byte(g_cur_section, 0x6A);
                sec_emit_byte(g_cur_section, (uint8_t)ops[0].imm_val);
            } else {
                sec_emit_byte(g_cur_section, 0x68);
                sec_emit_dword(g_cur_section, (uint32_t)ops[0].imm_val);
            }
        } else {
            sec_emit_byte(g_cur_section, 0xFF);
            emit_modrm_sib(g_cur_section, 6, &ops[0], pass);
        }
        return;
    }

    if ((strcmp(token, "popl") == 0 || strcmp(token, "pop") == 0) && op_count == 1) {
        if (ops[0].kind == OP_REG && ops[0].reg_type == REG_R32) {
            sec_emit_byte(g_cur_section, (uint8_t)(0x58 + ops[0].reg_num));
        } else {
            sec_emit_byte(g_cur_section, 0x8F);
            emit_modrm_sib(g_cur_section, 0, &ops[0], pass);
        }
        return;
    }

    /* Single operand: int */
    if (strcmp(token, "int") == 0 && op_count == 1) {
        const char *ep = raw_ops[0];
        uint32_t num = (uint32_t)eval_expr(&ep);
        if (num == 3) {
            sec_emit_byte(g_cur_section, 0xCC);
        } else {
            sec_emit_byte(g_cur_section, 0xCD);
            sec_emit_byte(g_cur_section, (uint8_t)num);
        }
        return;
    }

    /* Control flow: call / calll */
    if ((strcmp(token, "call") == 0 || strcmp(token, "calll") == 0) && op_count == 1) {
        if (raw_ops[0][0] == '*') {
            operand_t ind_op;
            parse_operand(raw_ops[0] + 1, &ind_op);
            sec_emit_byte(g_cur_section, 0xFF);
            emit_modrm_sib(g_cur_section, 2, &ind_op, pass);
        } else {
            int s_idx = find_or_add_symbol(raw_ops[0]);
            sec_emit_byte(g_cur_section, 0xE8);
            if (pass == 2) {
                add_relocation(g_cur_section, (uint32_t)g_sections[g_cur_section].size, s_idx, R_386_PC32);
            }
            sec_emit_dword(g_cur_section, (uint32_t)-4);
        }
        return;
    }

    /* Far jump: ljmp */
    if (strcmp(token, "ljmp") == 0 && op_count == 2) {
        const char *ep1 = raw_ops[0] + (raw_ops[0][0] == '$' ? 1 : 0);
        const char *label = raw_ops[1] + (raw_ops[1][0] == '$' ? 1 : 0);
        uint16_t seg = (uint16_t)eval_expr(&ep1);
        int s_idx = find_or_add_symbol(label);

        sec_emit_byte(g_cur_section, 0xEA);
        if (pass == 2) {
            add_relocation(g_cur_section, (uint32_t)g_sections[g_cur_section].size, s_idx, R_386_32);
        }
        sec_emit_dword(g_cur_section, 0);
        sec_emit_word(g_cur_section, seg);
        return;
    }

    /* Unconditional Jump: jmp / jmpl */
    if ((strcmp(token, "jmp") == 0 || strcmp(token, "jmpl") == 0) && op_count == 1) {
        if (raw_ops[0][0] == '*') {
            operand_t ind_op;
            parse_operand(raw_ops[0] + 1, &ind_op);
            sec_emit_byte(g_cur_section, 0xFF);
            emit_modrm_sib(g_cur_section, 4, &ind_op, pass);
            return;
        }

        /* Check for local label */
        {
            int is_local = 0;
            char target_lbl[64];
            int is_back = 0;
            int32_t target_off = -1;
            int k;

            if (isdigit((unsigned char)raw_ops[0][0]) && (raw_ops[0][1] == 'b' || raw_ops[0][1] == 'f')) {
                is_local = 1;
                sprintf(target_lbl, "%c", raw_ops[0][0]);
                is_back = (raw_ops[0][1] == 'b');
            } else if (strncmp(raw_ops[0], ".L", 2) == 0) {
                is_local = 2;
                strcpy(target_lbl, raw_ops[0]);
            }

            if (is_local == 1) {
                if (is_back) {
                    int max_l = -1;
                    for (k = 0; k < g_local_label_count; k++) {
                        if (strcmp(g_local_labels[k].name, target_lbl) == 0 &&
                            g_local_labels[k].section_idx == g_cur_section &&
                            g_local_labels[k].line_idx < line_idx && g_local_labels[k].line_idx > max_l) {
                            max_l = g_local_labels[k].line_idx;
                            target_off = (int32_t)g_local_labels[k].offset;
                        }
                    }
                } else {
                    int min_l = 999999;
                    for (k = 0; k < g_local_label_count; k++) {
                        if (strcmp(g_local_labels[k].name, target_lbl) == 0 &&
                            g_local_labels[k].section_idx == g_cur_section &&
                            g_local_labels[k].line_idx > line_idx && g_local_labels[k].line_idx < min_l) {
                            min_l = g_local_labels[k].line_idx;
                            target_off = (int32_t)g_local_labels[k].offset;
                        }
                    }
                }

                if (target_off >= 0) {
                    int32_t disp = target_off - ((int32_t)g_sections[g_cur_section].size + 2);
                    sec_emit_byte(g_cur_section, 0xEB);
                    sec_emit_byte(g_cur_section, (uint8_t)disp);
                } else {
                    sec_emit_byte(g_cur_section, 0xEB);
                    sec_emit_byte(g_cur_section, 0x00);
                }
                return;
            } else {
                int s_idx = find_or_add_symbol(raw_ops[0]);
                sec_emit_byte(g_cur_section, 0xE9);
                if (pass == 2) {
                    add_relocation(g_cur_section, (uint32_t)g_sections[g_cur_section].size, s_idx, R_386_PC32);
                }
                sec_emit_dword(g_cur_section, (uint32_t)-4);
                return;
            }
        }
    }

    /* Conditional Jumps (jcc) */
    {
        static const struct {
            const char *name;
            uint8_t op_short;
            uint8_t op_long;
        } jcc_table[] = {
            { "je",   0x74, 0x84 }, { "jz",   0x74, 0x84 },
            { "jne",  0x75, 0x85 }, { "jnz",  0x75, 0x85 },
            { "jb",   0x72, 0x82 }, { "jc",   0x72, 0x82 }, { "jnae", 0x72, 0x82 },
            { "jae",  0x73, 0x83 }, { "jnc",  0x73, 0x83 }, { "jnb",  0x73, 0x83 },
            { "jbe",  0x76, 0x86 }, { "jna",  0x76, 0x86 },
            { "ja",   0x77, 0x87 }, { "jnbe", 0x77, 0x87 },
            { "jl",   0x7C, 0x8C }, { "jnge", 0x7C, 0x8C },
            { "jge",  0x7D, 0x8D }, { "jnl",  0x7D, 0x8D },
            { "jle",  0x7E, 0x8E }, { "jng",  0x7E, 0x8E },
            { "jg",   0x7F, 0x8F }, { "jnle", 0x7F, 0x8F },
            { "js",   0x78, 0x88 },
            { "jns",  0x79, 0x89 }
        };
        int j;
        for (j = 0; j < (int)(sizeof(jcc_table) / sizeof(jcc_table[0])); j++) {
            if (strcmp(token, jcc_table[j].name) == 0 && op_count == 1) {
                if (isdigit((unsigned char)raw_ops[0][0]) && (raw_ops[0][1] == 'b' || raw_ops[0][1] == 'f')) {
                    int is_back = (raw_ops[0][1] == 'b');
                    char target_lbl[64];
                    int32_t target_off = -1;
                    int k;
                    sprintf(target_lbl, "%c", raw_ops[0][0]);

                    if (is_back) {
                        int max_l = -1;
                        for (k = 0; k < g_local_label_count; k++) {
                            if (strcmp(g_local_labels[k].name, target_lbl) == 0 &&
                                g_local_labels[k].section_idx == g_cur_section &&
                                g_local_labels[k].line_idx < line_idx && g_local_labels[k].line_idx > max_l) {
                                max_l = g_local_labels[k].line_idx;
                                target_off = (int32_t)g_local_labels[k].offset;
                            }
                        }
                    } else {
                        int min_l = 999999;
                        for (k = 0; k < g_local_label_count; k++) {
                            if (strcmp(g_local_labels[k].name, target_lbl) == 0 &&
                                g_local_labels[k].section_idx == g_cur_section &&
                                g_local_labels[k].line_idx > line_idx && g_local_labels[k].line_idx < min_l) {
                                min_l = g_local_labels[k].line_idx;
                                target_off = (int32_t)g_local_labels[k].offset;
                            }
                        }
                    }

                    if (target_off >= 0) {
                        int32_t disp = target_off - ((int32_t)g_sections[g_cur_section].size + 2);
                        sec_emit_byte(g_cur_section, jcc_table[j].op_short);
                        sec_emit_byte(g_cur_section, (uint8_t)disp);
                    } else {
                        sec_emit_byte(g_cur_section, jcc_table[j].op_short);
                        sec_emit_byte(g_cur_section, 0x00);
                    }
                } else {
                    int s_idx = find_or_add_symbol(raw_ops[0]);
                    sec_emit_byte(g_cur_section, 0x0F);
                    sec_emit_byte(g_cur_section, jcc_table[j].op_long);
                    if (pass == 2) {
                        add_relocation(g_cur_section, (uint32_t)g_sections[g_cur_section].size, s_idx, R_386_PC32);
                    }
                    sec_emit_dword(g_cur_section, (uint32_t)-4);
                }
                return;
            }
        }
    }

    /* Setcc instructions */
    {
        static const struct {
            const char *name;
            uint8_t opcode;
        } setcc_table[] = {
            { "sete",  0x94 }, { "setz",  0x94 },
            { "setne", 0x95 }, { "setnz", 0x95 },
            { "setb",  0x92 }, { "setc",  0x92 }, { "setnae", 0x92 },
            { "setae", 0x93 }, { "setnc", 0x93 }, { "setnb",  0x93 },
            { "setbe", 0x96 }, { "setna", 0x96 },
            { "seta",  0x97 }, { "setnbe",0x97 },
            { "setl",  0x9C }, { "setnge",0x9C },
            { "setge", 0x9D }, { "setnl", 0x9D },
            { "setle", 0x9E }, { "setng", 0x9E },
            { "setg",  0x9F }, { "setnle",0x9F }
        };
        int s;
        for (s = 0; s < (int)(sizeof(setcc_table) / sizeof(setcc_table[0])); s++) {
            if (strcmp(token, setcc_table[s].name) == 0 && op_count == 1) {
                sec_emit_byte(g_cur_section, 0x0F);
                sec_emit_byte(g_cur_section, setcc_table[s].opcode);
                emit_modrm_sib(g_cur_section, 0, &ops[0], pass);
                return;
            }
        }
    }

    /* CMOVcc instructions */
    if (strncmp(token, "cmov", 4) == 0 && op_count == 2) {
        uint8_t op_byte = 0;
        if (strcmp(token, "cmovel") == 0 || strcmp(token, "cmovzl") == 0) op_byte = 0x44;
        else if (strcmp(token, "cmovnel") == 0 || strcmp(token, "cmovnzl") == 0) op_byte = 0x45;
        else if (strcmp(token, "cmovbl") == 0 || strcmp(token, "cmovcl") == 0) op_byte = 0x42;
        else if (strcmp(token, "cmovael") == 0 || strcmp(token, "cmovncl") == 0) op_byte = 0x43;
        else if (strcmp(token, "cmovbel") == 0) op_byte = 0x46;
        else if (strcmp(token, "cmoval") == 0) op_byte = 0x47;
        else if (strcmp(token, "cmovll") == 0) op_byte = 0x4C;
        else if (strcmp(token, "cmovgel") == 0) op_byte = 0x4D;
        else if (strcmp(token, "cmovlel") == 0) op_byte = 0x4E;
        else if (strcmp(token, "cmovgl") == 0) op_byte = 0x4F;

        if (op_byte && ops[1].kind == OP_REG) {
            sec_emit_byte(g_cur_section, 0x0F);
            sec_emit_byte(g_cur_section, op_byte);
            emit_modrm_sib(g_cur_section, ops[1].reg_num, &ops[0], pass);
            return;
        }
    }

    /* LEA: leal */
    if (strcmp(token, "leal") == 0 && op_count == 2 && ops[1].kind == OP_REG) {
        sec_emit_byte(g_cur_section, 0x8D);
        emit_modrm_sib(g_cur_section, ops[1].reg_num, &ops[0], pass);
        return;
    }

    /* MOVSX / MOVZX */
    if (strcmp(token, "movzbl") == 0 && op_count == 2 && ops[1].kind == OP_REG) {
        sec_emit_byte(g_cur_section, 0x0F);
        sec_emit_byte(g_cur_section, 0xB6);
        emit_modrm_sib(g_cur_section, ops[1].reg_num, &ops[0], pass);
        return;
    }
    if (strcmp(token, "movsbl") == 0 && op_count == 2 && ops[1].kind == OP_REG) {
        sec_emit_byte(g_cur_section, 0x0F);
        sec_emit_byte(g_cur_section, 0xBE);
        emit_modrm_sib(g_cur_section, ops[1].reg_num, &ops[0], pass);
        return;
    }
    if (strcmp(token, "movzwl") == 0 && op_count == 2 && ops[1].kind == OP_REG) {
        sec_emit_byte(g_cur_section, 0x0F);
        sec_emit_byte(g_cur_section, 0xB7);
        emit_modrm_sib(g_cur_section, ops[1].reg_num, &ops[0], pass);
        return;
    }

    /* MOV: movb, movw, movl */
    if ((strcmp(token, "movb") == 0 || strcmp(token, "movw") == 0 || strcmp(token, "movl") == 0 || strcmp(token, "mov") == 0) && op_count == 2) {
        int is_byte = (strcmp(token, "movb") == 0 || (ops[0].reg_type == REG_R8 || ops[1].reg_type == REG_R8));
        int is_word = (strcmp(token, "movw") == 0 || (ops[0].reg_type == REG_R16 || ops[1].reg_type == REG_R16));

        /* Segment register moves */
        if (ops[0].reg_type == REG_SREG && ops[1].kind == OP_REG) {
            sec_emit_byte(g_cur_section, 0x8C);
            sec_emit_byte(g_cur_section, (uint8_t)(0xC0 | (ops[0].reg_num << 3) | ops[1].reg_num));
            return;
        }
        if (ops[0].kind == OP_REG && ops[1].reg_type == REG_SREG) {
            sec_emit_byte(g_cur_section, 0x8E);
            sec_emit_byte(g_cur_section, (uint8_t)(0xC0 | (ops[1].reg_num << 3) | ops[0].reg_num));
            return;
        }

        if (is_word) sec_emit_byte(g_cur_section, 0x66);

        if (ops[0].kind == OP_IMM) {
            if (ops[1].kind == OP_REG) {
                if (is_byte) {
                    sec_emit_byte(g_cur_section, (uint8_t)(0xB0 + ops[1].reg_num));
                    sec_emit_byte(g_cur_section, (uint8_t)ops[0].imm_val);
                } else if (is_word) {
                    sec_emit_byte(g_cur_section, (uint8_t)(0xB8 + ops[1].reg_num));
                    sec_emit_word(g_cur_section, (uint16_t)ops[0].imm_val);
                } else {
                    sec_emit_byte(g_cur_section, (uint8_t)(0xB8 + ops[1].reg_num));
                    if (ops[0].has_sym) {
                        int s_idx = find_or_add_symbol(ops[0].sym_name);
                        if (pass == 2) {
                            add_relocation(g_cur_section, (uint32_t)g_sections[g_cur_section].size, s_idx, R_386_32);
                        }
                        sec_emit_dword(g_cur_section, (uint32_t)ops[0].imm_val);
                    } else {
                        sec_emit_dword(g_cur_section, (uint32_t)ops[0].imm_val);
                    }
                }
            } else {
                /* mov $imm, r/m */
                sec_emit_byte(g_cur_section, is_byte ? 0xC6 : 0xC7);
                emit_modrm_sib(g_cur_section, 0, &ops[1], pass);
                if (is_byte) sec_emit_byte(g_cur_section, (uint8_t)ops[0].imm_val);
                else if (is_word) sec_emit_word(g_cur_section, (uint16_t)ops[0].imm_val);
                else {
                    if (ops[0].has_sym) {
                        int s_idx = find_or_add_symbol(ops[0].sym_name);
                        if (pass == 2) {
                            add_relocation(g_cur_section, (uint32_t)g_sections[g_cur_section].size, s_idx, R_386_32);
                        }
                        sec_emit_dword(g_cur_section, (uint32_t)ops[0].imm_val);
                    } else {
                        sec_emit_dword(g_cur_section, (uint32_t)ops[0].imm_val);
                    }
                }
            }
        } else if (ops[0].kind == OP_REG) {
            /* mov %reg, r/m */
            sec_emit_byte(g_cur_section, is_byte ? 0x88 : 0x89);
            emit_modrm_sib(g_cur_section, ops[0].reg_num, &ops[1], pass);
        } else if (ops[1].kind == OP_REG) {
            /* mov r/m, %reg */
            sec_emit_byte(g_cur_section, is_byte ? 0x8A : 0x8B);
            emit_modrm_sib(g_cur_section, ops[1].reg_num, &ops[0], pass);
        }
        return;
    }

    /* Binary ALU Operations: add, sub, and, or, xor, cmp, sbb, adc */
    {
        static const struct {
            const char *b_name;
            const char *w_name;
            const char *l_name;
            int op_num;
        } alu_ops[] = {
            { "addb", "addw", "addl", 0 },
            { "orb",  "orw",  "orl",  1 },
            { "adcb", "adcw", "adcl", 2 },
            { "sbbb", "sbbw", "sbbl", 3 },
            { "andb", "andw", "andl", 4 },
            { "subb", "subw", "subl", 5 },
            { "xorb", "xorw", "xorl", 6 },
            { "cmpb", "cmpw", "cmpl", 7 }
        };
        int a;
        for (a = 0; a < 8; a++) {
            int is_b = (strcmp(token, alu_ops[a].b_name) == 0);
            int is_w = (strcmp(token, alu_ops[a].w_name) == 0);
            int is_l = (strcmp(token, alu_ops[a].l_name) == 0);

            if ((is_b || is_w || is_l) && op_count == 2) {
                int op_code = alu_ops[a].op_num;
                if (is_w) sec_emit_byte(g_cur_section, 0x66);

                if (ops[0].kind == OP_IMM) {
                    if (is_b) {
                        sec_emit_byte(g_cur_section, 0x80);
                        emit_modrm_sib(g_cur_section, op_code, &ops[1], pass);
                        sec_emit_byte(g_cur_section, (uint8_t)ops[0].imm_val);
                    } else if (is_w) {
                        if (ops[0].imm_val >= -128 && ops[0].imm_val <= 127 && !ops[0].has_sym) {
                            sec_emit_byte(g_cur_section, 0x83);
                            emit_modrm_sib(g_cur_section, op_code, &ops[1], pass);
                            sec_emit_byte(g_cur_section, (uint8_t)ops[0].imm_val);
                        } else {
                            sec_emit_byte(g_cur_section, 0x81);
                            emit_modrm_sib(g_cur_section, op_code, &ops[1], pass);
                            sec_emit_word(g_cur_section, (uint16_t)ops[0].imm_val);
                        }
                    } else {
                        if (ops[0].imm_val >= -128 && ops[0].imm_val <= 127 && !ops[0].has_sym) {
                            sec_emit_byte(g_cur_section, 0x83);
                            emit_modrm_sib(g_cur_section, op_code, &ops[1], pass);
                            sec_emit_byte(g_cur_section, (uint8_t)ops[0].imm_val);
                        } else {
                            sec_emit_byte(g_cur_section, 0x81);
                            emit_modrm_sib(g_cur_section, op_code, &ops[1], pass);
                            if (ops[0].has_sym) {
                                int s_idx = find_or_add_symbol(ops[0].sym_name);
                                if (pass == 2) {
                                    add_relocation(g_cur_section, (uint32_t)g_sections[g_cur_section].size, s_idx, R_386_32);
                                }
                                sec_emit_dword(g_cur_section, (uint32_t)ops[0].imm_val);
                            } else {
                                sec_emit_dword(g_cur_section, (uint32_t)ops[0].imm_val);
                            }
                        }
                    }
                } else if (ops[0].kind == OP_REG) {
                    /* op %reg, r/m */
                    sec_emit_byte(g_cur_section, (uint8_t)((op_code << 3) | (is_b ? 0x00 : 0x01)));
                    emit_modrm_sib(g_cur_section, ops[0].reg_num, &ops[1], pass);
                } else if (ops[1].kind == OP_REG) {
                    /* op r/m, %reg */
                    sec_emit_byte(g_cur_section, (uint8_t)((op_code << 3) | (is_b ? 0x02 : 0x03)));
                    emit_modrm_sib(g_cur_section, ops[1].reg_num, &ops[0], pass);
                }
                return;
            }
        }
    }

    /* TEST: testb, testl */
    if ((strcmp(token, "testb") == 0 || strcmp(token, "testl") == 0) && op_count == 2) {
        int is_b = (strcmp(token, "testb") == 0);
        if (ops[0].kind == OP_IMM) {
            sec_emit_byte(g_cur_section, is_b ? 0xF6 : 0xF7);
            emit_modrm_sib(g_cur_section, 0, &ops[1], pass);
            if (is_b) sec_emit_byte(g_cur_section, (uint8_t)ops[0].imm_val);
            else sec_emit_dword(g_cur_section, (uint32_t)ops[0].imm_val);
        } else if (ops[0].kind == OP_REG) {
            sec_emit_byte(g_cur_section, is_b ? 0x84 : 0x85);
            emit_modrm_sib(g_cur_section, ops[0].reg_num, &ops[1], pass);
        }
        return;
    }

    /* IMUL: imull */
    if (strcmp(token, "imull") == 0) {
        if (op_count == 2 && ops[0].kind == OP_IMM && ops[1].kind == OP_REG) {
            if (ops[0].imm_val >= -128 && ops[0].imm_val <= 127 && !ops[0].has_sym) {
                sec_emit_byte(g_cur_section, 0x6B);
                emit_modrm_sib(g_cur_section, ops[1].reg_num, &ops[1], pass);
                sec_emit_byte(g_cur_section, (uint8_t)ops[0].imm_val);
            } else {
                sec_emit_byte(g_cur_section, 0x69);
                emit_modrm_sib(g_cur_section, ops[1].reg_num, &ops[1], pass);
                sec_emit_dword(g_cur_section, (uint32_t)ops[0].imm_val);
            }
            return;
        } else if (op_count == 2 && ops[1].kind == OP_REG) {
            sec_emit_byte(g_cur_section, 0x0F);
            sec_emit_byte(g_cur_section, 0xAF);
            emit_modrm_sib(g_cur_section, ops[1].reg_num, &ops[0], pass);
            return;
        } else if (op_count == 3 && ops[0].kind == OP_IMM && ops[2].kind == OP_REG) {
            if (ops[0].imm_val >= -128 && ops[0].imm_val <= 127 && !ops[0].has_sym) {
                sec_emit_byte(g_cur_section, 0x6B);
                emit_modrm_sib(g_cur_section, ops[2].reg_num, &ops[1], pass);
                sec_emit_byte(g_cur_section, (uint8_t)ops[0].imm_val);
            } else {
                sec_emit_byte(g_cur_section, 0x69);
                emit_modrm_sib(g_cur_section, ops[2].reg_num, &ops[1], pass);
                sec_emit_dword(g_cur_section, (uint32_t)ops[0].imm_val);
            }
            return;
        }
    }

    /* Unary Math: incl, decl, negl, divl, idivl, mull */
    if (strcmp(token, "incl") == 0 && op_count == 1) {
        if (ops[0].kind == OP_REG) sec_emit_byte(g_cur_section, (uint8_t)(0x40 + ops[0].reg_num));
        else { sec_emit_byte(g_cur_section, 0xFF); emit_modrm_sib(g_cur_section, 0, &ops[0], pass); }
        return;
    }
    if (strcmp(token, "decl") == 0 && op_count == 1) {
        if (ops[0].kind == OP_REG) sec_emit_byte(g_cur_section, (uint8_t)(0x48 + ops[0].reg_num));
        else { sec_emit_byte(g_cur_section, 0xFF); emit_modrm_sib(g_cur_section, 1, &ops[0], pass); }
        return;
    }
    if ((strcmp(token, "notl") == 0 || strcmp(token, "not") == 0) && op_count == 1) {
        sec_emit_byte(g_cur_section, 0xF7);
        emit_modrm_sib(g_cur_section, 2, &ops[0], pass);
        return;
    }
    if (strcmp(token, "notb") == 0 && op_count == 1) {
        sec_emit_byte(g_cur_section, 0xF6);
        emit_modrm_sib(g_cur_section, 2, &ops[0], pass);
        return;
    }
    if (strcmp(token, "notw") == 0 && op_count == 1) {
        sec_emit_byte(g_cur_section, 0x66);
        sec_emit_byte(g_cur_section, 0xF7);
        emit_modrm_sib(g_cur_section, 2, &ops[0], pass);
        return;
    }
    if ((strcmp(token, "negl") == 0 || strcmp(token, "neg") == 0) && op_count == 1) {
        sec_emit_byte(g_cur_section, 0xF7);
        emit_modrm_sib(g_cur_section, 3, &ops[0], pass);
        return;
    }
    if (strcmp(token, "negb") == 0 && op_count == 1) {
        sec_emit_byte(g_cur_section, 0xF6);
        emit_modrm_sib(g_cur_section, 3, &ops[0], pass);
        return;
    }
    if (strcmp(token, "negw") == 0 && op_count == 1) {
        sec_emit_byte(g_cur_section, 0x66);
        sec_emit_byte(g_cur_section, 0xF7);
        emit_modrm_sib(g_cur_section, 3, &ops[0], pass);
        return;
    }
    if (strcmp(token, "divl") == 0 && op_count == 1) {
        sec_emit_byte(g_cur_section, 0xF7);
        emit_modrm_sib(g_cur_section, 6, &ops[0], pass);
        return;
    }
    if (strcmp(token, "idivl") == 0 && op_count == 1) {
        sec_emit_byte(g_cur_section, 0xF7);
        emit_modrm_sib(g_cur_section, 7, &ops[0], pass);
        return;
    }
    if (strcmp(token, "mull") == 0 && op_count == 1) {
        sec_emit_byte(g_cur_section, 0xF7);
        emit_modrm_sib(g_cur_section, 4, &ops[0], pass);
        return;
    }

    /* Shifts: shll, shrl, sarl */
    if ((strcmp(token, "shll") == 0 || strcmp(token, "sall") == 0 ||
         strcmp(token, "shrl") == 0 || strcmp(token, "sarl") == 0) && op_count >= 1) {
        int digit = 4;
        if (strcmp(token, "shrl") == 0) digit = 5;
        if (strcmp(token, "sarl") == 0) digit = 7;

        if (op_count == 1) {
            /* Implicit $1 */
            sec_emit_byte(g_cur_section, 0xD1);
            emit_modrm_sib(g_cur_section, digit, &ops[0], pass);
        } else if (ops[0].kind == OP_REG && ops[0].reg_num == 1 /* %cl */) {
            sec_emit_byte(g_cur_section, 0xD3);
            emit_modrm_sib(g_cur_section, digit, &ops[1], pass);
        } else if (ops[0].kind == OP_IMM) {
            if (ops[0].imm_val == 1) {
                sec_emit_byte(g_cur_section, 0xD1);
                emit_modrm_sib(g_cur_section, digit, &ops[1], pass);
            } else {
                sec_emit_byte(g_cur_section, 0xC1);
                emit_modrm_sib(g_cur_section, digit, &ops[1], pass);
                sec_emit_byte(g_cur_section, (uint8_t)ops[0].imm_val);
            }
        }
        return;
    }

    /* Double Shift: shldl */
    if (strcmp(token, "shldl") == 0 && op_count >= 2) {
        if (op_count == 3 && ops[0].kind == OP_IMM && ops[1].kind == OP_REG) {
            sec_emit_byte(g_cur_section, 0x0F);
            sec_emit_byte(g_cur_section, 0xA4);
            emit_modrm_sib(g_cur_section, ops[1].reg_num, &ops[2], pass);
            sec_emit_byte(g_cur_section, (uint8_t)ops[0].imm_val);
            return;
        } else if (op_count == 2 && ops[0].kind == OP_REG) {
            sec_emit_byte(g_cur_section, 0x0F);
            sec_emit_byte(g_cur_section, 0xA5);
            emit_modrm_sib(g_cur_section, ops[0].reg_num, &ops[1], pass);
            return;
        }
    }

    /* I/O Port Instructions */
    if (strcmp(token, "inb") == 0 && op_count == 2) {
        if (strcmp(raw_ops[0], "%dx") == 0) sec_emit_byte(g_cur_section, 0xEC);
        else {
            const char *ep = raw_ops[0] + (raw_ops[0][0] == '$' ? 1 : 0);
            sec_emit_byte(g_cur_section, 0xE4);
            sec_emit_byte(g_cur_section, (uint8_t)eval_expr(&ep));
        }
        return;
    }
    if (strcmp(token, "outb") == 0 && op_count == 2) {
        if (strcmp(raw_ops[1], "%dx") == 0) sec_emit_byte(g_cur_section, 0xEE);
        else {
            const char *ep = raw_ops[1] + (raw_ops[1][0] == '$' ? 1 : 0);
            sec_emit_byte(g_cur_section, 0xE6);
            sec_emit_byte(g_cur_section, (uint8_t)eval_expr(&ep));
        }
        return;
    }
    if (strcmp(token, "inw") == 0 && op_count == 2) {
        sec_emit_byte(g_cur_section, 0x66);
        sec_emit_byte(g_cur_section, 0xED);
        return;
    }
    if (strcmp(token, "outw") == 0 && op_count == 2) {
        sec_emit_byte(g_cur_section, 0x66);
        sec_emit_byte(g_cur_section, 0xEF);
        return;
    }
    if (strcmp(token, "inl") == 0 && op_count == 2) {
        sec_emit_byte(g_cur_section, 0xED);
        return;
    }
    if (strcmp(token, "outl") == 0 && op_count == 2) {
        sec_emit_byte(g_cur_section, 0xEF);
        return;
    }

    /* Descriptor tables */
    if (strcmp(token, "lgdt") == 0 && op_count == 1) {
        sec_emit_byte(g_cur_section, 0x0F);
        sec_emit_byte(g_cur_section, 0x01);
        emit_modrm_sib(g_cur_section, 2, &ops[0], pass);
        return;
    }
    if (strcmp(token, "lidt") == 0 && op_count == 1) {
        sec_emit_byte(g_cur_section, 0x0F);
        sec_emit_byte(g_cur_section, 0x01);
        emit_modrm_sib(g_cur_section, 3, &ops[0], pass);
        return;
    }

    fprintf(stderr, "Warning: Unhandled instruction '%s' (line %d)\n", token, line_idx);
}

static char *my_strdup(const char *s) {
    size_t len = strlen(s) + 1;
    char *p = (char*)malloc(len);
    if (p) memcpy(p, s, len);
    return p;
}

/* Macro Preprocessor & Source Expander */
static void preprocess_and_assemble(const char *src_path) {
    FILE *fp;
    char line[4096];
    char **expanded_lines = NULL;
    int line_capacity = 4096;
    int total_lines = 0;
    int in_macro = 0;
    int in_c_comment = 0;
    int pass;
    int l;

    expanded_lines = (char**)malloc(line_capacity * sizeof(char*));
    if (!expanded_lines) {
        fprintf(stderr, "Out of memory\n");
        exit(1);
    }

    fp = fopen(src_path, "r");
    if (!fp) {
        fprintf(stderr, "Error opening source file: %s\n", src_path);
        exit(1);
    }

    while (fgets(line, sizeof(line), fp)) {
        char *p;
        clean_string(line);
        p = (char*)skip_ws(line);

        if (in_c_comment) {
            char *end_c = strstr(line, "*/");
            if (end_c) {
                in_c_comment = 0;
            }
            continue;
        }
        if (strncmp(p, "/*", 2) == 0) {
            char *end_c = strstr(p, "*/");
            if (!end_c) {
                in_c_comment = 1;
            }
            continue;
        }
        if (!*p || *p == '#' || *p == ';' || (p[0] == '/' && p[1] == '/')) continue;
        if (strncmp(p, ".macro", 6) == 0) {
            char mname[64];
            char mparams[4][32];
            int pcount = 0;
            p += 6;
            p = (char*)skip_ws(p);
            sscanf(p, "%63s", mname);
            p += strlen(mname);
            p = (char*)skip_ws(p);
            while (*p && *p != '#' && *p != '\n' && *p != '\r') {
                int plen = 0;
                while (*p && *p != ',' && !isspace((unsigned char)*p)) {
                    if (plen < 31) mparams[pcount][plen++] = *p;
                    p++;
                }
                mparams[pcount][plen] = '\0';
                clean_string(mparams[pcount]);
                pcount++;
                if (*p == ',') p++;
                p = (char*)skip_ws(p);
            }

            clean_string(mname);
            strcpy(g_macros[g_macro_count].name, mname);
            g_macros[g_macro_count].param_count = pcount;
            for (l = 0; l < pcount; l++) {
                strcpy(g_macros[g_macro_count].params[l], mparams[l]);
            }
            g_macros[g_macro_count].line_count = 0;
            in_macro = 1;
            continue;
        }

        if (strncmp(p, ".endm", 5) == 0) {
            if (in_macro) {
                g_macro_count++;
                in_macro = 0;
            }
            continue;
        }

        if (in_macro) {
            macro_def_t *m = &g_macros[g_macro_count];
            if (m->line_count < MAX_MACRO_LINES) {
                strncpy(m->lines[m->line_count++], line, 255);
            }
            continue;
        }

        /* Check for macro call */
        {
            int is_mcall = 0;
            int m_idx;
            char first_token[64];
            const char *tp = p;
            int tlen = 0;
            while (*tp && !isspace((unsigned char)*tp)) {
                if (tlen < 63) first_token[tlen++] = *tp;
                tp++;
            }
            first_token[tlen] = '\0';
            clean_string(first_token);
            tp = skip_ws(tp);

            for (m_idx = 0; m_idx < g_macro_count; m_idx++) {
                if (strcmp(g_macros[m_idx].name, first_token) == 0) {
                    macro_def_t *m = &g_macros[m_idx];
                    char args[4][32];
                    int acount = 0;
                    int ml;

                    /* Parse arguments */
                    while (*tp && *tp != '#' && *tp != '\n' && *tp != '\r') {
                        int alen = 0;
                        while (*tp && *tp != ',' && !isspace((unsigned char)*tp)) {
                            if (alen < 31) args[acount][alen++] = *tp;
                            tp++;
                        }
                        args[acount][alen] = '\0';
                        clean_string(args[acount]);
                        acount++;
                        if (*tp == ',') tp++;
                        tp = skip_ws(tp);
                    }

                    /* Expand macro lines */
                    for (ml = 0; ml < m->line_count; ml++) {
                        char exp[512];
                        char *src_l = m->lines[ml];
                        int pi;
                        strcpy(exp, src_l);

                        for (pi = 0; pi < m->param_count && pi < acount; pi++) {
                            char param_ref[34];
                            char *pos;
                            sprintf(param_ref, "\\%s", m->params[pi]);

                            while ((pos = strstr(exp, param_ref)) != NULL) {
                                char temp[512];
                                size_t prefix_len = pos - exp;
                                memcpy(temp, exp, prefix_len);
                                temp[prefix_len] = '\0';
                                strcat(temp, args[pi]);
                                strcat(temp, pos + strlen(param_ref));
                                strcpy(exp, temp);
                            }
                        }

                        if (total_lines >= line_capacity) {
                            line_capacity *= 2;
                            expanded_lines = (char**)realloc(expanded_lines, line_capacity * sizeof(char*));
                        }
                        expanded_lines[total_lines++] = my_strdup(exp);
                    }
                    is_mcall = 1;
                    break;
                }
            }

            if (!is_mcall) {
                if (total_lines >= line_capacity) {
                    line_capacity *= 2;
                    expanded_lines = (char**)realloc(expanded_lines, line_capacity * sizeof(char*));
                }
                expanded_lines[total_lines++] = my_strdup(line);
            }
        }
    }
    fclose(fp);

    /* Two-Pass Assembly */
    for (pass = 1; pass <= 2; pass++) {
        g_cur_section = 1;
        for (l = 1; l <= g_section_count; l++) {
            g_sections[l].size = 0;
        }
        if (pass == 2) {
            g_reloc_count = 0;
        }

        for (l = 0; l < total_lines; l++) {
            assemble_line(expanded_lines[l], l, pass);
        }
    }

    for (l = 0; l < total_lines; l++) {
        free(expanded_lines[l]);
    }
    free(expanded_lines);
}

static void print_usage(const char *prog) {
    fprintf(stderr, "Usage: %s [options] <input.s> -o <output.o>\n", prog);
}

int main(int argc, char **argv) {
    const char *input_path = NULL;
    const char *output_path = NULL;
    int i;
    FILE *f_out;
    Elf32_Ehdr ehdr;
    Elf32_Shdr shdrs[MAX_SECTIONS * 2 + 4];
    int shdr_count = 0;
    char *shstrtab;
    size_t shstrtab_len = 1;
    char *strtab;
    size_t strtab_len = 1;
    Elf32_Sym *symtab;
    size_t symtab_count = 0;
    uint32_t cur_file_offset;
    uint32_t shstrtab_offset;
    uint32_t strtab_offset;
    uint32_t symtab_offset;
    int sym_reindex[MAX_SYMBOLS];
    int local_sym_count = 0;
    int sec_idx;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) {
            output_path = argv[++i];
        } else if (strcmp(argv[i], "-m32") == 0 || strcmp(argv[i], "-c") == 0) {
            /* Accepted flags */
        } else if (argv[i][0] == '-') {
            /* Ignore flags */
        } else {
            input_path = argv[i];
        }
    }

    if (!input_path || !output_path) {
        print_usage(argv[0]);
        return 1;
    }

    /* Initialize default .text section */
    g_section_count = 0;
    g_symbol_count = 0;
    g_reloc_count = 0;
    g_macro_count = 0;
    g_local_label_count = 0;

    get_or_create_section(".text", SHT_PROGBITS, SHF_ALLOC | SHF_EXECINSTR, 4);

    preprocess_and_assemble(input_path);

    /* Mark undefined symbols as STB_GLOBAL and SHN_UNDEF */
    for (i = 1; i <= g_symbol_count; i++) {
        if (!g_symbols[i].defined) {
            g_symbols[i].binding = STB_GLOBAL;
            g_symbols[i].section_idx = SHN_UNDEF;
            g_symbols[i].value = 0;
        }
    }

    /* Prepare String Tables */
    shstrtab = (char*)malloc(65536);
    shstrtab[0] = '\0';
    shstrtab_len = 1;

    strtab = (char*)malloc(131072);
    strtab[0] = '\0';
    strtab_len = 1;

    /* Build Symbol Table (Locals first, then Globals) */
    symtab = (Elf32_Sym*)calloc(MAX_SYMBOLS, sizeof(Elf32_Sym));
    symtab_count = 1; /* Symbol 0 is NULL */

    for (i = 1; i <= g_symbol_count; i++) {
        if (g_symbols[i].binding == STB_LOCAL) {
            sym_reindex[i] = (int)symtab_count;
            symtab[symtab_count].st_name = (uint32_t)strtab_len;
            strcpy(strtab + strtab_len, g_symbols[i].name);
            strtab_len += strlen(g_symbols[i].name) + 1;
            symtab[symtab_count].st_value = g_symbols[i].value;
            symtab[symtab_count].st_size = g_symbols[i].size;
            symtab[symtab_count].st_info = ELF32_ST_INFO(STB_LOCAL, g_symbols[i].type);
            symtab[symtab_count].st_shndx = (uint16_t)g_symbols[i].section_idx;
            symtab_count++;
            local_sym_count++;
        }
    }

    for (i = 1; i <= g_symbol_count; i++) {
        if (g_symbols[i].binding == STB_GLOBAL) {
            sym_reindex[i] = (int)symtab_count;
            symtab[symtab_count].st_name = (uint32_t)strtab_len;
            strcpy(strtab + strtab_len, g_symbols[i].name);
            strtab_len += strlen(g_symbols[i].name) + 1;
            symtab[symtab_count].st_value = g_symbols[i].value;
            symtab[symtab_count].st_size = g_symbols[i].size;
            symtab[symtab_count].st_info = ELF32_ST_INFO(STB_GLOBAL, g_symbols[i].type);
            symtab[symtab_count].st_shndx = (uint16_t)g_symbols[i].section_idx;
            symtab_count++;
        }
    }

    /* Layout Sections and Calculate Offsets */
    cur_file_offset = sizeof(Elf32_Ehdr);

    /* Section 0: NULL */
    memset(&shdrs[0], 0, sizeof(Elf32_Shdr));
    shdr_count = 1;

    for (sec_idx = 1; sec_idx <= g_section_count; sec_idx++) {
        section_t *sec = &g_sections[sec_idx];
        Elf32_Shdr *shdr = &shdrs[shdr_count++];
        memset(shdr, 0, sizeof(Elf32_Shdr));

        shdr->sh_name = (uint32_t)shstrtab_len;
        strcpy(shstrtab + shstrtab_len, sec->name);
        shstrtab_len += strlen(sec->name) + 1;

        shdr->sh_type = sec->type;
        shdr->sh_flags = sec->flags;
        shdr->sh_addr = 0;
        shdr->sh_addralign = sec->align ? sec->align : 4;
        shdr->sh_entsize = 0;

        if (sec->type != SHT_NOBITS && sec->size > 0) {
            cur_file_offset = ALIGN_UP(cur_file_offset, shdr->sh_addralign);
            shdr->sh_offset = cur_file_offset;
            shdr->sh_size = (uint32_t)sec->size;
            cur_file_offset += sec->size;
        } else {
            shdr->sh_offset = cur_file_offset;
            shdr->sh_size = (uint32_t)sec->size;
        }
    }

    /* Relocation Sections */
    for (sec_idx = 1; sec_idx <= g_section_count; sec_idx++) {
        int r_count = 0;
        int r;
        for (r = 0; r < g_reloc_count; r++) {
            if (g_relocs[r].section_idx == sec_idx) r_count++;
        }

        if (r_count > 0) {
            Elf32_Shdr *shdr = &shdrs[shdr_count++];
            char rel_name[64];
            memset(shdr, 0, sizeof(Elf32_Shdr));
            sprintf(rel_name, ".rel%s", g_sections[sec_idx].name);

            shdr->sh_name = (uint32_t)shstrtab_len;
            strcpy(shstrtab + shstrtab_len, rel_name);
            shstrtab_len += strlen(rel_name) + 1;

            shdr->sh_type = SHT_REL;
            shdr->sh_flags = 0;
            shdr->sh_addr = 0;
            shdr->sh_addralign = 4;
            shdr->sh_entsize = sizeof(Elf32_Rel);
            shdr->sh_info = sec_idx;

            cur_file_offset = ALIGN_UP(cur_file_offset, 4);
            shdr->sh_offset = cur_file_offset;
            shdr->sh_size = (uint32_t)(r_count * sizeof(Elf32_Rel));
            cur_file_offset += shdr->sh_size;
        }
    }

    /* .symtab section */
    cur_file_offset = ALIGN_UP(cur_file_offset, 4);
    symtab_offset = cur_file_offset;
    {
        Elf32_Shdr *shdr = &shdrs[shdr_count++];
        memset(shdr, 0, sizeof(Elf32_Shdr));
        shdr->sh_name = (uint32_t)shstrtab_len;
        strcpy(shstrtab + shstrtab_len, ".symtab");
        shstrtab_len += 8;

        shdr->sh_type = SHT_SYMTAB;
        shdr->sh_flags = 0;
        shdr->sh_addr = 0;
        shdr->sh_offset = symtab_offset;
        shdr->sh_size = (uint32_t)(symtab_count * sizeof(Elf32_Sym));
        shdr->sh_link = shdr_count; /* Next section is .strtab */
        shdr->sh_info = (uint32_t)(local_sym_count + 1);
        shdr->sh_addralign = 4;
        shdr->sh_entsize = sizeof(Elf32_Sym);
        cur_file_offset += shdr->sh_size;
    }

    /* .strtab section */
    strtab_offset = cur_file_offset;
    {
        Elf32_Shdr *shdr = &shdrs[shdr_count++];
        memset(shdr, 0, sizeof(Elf32_Shdr));
        shdr->sh_name = (uint32_t)shstrtab_len;
        strcpy(shstrtab + shstrtab_len, ".strtab");
        shstrtab_len += 8;

        shdr->sh_type = SHT_STRTAB;
        shdr->sh_flags = 0;
        shdr->sh_addr = 0;
        shdr->sh_offset = strtab_offset;
        shdr->sh_size = (uint32_t)strtab_len;
        shdr->sh_link = 0;
        shdr->sh_info = 0;
        shdr->sh_addralign = 1;
        shdr->sh_entsize = 0;
        cur_file_offset += shdr->sh_size;
    }

    /* .shstrtab section */
    cur_file_offset = ALIGN_UP(cur_file_offset, 4);
    shstrtab_offset = cur_file_offset;
    {
        Elf32_Shdr *shdr = &shdrs[shdr_count++];
        memset(shdr, 0, sizeof(Elf32_Shdr));
        shdr->sh_name = (uint32_t)shstrtab_len;
        strcpy(shstrtab + shstrtab_len, ".shstrtab");
        shstrtab_len += 10;

        shdr->sh_type = SHT_STRTAB;
        shdr->sh_flags = 0;
        shdr->sh_addr = 0;
        shdr->sh_offset = shstrtab_offset;
        shdr->sh_size = (uint32_t)shstrtab_len;
        shdr->sh_link = 0;
        shdr->sh_info = 0;
        shdr->sh_addralign = 1;
        shdr->sh_entsize = 0;
        cur_file_offset += shdr->sh_size;
    }

    /* Update .shstrtab size in its section header */
    shdrs[shdr_count - 1].sh_size = (uint32_t)shstrtab_len;

    /* Update sh_link on .rel sections to point to .symtab */
    for (i = 1; i < shdr_count; i++) {
        if (shdrs[i].sh_type == SHT_REL) {
            shdrs[i].sh_link = (uint32_t)(shdr_count - 3); /* .symtab index */
        }
    }

    /* Section Header Offset */
    cur_file_offset = ALIGN_UP(cur_file_offset, 4);

    /* Prepare ELF Header */
    memset(&ehdr, 0, sizeof(ehdr));
    memcpy(ehdr.e_ident, "\x7f\x45\x4c\x46\x01\x01\x01\x00\x00\x00\x00\x00\x00\x00\x00\x00", 16);
    ehdr.e_type = ET_REL;
    ehdr.e_machine = EM_386;
    ehdr.e_version = EV_CURRENT;
    ehdr.e_shoff = cur_file_offset;
    ehdr.e_ehsize = sizeof(Elf32_Ehdr);
    ehdr.e_shentsize = sizeof(Elf32_Shdr);
    ehdr.e_shnum = (uint16_t)shdr_count;
    ehdr.e_shstrndx = (uint16_t)(shdr_count - 1);

    /* Write Object File */
    f_out = fopen(output_path, "wb");
    if (!f_out) {
        fprintf(stderr, "Error creating output file: %s\n", output_path);
        return 1;
    }

    /* 1. Header */
    fwrite(&ehdr, 1, sizeof(ehdr), f_out);

    /* 2. Section Data */
    for (sec_idx = 1; sec_idx <= g_section_count; sec_idx++) {
        section_t *sec = &g_sections[sec_idx];
        if (sec->type != SHT_NOBITS && sec->size > 0 && sec->data) {
            fseek(f_out, shdrs[sec_idx].sh_offset, SEEK_SET);
            fwrite(sec->data, 1, sec->size, f_out);
        }
    }

    /* 3. Relocations */
    for (sec_idx = 1; sec_idx <= g_section_count; sec_idx++) {
        int r;
        for (i = 1; i < shdr_count; i++) {
            if (shdrs[i].sh_type == SHT_REL && shdrs[i].sh_info == (uint32_t)sec_idx) {
                fseek(f_out, shdrs[i].sh_offset, SEEK_SET);
                for (r = 0; r < g_reloc_count; r++) {
                    if (g_relocs[r].section_idx == sec_idx) {
                        Elf32_Rel rel;
                        int remapped_sym = sym_reindex[g_relocs[r].sym_idx];
                        rel.r_offset = g_relocs[r].offset;
                        rel.r_info = ELF32_R_INFO(remapped_sym, g_relocs[r].type);
                        fwrite(&rel, 1, sizeof(rel), f_out);
                    }
                }
                break;
            }
        }
    }

    /* 4. Symbol Table */
    fseek(f_out, symtab_offset, SEEK_SET);
    fwrite(symtab, 1, symtab_count * sizeof(Elf32_Sym), f_out);

    /* 5. String Table */
    fseek(f_out, strtab_offset, SEEK_SET);
    fwrite(strtab, 1, strtab_len, f_out);

    /* 6. Section Header String Table */
    fseek(f_out, shstrtab_offset, SEEK_SET);
    fwrite(shstrtab, 1, shstrtab_len, f_out);

    /* 7. Section Headers */
    fseek(f_out, cur_file_offset, SEEK_SET);
    fwrite(shdrs, 1, shdr_count * sizeof(Elf32_Shdr), f_out);

    fclose(f_out);

    /* Cleanup */
    for (sec_idx = 1; sec_idx <= g_section_count; sec_idx++) {
        if (g_sections[sec_idx].data) free(g_sections[sec_idx].data);
    }
    free(shstrtab);
    free(strtab);
    free(symtab);

    return 0;
}
