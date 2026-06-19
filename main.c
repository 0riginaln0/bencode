// main.c - example usage of bj.h (bencode reader)
// Compile: cc -std=c11 -O2 main.c -o bparse
// Run: ./bparse file.torrent
#include <stdio.h>

#define BENCODE_IMPLEMENTATION
#include "bencode.h"

static void print_indent(int d) {
    for (int i = 0; i < d; i++) putchar(' ');
}

static void print_str(Bencode_Value s) {
    fwrite(s.start, 1, (size_t)(s.end - s.start), stdout);
}

/* forward */
static void print_value(Bencode_Reader *r, Bencode_Value v, int indent);

static void print_list(Bencode_Reader *r, Bencode_Value list, int indent) {
    print_indent(indent); printf("list [\n");
    Bencode_Value item;
    while (bj_iter_list(r, list, &item)) {
        print_value(r, item, indent + 2);
    }
    print_indent(indent); printf("]\n");
}

static void print_dict(Bencode_Reader *r, Bencode_Value dict, int indent) {
    print_indent(indent); printf("dict {\n");
    Bencode_Value key, val;
    while (bj_iter_dict(r, dict, &key, &val)) {
        print_indent(indent + 2); printf("key: ");
        print_str(key);
        printf("\n");
        print_value(r, val, indent + 4);
    }
    print_indent(indent); printf("}\n");
}

static void print_value(Bencode_Reader *r, Bencode_Value v, int indent) {
    switch (v.type) {
    case BJ_STR:
        print_indent(indent);
        printf("string (len=%ld): ", (long)(v.end - v.start));
        print_str(v);
        printf("\n");
        break;
    case BJ_INT:
        print_indent(indent);
        printf("int: ");
        fwrite(v.start, 1, (size_t)(v.end - v.start), stdout);
        printf("\n");
        break;
    case BJ_LIST:
        print_list(r, v, indent);
        break;
    case BJ_DICT:
        print_dict(r, v, indent);
        break;
    case BJ_END:
        print_indent(indent);
        printf("end\n");
        break;
    case BJ_ERROR:
    default:
        print_indent(indent);
        printf("error at position\n");
        break;
    }
}

static char *read_file(const char *path, size_t *out_len) {
    FILE *f = strcmp(path, "-") == 0 ? stdin : fopen(path, "rb");
    if (!f) return NULL;
    if (f == stdin) {
        /* read stdin into buffer */
        size_t cap = 4096, len = 0;
        char *buf = malloc(cap);
        while (!feof(f)) {
            size_t n = fread(buf + len, 1, cap - len, f);
            len += n;
            if (len == cap) {
                cap *= 2;
                buf = realloc(buf, cap);
            }
        }
        *out_len = len;
        return buf;
    } else {
        fseek(f, 0, SEEK_END);
        long sz = ftell(f);
        fseek(f, 0, SEEK_SET);
        if (sz < 0) { fclose(f); return NULL; }
        char *buf = malloc((size_t)sz);
        if (!buf) { fclose(f); return NULL; }
        if (fread(buf, 1, (size_t)sz, f) != (size_t)sz) { free(buf); fclose(f); return NULL; }
        fclose(f);
        *out_len = (size_t)sz;
        return buf;
    }
}

int main(int argc, char **argv) {
    char *data = "d1:a1:al1:b1:bee";

    printf("%zu\n", strlen(data));
    fflush(stdout);

    Bencode_Reader r = bj_reader(data, strlen(data));
    Bencode_Value v = bj_read(&r);
    if (v.type == BJ_ERROR) {
        int col;
        bj_location(&r, &col);
        fprintf(stderr, "parse error at col %d: %s\n", col, r.error ? r.error : "unknown");
        return 2;
    }

    print_value(&r, v, 0);
    printf("%zu\n", strlen(data));

    return 0;
}
