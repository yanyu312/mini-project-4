#define _CRT_SECURE_NO_WARNINGS
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <math.h>
#include <stdbool.h>
#include "logger.h"

#define ALPHABET 256
#define EOF_MARK 256           // Huffman 內部用的 EOF 符號
#define MAX_SYMBOLS (ALPHABET+1)

typedef struct Node {
    long long freq;
    int symbol;                // 0..255 或 EOF_MARK；內部節點為 -1
    int minSym;                // 子樹中最小的 symbol（做 tie-break）
    struct Node *l, *r;
} Node;

typedef struct {
    Node **a;
    int n, cap;
} MinHeap;

/* ----------------- 小工具 ----------------- */
static Node* node_new(long long f, int sym, Node *l, Node *r) {
    Node *p = (Node*)malloc(sizeof(Node));
    p->freq = f; p->symbol = sym; p->l = l; p->r = r;
    int ml = l ? l->minSym : 1000000;
    int mr = r ? r->minSym : 1000000;
    if (sym >= 0) p->minSym = sym;
    else p->minSym = (ml < mr ? ml : mr);
    return p;
}
static void tree_free(Node *p){ if(!p) return; tree_free(p->l); tree_free(p->r); free(p); }
static int cmp_node(Node *x, Node *y){
    if (x->freq != y->freq) return (x->freq < y->freq) ? -1 : 1;
    // tie-break：minSym 小者在前，確保可重現性
    if (x->minSym != y->minSym) return (x->minSym < y->minSym) ? -1 : 1;
    return 0;
}

/* ----------------- 最小堆 ----------------- */
static void heap_init(MinHeap *h, int cap){ h->a=(Node**)malloc(sizeof(Node*)*cap); h->n=0; h->cap=cap; }
static void heap_free(MinHeap *h){ free(h->a); }
static void heap_push(MinHeap *h, Node *v){
    if(h->n==h->cap){ h->cap*=2; h->a=(Node**)realloc(h->a,sizeof(Node*)*h->cap); }
    int i=h->n++; h->a[i]=v;
    while(i>0){
        int p=(i-1)/2;
        if (cmp_node(h->a[i], h->a[p])>=0) break;
        Node* t=h->a[i]; h->a[i]=h->a[p]; h->a[p]=t; i=p;
    }
}
static Node* heap_pop(MinHeap *h){
    if(h->n==0) return NULL;
    Node* ret=h->a[0];
    h->a[0]=h->a[--h->n];
    int i=0;
    while(1){
        int l=i*2+1, r=i*2+2, m=i;
        if(l<h->n && cmp_node(h->a[l],h->a[m])<0) m=l;
        if(r<h->n && cmp_node(h->a[r],h->a[m])<0) m=r;
        if(m==i) break;
        Node* t=h->a[i]; h->a[i]=h->a[m]; h->a[m]=t; i=m;
    }
    return ret;
}

/* ----------------- 產生 codeword ----------------- */
typedef struct { char *s; } Str;
static void gen_codes_rec(Node *p, char *buf, int d, Str code[MAX_SYMBOLS]){
    if(!p) return;
    if(p->l==NULL && p->r==NULL && p->symbol>=0){
        buf[d]='\0';
        code[p->symbol].s = (char*)malloc(d+1);
        strcpy(code[p->symbol].s, d?buf:"0"); // 單一符號邊界：給 '0'
        return;
    }
    buf[d]='0'; gen_codes_rec(p->l, buf, d+1, code);
    buf[d]='1'; gen_codes_rec(p->r, buf, d+1, code);
}
static void gen_codes(Node *root, Str code[MAX_SYMBOLS]){
    for(int i=0;i<MAX_SYMBOLS;i++) code[i].s=NULL;
    char buf[2048];
    gen_codes_rec(root, buf, 0, code);
}

