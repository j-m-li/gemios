/*
 * This is free and unencumbered software released into the public domain.
 * GEMIOS - Custom Host Tool: ld.lld replacement (ELF32 x86 Linker)
 *
 * Statically links 32-bit x86 ELF relocatable objects into an ELF32 executable.
 * Standard C90 compliant.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_MSC_VER)
typedef unsigned char uint8_t;
typedef unsigned short uint16_t;
typedef unsigned int uint32_t;
typedef int int32_t;
#else
#include <stdint.h>
#endif

/* ELF32 Header Definitions */
#define EI_NIDENT 16

#define ET_REL  1
#define ET_EXEC 2

#define EM_386 3

#define EV_CURRENT 1

#define SHT_NULL     0
#define SHT_PROGBITS 1
#define SHT_SYMTAB   2
#define SHT_STRTAB   3
#define SHT_RELA     4
#define SHT_HASH     5
#define SHT_DYNAMIC  6
#define SHT_NOTE     7
#define SHT_NOBITS   8
#define SHT_REL      9
#define SHT_SHLIB    10
#define SHT_DYNSYM   11

#define SHF_WRITE     (1 << 0)
#define SHF_ALLOC     (1 << 1)
#define SHF_EXECINSTR (1 << 2)

#define SHN_UNDEF  0
#define SHN_ABS    0xFFF1
#define SHN_COMMON 0xFFF2

#define STB_LOCAL  0
#define STB_GLOBAL 1
#define STB_WEAK   2

#define STT_NOTYPE  0
#define STT_OBJECT  1
#define STT_FUNC    2
#define STT_SECTION 3
#define STT_FILE    4

#define ELF32_ST_BIND(i)   ((i) >> 4)
#define ELF32_ST_TYPE(i)   ((i) & 0x0F)
#define ELF32_ST_INFO(b,t) (((b) << 4) + ((t) & 0x0F))

#define ELF32_R_SYM(i)     ((i) >> 8)
#define ELF32_R_TYPE(i)    ((uint8_t)(i))
#define ELF32_R_INFO(s,t)  (((s) << 8) + (uint8_t)(t))

#define R_386_NONE   0
#define R_386_32     1
#define R_386_PC32   2
#define R_386_GOT32  3
#define R_386_PLT32  4
#define R_386_COPY   5
#define R_386_GLOB_DAT 6
#define R_386_JMP_SLOT 7
#define R_386_RELATIVE 8
#define R_386_GOTOFF 9
#define R_386_GOTPC  10
#define R_386_16     20
#define R_386_PC16   21
#define R_386_8      22
#define R_386_PC8    23

#define PT_NULL  0
#define PT_LOAD  1
#define PT_NOTE  4
#define PT_GNU_STACK 0x6474e551

#define PF_X 1
#define PF_W 2
#define PF_R 4

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
    uint32_t p_type;
    uint32_t p_offset;
    uint32_t p_vaddr;
    uint32_t p_paddr;
    uint32_t p_filesz;
    uint32_t p_memsz;
    uint32_t p_flags;
    uint32_t p_align;
} Elf32_Phdr;

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

#define ALIGN_UP(val, align) (((val) + (align) - 1) & ~((align) - 1))

#define MAX_INPUT_FILES 128
#define MAX_SECTIONS_PER_FILE 64
#define MAX_OUTPUT_SECTIONS 16
#define MAX_GLOBAL_SYMBOLS 4096

/* Input Section Mapping */
typedef struct {
    int      out_sec_idx;   /* Output section index */
    uint32_t out_offset;    /* Offset within output section */
    uint32_t vaddr;         /* Resolved virtual address */
} in_sec_map_t;

/* Input Object File */
typedef struct {
    const char   *path;
    uint8_t      *data;
    size_t        size;
    Elf32_Ehdr   *ehdr;
    Elf32_Shdr   *shdrs;
    const char   *shstrtab;
    Elf32_Sym    *symtab;
    size_t        sym_count;
    const char   *strtab;
    in_sec_map_t  sec_map[MAX_SECTIONS_PER_FILE];
} input_obj_t;

