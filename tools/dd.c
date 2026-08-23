/*
 * This is free and unencumbered software released into the public domain.
 * GEMIOS Custom DD Tool
 * Pure ANSI C90 implementation of standard dd data duplicator & converter.
 */

#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>
#include <errno.h>

#define STATUS_DEFAULT 0
#define STATUS_NONE    1
#define STATUS_NOXFER  2
#define STATUS_PROGRESS 3

#define CONV_NOTRUNC 0x01
#define CONV_SYNC    0x02
#define CONV_NOERROR 0x04
#define CONV_FSYNC   0x08

/* Parse number with unit multiplier */
static long parse_size(const char *str) {
    char *endptr;
    long val;
    if (!str || !*str) return 0;
    val = strtol(str, &endptr, 0);
    while (*endptr) {
        if (*endptr == 'c' || *endptr == 'C') {
            val *= 1;
            endptr++;
        } else if (*endptr == 'w' || *endptr == 'W') {
            val *= 2;
            endptr++;
        } else if (*endptr == 'b' || *endptr == 'B') {
            if (*(endptr + 1) == '\0') {
                val *= 512;
                endptr++;
            } else {
                endptr++;
            }
        } else if (*endptr == 'k' || *endptr == 'K') {
            val *= 1024;
            endptr++;
            if (*endptr == 'b' || *endptr == 'B' || *endptr == 'i' || *endptr == 'I') endptr++;
        } else if (*endptr == 'm' || *endptr == 'M') {
            val *= (1024 * 1024);
            endptr++;
            if (*endptr == 'b' || *endptr == 'B' || *endptr == 'i' || *endptr == 'I') endptr++;
        } else if (*endptr == 'g' || *endptr == 'G') {
            val *= (1024 * 1024 * 1024);
            endptr++;
            if (*endptr == 'b' || *endptr == 'B' || *endptr == 'i' || *endptr == 'I') endptr++;
        } else if (*endptr == 'x' || *endptr == '*') {
            long mult;
            endptr++;
            mult = parse_size(endptr);
            return val * mult;
        } else {
            endptr++;
        }
    }
    return val;
}

static void print_stats(long in_full, long in_part, long out_full, long out_part,
                        unsigned long total_bytes, double elapsed_sec, int status_level) {
    if (status_level == STATUS_NONE) return;

    fprintf(stderr, "%ld+%ld records in\n", in_full, in_part);
    fprintf(stderr, "%ld+%ld records out\n", out_full, out_part);

    if (status_level != STATUS_NOXFER) {
        if (elapsed_sec > 0.000001) {
            double rate = (double)total_bytes / elapsed_sec;
            if (rate >= 1024.0 * 1024.0 * 1024.0) {
                fprintf(stderr, "%lu bytes copied, %.4f s, %.1f GB/s\n",
                        total_bytes, elapsed_sec, rate / (1024.0 * 1024.0 * 1024.0));
            } else if (rate >= 1024.0 * 1024.0) {
                fprintf(stderr, "%lu bytes copied, %.4f s, %.1f MB/s\n",
                        total_bytes, elapsed_sec, rate / (1024.0 * 1024.0));
            } else if (rate >= 1024.0) {
                fprintf(stderr, "%lu bytes copied, %.4f s, %.1f kB/s\n",
                        total_bytes, elapsed_sec, rate / 1024.0);
            } else {
                fprintf(stderr, "%lu bytes copied, %.4f s, %.0f B/s\n",
                        total_bytes, elapsed_sec, rate);
            }
        } else {
            fprintf(stderr, "%lu bytes copied, 0.0000 s, inf B/s\n", total_bytes);
        }
    }
}

