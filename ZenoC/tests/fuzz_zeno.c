/* ZenoC deterministic standalone fuzzer.
 *
 * Seeded PRNG (no external dependency, reproducible on any platform/CI):
 * mutates an embedded corpus and drives the three hostile-input surfaces —
 * the JSON parser, the completion parser and the JSON escaper. Intended to
 * run under ASan/UBSan in CI; any crash reproduces from the seed. */

#include "zeno_internal.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static uint32_t fuzz_state = 0x5EEDC0DEU;

static uint32_t fuzz_next(void) {
    fuzz_state ^= fuzz_state << 13;
    fuzz_state ^= fuzz_state >> 17;
    fuzz_state ^= fuzz_state << 5;
    return fuzz_state;
}

static const char *corpus[] = {
    "{\"a\":[1,2,{\"b\":\"c\"}],\"d\":true,\"e\":null}",
    "[[{\"k\":\"v\\n\\t\\u0041\"},3.14e-2,false]]",
    "{\"tool_calls\":[{\"id\":\"c1\",\"function\":{\"name\":\"read_text_file\",\"arguments\":\"{}\"}}]}",
    "{\"choices\":[{\"message\":{\"content\":\"hi\",\"tool_calls\":[{\"id\":\"x\",\"function\":{\"name\":\"write_text_file\",\"arguments\":\"{\\\"relative_path\\\":\\\"a.txt\\\",\\\"content\\\":\\\"b\\\"}\"}}]}}],\"usage\":{\"prompt_tokens\":3,\"completion_tokens\":4}}",
    "\"escaped \\\" quote \\\\ slash \\u00e9\"",
    "[\"nested\",{\"deep\":[{\"deeper\":[1,2,3,{\"x\":\"y\"}]}]}]",
    "{\"usage\":{\"prompt_tokens\":10,\"completion_tokens\":5},\"truncated\":\"%s%.99999d\"}",
    "{}", "[]", "null", "123", "\"solo\"", "[,", "{\"k\":}", "{\"a\":1,}",
};

static char buffer[4096];

static size_t mutate(const char *seed) {
    size_t len = strlen(seed);
    if (len == 0 || len >= sizeof(buffer)) { memcpy(buffer, "{}", 3); return 2; }
    memcpy(buffer, seed, len + 1);
    int mutations = (int)(fuzz_next() % 8U);
    for (int m = 0; m < mutations && len > 0; m++) {
        size_t position = fuzz_next() % len;
        switch (fuzz_next() % 5U) {
            case 0: buffer[position] = (char)(32U + fuzz_next() % 95U); break;
            case 1: memmove(&buffer[position], &buffer[position + 1], len - position); len--; buffer[len] = '\0'; break;
            case 2:
                if (len + 1 < sizeof(buffer)) {
                    memmove(&buffer[position + 1], &buffer[position], len - position);
                    buffer[position] = (char)(32U + fuzz_next() % 95U);
                    len++; buffer[len] = '\0';
                }
                break;
            case 3: buffer[position] = "{}[]\":,0ntf-."[(fuzz_next() % 12U)]; break;
            default: if (len + 4 < sizeof(buffer)) { memcpy(&buffer[position], "\\u00", 4); memmove(&buffer[position + 4], &buffer[position], len - position); len += 4; buffer[len] = '\0'; } break;
        }
    }
    buffer[len < sizeof(buffer) ? len : sizeof(buffer) - 1] = '\0';
    return len < sizeof(buffer) ? len : sizeof(buffer) - 1;
}

int main(int argc, char **argv) {
    long iterations = argc > 1 ? atol(argv[1]) : 20000;
    if (iterations < 1) iterations = 1;
    for (long round = 0; round < iterations; round++) {
        const char *seed = corpus[fuzz_next() % (sizeof(corpus) / sizeof(corpus[0]))];
        (void)mutate(seed);
        /* Target 1: the JSON parser is the hostile-input frontier. */
        char *error = NULL;
        ZjNode *node = zj_parse(buffer, &error);
        if (node != NULL) {
            char *serialized = zj_stringify_compact(node);
            if (serialized != NULL) {
                char *reerror = NULL;
                ZjNode *reparsed = zj_parse(serialized, &reerror);
                /* Round-trip stability: reparse of serialized output must be
                 * byte-identical when valid. */
                if (reparsed != NULL) {
                    char *again = zj_stringify_compact(reparsed);
                    if (again == NULL || strcmp(again, serialized) != 0) { fprintf(stderr, "FUZZ: round-trip instability round %ld\n", round); return 1; }
                    free(again);
                } else if (reerror == NULL) { fprintf(stderr, "FUZZ: clean failure invariant round %ld\n", round); return 1; }
                free(reerror);
                zj_free(reparsed);
            }
            free(serialized);
            zj_free(node);
        } else if (error == NULL) { fprintf(stderr, "FUZZ: failed parse without error text round %ld\n", round); return 1; }
        free(error);
        /* Target 2: completion parsing (structured tool calls + fallbacks). */
        char *content = NULL; char *calls = NULL;
        (void)zeno_parse_completion(buffer, &content, &calls);
        free(content); free(calls);
        /* Target 3: escaping (feeds every string we ever emit). */
        char *escaped = zeno_json_escape(buffer);
        free(escaped);
    }
    printf("zenoc_fuzz: %ld mutations across %zu seeds, no violations\n", iterations, sizeof(corpus) / sizeof(corpus[0]));
    return 0;
}