/* Output Section */
typedef struct {
    char     name[32];
    uint32_t type;
    uint32_t flags;
    uint32_t align;
    uint32_t vaddr;
    uint32_t offset;
    uint32_t size;
    uint8_t *data;
    size_t   capacity;
} out_sec_t;

/* Global Symbol */
typedef struct {
    char     name[128];
    uint32_t vaddr;
    uint32_t size;
    uint8_t  info;
    uint8_t  defined;
} global_sym_t;

static input_obj_t g_inputs[MAX_INPUT_FILES];
static int g_input_count = 0;

static out_sec_t g_out_secs[MAX_OUTPUT_SECTIONS];
static int g_out_sec_count = 0;

static global_sym_t g_globals[MAX_GLOBAL_SYMBOLS];
static int g_global_count = 0;

/* Standard Output Section Indices */
enum {
    OUT_SEC_MULTIBOOT = 0,
    OUT_SEC_TEXT,
    OUT_SEC_RODATA,
    OUT_SEC_DATA,
    OUT_SEC_BSS
};

static void out_sec_append(out_sec_t *sec, const void *src, size_t len, uint32_t align) {
    uint32_t aligned_size;

    if (align < 1) align = 1;
    aligned_size = ALIGN_UP(sec->size, align);

    if (sec->type != SHT_NOBITS) {
        if (aligned_size + len > sec->capacity) {
            size_t new_cap = (sec->capacity == 0) ? 65536 : sec->capacity * 2;
            while (new_cap < aligned_size + len) new_cap *= 2;
            sec->data = (uint8_t*)realloc(sec->data, new_cap);
            if (!sec->data) {
                fprintf(stderr, "Out of memory in out_sec_append\n");
                exit(1);
            }
            sec->capacity = new_cap;
        }

        if (aligned_size > sec->size) {
            memset(sec->data + sec->size, 0, aligned_size - sec->size);
        }

        if (src && len > 0) {
            memcpy(sec->data + aligned_size, src, len);
        }
    }

    sec->size = aligned_size + (uint32_t)len;
}

static int find_or_add_global_sym(const char *name) {
    int i;
    for (i = 0; i < g_global_count; i++) {
        if (strcmp(g_globals[i].name, name) == 0) {
            return i;
        }
    }
    if (g_global_count >= MAX_GLOBAL_SYMBOLS) {
        fprintf(stderr, "Error: Exceeded MAX_GLOBAL_SYMBOLS (%d)\n", MAX_GLOBAL_SYMBOLS);
        exit(1);
    }
    memset(&g_globals[g_global_count], 0, sizeof(global_sym_t));
    strncpy(g_globals[g_global_count].name, name, sizeof(g_globals[g_global_count].name) - 1);
    return g_global_count++;
}

static void print_usage(const char *prog) {
    fprintf(stderr, "Usage: %s -o <output.elf> [options] <input_objects...>\n", prog);
}

