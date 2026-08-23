/*
 * This is free and unencumbered software released into the public domain.
 * GEMIOS - Custom Host Tool: 32-bit x86 Assembler (as replacement)
 *
 * Assembles x86 assembly source (.s / .S) into standard ELF32 relocatable object (.o).
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

#define MAX_SECTIONS 16
#define MAX_SYMBOLS 1024
#define MAX_RELOCS 1024
#define MAX_MACROS 64
#define MAX_MACRO_LINES 128

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
    char     name[64];
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

static section_t g_sections[MAX_SECTIONS];
static int g_section_count = 0;
static int g_cur_section = 1;

static symbol_t g_symbols[MAX_SYMBOLS];
static int g_symbol_count = 0;

static reloc_t g_relocs[MAX_RELOCS];
static int g_reloc_count = 0;

static macro_def_t g_macros[MAX_MACROS];
static int g_macro_count = 0;

/* Local label tracking (e.g. 1:, 2:) */
typedef struct {
    int      id;
    int      section_idx;
    uint32_t offset;
    int      line_idx;
} local_label_t;

static local_label_t g_local_labels[512];
static int g_local_label_count = 0;

static void clean_string(char *s) {
    size_t len = strlen(s);
    while (len > 0 && (s[len-1] == ' ' || s[len-1] == '\t' || s[len-1] == '\r' || s[len-1] == '\n')) {
        s[--len] = '\0';
    }
}

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
            size_t new_cap = (sec->capacity == 0) ? 4096 : sec->capacity * 2;
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
    char clean_name[64];
    strncpy(clean_name, name, 63);
    clean_name[63] = '\0';
    clean_string(clean_name);

    for (i = 1; i <= g_symbol_count; i++) {
        if (strcmp(g_symbols[i].name, clean_name) == 0) {
            return i;
        }
    }
    if (g_symbol_count + 1 >= MAX_SYMBOLS) {
        fprintf(stderr, "Error: Too many symbols\n");
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
        fprintf(stderr, "Error: Too many relocations\n");
        exit(1);
    }
    g_relocs[g_reloc_count].section_idx = sec_idx;
    g_relocs[g_reloc_count].offset = offset;
    g_relocs[g_reloc_count].sym_idx = sym_idx;
    g_relocs[g_reloc_count].type = type;
    g_reloc_count++;
}

