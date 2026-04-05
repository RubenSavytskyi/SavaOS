#ifndef STRING_H
#define STRING_H

#include "types.h"

int    kstrlen(const char* s);
void   kstrcpy(char* dst, const char* src);
void   kstrncpy(char* dst, const char* src, int n);
int    kstrcmp(const char* a, const char* b);
int    kstrncmp(const char* a, const char* b, int n);
char*  kstrcat(char* dst, const char* src);
void*  kmemset(void* ptr, int val, size_t n);
void*  kmemcpy(void* dst, const void* src, size_t n);
int    kmemcmp(const void* a, const void* b, size_t n);

void   kitoa(int val, char* buf, int base);
void   kutoa(u32 val, char* buf, int base);
int    katoi(const char* s);

int    ksnprintf(char* buf, int size, const char* fmt, ...);

#endif
