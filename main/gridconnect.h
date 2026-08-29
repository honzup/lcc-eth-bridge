#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

// Pure CAN <-> GridConnect codec. No ESP dependencies — host-testable.
typedef struct {
    uint32_t id;    // 11-bit (std) or 29-bit (ext). LCC always uses ext.
    bool     ext;   // extended 29-bit ID
    bool     rtr;   // remote frame (unused by LCC; carried for completeness)
    uint8_t  dlc;   // 0..8
    uint8_t  data[8];
} can_frame_t;

// Encode a frame as a GridConnect line ":X........N....;", NUL-terminated.
// Returns the string length, or -1 if the frame is invalid or `cap` too small.
int gc_encode(const can_frame_t *f, char *out, size_t cap);

// Decode one complete GridConnect line (leading ':' .. terminating ';').
// Returns 0 on success, -1 if malformed. Hardened — this parses network input.
int gc_decode(const char *line, can_frame_t *f);
