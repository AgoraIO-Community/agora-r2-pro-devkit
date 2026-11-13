/* SPDX-License-Identifier: Apache-2.0 */
#include "device_identity.h"
#include "check.h"
#include <string.h>

static int identity(const char *input, char *output, size_t capacity) {
    FILE *file = tmpfile();
    CHECK(file);
    CHECK(fwrite(input, 1, strlen(input), file) == strlen(input));
    rewind(file);
    int result = rv1126b_identity_from_serial(file, output, capacity);
    CHECK(fclose(file) == 0);
    return result;
}

int main(void) {
    char output[33];
    CHECK(identity("processor: 0\nSerial\t: 0123456789abcdef\n", output, sizeof(output)) == 0);
    CHECK(!strcmp(output, "RV1126B-DC61B88C55DCABA433CD588C"));
    CHECK(identity("Serial: 0123456789ABCDEF\n", output, sizeof(output)) == 0);
    CHECK(!strcmp(output, "RV1126B-DC61B88C55DCABA433CD588C"));
    const char *bad[] = {"",
                         "Serial: 0000000000000000\n",
                         "Serial: FFFFFFFFFFFFFFFF\n",
                         "Serial: 0123456789abcdeg\n",
                         "Serial: 0123456789abcdef0\n",
                         "Serial: 0123456789abcdef\nSerial: 0123456789abcdef\n"};
    for (unsigned i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i) {
        strcpy(output, "unchanged");
        CHECK(identity(bad[i], output, sizeof(output)) < 0);
        CHECK(!strcmp(output, "unchanged"));
    }
    CHECK(identity("Serial: 0123456789ABCDEF\n", output, 32) < 0);
    CHECK(rv1126b_identity_from_mac("../etc", output, sizeof(output)) < 0);
    puts("Stable identity HMAC vector, normalization and invalid hardware IDs: PASS");
    return 0;
}
