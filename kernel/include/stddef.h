#pragma once
/* Both <stdint.h> and <stddef.h> are not ideal implementations of what's required
 * for a standards compliant freestanding set of headers. These only implement what's needed
 * for northport to build.
 */

typedef __SIZE_TYPE__ size_t;
typedef __PTRDIFF_TYPE__ ptrdiff_t;

#ifdef __cplusplus
typedef decltype(nullptr) nullptr_t;
#endif

#define NULL ((void*)0)

#define offsetof(s, m) __builtin_offsetof(s, m)
