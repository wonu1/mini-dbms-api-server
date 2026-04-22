#include <stdio.h>
#include <stdlib.h>

#define DEFAULT_COUNT 10000
#define DEFAULT_TABLE "users"

int main(int argc, char *argv[]) {
    int count = DEFAULT_COUNT;
    const char *table = DEFAULT_TABLE;
    char out_path[256];
    FILE *fp;

    if (argc > 1) count = atoi(argv[1]);
    if (argc > 2) table = argv[2];

    if (count <= 0) {
        fprintf(stderr, "Usage: %s [count] [table]\n", argv[0]);
        return 1;
    }

    snprintf(out_path, sizeof(out_path), "samples/bench_%s.sql", table);

    fp = fopen(out_path, "w");
    if (!fp) {
        fprintf(stderr, "gen_data: cannot open '%s'\n", out_path);
        return 1;
    }

    printf("Generating %d INSERT statements for table '%s'...\n", count, table);

    for (int i = 1; i <= count; i++) {
        int age = 18 + ((i - 1) % 63);

        fprintf(fp,
                "INSERT INTO %s (name, age, email) VALUES "
                "('user_%07d', %d, 'user%d@example.com');\n",
                table, i, age, i);

        if (i % 100000 == 0) {
            printf("  %d / %d (%.0f%%)\n",
                   i, count, (double)i / count * 100.0);
        }
    }

    fclose(fp);
    printf("Done: %s\n", out_path);
    printf("Run with: ./sqlp %s\n", out_path);
    return 0;
}
