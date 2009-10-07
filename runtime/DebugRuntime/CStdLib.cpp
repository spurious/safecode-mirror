//===------- CStdLib.cpp - CStdLib transform pass runtime functions -------===//
// 
//                          The SAFECode Compiler
//
// This file was developed by the LLVM research group and is distributed under
// the University of Illinois Open Source License. See LICENSE.TXT for details.
// 
//===----------------------------------------------------------------------===//
//
// This file provides all external functions included by the CStdLib pass.
//
//===----------------------------------------------------------------------===//

#include "safecode/Runtime/DebugRuntime.h"

#include <algorithm>
#include <cassert>
#include <cstring>
#include <iostream> // Debug

using namespace NAMESPACE_SC;

extern "C" {
  size_t strnlen(const char *s, size_t maxlen) {
    size_t i;
    for (i = 0; i < maxlen && s[i]; ++i)
      ;
    return i;
  }

  size_t strnlen_opt(const char *s, size_t maxlen) {
    const char *end = (const char *)memchr(s, '\0', maxlen);
    return (end ? end - s : maxlen);
  }
}

/**
 * Optimized inline assembly implementation of strncpy that returns the number
 * of characters copied (including \0)
 *
 * @param   dst      Destination string pointer
 * @param   src      Source string pointer
 * @param   size     Number of characters to copy
 * @return  Number of characters copied (including \0)
 */
static size_t strncpy_asm(char *dst, const char *src, size_t size) {
  long copied;

#if defined(i386) || defined(__i386__) || defined(__x86__)
  __asm__ __volatile__(
    "0: xorl      %%ecx, %%ecx      \n"
    "   cmpl      %%edi, %%ecx      \n"
    "   adcl      $0, %%ecx         \n"
    "   decl      %%edi             \n"
    "   testl     %%ecx, %%ecx      \n"
    "   je        1f                \n"
    "   movsbl    (%%edx), %%ecx    \n"
    "   movb      %%cl, (%%eax)     \n"
    "   incl      %%eax             \n"
    "   incl      %%edx             \n"
    "   testl     %%ecx, %%ecx      \n"
    "   jne       0b                \n"
    "1: subl      %%esi, %%eax      \n"
    : "=a"(copied)
    : "a"(dst), "S"(dst), "d"(src), "D"(size)
    : "%ecx", "memory"
  );
#else
  strncpy(dst, src, size);
  copied = strnlen(dst, size);
#endif

  return copied;
}

/**
 * Secure runtime wrapper function to replace memcpy()
 *
 * @param   dstPool  Pool handle for destination memory area
 * @param   srcPool  Pool handle for source memory area
 * @param   dst      Destination memory area
 * @param   src      Source memory area
 * @param   n        Maximum number of bytes to copy
 * @return  Destination memory area
 */
void *pool_memcpy(DebugPoolTy *dstPool, DebugPoolTy *srcPool, void *dst, const void *src, size_t n) {
  size_t dstSize = 0, srcSize = 0, stop = 0;
  void *dstBegin = dst, *dstEnd = NULL, *srcBegin = (char *)src, *srcEnd = NULL;

  assert(dstPool && srcPool && dst && src && "Null pool parameters!");

  // Retrieve destination memory area's bounds from pool handle.
  bool foundDst = dstPool->Objects.find(dstBegin, dstBegin, dstEnd);
  assert(foundDst && "Destination buffer not found in pool!");

  // Retrieve source memory area's bounds from pool handle.
  bool foundSrc = srcPool->Objects.find(srcBegin, srcBegin, srcEnd);
  assert(foundSrc && "Source string not found in pool!");

  assert(dstBegin <= dstEnd && "Destination pointer out of bounds!");
  assert(srcBegin <= srcEnd && "Source pointer out of bounds!");

  // Calculate the maximum number of bytes to copy.
  dstSize = (char *)dstEnd - (char *)dst + 1;
  srcSize = (char *)srcEnd - (char *)src + 1;
  assert(n <= srcSize && "Cannot copy more bytes than the size of the source!");

  stop = std::min(n, srcSize);
  assert(stop <= dstSize && "Copy violated destination bounds!");

  memcpy(dst, src, stop);

  return dst;
}

/**
 * Secure runtime wrapper function to replace memmove()
 *
 * @param   dstPool  Pool handle for destination memory area
 * @param   srcPool  Pool handle for source memory area
 * @param   dst      Destination memory area
 * @param   src      Source memory area
 * @param   n        Maximum number of bytes to copy
 * @return  Destination memory area
 */
