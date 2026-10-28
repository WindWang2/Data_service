// Minimal declarations for the already supplied GNU libiconv Windows DLL.
// The ABI and exported names are verified against iconv-2.dll; no implementation is duplicated.
#pragma once
#include <stddef.h>
typedef void* iconv_t;
extern iconv_t libiconv_open(const char*, const char*);
extern size_t libiconv(iconv_t, char**, size_t*, char**, size_t*);
extern int libiconv_close(iconv_t);
#define iconv_open libiconv_open
#define iconv libiconv
#define iconv_close libiconv_close