/* ----------------- CSV symbol 轉義 ----------------- */
/* 將單一 byte b 轉為可逆字串：\n \r \t \\ \, \" 其他不可列印→\xNN */
static void symbol_to_esc(unsigned char b, char dst[8]) {
    switch (b) {
        case '\n': strcpy(dst, "\\n");  break;
        case '\r': strcpy(dst, "\\r");  break;
        case '\t': strcpy(dst, "\\t");  break;
        case '\\': strcpy(dst, "\\\\"); break;
        case ',':  strcpy(dst, "\\,");  break;
        case '\"': strcpy(dst, "\\\""); break;
        default:
            if (b >= 32 && b <= 126) {
                dst[0] = (char)b; dst[1] = '\0';
            } else {
                static const char hex[]="0123456789abcdef";
                dst[0]='\\'; dst[1]='x';
                dst[2]=hex[(b>>4)&0xF]; dst[3]=hex[b&0xF];
                dst[4]='\0';
            }
    }
}

/* ----------------- 位元寫出 ----------------- */
typedef struct {
    FILE *f; unsigned acc; int nbits;
    long long total_bits;
} BitW;

static void bw_init(BitW *bw, FILE *f){ bw->f=f; bw->acc=0; bw->nbits=0; bw->total_bits=0; }
static void bw_put(BitW *bw, int bit){
    bw->acc = (bw->acc<<1) | (bit&1);
    bw->nbits++;
    bw->total_bits++;
    if(bw->nbits==8){
        fputc((unsigned char)bw->acc, bw->f);
        bw->acc=0; bw->nbits=0;
    }
}
static void bw_put_code(BitW *bw, const char *code){
    for(const char *p=code; *p; ++p) bw_put(bw, *p=='1');
}
static void bw_flush_zero(BitW *bw){
    if(bw->nbits>0){
        while(bw->nbits!=0) bw_put(bw, 0);
    }
}

/* ----------------- 統計與建樹 ----------------- */
static Node* build_huffman(long long freq[MAX_SYMBOLS], int *unique_out){
    MinHeap h; heap_init(&h, 512);
    int unique=0;
    for(int s=0;s<MAX_SYMBOLS;s++){
        if(freq[s]>0){
            heap_push(&h, node_new(freq[s], s, NULL, NULL));
            unique++;
        }
    }
    if(unique==0){ heap_free(&h); return NULL; }
    while(h.n>=2){
        Node *a=heap_pop(&h), *b=heap_pop(&h);
        heap_push(&h, node_new(a->freq+b->freq, -1, a, b));
    }
    Node *root=heap_pop(&h);
    heap_free(&h);
    if(unique_out) *unique_out=unique;
    return root;
}

static int code_len(const char *s){ return (int)strlen(s); }

/* 用於輸出 codebook 的排序：count asc，其次 symbol 字串 asc */
typedef struct {
    char esc[8];
    int symbol;
    long long cnt;
    double prob;
    const char *code;
    double selfinfo;
} Row;

static int cmp_row(const void *A, const void *B){
    const Row *a=(const Row*)A, *b=(const Row*)B;
    if(a->cnt!=b->cnt) return (a->cnt<b->cnt)?-1:1;
    return strcmp(a->esc, b->esc);
}

