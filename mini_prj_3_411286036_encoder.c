#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <ctype.h>

typedef struct {
    FILE *file;
    uint8_t buffer;
    int bits;
} BitStream;

void bitstream_init(BitStream *bs, FILE *f) {
    bs->file = f;
    bs->buffer = 0;
    bs->bits = 0;
}

void bitstream_flush(BitStream *bs) {
    if (bs->bits > 0) {
        bs->buffer <<= (8 - bs->bits);
        fputc(bs->buffer, bs->file);
        bs->buffer = 0;
        bs->bits = 0;
    }
}

void bitstream_write(BitStream *bs, unsigned value, int width) {
    for (int i = width - 1; i >= 0; --i) {
        bs->buffer = (bs->buffer << 1) | ((value >> i) & 1);
        if (++bs->bits == 8) {
            fputc(bs->buffer, bs->file);
            bs->buffer = 0;
            bs->bits = 0;
        }
    }
}

int bits_required(unsigned max) {
    int bits = 0;
    while ((1U << bits) <= max) ++bits;
    return bits;
}

typedef struct {
    unsigned char ch;
    size_t freq;
} CharStat;

int compare_freq(const void *a, const void *b) {
    const CharStat *x = a, *y = b;
    if (x->freq != y->freq) return (x->freq < y->freq) ? -1 : 1;
    return (x->ch < y->ch) ? -1 : 1;
}

const char *escape_char(unsigned char c, char buf[8]) {
    if (c == '\n') return "\\n";
    if (c == '\r') return "\\r";
    if (c == '\t') return "\\t";
    if (c == '\"') return "\\x22";/*雙引號excel和decoder太難處理，改為\x22*/
    if (isprint(c) && c != ',') {
        buf[0] = c;
        buf[1] = '\0';
        return buf;
    }
    static const char hex[] = "0123456789ABCDEF";
    buf[0] = '\\';
    buf[1] = 'x';
    buf[2] = hex[(c >> 4) & 0xF];
    buf[3] = hex[c & 0xF];
    buf[4] = '\0';
    return buf;
}

int main(int argc, char **argv) {
    const char *input = "input.txt";
    const char *codebook = "codebook.csv";
    const char *encoded = "encoded.bin";

    if (argc == 4) {
        input = argv[1];
        codebook = argv[2];
        encoded = argv[3];
    } else if (argc != 1) {
        fprintf(stderr, "Usage: %s input.txt codebook.csv encoded.bin\n", argv[0]);
        return 1;
    }

    FILE *FIN = fopen(input, "rb");
    if (!FIN) { perror("input"); return 1; }

    size_t total = 0, freq[256] = {0};
    int ch;
    while ((ch = fgetc(FIN)) != EOF) {
        freq[(unsigned char)ch]++;
        total++;
    }
    fclose(FIN);

    if (total == 0) {
        FILE *FC = fopen(codebook, "wb");
        if (FC) fclose(FC);
        FILE *FE = fopen(encoded, "wb");
        if (FE) {
            BitStream bs;
            bitstream_init(&bs, FE);
            bitstream_write(&bs, 1, 1);
            bitstream_flush(&bs);
            fclose(FE);
        }
        return 0;
    }

    CharStat stats[256];
    int unique = 0;
    for (int i = 0; i < 256; ++i) {
        if (freq[i]) {
            stats[unique].ch = (unsigned char)i;
            stats[unique].freq = freq[i];
            unique++;
        }
    }

    qsort(stats, unique, sizeof(CharStat), compare_freq);

    int bitlen = bits_required(unique);
    unsigned eof_code = (1U << bitlen) - 1;
    unsigned mapping[256];
    for (int i = 0; i < 256; ++i) mapping[i] = ~0U;

    unsigned code = 0;
    for (int i = 0; i < unique; ++i) {
        if (code == eof_code) code++;
        mapping[stats[i].ch] = code++;
    }

    FILE *FC = fopen(codebook, "wb");
    if (!FC) { perror("codebook"); return 1; }

    for (int i = 0; i < unique; ++i) {
        char esc[8];
        const char *sym = escape_char(stats[i].ch, esc);
        double prob = (double)stats[i].freq / total;
        unsigned val = mapping[stats[i].ch];
        char bits[32];
        for (int j = bitlen - 1; j >= 0; --j)
            bits[bitlen - 1 - j] = ((val >> j) & 1) ? '1' : '0';
        bits[bitlen] = '\0';
        fprintf(FC, "\"%s\",%zu,%.7f,\"%s\"\n", sym, stats[i].freq, prob, bits);
    }
    fclose(FC);

    FIN = fopen(input, "rb");
    if (!FIN) { perror("input reopen"); return 1; }
    FILE *FE = fopen(encoded, "wb");
    if (!FE) { perror("encoded"); fclose(FIN); return 1; }

    BitStream BS;
    bitstream_init(&BS, FE);

    while ((ch = fgetc(FIN)) != EOF) {
        bitstream_write(&BS, mapping[(unsigned char)ch], bitlen);
    }
    fclose(FIN);

    bitstream_write(&BS, eof_code, bitlen);
    bitstream_flush(&BS);
    fclose(FE);

    return 0;
}


