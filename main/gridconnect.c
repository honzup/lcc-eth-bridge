#include "gridconnect.h"

static int hexval(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}
static char hexdig(int v) { return "0123456789ABCDEF"[v & 0xF]; }

int gc_encode(const can_frame_t *f, char *out, size_t cap) {
    if (!f || !out || f->dlc > 8) return -1;
    int idlen = f->ext ? 8 : 3;
    size_t need = 1 + 1 + idlen + 1 + (size_t)f->dlc * 2 + 1 + 1; // :X id N data ; \0
    if (cap < need) return -1;
    char *p = out;
    *p++ = ':';
    *p++ = f->ext ? 'X' : 'S';
    for (int i = idlen - 1; i >= 0; --i) *p++ = hexdig((f->id >> (i * 4)) & 0xF);
    *p++ = f->rtr ? 'R' : 'N';
    for (int i = 0; i < f->dlc; ++i) { *p++ = hexdig(f->data[i] >> 4); *p++ = hexdig(f->data[i] & 0xF); }
    *p++ = ';';
    *p = '\0';
    return (int)(p - out);
}

int gc_decode(const char *line, can_frame_t *f) {
    if (!line || !f) return -1;
    const char *p = line;
    while (*p == ' ' || *p == '\r' || *p == '\n' || *p == '\t') ++p; // tolerate framing ws
    if (*p++ != ':') return -1;
    if (*p == 'X') f->ext = true; else if (*p == 'S') f->ext = false; else return -1;
    ++p;
    int idlen = f->ext ? 8 : 3;
    uint32_t id = 0;
    for (int i = 0; i < idlen; ++i) { int v = hexval(*p++); if (v < 0) return -1; id = (id << 4) | (uint32_t)v; }
    f->id = f->ext ? (id & 0x1FFFFFFFu) : (id & 0x7FFu);
    if (*p == 'N') f->rtr = false; else if (*p == 'R') f->rtr = true; else return -1;
    ++p;
    int n = 0;
    while (*p && *p != ';') {
        int hi = hexval(*p++); if (hi < 0) return -1;
        int lo = hexval(*p++); if (lo < 0) return -1;
        if (n >= 8) return -1;                 // >8 data bytes = not a classic CAN frame
        f->data[n++] = (uint8_t)((hi << 4) | lo);
    }
    if (*p != ';') return -1;                  // must terminate with ';'
    f->dlc = (uint8_t)(f->rtr ? 0 : n);
    return 0;
}
