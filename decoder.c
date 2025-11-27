#define _CRT_SECURE_NO_WARNINGS
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <ctype.h>
#include <stdbool.h>
#include "logger.h"

#define EOF_MARK 256

typedef struct Node {
    int symbol;         // 葉節點: 0..255 或 EOF_MARK；內部節點：-1
    struct Node *l, *r;
} Node;

static Node* node_new(int sym){ Node* n=(Node*)calloc(1,sizeof(Node)); n->symbol=sym; return n; }
static void tree_free(Node *n){ if(!n) return; tree_free(n->l); tree_free(n->r); free(n); }

static void tree_insert(Node **root, const char *code, int symbol) {
    if (!*root) *root = node_new(-1);
    Node *cur = *root;
    for (const char *p = code; *p; ++p) {
        if (*p == '0') { if (!cur->l) cur->l = node_new(-1); cur = cur->l; }
        else if (*p == '1') { if (!cur->r) cur->r = node_new(-1); cur = cur->r; }
    }
    cur->symbol = symbol;
}

/* --------- CSV 解析（支援 RFC 4180 引號欄位） --------- */
static void csv_next_field(char **cursor, char *dst, size_t dstsz) {
    char *p = *cursor;
    size_t k = 0;
    int quoted = 0;

    while (*p == ' ' || *p == '\t') p++;
    if (*p == '"') { quoted = 1; p++; }

    while (*p) {
        if (quoted) {
            if (*p == '"') {
                if (*(p+1) == '"') { // 內嵌雙引號
                    if (k + 1 < dstsz) dst[k++] = '"';
                    p += 2;
                } else {
                    p++;
                    while (*p == ' ' || *p == '\t') p++;
                    if (*p == ',') p++;
                    break;
                }
            } else {
                if (k + 1 < dstsz) dst[k++] = *p;
                p++;
            }
        } else {
            if (*p == ',' || *p == '\r' || *p == '\n') {
                if (*p == ',') p++;
                break;
            } else {
                if (k + 1 < dstsz) dst[k++] = *p;
                p++;
            }
        }
    }
    dst[k] = '\0';
    *cursor = p;
}

/* 轉回單一位元組/特殊符號 */
static int parse_symbol_token(const char *s) {
    if (strcmp(s, "<EOF>") == 0) return EOF_MARK;
    if (strcmp(s, "\\n")  == 0) return '\n';
    if (strcmp(s, "\\r")  == 0) return '\r';
    if (strcmp(s, "\\t")  == 0) return '\t';
    if (strcmp(s, "\\\\") == 0) return '\\';
    if (strcmp(s, "\\,")  == 0) return ',';
    if (strcmp(s, "\\\"") == 0) return '\"';
    if (s[0]=='\\' && s[1]=='x' && isxdigit((unsigned char)s[2]) && isxdigit((unsigned char)s[3]) && s[4]=='\0'){
        int hi = isdigit((unsigned char)s[2]) ? s[2]-'0' : 10 + (tolower(s[2])-'a');
        int lo = isdigit((unsigned char)s[3]) ? s[3]-'0' : 10 + (tolower(s[3])-'a');
        return (hi<<4)|lo;
    }
    return (unsigned char)s[0];
}

/* 載入 codebook 並建樹 */
static Node* load_codebook_and_build_tree(const char *cb_fn, int *entries) {
    FILE *fp = fopen(cb_fn, "rb");
    if (!fp) { log_error("decoder","open_codebook failed file=%s", cb_fn); return NULL; }
    log_info("decoder","load_codebook file=%s", cb_fn);

    Node *root = NULL;
    char line[4096];
    int cnt = 0;

    while (fgets(line, sizeof line, fp)) {
        if (line[0]=='\r' || line[0]=='\n' || line[0]=='\0') continue;

        char *cur = line;
        char f0[256], f1[64], f2[128], f3[2048], f4[128];
        csv_next_field(&cur, f0, sizeof f0); // symbol
        csv_next_field(&cur, f1, sizeof f1); // count
        csv_next_field(&cur, f2, sizeof f2); // prob
        csv_next_field(&cur, f3, sizeof f3); // codeword
        csv_next_field(&cur, f4, sizeof f4); // self-info

        if (f0[0]=='\0' || f3[0]=='\0') continue;
        int sym = parse_symbol_token(f0);
        tree_insert(&root, f3, sym);
        cnt++;
    }
    fclose(fp);
    if(entries) *entries = cnt;
    return root;
}

/* 依樹解碼 bitstream，遇到 EOF_MARK 結束 */
static int decode_bitstream(const char *enc_fn, const char *out_fn, Node *root) {
    FILE *fin = fopen(enc_fn, "rb");
    if (!fin) { log_error("decoder","open encoded failed file=%s", enc_fn); return -1; }
    FILE *fout = fopen(out_fn, "wb");
    if (!fout) { fclose(fin); log_error("decoder","open output failed file=%s", out_fn); return -1; }

    Node *cur = root; int outc = 0;
    int ch;
    while ((ch=fgetc(fin)) != EOF) {
        unsigned char byte = (unsigned char)ch;
        for (int b=7;b>=0;--b){
            int bit = (byte>>b)&1;
            cur = bit ? cur->r : cur->l;
            if (!cur) { // 不應發生：樹錯誤或 bitstream 損壞
                log_error("decoder","invalid_traverse");
                fclose(fin); fclose(fout); return -2;
            }
            if (cur->symbol != -1) {
                if (cur->symbol == EOF_MARK) {
                    fclose(fin); fclose(fout);
                    return outc;
                }
                fputc((unsigned char)cur->symbol, fout);
                outc++;
                cur = root;
            }
        }
    }
    fclose(fin); fclose(fout);
    return outc; // 若沒遇到 EOF_MARK，仍回傳已解碼數（視為不完整）
}

int main(int argc, char **argv){
    if(argc<4){
        fprintf(stderr, "Usage: %s out_fn cb_fn enc_fn\n", argv[0]);
        return 1;
    }
    const char *out_fn = argv[1];
    const char *cb_fn  = argv[2];
    const char *enc_fn = argv[3];

    log_info("decoder","start output_file=%s input_codebook=%s input_encoded=%s",
             out_fn, cb_fn, enc_fn);

    int entries=0;
    Node *root = load_codebook_and_build_tree(cb_fn, &entries);
    if(!root){ log_error("decoder","load_codebook failed status=error"); return 2; }

    log_info("decoder","build_tree entries=%d", entries);

    int n = decode_bitstream(enc_fn, out_fn, root);
    if(n < 0){
        log_error("decoder","decode failed status=error");
        tree_free(root);
        return 3;
    }

    log_info("metrics","summary input_encoded=%s input_codebook=%s output_file=%s num_decoded_symbols=%d status=ok",
             enc_fn, cb_fn, out_fn, n);
    log_info("decoder","finish status=ok");

    tree_free(root);
    return 0;
}
