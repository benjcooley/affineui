/*
 * bin2c — tiny build-time helper that turns a binary file into a C header
 * containing a byte array. Used to embed the vendored default font
 * (Roboto) into the AffineUI library so rendering needs no runtime asset
 * path. Self-contained: needs only the C compiler already in the build.
 *
 *   bin2c <input-file> <output-header> <symbol-name>
 *
 * Emits:
 *   static unsigned char <symbol>[] = { ... };
 *   static const unsigned int <symbol>_len = N;
 */
#include <stdio.h>

int main(int argc, char** argv) {
    if (argc != 4) {
        fprintf(stderr, "usage: bin2c <input> <output.h> <symbol>\n");
        return 1;
    }
    FILE* in = fopen(argv[1], "rb");
    if (!in) { fprintf(stderr, "bin2c: cannot open input '%s'\n", argv[1]); return 1; }
    FILE* out = fopen(argv[2], "w");
    if (!out) { fprintf(stderr, "bin2c: cannot open output '%s'\n", argv[2]); fclose(in); return 1; }

    fprintf(out, "// Auto-generated from %s by bin2c. Do not edit.\n", argv[1]);
    fprintf(out, "static unsigned char %s[] = {\n", argv[3]);

    unsigned long total = 0;
    int col = 0, c;
    while ((c = fgetc(in)) != EOF) {
        fprintf(out, "0x%02x,", (unsigned)(c & 0xff));
        if (++col == 20) { fputc('\n', out); col = 0; }
        ++total;
    }
    fprintf(out, "\n};\nstatic const unsigned int %s_len = %luu;\n", argv[3], total);

    fclose(in);
    fclose(out);
    return 0;
}