void *pool_memmove(DebugPoolTy *dstPool, DebugPoolTy *srcPool, void *dst, const void *src, size_t n) {
  size_t dstSize = 0, srcSize = 0, stop = 0;
  void *dstBegin = dst, *dstEnd = NULL, *srcBegin = (char *)src, *srcEnd = NULL;

  assert(dstPool && srcPool && dst && src && "Null pool parameters!");

  // Retrieve destination memory area's bounds from pool handle.
  bool foundDst = dstPool->Objects.find(dstBegin, dstBegin, dstEnd);
  assert(foundDst && "Destination buffer not found in pool!");

  // Retrieve source memory area's bounds from pool handle.
  bool foundSrc = srcPool->Objects.find(srcBegin, srcBegin, srcEnd);
  assert(foundSrc && "Source string not found in pool!");

  assert(dstBegin <= dstEnd && "Destination pointer out of bounds!");
  assert(srcBegin <= srcEnd && "Source pointer out of bounds!");

  // Calculate the maximum number of bytes to copy.
  dstSize = (char *)dstEnd - (char *)dst + 1;
  srcSize = (char *)srcEnd - (char *)src + 1;
  assert(n <= srcSize && "Cannot copy more bytes than the size of the source!");

  stop = std::min(n, srcSize);
  assert(stop <= dstSize && "Copy violated destination bounds!");

  memmove(dst, src, stop);

  return dst;
}

/**
 * Secure runtime wrapper function to replace mempcpy()
 *
 * @param   dstPool  Pool handle for destination memory area
 * @param   srcPool  Pool handle for source memory area
 * @param   dst      Destination memory area
 * @param   src      Source memory area
 * @param   n        Maximum number of bytes to copy
 * @return  Destination memory area
 */
void *pool_mempcpy(DebugPoolTy *dstPool, DebugPoolTy *srcPool, void *dst, const void *src, size_t n) {
  size_t dstSize = 0, srcSize = 0, stop = 0;
  void *dstBegin = dst, *dstEnd = NULL, *srcBegin = (char *)src, *srcEnd = NULL;

  assert(dstPool && srcPool && dst && src && "Null pool parameters!");

  // Retrieve destination memory area's bounds from pool handle.
  bool foundDst = dstPool->Objects.find(dstBegin, dstBegin, dstEnd);
  assert(foundDst && "Destination buffer not found in pool!");

  // Retrieve source memory area's bounds from pool handle.
  bool foundSrc = srcPool->Objects.find(srcBegin, srcBegin, srcEnd);
  assert(foundSrc && "Source string not found in pool!");

  assert(dstBegin <= dstEnd && "Destination pointer out of bounds!");
  assert(srcBegin <= srcEnd && "Source pointer out of bounds!");

  // Calculate the maximum number of bytes to copy.
  dstSize = (char *)dstEnd - (char *)dst + 1;
  srcSize = (char *)srcEnd - (char *)src + 1;
  assert(n <= srcSize && "Cannot copy more bytes than the size of the source!");

  stop = std::min(n, srcSize);
  assert(stop <= dstSize && "Copy violated destination bounds!");

#ifndef _GNU_SOURCE
  mempcpy(dst, src, stop);
#endif

  return dst;
}

/**
 * Secure runtime wrapper function to replace memset()
 *
 * @param   sPool  Pool handle for s
 * @param   s      Pointer to memory area
 * @return  Pointer to memory area
 */
void *pool_memset(DebugPoolTy *sPool, void *s, int c, size_t n) {
  size_t sSize = 0, stop = 0;
  void *sBegin = s, *sEnd = NULL;

  assert(sPool && s && "Null pool parameters!");

  bool foundS = sPool->Objects.find(sBegin, sBegin, sEnd);
  assert(foundS && "S not found in pool!");
  assert(sBegin <= sEnd && "S pointer out of bounds!");

  sSize = (char *)sEnd - (char *)s + 1;
  assert(n <= sSize && "n larger than s!");

  stop = std::min(n, sSize);
  return memset(s, c, stop);
}

/**
 * Secure runtime wrapper function to replace strcpy()
 *
 * @param   dstPool  Pool handle for destination buffer
 * @param   srcPool  Pool handle for source string
 * @param   dst      Destination string pointer
 * @param   src      Source string pointer
 * @return  Destination string pointer
 */
char *pool_strcpy(DebugPoolTy *dstPool, DebugPoolTy *srcPool, char *dst, const char *src) {
  size_t copied = 0, dstSize = 0, srcSize = 0, stop = 0;
  void *dstBegin = dst, *dstEnd = NULL, *srcBegin = (char *)src, *srcEnd = NULL;

  // Ensure that all pointers.
  assert(dstPool && srcPool && dst && src && "Null pool parameters!");

  // Retrieve destination buffer's bounds from pool handle.
  bool foundDst = dstPool->Objects.find(dstBegin, dstBegin, dstEnd);
  assert(foundDst && "Destination buffer not found in pool!");

  // Retrieve source string's bounds from pool handle.
  bool foundSrc = srcPool->Objects.find(srcBegin, srcBegin, srcEnd);
  assert(foundSrc && "Source string not found in pool!");

  assert(dstBegin <= dstEnd && "Destination pointer out of bounds!");
  assert(srcBegin <= srcEnd && "Source pointer out of bounds!");

  // Calculate the maximum number of bytes to copy.
  dstSize = (char *)dstEnd - dst + 1;
  srcSize = (char *)srcEnd - (char *)src + 1;
  stop = std::min(dstSize, srcSize);

  // Copy the source string to the destination buffer and record the number of bytes copied (including \0).
  copied = strncpy_asm(dst, src, stop);

  std::cout << "Destination: " << dstSize << ", source: " << srcSize << ", copied: " << copied << std::endl;
  std::cout << dst[copied - 1] << std::endl;

  assert(!dst[copied - 1] && "Copy violated destination bounds!");

  return dst;
}

