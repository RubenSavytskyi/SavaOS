#include "string.h"
#include "types.h"

#define SERIAL_COM1_BASE 0x3F8

static void serial_init(void) {
    outb(SERIAL_COM1_BASE + 1, 0x00);    
    outb(SERIAL_COM1_BASE + 3, 0x80);    
    outb(SERIAL_COM1_BASE + 0, 0x03);    
    outb(SERIAL_COM1_BASE + 1, 0x00);    
    outb(SERIAL_COM1_BASE + 3, 0x03);    
    outb(SERIAL_COM1_BASE + 2, 0xC7);    
    outb(SERIAL_COM1_BASE + 4, 0x0B);    
}

static void serial_putchar(char c) {
    static int inited = 0;
    if (!inited) {
        serial_init();
        inited = 1;
    }
    while ((inb(SERIAL_COM1_BASE + 5) & 0x20) == 0); 
    outb(SERIAL_COM1_BASE, c);
}

int kstrlen(const char* s) {
    int n = 0; while (s[n]) n++; return n;
}
void kstrcpy(char* dst, const char* src) {
    while ((*dst++ = *src++));
}
void kstrncpy(char* dst, const char* src, int n) {
    int i = 0;
    while (i < n-1 && src[i]) { dst[i] = src[i]; i++; }
    dst[i] = 0;
}
int kstrcmp(const char* a, const char* b) {
    while (*a && *a == *b) { a++; b++; }
    return (unsigned char)*a - (unsigned char)*b;
}
int kstrncmp(const char* a, const char* b, int n) {
    for (int i = 0; i < n; i++) {
        if (a[i] != b[i]) return (unsigned char)a[i] - (unsigned char)b[i];
        if (!a[i]) return 0;
    }
    return 0;
}
char* kstrcat(char* dst, const char* src) {
    char* p = dst;
    while (*p) p++;
    while ((*p++ = *src++));
    return dst;
}
void* kmemset(void* ptr, int val, size_t n) {
    u8* p = (u8*)ptr;
    while (n--) *p++ = (u8)val;
    return ptr;
}
void* kmemcpy(void* dst, const void* src, size_t n) {
    u8* d = (u8*)dst; const u8* s = (const u8*)src;
    while (n--) *d++ = *s++;
    return dst;
}
int kmemcmp(const void* a, const void* b, size_t n) {
    const u8* x = (const u8*)a; const u8* y = (const u8*)b;
    while (n--) { if (*x != *y) return *x - *y; x++; y++; }
    return 0;
}

void kitoa(int val, char* buf, int base) {
    if (val < 0 && base == 10) { *buf++ = '-'; val = -val; }
    kutoa((u32)val, buf, base);
}
void kutoa(u32 val, char* buf, int base) {
    char tmp[34]; int i = 0;
    if (!val) { buf[0]='0'; buf[1]=0; return; }
    while (val) {
        int d = val % base;
        tmp[i++] = d < 10 ? '0'+d : 'a'+d-10;
        val /= base;
    }
    for (int j = 0; j < i; j++) buf[j] = tmp[i-1-j];
    buf[i] = 0;
}
int katoi(const char* s) {
    int r = 0, neg = 0;
    if (*s == '-') { neg = 1; s++; }
    while (*s >= '0' && *s <= '9') r = r*10 + (*s++ - '0');
    return neg ? -r : r;
}

void str_copy_n(char *dst, const char *src, int max) {
    int i = 0;
    if (max <= 0) return;
    while (src[i] && i < max - 1) { dst[i] = src[i]; i++; }
    dst[i] = 0;
}

int str_cmp_simple(const char *a, const char *b) {
    if (!a) a = "";
    if (!b) b = "";
    for (int i = 0; a[i] || b[i]; i++) {
        unsigned char ca = (unsigned char)a[i];
        unsigned char cb = (unsigned char)b[i];
        if (ca != cb) return (int)ca - (int)cb;
        if (!a[i] && !b[i]) break;
    }
    return 0;
}

int kvsnprintf(char* buf, int size, const char* fmt, __builtin_va_list ap) {
    int pos = 0;
#define PUT(c) if (pos < size-1) buf[pos++] = (c)
    char tmp[34];
    while (*fmt && pos < size-1) {
        if (*fmt != '%') { PUT(*fmt++); continue; }
        fmt++;
        switch (*fmt++) {
            case 'd': { int v = __builtin_va_arg(ap, int); kitoa(v,tmp,10); for(char*p=tmp;*p;p++) PUT(*p); break; }
            case 'u': { u32 v = __builtin_va_arg(ap, u32); kutoa(v,tmp,10); for(char*p=tmp;*p;p++) PUT(*p); break; }
            case 'x': { u32 v = __builtin_va_arg(ap, u32); kutoa(v,tmp,16); for(char*p=tmp;*p;p++) PUT(*p); break; }
            case 'X': { u32 v = __builtin_va_arg(ap, u32); kutoa(v,tmp,16);
                        for(char*p=tmp;*p;p++) { PUT(*p>='a'?*p-32:*p); } break; }
            case 's': { const char* s = __builtin_va_arg(ap, const char*); while(*s) PUT(*s++); break; }
            case 'c': { PUT((char)__builtin_va_arg(ap, int)); break; }
            case '%': PUT('%'); break;
            default:  PUT('?'); break;
        }
    }
#undef PUT
    buf[pos] = 0;
    return pos;
}

int ksnprintf(char* buf, int size, const char* fmt, ...) {
    __builtin_va_list ap;
    __builtin_va_start(ap, fmt);
    int len = kvsnprintf(buf, size, fmt, ap);
    __builtin_va_end(ap);
    return len;
}

void kprintf(const char* fmt, ...) {
    char buf[512];
    __builtin_va_list ap;
    __builtin_va_start(ap, fmt);
    int len = kvsnprintf(buf, sizeof(buf), fmt, ap);
    __builtin_va_end(ap);
    for (int i = 0; i < len && i < (int)sizeof(buf); i++) {
        serial_putchar(buf[i]);
    }
}