int main(int argc, char **argv) {
    const char *output_file = NULL;
    uint32_t image_base = 0x100000;
    int i;
    int f;
    int s;
    int cur_out_idx;
    uint32_t cur_vaddr;
    uint32_t file_offset;
    uint32_t total_filesz;
    uint32_t total_memsz;
    uint32_t entry_point;
    int start_sym_idx;
    FILE *f_out;
    Elf32_Ehdr out_ehdr;
    Elf32_Phdr out_phdrs[2];
    Elf32_Shdr out_shdrs[MAX_OUTPUT_SECTIONS + 4];
    int out_sh_count;
    char *shstrtab_buf;
    size_t shstrtab_len;
    char *strtab_buf;
    size_t strtab_len;
    Elf32_Sym *out_syms;
    size_t out_sym_count;
    size_t sym_idx;
    uint32_t sh_name_symtab;
    uint32_t sh_name_shstrtab;
    uint32_t sh_name_strtab;
    uint32_t symtab_offset;
    uint32_t shstrtab_offset;
    uint32_t strtab_offset;
    uint32_t symtab_size;
    uint32_t shstrtab_size;
    uint32_t strtab_size;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) {
            output_file = argv[++i];
        } else if (strcmp(argv[i], "-m") == 0 && i + 1 < argc) {
            i++; /* Ignore emulation flag */
        } else if (strcmp(argv[i], "-T") == 0 && i + 1 < argc) {
            i++; /* Linker script accepted */
        } else if (strncmp(argv[i], "--image-base=", 13) == 0) {
            image_base = (uint32_t)strtoul(argv[i] + 13, NULL, 0);
        } else if (strcmp(argv[i], "-nostdlib") == 0) {
            /* Accepted */
        } else if (argv[i][0] == '-') {
            /* Unknown flag: ignore for compatibility */
        } else {
            /* Input object file */
            if (g_input_count >= MAX_INPUT_FILES) {
                fprintf(stderr, "Error: Too many input files\n");
                return 1;
            }
            g_inputs[g_input_count++].path = argv[i];
        }
    }

    if (!output_file || g_input_count == 0) {
        print_usage(argv[0]);
        return 1;
    }

    /* 1. Initialize standard output sections */
    g_out_sec_count = 5;

    strcpy(g_out_secs[OUT_SEC_MULTIBOOT].name, ".multiboot");
    g_out_secs[OUT_SEC_MULTIBOOT].type = SHT_PROGBITS;
    g_out_secs[OUT_SEC_MULTIBOOT].flags = SHF_ALLOC;
    g_out_secs[OUT_SEC_MULTIBOOT].align = 4;

    strcpy(g_out_secs[OUT_SEC_TEXT].name, ".text");
    g_out_secs[OUT_SEC_TEXT].type = SHT_PROGBITS;
    g_out_secs[OUT_SEC_TEXT].flags = SHF_ALLOC | SHF_EXECINSTR;
    g_out_secs[OUT_SEC_TEXT].align = 4096;

    strcpy(g_out_secs[OUT_SEC_RODATA].name, ".rodata");
    g_out_secs[OUT_SEC_RODATA].type = SHT_PROGBITS;
    g_out_secs[OUT_SEC_RODATA].flags = SHF_ALLOC;
    g_out_secs[OUT_SEC_RODATA].align = 4096;

    strcpy(g_out_secs[OUT_SEC_DATA].name, ".data");
    g_out_secs[OUT_SEC_DATA].type = SHT_PROGBITS;
    g_out_secs[OUT_SEC_DATA].flags = SHF_ALLOC | SHF_WRITE;
    g_out_secs[OUT_SEC_DATA].align = 4096;

    strcpy(g_out_secs[OUT_SEC_BSS].name, ".bss");
    g_out_secs[OUT_SEC_BSS].type = SHT_NOBITS;
    g_out_secs[OUT_SEC_BSS].flags = SHF_ALLOC | SHF_WRITE;
    g_out_secs[OUT_SEC_BSS].align = 4096;

    /* 2. Load all input object files */
    for (f = 0; f < g_input_count; f++) {
        input_obj_t *in = &g_inputs[f];
        FILE *fp;

        fp = fopen(in->path, "rb");
        if (!fp) {
            fprintf(stderr, "Error opening input file: %s\n", in->path);
            return 1;
        }

        fseek(fp, 0, SEEK_END);
        in->size = ftell(fp);
        fseek(fp, 0, SEEK_SET);

        in->data = (uint8_t*)malloc(in->size);
        if (!in->data) {
            fprintf(stderr, "Out of memory reading %s\n", in->path);
            fclose(fp);
            return 1;
        }

        if (fread(in->data, 1, in->size, fp) != in->size) {
            fprintf(stderr, "Error reading %s\n", in->path);
            fclose(fp);
            return 1;
        }
        fclose(fp);

        in->ehdr = (Elf32_Ehdr*)in->data;
        if (memcmp(in->ehdr->e_ident, "\x7f\x45\x4c\x46", 4) != 0 ||
            in->ehdr->e_type != ET_REL || in->ehdr->e_machine != EM_386) {
            fprintf(stderr, "Error: %s is not a valid 32-bit x86 ELF relocatable object\n", in->path);
            return 1;
        }

        in->shdrs = (Elf32_Shdr*)(in->data + in->ehdr->e_shoff);
        in->shstrtab = (const char*)(in->data + in->shdrs[in->ehdr->e_shstrndx].sh_offset);

        /* Find Symbol Table and String Table */
        for (s = 0; s < in->ehdr->e_shnum; s++) {
            if (in->shdrs[s].sh_type == SHT_SYMTAB) {
                in->symtab = (Elf32_Sym*)(in->data + in->shdrs[s].sh_offset);
                in->sym_count = in->shdrs[s].sh_size / sizeof(Elf32_Sym);
                in->strtab = (const char*)(in->data + in->shdrs[in->shdrs[s].sh_link].sh_offset);
                break;
            }
        }
    }

    /* 3. Assign input sections to output sections and collect section data */
    for (f = 0; f < g_input_count; f++) {
        input_obj_t *in = &g_inputs[f];

        for (s = 0; s < in->ehdr->e_shnum; s++) {
            Elf32_Shdr *shdr = &in->shdrs[s];
            const char *name = in->shstrtab + shdr->sh_name;
            out_sec_t *target_sec = NULL;
            int target_idx = -1;

            in->sec_map[s].out_sec_idx = -1;

            if (!(shdr->sh_flags & SHF_ALLOC)) {
                continue; /* Skip non-allocatable sections (.comment, .debug_*, etc.) */
            }

            if (strcmp(name, ".multiboot") == 0) {
                target_idx = OUT_SEC_MULTIBOOT;
            } else if (strncmp(name, ".text", 5) == 0) {
                target_idx = OUT_SEC_TEXT;
            } else if (strncmp(name, ".rodata", 7) == 0) {
                target_idx = OUT_SEC_RODATA;
            } else if (strncmp(name, ".data", 5) == 0) {
                target_idx = OUT_SEC_DATA;
            } else if (strncmp(name, ".bss", 4) == 0) {
                target_idx = OUT_SEC_BSS;
            }

            if (target_idx >= 0) {
                target_sec = &g_out_secs[target_idx];
                in->sec_map[s].out_sec_idx = target_idx;
                in->sec_map[s].out_offset = ALIGN_UP(target_sec->size, shdr->sh_addralign ? shdr->sh_addralign : 4);

                out_sec_append(target_sec,
                               (shdr->sh_type != SHT_NOBITS) ? (in->data + shdr->sh_offset) : NULL,
                               shdr->sh_size,
                               shdr->sh_addralign ? shdr->sh_addralign : 4);
            }
        }
    }

    /* 4. Layout output sections in virtual memory starting at image_base */
    cur_vaddr = image_base;
    for (cur_out_idx = 0; cur_out_idx < g_out_sec_count; cur_out_idx++) {
        out_sec_t *sec = &g_out_secs[cur_out_idx];
        cur_vaddr = ALIGN_UP(cur_vaddr, sec->align);
        sec->vaddr = cur_vaddr;
        cur_vaddr += sec->size;
    }

    /* Compute virtual address of each input section */
    for (f = 0; f < g_input_count; f++) {
        input_obj_t *in = &g_inputs[f];
        for (s = 0; s < in->ehdr->e_shnum; s++) {
            if (in->sec_map[s].out_sec_idx >= 0) {
                out_sec_t *out_sec = &g_out_secs[in->sec_map[s].out_sec_idx];
                in->sec_map[s].vaddr = out_sec->vaddr + in->sec_map[s].out_offset;
            }
        }
    }

    /* 5. Process COMMON symbols and collect global symbols */
    for (f = 0; f < g_input_count; f++) {
        input_obj_t *in = &g_inputs[f];
        if (!in->symtab) continue;

        for (sym_idx = 0; sym_idx < in->sym_count; sym_idx++) {
            Elf32_Sym *sym = &in->symtab[sym_idx];
            const char *name = in->strtab + sym->st_name;
            uint8_t bind = ELF32_ST_BIND(sym->st_info);

            if (sym->st_shndx == SHN_COMMON) {
                /* Allocate COMMON symbol in .bss */
                out_sec_t *bss = &g_out_secs[OUT_SEC_BSS];
                uint32_t align = sym->st_value ? sym->st_value : 4;
                uint32_t offset = ALIGN_UP(bss->size, align);
                int g_idx;

                out_sec_append(bss, NULL, sym->st_size, align);

                g_idx = find_or_add_global_sym(name);
                g_globals[g_idx].vaddr = bss->vaddr + offset;
                g_globals[g_idx].size = sym->st_size;
                g_globals[g_idx].info = sym->st_info;
                g_globals[g_idx].defined = 1;
            } else if (bind == STB_GLOBAL || bind == STB_WEAK) {
                if (sym->st_shndx != SHN_UNDEF) {
                    int g_idx = find_or_add_global_sym(name);
                    uint32_t sym_vaddr;

                    if (sym->st_shndx == SHN_ABS) {
                        sym_vaddr = sym->st_value;
                    } else if (in->sec_map[sym->st_shndx].out_sec_idx >= 0) {
                        sym_vaddr = in->sec_map[sym->st_shndx].vaddr + sym->st_value;
                    } else {
                        sym_vaddr = sym->st_value;
                    }

                    if (!g_globals[g_idx].defined || bind == STB_GLOBAL) {
                        g_globals[g_idx].vaddr = sym_vaddr;
                        g_globals[g_idx].size = sym->st_size;
                        g_globals[g_idx].info = sym->st_info;
                        g_globals[g_idx].defined = 1;
                    }
                }
            }
        }
    }

    /* Define linker special symbol: _kernel_end */
    {
        int g_idx = find_or_add_global_sym("_kernel_end");
        out_sec_t *bss = &g_out_secs[OUT_SEC_BSS];
        g_globals[g_idx].vaddr = ALIGN_UP(bss->vaddr + bss->size, 4096);
        g_globals[g_idx].size = 0;
        g_globals[g_idx].info = ELF32_ST_INFO(STB_GLOBAL, STT_OBJECT);
        g_globals[g_idx].defined = 1;
    }

    /* Verify all undefined global symbols can be resolved */
    for (f = 0; f < g_input_count; f++) {
        input_obj_t *in = &g_inputs[f];
        if (!in->symtab) continue;

        for (sym_idx = 0; sym_idx < in->sym_count; sym_idx++) {
            Elf32_Sym *sym = &in->symtab[sym_idx];
            const char *name = in->strtab + sym->st_name;
            uint8_t bind = ELF32_ST_BIND(sym->st_info);

            if (sym->st_shndx == SHN_UNDEF && (bind == STB_GLOBAL)) {
                int g_idx = find_or_add_global_sym(name);
                if (!g_globals[g_idx].defined) {
                    fprintf(stderr, "Error: Undefined reference to '%s' in %s\n", name, in->path);
                    return 1;
                }
            }
        }
    }

    /* 6. Apply Relocations */
    for (f = 0; f < g_input_count; f++) {
        input_obj_t *in = &g_inputs[f];

        for (s = 0; s < in->ehdr->e_shnum; s++) {
            Elf32_Shdr *rel_shdr = &in->shdrs[s];

            if (rel_shdr->sh_type == SHT_REL) {
                uint32_t target_sec_idx = rel_shdr->sh_info;
                Elf32_Rel *rels = (Elf32_Rel*)(in->data + rel_shdr->sh_offset);
                size_t rel_count = rel_shdr->sh_size / sizeof(Elf32_Rel);
                size_t r;

                if (in->sec_map[target_sec_idx].out_sec_idx < 0) {
                    continue; /* Target section not included in output */
                }

                {
                    out_sec_t *out_sec = &g_out_secs[in->sec_map[target_sec_idx].out_sec_idx];
                    uint32_t in_sec_offset = in->sec_map[target_sec_idx].out_offset;
                    uint32_t in_sec_vaddr = in->sec_map[target_sec_idx].vaddr;

                    for (r = 0; r < rel_count; r++) {
                        Elf32_Rel *rel = &rels[r];
                        uint32_t sym_id = ELF32_R_SYM(rel->r_info);
                        uint8_t type = ELF32_R_TYPE(rel->r_info);
                        Elf32_Sym *sym = &in->symtab[sym_id];
                        const char *sym_name = in->strtab + sym->st_name;
                        uint32_t S = 0;
                        uint32_t P = in_sec_vaddr + rel->r_offset;
                        uint8_t *loc = out_sec->data + in_sec_offset + rel->r_offset;
                        int32_t A = 0;

                        if (ELF32_ST_BIND(sym->st_info) == STB_LOCAL && sym->st_shndx != SHN_UNDEF) {
                            if (sym->st_shndx == SHN_ABS) {
                                S = sym->st_value;
                            } else if (in->sec_map[sym->st_shndx].out_sec_idx >= 0) {
                                S = in->sec_map[sym->st_shndx].vaddr + sym->st_value;
                            } else {
                                S = sym->st_value;
                            }
                        } else {
                            int g_idx = find_or_add_global_sym(sym_name);
                            S = g_globals[g_idx].vaddr;
                        }

                        if (type == R_386_32) {
                            memcpy(&A, loc, 4);
                            *(uint32_t*)loc = S + A;
                        } else if (type == R_386_PC32 || type == R_386_PLT32) {
                            memcpy(&A, loc, 4);
                            *(uint32_t*)loc = S + A - P;
                        } else if (type == R_386_16) {
                            int16_t a16;
                            memcpy(&a16, loc, 2);
                            *(uint16_t*)loc = (uint16_t)(S + a16);
                        } else if (type == R_386_PC16) {
                            int16_t a16;
                            memcpy(&a16, loc, 2);
                            *(uint16_t*)loc = (uint16_t)(S + a16 - P);
                        } else if (type == R_386_8) {
                            int8_t a8 = *(int8_t*)loc;
                            *(uint8_t*)loc = (uint8_t)(S + a8);
                        } else if (type == R_386_PC8) {
                            int8_t a8 = *(int8_t*)loc;
                            *(uint8_t*)loc = (uint8_t)(S + a8 - P);
                        } else if (type != R_386_NONE) {
                            fprintf(stderr, "Warning: Unsupported relocation type %u in %s\n", type, in->path);
                        }
                    }
                }
            }
        }
    }

    /* Find entry point symbol: _start */
    start_sym_idx = find_or_add_global_sym("_start");
    if (!g_globals[start_sym_idx].defined) {
        fprintf(stderr, "Error: Entry point '_start' not defined!\n");
        return 1;
    }
    entry_point = g_globals[start_sym_idx].vaddr;

    /* 7. Build Output Symbol Table (.symtab) and String Table (.strtab) */
    out_sym_count = g_global_count + 1; /* NULL symbol at 0 */
    out_syms = (Elf32_Sym*)calloc(out_sym_count, sizeof(Elf32_Sym));

    strtab_buf = (char*)malloc(65536);
    strtab_buf[0] = '\0';
    strtab_len = 1;

    for (i = 0; i < g_global_count; i++) {
        size_t name_len = strlen(g_globals[i].name) + 1;
        out_syms[i + 1].st_name = (uint32_t)strtab_len;
        memcpy(strtab_buf + strtab_len, g_globals[i].name, name_len);
        strtab_len += name_len;

        out_syms[i + 1].st_value = g_globals[i].vaddr;
        out_syms[i + 1].st_size = g_globals[i].size;
        out_syms[i + 1].st_info = g_globals[i].info ? g_globals[i].info : ELF32_ST_INFO(STB_GLOBAL, STT_NOTYPE);
        out_syms[i + 1].st_shndx = 1; /* Default to first section */
    }

    /* 8. Build Section Name String Table (.shstrtab) */
    shstrtab_buf = (char*)malloc(4096);
    shstrtab_buf[0] = '\0';
    shstrtab_len = 1;

    /* Add section names to .shstrtab */
    for (i = 0; i < g_out_sec_count; i++) {
        size_t nlen = strlen(g_out_secs[i].name) + 1;
        memcpy(shstrtab_buf + shstrtab_len, g_out_secs[i].name, nlen);
        shstrtab_len += nlen;
    }

    sh_name_symtab = (uint32_t)shstrtab_len;
    memcpy(shstrtab_buf + shstrtab_len, ".symtab\0", 8);
    shstrtab_len += 8;

    sh_name_shstrtab = (uint32_t)shstrtab_len;
    memcpy(shstrtab_buf + shstrtab_len, ".shstrtab\0", 10);
    shstrtab_len += 10;

    sh_name_strtab = (uint32_t)shstrtab_len;
    memcpy(shstrtab_buf + shstrtab_len, ".strtab\0", 8);
    shstrtab_len += 8;

    /* 9. Layout output file offsets */
    total_filesz = 0;
    total_memsz = 0;

    for (i = 0; i < g_out_sec_count; i++) {
        out_sec_t *sec = &g_out_secs[i];
        sec->offset = 0x1000 + (sec->vaddr - image_base);
        if (sec->type != SHT_NOBITS) {
            total_filesz = (sec->vaddr + sec->size) - image_base;
        }
        total_memsz = (sec->vaddr + sec->size) - image_base;
    }

    file_offset = ALIGN_UP(0x1000 + total_filesz, 4);

    symtab_offset = file_offset;
    symtab_size = (uint32_t)(out_sym_count * sizeof(Elf32_Sym));
    file_offset += ALIGN_UP(symtab_size, 4);

    shstrtab_offset = file_offset;
    shstrtab_size = (uint32_t)shstrtab_len;
    file_offset += ALIGN_UP(shstrtab_size, 4);

    strtab_offset = file_offset;
    strtab_size = (uint32_t)strtab_len;
    file_offset += ALIGN_UP(strtab_size, 4);

    /* 10. Prepare Section Headers */
    out_sh_count = 0;

    /* [0] NULL section */
    memset(&out_shdrs[out_sh_count++], 0, sizeof(Elf32_Shdr));

    {
        uint32_t name_pos = 1;
        for (i = 0; i < g_out_sec_count; i++) {
            out_sec_t *sec = &g_out_secs[i];
            Elf32_Shdr *shdr = &out_shdrs[out_sh_count++];

            shdr->sh_name = name_pos;
            name_pos += (uint32_t)(strlen(sec->name) + 1);

            shdr->sh_type = sec->type;
            shdr->sh_flags = sec->flags;
            shdr->sh_addr = sec->vaddr;
            shdr->sh_offset = sec->offset;
            shdr->sh_size = sec->size;
            shdr->sh_link = 0;
            shdr->sh_info = 0;
            shdr->sh_addralign = sec->align;
            shdr->sh_entsize = 0;
        }
    }

    /* .symtab section */
    {
        Elf32_Shdr *shdr = &out_shdrs[out_sh_count++];
        shdr->sh_name = sh_name_symtab;
        shdr->sh_type = SHT_SYMTAB;
        shdr->sh_flags = 0;
        shdr->sh_addr = 0;
        shdr->sh_offset = symtab_offset;
        shdr->sh_size = symtab_size;
        shdr->sh_link = g_out_sec_count + 3; /* Index of .strtab */
        shdr->sh_info = 1;
        shdr->sh_addralign = 4;
        shdr->sh_entsize = sizeof(Elf32_Sym);
    }

    /* .shstrtab section */
    {
        Elf32_Shdr *shdr = &out_shdrs[out_sh_count++];
        shdr->sh_name = sh_name_shstrtab;
        shdr->sh_type = SHT_STRTAB;
        shdr->sh_flags = 0;
        shdr->sh_addr = 0;
        shdr->sh_offset = shstrtab_offset;
        shdr->sh_size = shstrtab_size;
        shdr->sh_link = 0;
        shdr->sh_info = 0;
        shdr->sh_addralign = 1;
        shdr->sh_entsize = 0;
    }

    /* .strtab section */
    {
        Elf32_Shdr *shdr = &out_shdrs[out_sh_count++];
        shdr->sh_name = sh_name_strtab;
        shdr->sh_type = SHT_STRTAB;
        shdr->sh_flags = 0;
        shdr->sh_addr = 0;
        shdr->sh_offset = strtab_offset;
        shdr->sh_size = strtab_size;
        shdr->sh_link = 0;
        shdr->sh_info = 0;
        shdr->sh_addralign = 1;
        shdr->sh_entsize = 0;
    }

    /* 11. Prepare ELF Header and Program Headers */
    memset(&out_ehdr, 0, sizeof(out_ehdr));
    memcpy(out_ehdr.e_ident, "\x7f\x45\x4c\x46\x01\x01\x01\x00\x00\x00\x00\x00\x00\x00\x00\x00", 16);
    out_ehdr.e_type = ET_EXEC;
    out_ehdr.e_machine = EM_386;
    out_ehdr.e_version = EV_CURRENT;
    out_ehdr.e_entry = entry_point;
    out_ehdr.e_phoff = sizeof(Elf32_Ehdr);
    out_ehdr.e_shoff = file_offset;
    out_ehdr.e_flags = 0;
    out_ehdr.e_ehsize = sizeof(Elf32_Ehdr);
    out_ehdr.e_phentsize = sizeof(Elf32_Phdr);
    out_ehdr.e_phnum = 2;
    out_ehdr.e_shentsize = sizeof(Elf32_Shdr);
    out_ehdr.e_shnum = (uint16_t)out_sh_count;
    out_ehdr.e_shstrndx = (uint16_t)(g_out_sec_count + 2);

    /* LOAD Program Header */
    memset(&out_phdrs[0], 0, sizeof(Elf32_Phdr));
    out_phdrs[0].p_type = PT_LOAD;
    out_phdrs[0].p_offset = 0x1000;
    out_phdrs[0].p_vaddr = image_base;
    out_phdrs[0].p_paddr = image_base;
    out_phdrs[0].p_filesz = total_filesz;
    out_phdrs[0].p_memsz = total_memsz;
    out_phdrs[0].p_flags = PF_R | PF_W | PF_X;
    out_phdrs[0].p_align = 4096;

    /* GNU_STACK Program Header */
    memset(&out_phdrs[1], 0, sizeof(Elf32_Phdr));
    out_phdrs[1].p_type = PT_GNU_STACK;
    out_phdrs[1].p_flags = PF_R | PF_W;

    /* 12. Write Output Executable */
    f_out = fopen(output_file, "wb");
    if (!f_out) {
        fprintf(stderr, "Error creating output file: %s\n", output_file);
        return 1;
    }

    /* Write ELF Header */
    fwrite(&out_ehdr, 1, sizeof(out_ehdr), f_out);

    /* Write Program Headers */
    fwrite(out_phdrs, 1, sizeof(out_phdrs), f_out);

    /* Pad up to first section offset (0x1000) */
    {
        uint8_t zero = 0;
        long cur = ftell(f_out);
        while (cur < 0x1000) {
            fwrite(&zero, 1, 1, f_out);
            cur++;
        }
    }

    /* Write Section Data */
    for (i = 0; i < g_out_sec_count; i++) {
        out_sec_t *sec = &g_out_secs[i];
        if (sec->type != SHT_NOBITS && sec->data && sec->size > 0) {
            fseek(f_out, sec->offset, SEEK_SET);
            fwrite(sec->data, 1, sec->size, f_out);
        }
    }

    /* Write .symtab */
    fseek(f_out, symtab_offset, SEEK_SET);
    fwrite(out_syms, 1, symtab_size, f_out);

    /* Write .shstrtab */
    fseek(f_out, shstrtab_offset, SEEK_SET);
    fwrite(shstrtab_buf, 1, shstrtab_size, f_out);

    /* Write .strtab */
    fseek(f_out, strtab_offset, SEEK_SET);
    fwrite(strtab_buf, 1, strtab_size, f_out);

    /* Write Section Headers */
    fseek(f_out, file_offset, SEEK_SET);
    fwrite(out_shdrs, 1, out_sh_count * sizeof(Elf32_Shdr), f_out);

    fclose(f_out);

    /* Cleanup */
    for (i = 0; i < g_out_sec_count; i++) {
        if (g_out_secs[i].data) free(g_out_secs[i].data);
    }
    for (f = 0; f < g_input_count; f++) {
        if (g_inputs[f].data) free(g_inputs[f].data);
    }
    free(out_syms);
    free(strtab_buf);
    free(shstrtab_buf);

    printf("Linked %s: entry=0x%08x, base=0x%08x, filesz=%u, memsz=%u (%d symbols)\n",
           output_file, entry_point, image_base, total_filesz, total_memsz, g_global_count);

    return 0;
}
