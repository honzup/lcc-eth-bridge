// Host test for the pure GridConnect codec:  gcc -I main test/test_gridconnect.c main/gridconnect.c -o /tmp/gctest && /tmp/gctest
#include "gridconnect.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>

int main(void) {
    char buf[40];
    can_frame_t b;

    // round-trip an extended, LCC-style frame
    can_frame_t a = { .id = 0x19170640, .ext = true, .dlc = 6,
                      .data = {0x05,0x01,0x01,0x01,0x07,0x01} };
    assert(gc_encode(&a, buf, sizeof buf) > 0);
    assert(strcmp(buf, ":X19170640N050101010701;") == 0);
    assert(gc_decode(buf, &b) == 0);
    assert(b.ext && b.id == 0x19170640 && b.dlc == 6 && memcmp(b.data, a.data, 6) == 0);

    // zero-data frame
    can_frame_t z = { .id = 0x1F, .ext = true, .dlc = 0 };
    assert(gc_encode(&z, buf, sizeof buf) > 0);
    assert(strcmp(buf, ":X0000001FN;") == 0);
    assert(gc_decode(":X0000001FN;", &b) == 0 && b.dlc == 0);

    // leading framing whitespace tolerated
    assert(gc_decode("\r\n:X0000001FN;", &b) == 0);

    // malformed inputs are rejected (trust boundary)
    assert(gc_decode(":X0000001FN", &b) == -1);                     // no terminator
    assert(gc_decode(":X0000001GN;", &b) == -1);                    // bad hex
    assert(gc_decode(":X00000001N001122334455667788;", &b) == -1);  // 9 data bytes
    assert(gc_decode("X0000001FN;", &b) == -1);                     // missing ':'
    assert(gc_encode(&a, buf, 4) == -1);                            // buffer too small

    printf("gridconnect: all tests passed\n");
    return 0;
}