/* Simple Expression Parser */
static const char *skip_ws(const char *p) {
    while (*p && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')) p++;
    return p;
}

static int parse_reg(const char *p, int *type, int *num) {
    p = skip_ws(p);
    if (*p != '%') return 0;
    p++;

    /* 32-bit registers */
    if (strcmp(p, "eax") == 0) { *type = 32; *num = 0; return 1; }
    if (strcmp(p, "ecx") == 0) { *type = 32; *num = 1; return 1; }
    if (strcmp(p, "edx") == 0) { *type = 32; *num = 2; return 1; }
    if (strcmp(p, "ebx") == 0) { *type = 32; *num = 3; return 1; }
    if (strcmp(p, "esp") == 0) { *type = 32; *num = 4; return 1; }
    if (strcmp(p, "ebp") == 0) { *type = 32; *num = 5; return 1; }
    if (strcmp(p, "esi") == 0) { *type = 32; *num = 6; return 1; }
    if (strcmp(p, "edi") == 0) { *type = 32; *num = 7; return 1; }

    /* 16-bit registers */
    if (strcmp(p, "ax") == 0) { *type = 16; *num = 0; return 1; }
    if (strcmp(p, "cx") == 0) { *type = 16; *num = 1; return 1; }
    if (strcmp(p, "dx") == 0) { *type = 16; *num = 2; return 1; }
    if (strcmp(p, "bx") == 0) { *type = 16; *num = 3; return 1; }
    if (strcmp(p, "sp") == 0) { *type = 16; *num = 4; return 1; }
    if (strcmp(p, "bp") == 0) { *type = 16; *num = 5; return 1; }
    if (strcmp(p, "si") == 0) { *type = 16; *num = 6; return 1; }
    if (strcmp(p, "di") == 0) { *type = 16; *num = 7; return 1; }

    /* 8-bit registers */
    if (strcmp(p, "al") == 0) { *type = 8; *num = 0; return 1; }
    if (strcmp(p, "cl") == 0) { *type = 8; *num = 1; return 1; }
    if (strcmp(p, "dl") == 0) { *type = 8; *num = 2; return 1; }
    if (strcmp(p, "bl") == 0) { *type = 8; *num = 3; return 1; }
    if (strcmp(p, "ah") == 0) { *type = 8; *num = 4; return 1; }
    if (strcmp(p, "ch") == 0) { *type = 8; *num = 5; return 1; }
    if (strcmp(p, "dh") == 0) { *type = 8; *num = 6; return 1; }
    if (strcmp(p, "bh") == 0) { *type = 8; *num = 7; return 1; }

    /* Segment registers */
    if (strcmp(p, "es") == 0) { *type = 100; *num = 0; return 1; }
    if (strcmp(p, "cs") == 0) { *type = 100; *num = 1; return 1; }
    if (strcmp(p, "ss") == 0) { *type = 100; *num = 2; return 1; }
    if (strcmp(p, "ds") == 0) { *type = 100; *num = 3; return 1; }
    if (strcmp(p, "fs") == 0) { *type = 100; *num = 4; return 1; }
    if (strcmp(p, "gs") == 0) { *type = 100; *num = 5; return 1; }

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
    } else if (isdigit((unsigned char)*p)) {
        if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) {
            val = (int32_t)strtoul(p, (char**)&p, 16);
        } else {
            val = (int32_t)strtoul(p, (char**)&p, 10);
        }
    } else if (isalpha((unsigned char)*p) || *p == '_' || *p == '.') {
        char sym_name[64];
        int len = 0;
        int i;

        while (isalnum((unsigned char)*p) || *p == '_' || *p == '.') {
            if (len < 63) sym_name[len++] = *p;
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

static void parse_operands(const char *p, char ops[3][64], int *op_count) {
    int i = 0;
    *op_count = 0;
    p = skip_ws(p);

    while (*p && *p != '#' && *p != ';') {
        int len = 0;
        while (*p && *p != ',' && *p != '#' && *p != ';') {
            if (len < 63) ops[i][len++] = *p;
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
        if (*p == ',') p++;
        p = skip_ws(p);
    }
    *op_count = i;
}

static void assemble_line(const char *line, int line_idx, int pass) {
    const char *p;
    char token[64];
    int len;
    char ops[3][64];
    int op_count;

    p = skip_ws(line);
    if (!*p || *p == '#' || *p == ';') return;

    /* Handle C-style block comments */
    if (p[0] == '/' && p[1] == '*') return;

    /* Check for Local Label Definition (e.g. 1:, 2:) */
    if (isdigit((unsigned char)*p) && p[1] == ':') {
        int id = *p - '0';
        if (pass == 1) {
            g_local_labels[g_local_label_count].id = id;
            g_local_labels[g_local_label_count].section_idx = g_cur_section;
            g_local_labels[g_local_label_count].offset = (uint32_t)g_sections[g_cur_section].size;
            g_local_labels[g_local_label_count].line_idx = line_idx;
            g_local_label_count++;
        } else if (pass == 2) {
            int k;
            for (k = 0; k < g_local_label_count; k++) {
                if (g_local_labels[k].line_idx == line_idx) {
                    g_local_labels[k].offset = (uint32_t)g_sections[g_cur_section].size;
                    break;
                }
            }
        }
        p += 2;
        p = skip_ws(p);
        if (!*p || *p == '#' || *p == ';') return;
    } else {
        const char *colon = strchr(p, ':');
        if (colon && colon > p && (isalpha((unsigned char)*p) || *p == '_' || *p == '.')) {
            char label_name[64];
            size_t llen = colon - p;
            if (llen < 63) {
                int s_idx;
                memcpy(label_name, p, llen);
                label_name[llen] = '\0';
                clean_string(label_name);
                s_idx = find_or_add_symbol(label_name);
                g_symbols[s_idx].value = (uint32_t)g_sections[g_cur_section].size;
                g_symbols[s_idx].section_idx = g_cur_section;
                g_symbols[s_idx].defined = 1;

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

    parse_operands(p, ops, &op_count);

    /* Directives */
    if (strcmp(token, ".set") == 0) {
        if (op_count >= 2) {
            int s_idx = find_or_add_symbol(ops[0]);
            const char *ep = ops[1];
            g_symbols[s_idx].value = (uint32_t)eval_expr(&ep);
            g_symbols[s_idx].section_idx = SHN_ABS;
            g_symbols[s_idx].defined = 1;
        }
        return;
    }

    if (strcmp(token, ".section") == 0) {
        if (op_count >= 1) {
            uint32_t type = SHT_PROGBITS;
            uint32_t flags = SHF_ALLOC;
            if (strcmp(ops[0], ".text") == 0) {
                flags = SHF_ALLOC | SHF_EXECINSTR;
            } else if (strcmp(ops[0], ".bss") == 0) {
                type = SHT_NOBITS;
                flags = SHF_ALLOC | SHF_WRITE;
            } else if (strcmp(ops[0], ".data") == 0) {
                flags = SHF_ALLOC | SHF_WRITE;
            }
            g_cur_section = get_or_create_section(ops[0], type, flags, 4);
        }
        return;
    }

    if (strcmp(token, ".align") == 0) {
        if (op_count >= 1) {
            uint32_t a = (uint32_t)atoi(ops[0]);
            uint32_t aligned = ALIGN_UP(g_sections[g_cur_section].size, a);
            while (g_sections[g_cur_section].size < aligned) {
                sec_emit_byte(g_cur_section, 0);
            }
            if (a > g_sections[g_cur_section].align) {
                g_sections[g_cur_section].align = a;
            }
        }
        return;
    }

    if (strcmp(token, ".long") == 0) {
        if (op_count >= 1) {
            const char *ep = ops[0];
            uint32_t val = (uint32_t)eval_expr(&ep);
            sec_emit_dword(g_cur_section, val);
        }
        return;
    }

    if (strcmp(token, ".skip") == 0) {
        if (op_count >= 1) {
            const char *ep = ops[0];
            uint32_t count = (uint32_t)eval_expr(&ep);
            size_t c;
            for (c = 0; c < count; c++) {
                sec_emit_byte(g_cur_section, 0);
            }
        }
        return;
    }

    if (strcmp(token, ".global") == 0 || strcmp(token, ".globl") == 0) {
        if (op_count >= 1) {
            int s_idx = find_or_add_symbol(ops[0]);
            g_symbols[s_idx].binding = STB_GLOBAL;
        }
        return;
    }

    if (strcmp(token, ".type") == 0 || strcmp(token, ".size") == 0 || strcmp(token, ".extern") == 0) {
        return; /* Ignored / Metadata */
    }

    /* Instructions */
    if (strcmp(token, "cli") == 0) { sec_emit_byte(g_cur_section, 0xFA); return; }
    if (strcmp(token, "sti") == 0) { sec_emit_byte(g_cur_section, 0xFB); return; }
    if (strcmp(token, "hlt") == 0) { sec_emit_byte(g_cur_section, 0xF4); return; }
    if (strcmp(token, "ret") == 0) { sec_emit_byte(g_cur_section, 0xC3); return; }
    if (strcmp(token, "iret") == 0) { sec_emit_byte(g_cur_section, 0xCF); return; }
    if (strcmp(token, "pusha") == 0 || strcmp(token, "pushal") == 0) { sec_emit_byte(g_cur_section, 0x60); return; }
    if (strcmp(token, "popa") == 0 || strcmp(token, "popal") == 0) { sec_emit_byte(g_cur_section, 0x61); return; }
    if (strcmp(token, "pushfl") == 0 || strcmp(token, "pushf") == 0) { sec_emit_byte(g_cur_section, 0x9C); return; }
    if (strcmp(token, "popfl") == 0 || strcmp(token, "popf") == 0) { sec_emit_byte(g_cur_section, 0x9D); return; }

    if (strcmp(token, "int") == 0 && op_count == 1) {
        const char *ep = ops[0];
        uint32_t num = (uint32_t)eval_expr(&ep);
        if (num == 3) {
            sec_emit_byte(g_cur_section, 0xCC);
        } else {
            sec_emit_byte(g_cur_section, 0xCD);
            sec_emit_byte(g_cur_section, (uint8_t)num);
        }
        return;
    }

    if ((strcmp(token, "push") == 0 || strcmp(token, "pushl") == 0) && op_count == 1) {
        int rtype, rnum;
        if (parse_reg(ops[0], &rtype, &rnum)) {
            sec_emit_byte(g_cur_section, (uint8_t)(0x50 + rnum));
        } else if (ops[0][0] == '$') {
            const char *ep = ops[0] + 1;
            uint32_t imm = (uint32_t)eval_expr(&ep);
            sec_emit_byte(g_cur_section, 0x68);
            sec_emit_dword(g_cur_section, imm);
        }
        return;
    }

    if ((strcmp(token, "pop") == 0 || strcmp(token, "popl") == 0) && op_count == 1) {
        int rtype, rnum;
        if (parse_reg(ops[0], &rtype, &rnum)) {
            sec_emit_byte(g_cur_section, (uint8_t)(0x58 + rnum));
        }
        return;
    }

    if (strncmp(token, "mov", 3) == 0 && op_count == 2) {
        int rtype1, rnum1, rtype2, rnum2;
        int is_reg1 = parse_reg(ops[0], &rtype1, &rnum1);
        int is_reg2 = parse_reg(ops[1], &rtype2, &rnum2);

        if (is_reg1 && is_reg2) {
            if (rtype1 == 100) {
                /* mov %sreg, %reg */
                sec_emit_byte(g_cur_section, 0x8C);
                sec_emit_byte(g_cur_section, (uint8_t)(0xC0 | (rnum1 << 3) | rnum2));
                return;
            } else if (rtype2 == 100) {
                /* mov %reg, %sreg */
                sec_emit_byte(g_cur_section, 0x8E);
                sec_emit_byte(g_cur_section, (uint8_t)(0xC0 | (rnum2 << 3) | rnum1));
                return;
            } else if (rtype1 == 32 && rtype2 == 32) {
                sec_emit_byte(g_cur_section, 0x89);
                sec_emit_byte(g_cur_section, (uint8_t)(0xC0 | (rnum1 << 3) | rnum2));
                return;
            }
        }

        if (ops[0][0] == '$' && is_reg2) {
            const char *ep = ops[0] + 1;
            if (isalpha((unsigned char)*ep) || *ep == '_' || *ep == '.') {
                /* Relocated symbol */
                int s_idx = find_or_add_symbol(ep);
                if (pass == 2) {
                    add_relocation(g_cur_section, (uint32_t)g_sections[g_cur_section].size + 1, s_idx, R_386_32);
                }
                sec_emit_byte(g_cur_section, (uint8_t)(0xB8 + rnum2));
                sec_emit_dword(g_cur_section, 0);
            } else {
                uint32_t imm = (uint32_t)eval_expr(&ep);
                if (rtype2 == 32) {
                    sec_emit_byte(g_cur_section, (uint8_t)(0xB8 + rnum2));
                    sec_emit_dword(g_cur_section, imm);
                } else if (rtype2 == 16) {
                    sec_emit_byte(g_cur_section, 0x66);
                    sec_emit_byte(g_cur_section, (uint8_t)(0xB8 + rnum2));
                    sec_emit_word(g_cur_section, (uint16_t)imm);
                } else if (rtype2 == 8) {
                    sec_emit_byte(g_cur_section, (uint8_t)(0xB0 + rnum2));
                    sec_emit_byte(g_cur_section, (uint8_t)imm);
                }
            }
            return;
        }

        /* mov disp(%esp), %reg */
        if (strstr(ops[0], "(%esp)") && is_reg2) {
            const char *ep = ops[0];
            int disp = (int)eval_expr(&ep);
            if (token[3] == 'w') sec_emit_byte(g_cur_section, 0x66);
            sec_emit_byte(g_cur_section, (token[3] == 'b') ? 0x8A : 0x8B);
            sec_emit_byte(g_cur_section, (uint8_t)(0x44 | (rnum2 << 3)));
            sec_emit_byte(g_cur_section, 0x24); /* SIB */
            sec_emit_byte(g_cur_section, (uint8_t)disp);
            return;
        }

        /* mov disp(%eax), %reg */
        if (strstr(ops[0], "(%eax)") && is_reg2) {
            const char *ep = ops[0];
            int disp = (int)eval_expr(&ep);
            if (token[3] == 'w') sec_emit_byte(g_cur_section, 0x66);
            sec_emit_byte(g_cur_section, (token[3] == 'b') ? 0x8A : 0x8B);
            sec_emit_byte(g_cur_section, (uint8_t)(0x40 | (rnum2 << 3)));
            sec_emit_byte(g_cur_section, (uint8_t)disp);
            return;
        }
    }

    if (strcmp(token, "xorl") == 0 && op_count == 2) {
        int rtype1, rnum1, rtype2, rnum2;
        if (parse_reg(ops[0], &rtype1, &rnum1) && parse_reg(ops[1], &rtype2, &rnum2)) {
            sec_emit_byte(g_cur_section, 0x31);
            sec_emit_byte(g_cur_section, (uint8_t)(0xC0 | (rnum1 << 3) | rnum2));
            return;
        }
    }

    if (strcmp(token, "addl") == 0 && op_count == 2) {
        if (ops[0][0] == '$') {
            const char *ep = ops[0] + 1;
            uint32_t imm = (uint32_t)eval_expr(&ep);
            int rtype, rnum;
            if (parse_reg(ops[1], &rtype, &rnum)) {
                sec_emit_byte(g_cur_section, 0x83);
                sec_emit_byte(g_cur_section, (uint8_t)(0xC0 | rnum));
                sec_emit_byte(g_cur_section, (uint8_t)imm);
                return;
            }
        }
    }

    if (strcmp(token, "testb") == 0 && op_count == 2) {
        const char *ep = ops[0] + 1;
        uint32_t imm = (uint32_t)eval_expr(&ep);
        sec_emit_byte(g_cur_section, 0xA8);
        sec_emit_byte(g_cur_section, (uint8_t)imm);
        return;
    }

    if (strcmp(token, "orb") == 0 && op_count == 2) {
        const char *ep = ops[0] + 1;
        uint32_t imm = (uint32_t)eval_expr(&ep);
        sec_emit_byte(g_cur_section, 0x0C);
        sec_emit_byte(g_cur_section, (uint8_t)imm);
        return;
    }

    if (strcmp(token, "andb") == 0 && op_count == 2) {
        const char *ep = ops[0] + 1;
        uint32_t imm = (uint32_t)eval_expr(&ep);
        sec_emit_byte(g_cur_section, 0x24);
        sec_emit_byte(g_cur_section, (uint8_t)imm);
        return;
    }

    if (strcmp(token, "inb") == 0 && op_count == 2) {
        if (strcmp(ops[0], "%dx") == 0) {
            sec_emit_byte(g_cur_section, 0xEC);
        } else if (ops[0][0] == '$') {
            const char *ep = ops[0] + 1;
            uint32_t port = (uint32_t)eval_expr(&ep);
            sec_emit_byte(g_cur_section, 0xE4);
            sec_emit_byte(g_cur_section, (uint8_t)port);
        }
        return;
    }

    if (strcmp(token, "outb") == 0 && op_count == 2) {
        if (strcmp(ops[1], "%dx") == 0) {
            sec_emit_byte(g_cur_section, 0xEE);
        } else if (ops[1][0] == '$') {
            const char *ep = ops[1] + 1;
            uint32_t port = (uint32_t)eval_expr(&ep);
            sec_emit_byte(g_cur_section, 0xE6);
            sec_emit_byte(g_cur_section, (uint8_t)port);
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

    if (strcmp(token, "lgdt") == 0 && op_count == 1) {
        sec_emit_byte(g_cur_section, 0x0F);
        sec_emit_byte(g_cur_section, 0x01);
        sec_emit_byte(g_cur_section, 0x10); /* (%eax) */
        return;
    }

    if (strcmp(token, "lidt") == 0 && op_count == 1) {
        sec_emit_byte(g_cur_section, 0x0F);
        sec_emit_byte(g_cur_section, 0x01);
        if (strstr(ops[0], "(%esp)")) {
            sec_emit_byte(g_cur_section, 0x1C);
            sec_emit_byte(g_cur_section, 0x24);
        } else {
            sec_emit_byte(g_cur_section, 0x18); /* (%eax) */
        }
        return;
    }

    if (strcmp(token, "ljmp") == 0 && op_count == 2) {
        const char *ep1 = ops[0] + (ops[0][0] == '$' ? 1 : 0);
        const char *label = ops[1] + (ops[1][0] == '$' ? 1 : 0);
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

    if (strcmp(token, "call") == 0 && op_count == 1) {
        int s_idx = find_or_add_symbol(ops[0]);
        sec_emit_byte(g_cur_section, 0xE8);
        if (pass == 2) {
            add_relocation(g_cur_section, (uint32_t)g_sections[g_cur_section].size, s_idx, R_386_PC32);
        }
        sec_emit_dword(g_cur_section, (uint32_t)-4);
        return;
    }

    if ((strcmp(token, "jmp") == 0 || strcmp(token, "jnz") == 0 || strcmp(token, "jne") == 0) && op_count == 1) {
        int is_jnz = (strcmp(token, "jnz") == 0 || strcmp(token, "jne") == 0);
        if (isdigit((unsigned char)ops[0][0]) && (ops[0][1] == 'b' || ops[0][1] == 'f')) {
            int id = ops[0][0] - '0';
            int is_back = (ops[0][1] == 'b');
            int32_t target_off = -1;
            int i;

            if (is_back) {
                int max_line = -1;
                for (i = 0; i < g_local_label_count; i++) {
                    if (g_local_labels[i].id == id && g_local_labels[i].section_idx == g_cur_section &&
                        g_local_labels[i].line_idx < line_idx && g_local_labels[i].line_idx > max_line) {
                        max_line = g_local_labels[i].line_idx;
                        target_off = (int32_t)g_local_labels[i].offset;
                    }
                }
            } else {
                int min_line = 999999;
                for (i = 0; i < g_local_label_count; i++) {
                    if (g_local_labels[i].id == id && g_local_labels[i].section_idx == g_cur_section &&
                        g_local_labels[i].line_idx > line_idx && g_local_labels[i].line_idx < min_line) {
                        min_line = g_local_labels[i].line_idx;
                        target_off = (int32_t)g_local_labels[i].offset;
                    }
                }
            }

            if (target_off >= 0) {
                int32_t disp = target_off - ((int32_t)g_sections[g_cur_section].size + 2);
                sec_emit_byte(g_cur_section, is_jnz ? 0x75 : 0xEB);
                sec_emit_byte(g_cur_section, (uint8_t)disp);
            } else {
                /* Forward local label placeholder */
                sec_emit_byte(g_cur_section, is_jnz ? 0x75 : 0xEB);
                sec_emit_byte(g_cur_section, 0x00);
            }
            return;
        } else {
            int s_idx = find_or_add_symbol(ops[0]);
            if (is_jnz) {
                sec_emit_byte(g_cur_section, 0x0F);
                sec_emit_byte(g_cur_section, 0x85);
            } else {
                sec_emit_byte(g_cur_section, 0xE9);
            }
            if (pass == 2) {
                add_relocation(g_cur_section, (uint32_t)g_sections[g_cur_section].size, s_idx, R_386_PC32);
            }
            sec_emit_dword(g_cur_section, (uint32_t)-4);
            return;
        }
    }
}

/* Macro Preprocessor & Source Expander */
static void preprocess_and_assemble(const char *src_path) {
    FILE *fp;
    char line[512];
    char expanded_lines[4096][256];
    int total_lines = 0;
    int in_macro = 0;
    int pass;
    int l;

    fp = fopen(src_path, "r");
    if (!fp) {
        fprintf(stderr, "Error opening source file: %s\n", src_path);
        exit(1);
    }

    while (fgets(line, sizeof(line), fp)) {
        char *p;
        clean_string(line);
        p = (char*)skip_ws(line);
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
                        char exp[256];
                        char *src_l = m->lines[ml];
                        int pi;
                        strcpy(exp, src_l);

                        for (pi = 0; pi < m->param_count && pi < acount; pi++) {
                            char param_ref[34];
                            char *pos;
                            sprintf(param_ref, "\\%s", m->params[pi]);

                            while ((pos = strstr(exp, param_ref)) != NULL) {
                                char temp[256];
                                size_t prefix_len = pos - exp;
                                memcpy(temp, exp, prefix_len);
                                temp[prefix_len] = '\0';
                                strcat(temp, args[pi]);
                                strcat(temp, pos + strlen(param_ref));
                                strcpy(exp, temp);
                            }
                        }

                        if (total_lines < 4096) {
                            strncpy(expanded_lines[total_lines++], exp, 255);
                        }
                    }
                    is_mcall = 1;
                    break;
                }
            }

            if (!is_mcall && total_lines < 4096) {
                strncpy(expanded_lines[total_lines++], line, 255);
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
    shstrtab = (char*)malloc(4096);
    shstrtab[0] = '\0';
    shstrtab_len = 1;

    strtab = (char*)malloc(65536);
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
