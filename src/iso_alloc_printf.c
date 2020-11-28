/* iso_alloc_printf.c - A secure memory allocator
 * Copyright 2020 - chris.rohlf@gmail.com */

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define INTERNAL_HIDDEN __attribute__((visibility("hidden")))


/* This primitive printf implementation is only ever called
 * called from the LOG and LOG_AND_ABORT macros. We need to
 * be able to print basic log messages without invoking
 * malloc() or we will deadlock anytime we want to log */
static int8_t fmt_buf[64];
static int8_t asc_hex[] = "0123456789abcdef";

INTERNAL_HIDDEN int8_t *_fmt(uint64_t n, uint32_t base) {
    int8_t *ptr;
    uint32_t count = 0;

    memset(fmt_buf, 0x0, sizeof(fmt_buf));
    ptr = &fmt_buf[63];

    while(n != 0) {
        *--ptr = asc_hex[n % base];
        n /= base;
        count++;
    };

    if(count == 0) {
        ptr = (int8_t *) "0";
    }

    return ptr;
}

INTERNAL_HIDDEN void _iso_alloc_printf(const char *f, ...) {
    if(f == NULL) {
        return;
    }

    uint64_t i;
    uint32_t j;
    char *s;
    va_list arg;
    va_start(arg, f);
    char _out[65535];
    char *p = _out;
    memset(_out, 0x0, sizeof(_out));

    for(const char *idx = f; *idx != '\0'; idx++) {
        if(p >= (char *) (_out + sizeof(_out))) {
            break;
        }
        while(*idx != '%' && *idx != '\0') {
            *p = *idx;
            p++;

            if(*idx == '\n') {
                break;
            }

            idx++;
        }

        idx++;
        p++;

        if(*idx == '\0') {
            break;
        }

        if(*idx == 'x' || *idx == 'p') {
            i = va_arg(arg, int64_t);
            s = (char *) _fmt(i, 16);
            strncpy(p, s, strlen(s));
            p += strlen(s);
        } else if(*idx == 'd' || *idx == 'u') {
            j = va_arg(arg, int32_t);

            if(0 > j) {
                j = -j;
                *p = '-';
                p++;
            }

            s = (char *) _fmt(j, 10);

            strncpy(p, s, strlen(s));
            p += strlen(s);
        } else if(*idx == 'l') {
            if(*(idx + 1) == 'd' || *(idx + 1) == 'u') {
                idx++;
            }

            i = va_arg(arg, int64_t);

            if(0 > i) {
                i = -i;
                *p = '-';
                p++;
            }

            s = (char *) _fmt(i, 10);

            strncpy(p, s, strlen(s));
            p += strlen(s);
        } else if(*idx == 's') {
            s = va_arg(arg, char *);

            if(s == NULL) {
                break;
            }

            strncpy(p, s, strlen(s));
            p += strlen(s);
        }
    }

    write(STDOUT_FILENO, _out, sizeof(_out));
    fflush(stdout);
    va_end(arg);
}
