#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>
#include <wchar.h>
#include <locale.h>
#include <io.h>
#include <fcntl.h>

//------------------------------------------------------
// 結構定義
//------------------------------------------------------
typedef struct {
    unsigned code;
    unsigned char symbol;
} CodeEntry;

//------------------------------------------------------
// 函式宣告
//------------------------------------------------------
unsigned char decode_symbol(const char *tok);
int parse_codebook(const char *filename, CodeEntry **entries, int *code_len);
void build_lookup_table(CodeEntry *entries, int count, int code_len, unsigned char *table);
int decode_file(const char *input_bin, const char *output_txt, unsigned char *table, int code_len);

//------------------------------------------------------
// 主程式入口
//------------------------------------------------------
int main(int argc, char **argv) {
    const char *encoded_file = "encoded.bin";
    const char *out_file = "output.txt";
    const char *codebook_file = "codebook.csv";

    if (argc == 4) {
        encoded_file = argv[3];
        codebook_file = argv[2];
        out_file = argv[1];
    } else if (argc != 1) {
        fprintf(stderr, "Usage: %s [output.txt codebook.csv encoded.bin]\n", argv[0]);
        return 1;
    }

    CodeEntry *entries = NULL;
    int code_len = -1;
    int entry_count = parse_codebook(codebook_file, &entries, &code_len);
    if (entry_count <= 0) {
        fprintf(stderr, "Failed to parse codebook\n");
        return 1;
    }

    size_t table_size = 1u << code_len;
    unsigned char *lookup = calloc(table_size, sizeof(unsigned char));
    if (!lookup) {
        free(entries);
        fprintf(stderr, "Memory allocation failed\n");
        return 1;
    }

    build_lookup_table(entries, entry_count, code_len, lookup);
    free(entries);

    if (decode_file(encoded_file, out_file, lookup, code_len) != 0) {
        fprintf(stderr, "Decoding failed\n");
        free(lookup);
        return 1;
    }

    free(lookup);
    printf("Decoding complete.\n");
    return 0;
}

//------------------------------------------------------
// decode_symbol：解析符號字串（例如 "\n", "\t", "\x41"）
//------------------------------------------------------
unsigned char decode_symbol(const char *tok) {
    if (strcmp(tok, "\\r") == 0) return '\r';
    if (strcmp(tok, "\\n") == 0) return '\n';
    if (strcmp(tok, "\\t") == 0) return '\t';
    if (strncmp(tok, "\\x", 2) == 0 && isdigit(tok[2]) && isxdigit(tok[3]) && tok[4] == '\0') {
        return (unsigned char)strtol(tok + 2, NULL, 16);
    }
    return tok[0] ? (unsigned char)tok[0] : '\"';
}

//------------------------------------------------------
// parse_codebook：從 CSV 讀取符號與對應的編碼
//------------------------------------------------------
int parse_codebook(const char *filename, CodeEntry **entries, int *code_len) {
    FILE *FP = fopen(filename, "r");
    if (!FP) return -1;

    size_t capacity = 64, count = 0;
    *entries = malloc(capacity * sizeof(CodeEntry));
    if (!*entries) { fclose(FP); return -1; }

    char line[256];
    while (fgets(line, sizeof(line), FP)) {
        char symbol_str[32], code_str[32];
        int freq;
        double prob;
        unsigned amp;

        if (sscanf(line, "\"%31[^\"]\",%d,%lf,\"%31[^\"]\"", symbol_str, &freq, &prob, code_str) != 4)
            continue;

        unsigned code = 0;
        for (int i = 0; code_str[i]; ++i)
            code = (code << 1) | (code_str[i] == '1');
        
        if (*code_len == -1) *code_len = strlen(code_str);
        else if (*code_len != (int)strlen(code_str)) {
            fclose(FP);
            free(*entries);
            return -2;
        }

        if (count == capacity) {
            capacity *= 2;
            CodeEntry *temp = realloc(*entries, capacity * sizeof(CodeEntry));
            if (!temp) { fclose(FP); free(*entries); return -1; }
            *entries = temp;
        }

        (*entries)[count].code = code;
        (*entries)[count].symbol = decode_symbol(symbol_str);
        count++;
    }

    fclose(FP);
    return count;
}

//------------------------------------------------------
// build_lookup_table：依據 codebook 建立查表陣列
//------------------------------------------------------
void build_lookup_table(CodeEntry *entries, int count, int code_len, unsigned char *table) {
    size_t size = 1u << code_len;
    for (int i = 0; i < count; ++i) {
        if (entries[i].code < size)
            table[entries[i].code] = entries[i].symbol;
    }
}

//------------------------------------------------------
// decode_file：使用查表法解碼輸入的二進位檔
//------------------------------------------------------
int decode_file(const char *input_bin, const char *output_txt, unsigned char *table, int code_len) {
    FILE *OUT = fopen(output_txt, "wb");
    FILE *IN = fopen(input_bin, "rb");
    if (!IN || !OUT) return -1;

    unsigned acc = 0;
    int bits = 0;
    int byte;
    unsigned eof_code = (1u << code_len) - 1;

    while ((byte = fgetc(IN)) != EOF) {
        for (int i = 7; i >= 0; --i) {
            acc = (acc << 1) | ((byte >> i) & 1);
            if (++bits == code_len) {
                if (acc == eof_code) {
                    fclose(IN);
                    fclose(OUT);
                    return 0;
                }
                fputc(table[acc], OUT);
                acc = 0;
                bits = 0;
            }
        }
    }

    fclose(IN);
    fclose(OUT);
    return 0;
}
