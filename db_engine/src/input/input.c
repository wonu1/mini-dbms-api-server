#include <stdio.h>
#include <stdlib.h>
#include "../../include/interface.h"

/*
 * 이 파일은 SQL 파일 전체를 문자열로 읽어오는 역할만 한다.
 * CLI 엔진은 파일에 적힌 SQL을 한 번에 읽은 뒤 lexer/parser 단계로 넘긴다.
 */

/*
 * input_read_file
 * Read a whole file and return null-terminated SQL text.
 * Caller must free() the returned string.
 */
char *input_read_file(const char *path) {
    if (!path) return NULL;

    FILE *fp = fopen(path, "rb");
    if (!fp) {
        fprintf(stderr, "input: cannot open file '%s'\n", path);
        return NULL;
    }

    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        return NULL;
    }

    long size = ftell(fp);
    if (size < 0) {
        fclose(fp);
        return NULL;
    }

    if (fseek(fp, 0, SEEK_SET) != 0) {
        fclose(fp);
        return NULL;
    }

    char *buf = malloc((size_t)size + 1);
    if (!buf) {
        fclose(fp);
        return NULL;
    }

    size_t read = fread(buf, 1, (size_t)size, fp);
    if (ferror(fp) || read != (size_t)size) {
        free(buf);
        fclose(fp);
        return NULL;
    }

    buf[read] = '\0';
    fclose(fp);
    return buf;
}