/**
 * Secure runtime wrapper function to replace strlen()
 *
 * @param   stringPool  Pool handle for the string
 * @param   string      String pointer
 * @return  Length of the string
 */
size_t pool_strlen(DebugPoolTy *stringPool, const char *string) {
  size_t len = 0, maxlen = 0;
  void *stringBegin = (char *)string, *stringEnd = NULL;

  assert(stringPool && string && "Null pool parameters!");

  bool foundString = stringPool->Objects.find(stringBegin, stringBegin, stringEnd);
  assert(foundString && "String not found in pool!");
  assert(stringBegin <= stringEnd && "String pointer out of bounds!");

  maxlen = (char *)stringEnd - (char *)string;
  len = strnlen(string, maxlen);
  assert((len < maxlen || !string[maxlen]) && "String not terminated within bounds!");

  return len;
}

/**
 * Secure runtime wrapper function to replace strncpy()
 *
 * @param   dstPool  Pool handle for destination buffer
 * @param   srcPool  Pool handle for source string
 * @param   dst      Destination string pointer
 * @param   src      Source string pointer
 * @param   n        Maximum number of bytes to copy
 * @return  Destination string pointer
 */
char *pool_strncpy(DebugPoolTy *dstPool, DebugPoolTy *srcPool, char *dst, const char *src, size_t n) {
  size_t copied = 0, dstSize = 0, srcSize = 0, stop = 0;
  void *dstBegin = dst, *dstEnd = NULL, *srcBegin = (char *)src, *srcEnd = NULL;

  assert(dstPool && srcPool && dst && src && "Null pool parameters!");

  // Retrieve destination buffer's bounds from pool handle.
  bool foundDst = dstPool->Objects.find(dstBegin, dstBegin, dstEnd);
  assert(foundDst && "Destination buffer not found in pool!");

  // Retrieve source string's bounds from pool handle.
  bool foundSrc = srcPool->Objects.find(srcBegin, srcBegin, srcEnd);
  assert(foundSrc && "Source string not found in pool!");

  assert(dstBegin <= dstEnd && "Destination pointer out of bounds!");
  assert(srcBegin <= srcEnd && "Source pointer out of bounds!");

  // Calculate the maximum number of bytes to copy.
  dstSize = (char *)dstEnd - dst + 1;
  srcSize = (char *)srcEnd - (char *)src + 1;
  stop = std::min(dstSize, srcSize);

  if (stop < n) {
    copied = strncpy_asm(dst, src, stop);
  std::cout << "HI! Destination: " << dstSize << ", source: " << srcSize << ", copied: " << copied << ", stop: " << stop << std::endl;
    assert(copied != stop && "Copy violated destination bounds!");
//    assert(!dst[copied - 1] && "Copy violated destination bounds!");
  } else {
    copied = strncpy_asm(dst, src, n);
  std::cout << "Destination: " << dstSize << ", source: " << srcSize << ", copied: " << copied << ", stop: " << stop << std::endl;

    // Possibly not NULL terminated
    if (copied > dstSize)
      assert(copied > dstSize && "Copy violated destination bounds!");
  }

  std::cout << dst[copied - 1] << std::endl;

  return dst;
}

/**
 * Secure runtime wrapper function to replace strnlen()
 *
 * @param   stringPool  Pool handle for the string
 * @param   string      String pointer
 * @return  Length of the string
 */
size_t pool_strnlen(DebugPoolTy *stringPool, const char *string, size_t maxlen) {
  size_t len = 0, difflen = 0;
  void *stringBegin = (char *)string, *stringEnd = NULL;

  assert(stringPool && string && "Null pool parameters!");

  bool foundString = stringPool->Objects.find(stringBegin, stringBegin, stringEnd);
  assert(foundString && "String not found in pool!");
  assert(stringBegin <= stringEnd && "String pointer out of bounds!");

  difflen = (char *)stringEnd - (char *)string;
  assert(difflen <= maxlen && "String in pool longer than maxlen!");
  len = strnlen(string, difflen);
  assert((len < difflen || !string[difflen]) && "String not terminated within bounds!");

  return len;
}
