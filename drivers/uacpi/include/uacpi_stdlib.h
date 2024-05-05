#pragma once

#include <stddef.h>
#include <NanoPrintf.h>

#define UACPI_PRIx64 "lx"
#define UACPI_PRIX64 "lX"
#define UACPI_PRIu64 "lu"
#define PRIu64 "lu"

#define uacpi_offsetof offsetof
#define uacpi_snprintf npf_snprintf

#ifdef __cplusplus
extern "C" {
#endif

void* uacpi_memmove(void* dest, const void* src, size_t count);
void* uacpi_memset(void* dest, int ch, size_t count);
void* uacpi_memcpy(void* dest, const void* src, size_t count);
int uacpi_memcmp(const void* lhs, const void* rhs, size_t count);
int uacpi_strncmp(const char* lhs, const char* rhs, size_t count);
int uacpi_strcmp(const char* lhs, const char* rhs);
size_t uacpi_strnlen(const char* str, size_t strsz);
size_t uacpi_strlen(const char* str);

#ifdef __cplusplus
}
#endif