int main(int argc, char **argv) {
    const char *if_path = NULL;
    const char *of_path = NULL;
    long ibs = 512;
    long obs = 512;
    long count = -1;
    long skip = 0;
    long seek = 0;
    int status_level = STATUS_DEFAULT;
    int conv_flags = 0;
    int in_fd = -1;
    int out_fd = -1;
    int close_in = 0;
    int close_out = 0;
    int is_dev_zero = 0;
    char *in_buf = NULL;
    char *out_buf = NULL;
    long in_full = 0;
    long in_part = 0;
    long out_full = 0;
    long out_part = 0;
    unsigned long total_bytes = 0;
    clock_t start_clk;
    clock_t end_clk;
    double elapsed_sec;
    int i;

    for (i = 1; i < argc; i++) {
        char *arg = argv[i];
        if (strncmp(arg, "if=", 3) == 0) {
            if_path = arg + 3;
        } else if (strncmp(arg, "of=", 3) == 0) {
            of_path = arg + 3;
        } else if (strncmp(arg, "bs=", 3) == 0) {
            long sz = parse_size(arg + 3);
            if (sz > 0) {
                ibs = sz;
                obs = sz;
            }
        } else if (strncmp(arg, "ibs=", 4) == 0) {
            long sz = parse_size(arg + 4);
            if (sz > 0) ibs = sz;
        } else if (strncmp(arg, "obs=", 4) == 0) {
            long sz = parse_size(arg + 4);
            if (sz > 0) obs = sz;
        } else if (strncmp(arg, "count=", 6) == 0) {
            count = parse_size(arg + 6);
        } else if (strncmp(arg, "skip=", 5) == 0 || strncmp(arg, "iseek=", 6) == 0) {
            skip = parse_size(strchr(arg, '=') + 1);
        } else if (strncmp(arg, "seek=", 5) == 0 || strncmp(arg, "oseek=", 6) == 0) {
            seek = parse_size(strchr(arg, '=') + 1);
        } else if (strncmp(arg, "status=", 7) == 0) {
            const char *st = arg + 7;
            if (strcmp(st, "none") == 0) status_level = STATUS_NONE;
            else if (strcmp(st, "noxfer") == 0) status_level = STATUS_NOXFER;
            else if (strcmp(st, "progress") == 0) status_level = STATUS_PROGRESS;
            else status_level = STATUS_DEFAULT;
        } else if (strncmp(arg, "conv=", 5) == 0) {
            char *p = arg + 5;
            char *tok = strtok(p, ",");
            while (tok) {
                if (strcmp(tok, "notrunc") == 0) conv_flags |= CONV_NOTRUNC;
                else if (strcmp(tok, "sync") == 0) conv_flags |= CONV_SYNC;
                else if (strcmp(tok, "noerror") == 0) conv_flags |= CONV_NOERROR;
                else if (strcmp(tok, "fsync") == 0 || strcmp(tok, "fdatasync") == 0) conv_flags |= CONV_FSYNC;
                tok = strtok(NULL, ",");
            }
        } else if (strcmp(arg, "--help") == 0) {
            fprintf(stderr, "Usage: %s [if=FILE] [of=FILE] [bs=BYTES] [count=N] [skip=N] [seek=N] [status=LEVEL] [conv=CONVS]\n", argv[0]);
            return 0;
        } else {
            fprintf(stderr, "dd: unrecognized option '%s'\n", arg);
            return 1;
        }
    }

    if (ibs <= 0 || obs <= 0) {
        fprintf(stderr, "dd: invalid block size\n");
        return 1;
    }

    /* Allocate buffer */
    in_buf = (char*)malloc(ibs);
    if (!in_buf) {
        fprintf(stderr, "dd: memory allocation failed\n");
        return 1;
    }

    if (ibs != obs) {
        out_buf = (char*)malloc(obs);
        if (!out_buf) {
            fprintf(stderr, "dd: memory allocation failed\n");
            free(in_buf);
            return 1;
        }
    }

    /* Open input */
    if (!if_path || strcmp(if_path, "-") == 0) {
        in_fd = STDIN_FILENO;
    } else if (strcmp(if_path, "/dev/zero") == 0) {
        is_dev_zero = 1;
        memset(in_buf, 0, ibs);
        in_fd = -1;
    } else {
        in_fd = open(if_path, O_RDONLY);
        if (in_fd < 0) {
            fprintf(stderr, "dd: failed to open '%s': %s\n", if_path, strerror(errno));
            free(in_buf);
            if (out_buf) free(out_buf);
            return 1;
        }
        close_in = 1;
    }

    /* Open output */
    if (!of_path || strcmp(of_path, "-") == 0) {
        out_fd = STDOUT_FILENO;
    } else {
        int flags = O_WRONLY | O_CREAT;
        if (!(conv_flags & CONV_NOTRUNC)) {
            flags |= O_TRUNC;
        }
        out_fd = open(of_path, flags, 0644);
        if (out_fd < 0) {
            fprintf(stderr, "dd: failed to open '%s': %s\n", of_path, strerror(errno));
            if (close_in && in_fd >= 0) close(in_fd);
            free(in_buf);
            if (out_buf) free(out_buf);
            return 1;
        }
        close_out = 1;
    }

    start_clk = clock();

    /* Seek output if requested */
    if (seek > 0) {
        off_t seek_offset = (off_t)seek * (off_t)obs;
        if (lseek(out_fd, seek_offset, SEEK_SET) == (off_t)-1) {
            /* If seeking fails on a pipe or special file, write zeros */
            long k;
            char *zbuf = (char*)calloc(1, obs);
            if (zbuf) {
                for (k = 0; k < seek; k++) {
                    if (write(out_fd, zbuf, obs) < 0) break;
                }
                free(zbuf);
            }
        }
    }

    /* Skip input if requested */
    if (skip > 0 && in_fd >= 0 && !is_dev_zero) {
        off_t skip_offset = (off_t)skip * (off_t)ibs;
        if (lseek(in_fd, skip_offset, SEEK_SET) == (off_t)-1) {
            long k;
            for (k = 0; k < skip; k++) {
                ssize_t r = read(in_fd, in_buf, ibs);
                if (r <= 0) break;
            }
        }
    }

    /* Data Transfer Loop */
    if (is_dev_zero) {
        /* /dev/zero fast path */
        long blocks_remaining = count;
        memset(in_buf, 0, ibs);

        while (count < 0 || blocks_remaining > 0) {
            ssize_t written = write(out_fd, in_buf, ibs);
            if (written < 0) {
                fprintf(stderr, "dd: error writing '%s': %s\n", of_path ? of_path : "stdout", strerror(errno));
                break;
            }
            if ((size_t)written == (size_t)ibs) {
                in_full++;
                out_full++;
            } else {
                in_part++;
                out_part++;
            }
            total_bytes += written;
            if (count > 0) {
                blocks_remaining--;
            }
        }
    } else {
        /* Regular input stream */
        long blocks_read = 0;
        while (count < 0 || blocks_read < count) {
            ssize_t nread = read(in_fd, in_buf, ibs);
            if (nread < 0) {
                if (errno == EINTR) continue;
                fprintf(stderr, "dd: error reading '%s': %s\n", if_path ? if_path : "stdin", strerror(errno));
                if (conv_flags & CONV_NOERROR) {
                    continue;
                }
                break;
            }
            if (nread == 0) {
                /* End of file */
                break;
            }

            if ((size_t)nread == (size_t)ibs) {
                in_full++;
            } else {
                in_part++;
                if (conv_flags & CONV_SYNC) {
                    memset(in_buf + nread, 0, ibs - nread);
                    nread = ibs;
                }
            }

            /* Write buffer to output */
            {
                ssize_t nwritten = 0;
                while (nwritten < nread) {
                    ssize_t w = write(out_fd, in_buf + nwritten, nread - nwritten);
                    if (w < 0) {
                        if (errno == EINTR) continue;
                        fprintf(stderr, "dd: error writing '%s': %s\n", of_path ? of_path : "stdout", strerror(errno));
                        break;
                    }
                    nwritten += w;
                }
                total_bytes += nwritten;
                if ((size_t)nwritten == (size_t)obs) {
                    out_full++;
                } else {
                    out_part++;
                }
            }

            blocks_read++;
        }
    }

    if (conv_flags & CONV_FSYNC) {
        fsync(out_fd);
    }

    end_clk = clock();
    elapsed_sec = (double)(end_clk - start_clk) / CLOCKS_PER_SEC;

    print_stats(in_full, in_part, out_full, out_part, total_bytes, elapsed_sec, status_level);

    if (close_in && in_fd >= 0) close(in_fd);
    if (close_out && out_fd >= 0) close(out_fd);
    free(in_buf);
    if (out_buf) free(out_buf);

    return 0;
}