/* ----------------- 主流程 ----------------- */
int main(int argc, char **argv){
    if(argc<4){
        fprintf(stderr, "Usage: %s in_fn cb_fn enc_fn\n", argv[0]);
        return 1;
    }
    const char *in_fn = argv[1];
    const char *cb_fn = argv[2];
    const char *enc_fn= argv[3];

    log_info("encoder", "start input_file=%s", in_fn);

    /* 讀檔 → 統計 histogram */
    FILE *fin = fopen(in_fn, "rb");
    if(!fin){ log_error("encoder","open input failed file=%s", in_fn); return 2; }

    long long freq[MAX_SYMBOLS]={0};
    long long total=0;
    int c;
    while( (c=fgetc(fin)) != EOF ){
        freq[(unsigned char)c]++;
        total++;
    }
    // EOF 當作一種 symbol（Method 2），出現一次
    freq[EOF_MARK]=1; total+=1;
    fclose(fin);

    log_info("encoder","count_symbols num_symbols=%lld (including EOF) ", total);

    int unique=0;
    Node *root = build_huffman(freq, &unique);
    if(!root){ log_error("encoder","huffman build failed"); return 3; }
    log_info("encoder","build_huffman_tree unique_symbols=%d done", unique);

    /* 產生 codeword */
    Str code[MAX_SYMBOLS]; gen_codes(root, code);

    /* 統計各種指標 */
    double entropy=0.0;
    long long total_bits_huff=0;
    for(int s=0;s<MAX_SYMBOLS;s++){
        if(freq[s]==0) continue;
        double p=(double)freq[s]/(double)total;
        entropy += (p>0)? (-log(p)/log(2.0)):0.0;
        total_bits_huff += freq[s]*code_len(code[s].s?code[s].s:"");
    }
    double perplexity = pow(2.0, entropy);

    // fixed code bits per symbol（針對 unique 種數，含 EOF）
    int fixed_bits = 0;
    while ((1 << fixed_bits) < unique) fixed_bits++;
    long long total_bits_fixed = (long long)fixed_bits * total;


    double huff_bps = (double)total_bits_huff/(double)total;
    double fixed_bps= (double)total_bits_fixed/(double)total;
    double compression_ratio   = fixed_bps / huff_bps;        // >1 表示壓縮
    double compression_factor  = (double)total_bits_huff/(double)total_bits_fixed;
    double saving_percentage   = 1.0 - compression_factor;

    /* 輸出 codebook.csv */
    FILE *cb = fopen(cb_fn, "wb");
    if(!cb){ log_error("encoder","open codebook failed file=%s", cb_fn); return 4; }

    Row *rows = (Row*)malloc(sizeof(Row)*unique);
    int ridx=0;
    for(int s=0;s<MAX_SYMBOLS;s++){
        if(freq[s]==0) continue;
        if(s==EOF_MARK){
            strcpy(rows[ridx].esc, "<EOF>");
        }else{
            symbol_to_esc((unsigned char)s, rows[ridx].esc);
        }
        rows[ridx].symbol = s;
        rows[ridx].cnt    = freq[s];
        rows[ridx].prob   = (double)freq[s]/(double)total;
        rows[ridx].code   = code[s].s?code[s].s:"";
        rows[ridx].selfinfo = (rows[ridx].prob>0)? (-log(rows[ridx].prob)/log(2.0)) : 0.0;
        ridx++;
    }
    qsort(rows, unique, sizeof(Row), cmp_row);

    for(int i=0;i<unique;i++){
        fprintf(cb, "\"%s\",%lld,%.15f,\"%s\",%0.15f\n",
            rows[i].esc, rows[i].cnt, rows[i].prob, rows[i].code, rows[i].selfinfo);
    }
    fclose(cb);
    log_info("encoder","generate_codebook output_codebook=%s", cb_fn);

    /* 輸出 encoded.bin */
    fin = fopen(in_fn, "rb");
    if(!fin){ log_error("encoder","reopen input failed"); return 5; }
    FILE *fenc = fopen(enc_fn, "wb");
    if(!fenc){ fclose(fin); log_error("encoder","open encoded failed file=%s", enc_fn); return 6; }

    BitW bw; bw_init(&bw, fenc);
    while( (c=fgetc(fin)) != EOF ){
        bw_put_code(&bw, code[(unsigned char)c].s);
    }
    // 寫入 EOF 碼
    bw_put_code(&bw, code[EOF_MARK].s);
    bw_flush_zero(&bw);
    fclose(fin);
    fclose(fenc);

    log_info("encoder","encode_to_bitstream output_encoded=%s total_bits=%lld",
             enc_fn, bw.total_bits);

    /* metrics summary */
    log_info("metrics","summary input_file=%s output_codebook=%s output_encoded=%s "
                      "num_symbols=%lld unique_symbols=%d fixed_code_bits_per_symbol=%.1f "
                      "entropy_bits_per_symbol=%.6f perplexity=%.6f "
                      "huffman_bits_per_symbol=%.6f total_bits_fixed=%lld total_bits_huffman=%lld "
                      "compression_ratio=%.6f compression_factor=%.6f saving_percentage=%.6f",
             in_fn, cb_fn, enc_fn, total, unique, (double)fixed_bits,
             entropy, perplexity, huff_bps, total_bits_fixed, total_bits_huff,
             compression_ratio, compression_factor, saving_percentage);

    log_info("encoder","finish status=ok");

    for(int s=0;s<MAX_SYMBOLS;s++) if(code[s].s) free(code[s].s);
    tree_free(root);
    free(rows);
    return 0;
}
