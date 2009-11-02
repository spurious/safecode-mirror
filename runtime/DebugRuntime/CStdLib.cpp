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

#include "DebugReport.h"
#include "PoolAllocator.h"

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
 * Function to search within the object and external object pools
 *
 * @param   pool       Pool handle
 * @param   poolBegin  Pointer to set to the beginning of the pool handle
 * @param   poolEnd    Pointer to set to the end of the pool handle
 * @return  Object found in pool
 */
bool pool_find(DebugPoolTy *pool, void *&poolBegin, void *&poolEnd) {
  // Retrieve memory area's bounds from pool handle.
  if (pool->Objects.find(poolBegin, poolBegin, poolEnd) || ExternalObjects.find(poolBegin, poolBegin, poolEnd))
    return true;

  return false;
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

  // Retrieve both the destination and source buffer's bounds from the pool handle.
  assert(pool_find(dstPool, dstBegin, dstEnd) && "Destination buffer not found in pool!");
  assert(pool_find(srcPool, srcBegin, srcEnd) && "Source string not found in pool!");

  // Check that both the destination and source pointers fall within their respective bounds.
  if (dstBegin > dstEnd) {
    std::cout << "Destination pointer out of bounds!\n";

    OutOfBoundsViolation v;

    v.type = ViolationInfo::FAULT_OUT_OF_BOUNDS,
    v.faultPC = __builtin_return_address(0),
    v.faultPtr = dstBegin,
    v.dbgMetaData = NULL,
    v.PoolHandle = dstPool,
    v.SourceFile = __FILE__,
    v.lineNo = __LINE__,
    v.objStart = dstBegin,
    v.objLen = (unsigned)((char *)dstEnd - (char *)dstBegin) + 1;

    ReportMemoryViolation(&v);
  }

  if (srcBegin > srcEnd) {
    std::cout << "Source pointer out of bounds!\n";

    OutOfBoundsViolation v;

    v.type = ViolationInfo::FAULT_OUT_OF_BOUNDS,
    v.faultPC = __builtin_return_address(0),
    v.faultPtr = srcBegin,
    v.dbgMetaData = NULL,
    v.PoolHandle = srcPool,
    v.SourceFile = __FILE__,
    v.lineNo = __LINE__,
    v.objStart = srcBegin,
    v.objLen = (unsigned)((char *)srcEnd - (char *)srcBegin) + 1;

    ReportMemoryViolation(&v);
  }

  // Calculate the maximum number of bytes to copy.
  dstSize = (char *)dstEnd - (char *)dst + 1;
  srcSize = (char *)srcEnd - (char *)src + 1;

  if (n > srcSize) {
    std::cout << "Cannot copy more bytes than the size of the source!\n";

    WriteOOBViolation v;

    v.type = ViolationInfo::FAULT_WRITE_OUT_OF_BOUNDS,
    v.faultPC = __builtin_return_address(0),
    v.faultPtr = srcBegin,
    v.SourceFile = __FILE__,
    v.lineNo = __LINE__,
    v.PoolHandle = srcPool,
    v.dstSize = dstSize,
    v.srcSize = srcSize,
    v.dbgMetaData = NULL;

    ReportMemoryViolation(&v);
  }

  stop = std::min(n, srcSize);

  if (stop > dstSize) {
    std::cout << "Copy violated destination bounds!\n";

    WriteOOBViolation v;

    v.type = ViolationInfo::FAULT_WRITE_OUT_OF_BOUNDS,
    v.faultPC = __builtin_return_address(0),
    v.faultPtr = srcBegin,
    v.SourceFile = __FILE__,
    v.lineNo = __LINE__,
    v.PoolHandle = srcPool,
    v.dstSize = dstSize,
    v.srcSize = srcSize,
    v.dbgMetaData = NULL;

    ReportMemoryViolation(&v);
  }

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

  // Retrieve both the destination and source buffer's bounds from the pool handle.
  assert(pool_find(dstPool, dstBegin, dstEnd) && "Destination buffer not found in pool!");
  assert(pool_find(srcPool, srcBegin, srcEnd) && "Source string not found in pool!");

  // Check that both the destination and source pointers fall within their respective bounds.
  if (dstBegin > dstEnd) {
    std::cout << "Destination pointer out of bounds!\n";

    OutOfBoundsViolation v;

    v.type = ViolationInfo::FAULT_OUT_OF_BOUNDS,
    v.faultPC = __builtin_return_address(0),
    v.faultPtr = dstBegin,
    v.dbgMetaData = NULL,
    v.PoolHandle = dstPool,
    v.SourceFile = __FILE__,
    v.lineNo = __LINE__,
    v.objStart = dstBegin,
    v.objLen = (unsigned)((char *)dstEnd - (char *)dstBegin) + 1;

    ReportMemoryViolation(&v);
  }

  if (srcBegin > srcEnd) {
    std::cout << "Source pointer out of bounds!\n";

    OutOfBoundsViolation v;

    v.type = ViolationInfo::FAULT_OUT_OF_BOUNDS,
    v.faultPC = __builtin_return_address(0),
    v.faultPtr = srcBegin,
    v.dbgMetaData = NULL,
    v.PoolHandle = srcPool,
    v.SourceFile = __FILE__,
    v.lineNo = __LINE__,
    v.objStart = srcBegin,
    v.objLen = (unsigned)((char *)srcEnd - (char *)srcBegin) + 1;

    ReportMemoryViolation(&v);
  }

  // Calculate the maximum number of bytes to copy.
  dstSize = (char *)dstEnd - (char *)dst + 1;
  srcSize = (char *)srcEnd - (char *)src + 1;

  if (n > srcSize) {
    std::cout << "Cannot copy more bytes than the size of the source!\n";

    WriteOOBViolation v;

    v.type = ViolationInfo::FAULT_WRITE_OUT_OF_BOUNDS,
    v.faultPC = __builtin_return_address(0),
    v.faultPtr = srcBegin,
    v.SourceFile = __FILE__,
    v.lineNo = __LINE__,
    v.PoolHandle = srcPool,
    v.dstSize = dstSize,
    v.srcSize = srcSize,
    v.dbgMetaData = NULL;

    ReportMemoryViolation(&v);
  }

  stop = std::min(n, srcSize);

  if (stop > dstSize) {
    std::cout << "Copy violated destination bounds!\n";

    WriteOOBViolation v;

    v.type = ViolationInfo::FAULT_WRITE_OUT_OF_BOUNDS,
    v.faultPC = __builtin_return_address(0),
    v.faultPtr = srcBegin,
    v.SourceFile = __FILE__,
    v.lineNo = __LINE__,
    v.PoolHandle = srcPool,
    v.dstSize = dstSize,
    v.srcSize = srcSize,
    v.dbgMetaData = NULL;

    ReportMemoryViolation(&v);
  }

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

  // Retrieve both the destination and source buffer's bounds from the pool handle.
  assert(pool_find(dstPool, dstBegin, dstEnd) && "Destination buffer not found in pool!");
  assert(pool_find(srcPool, srcBegin, srcEnd) && "Source string not found in pool!");

  // Check that both the destination and source pointers fall within their respective bounds.
  if (dstBegin > dstEnd) {
    std::cout << "Destination pointer out of bounds!\n";

    OutOfBoundsViolation v;

    v.type = ViolationInfo::FAULT_OUT_OF_BOUNDS,
    v.faultPC = __builtin_return_address(0),
    v.faultPtr = dstBegin,
    v.dbgMetaData = NULL,
    v.PoolHandle = dstPool,
    v.SourceFile = __FILE__,
    v.lineNo = __LINE__,
    v.objStart = dstBegin,
    v.objLen = (unsigned)((char *)dstEnd - (char *)dstBegin) + 1;

    ReportMemoryViolation(&v);
  }

  if (srcBegin > srcEnd) {
    std::cout << "Source pointer out of bounds!\n";

    OutOfBoundsViolation v;

    v.type = ViolationInfo::FAULT_OUT_OF_BOUNDS,
    v.faultPC = __builtin_return_address(0),
    v.faultPtr = srcBegin,
    v.dbgMetaData = NULL,
    v.PoolHandle = srcPool,
    v.SourceFile = __FILE__,
    v.lineNo = __LINE__,
    v.objStart = srcBegin,
    v.objLen = (unsigned)((char *)srcEnd - (char *)srcBegin) + 1;

    ReportMemoryViolation(&v);
  }

  // Calculate the maximum number of bytes to copy.
  dstSize = (char *)dstEnd - (char *)dst + 1;
  srcSize = (char *)srcEnd - (char *)src + 1;

  if (n > srcSize) {
    std::cout << "Cannot copy more bytes than the size of the source!\n";

    WriteOOBViolation v;

    v.type = ViolationInfo::FAULT_WRITE_OUT_OF_BOUNDS,
    v.faultPC = __builtin_return_address(0),
    v.faultPtr = srcBegin,
    v.SourceFile = __FILE__,
    v.lineNo = __LINE__,
    v.PoolHandle = srcPool,
    v.dstSize = dstSize,
    v.srcSize = srcSize,
    v.dbgMetaData = NULL;

    ReportMemoryViolation(&v);
  }

  stop = std::min(n, srcSize);

  if (stop > dstSize) {
    std::cout << "Copy violated destination bounds!\n";

    WriteOOBViolation v;

    v.type = ViolationInfo::FAULT_WRITE_OUT_OF_BOUNDS,
    v.faultPC = __builtin_return_address(0),
    v.faultPtr = srcBegin,
    v.SourceFile = __FILE__,
    v.lineNo = __LINE__,
    v.PoolHandle = srcPool,
    v.dstSize = dstSize,
    v.srcSize = srcSize,
    v.dbgMetaData = NULL;

    ReportMemoryViolation(&v);
  }

#ifndef _GNU_SOURCE
  mempcpy(dst, src, stop);
#endif

  return dst;
}

/**
 * Secure runtime wrapper function to replace memset()
 *
 * @param   stringPool  Pool handle for the string
 * @param   string      String pointer
 * @return  Pointer to memory area
 */
void *pool_memset(DebugPoolTy *stringPool, void *string, int c, size_t n) {
  size_t stringSize = 0, stop = 0;
  void *stringBegin = string, *stringEnd = NULL;

  assert(stringPool && string && "Null pool parameters!");
  assert(pool_find(stringPool, stringBegin, stringEnd) && "S not found in pool!");

  if (stringBegin > stringEnd) {
    std::cout << "String pointer out of bounds!\n";

    OutOfBoundsViolation v;

    v.type = ViolationInfo::FAULT_OUT_OF_BOUNDS,
    v.faultPC = __builtin_return_address(0),
    v.faultPtr = stringBegin,
    v.dbgMetaData = NULL,
    v.PoolHandle = stringPool,
    v.SourceFile = __FILE__,
    v.lineNo = __LINE__,
    v.objStart = stringBegin,
    v.objLen = (unsigned)((char *)stringEnd - (char *)stringBegin) + 1;

    ReportMemoryViolation(&v);
  }

  stringSize = (char *)stringEnd - (char *)string + 1;
  if (n > stringSize) {
    std::cout << "Cannot write more bytes than the size of the destination string!\n";

    WriteOOBViolation v;

    v.type = ViolationInfo::FAULT_WRITE_OUT_OF_BOUNDS,
    v.faultPC = __builtin_return_address(0),
    v.faultPtr = stringBegin,
    v.SourceFile = __FILE__,
    v.lineNo = __LINE__,
    v.PoolHandle = stringPool,
    v.dstSize = stringSize,
    v.dbgMetaData = NULL;

    ReportMemoryViolation(&v);
  }

  stop = std::min(n, stringSize);
  return memset(string, c, stop);
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

  // Ensure all valid pointers.
  assert(dstPool && srcPool && dst && src && "Null pool parameters!");

  // Retrieve both the destination and source buffer's bounds from the pool handle.
  assert(pool_find(dstPool, dstBegin, dstEnd) && "Destination buffer not found in pool!");
  assert(pool_find(srcPool, srcBegin, srcEnd) && "Source string not found in pool!");

  // Check that both the destination and source pointers fall within their respective bounds.
  if (dstBegin > dstEnd) {
    std::cout << "Destination pointer out of bounds!\n";

    OutOfBoundsViolation v;

    v.type = ViolationInfo::FAULT_OUT_OF_BOUNDS,
    v.faultPC = __builtin_return_address(0),
    v.faultPtr = dstBegin,
    v.dbgMetaData = NULL,
    v.PoolHandle = dstPool,
    v.SourceFile = __FILE__,
    v.lineNo = __LINE__,
    v.objStart = dstBegin,
    v.objLen = (unsigned)((char *)dstEnd - (char *)dstBegin) + 1;

    ReportMemoryViolation(&v);
  }

  if (srcBegin > srcEnd) {
    std::cout << "Source pointer out of bounds!\n";

    OutOfBoundsViolation v;

    v.type = ViolationInfo::FAULT_OUT_OF_BOUNDS,
    v.faultPC = __builtin_return_address(0),
    v.faultPtr = srcBegin,
    v.dbgMetaData = NULL,
    v.PoolHandle = srcPool,
    v.SourceFile = __FILE__,
    v.lineNo = __LINE__,
    v.objStart = srcBegin,
    v.objLen = (unsigned)((char *)srcEnd - (char *)srcBegin) + 1;

    ReportMemoryViolation(&v);
  }

  // Calculate the maximum number of bytes to copy.
  dstSize = (char *)dstEnd - dst + 1;
  srcSize = (char *)srcEnd - (char *)src + 1;
  stop = std::min(dstSize, srcSize);

  // Copy the source string to the destination buffer and record the number of bytes copied (including \0).
  copied = strncpy_asm(dst, src, stop);

  if (dst[copied - 1]) {
    std::cout << "Copy violated destination bounds!\n";

    WriteOOBViolation v;

    v.type = ViolationInfo::FAULT_WRITE_OUT_OF_BOUNDS,
    v.faultPC = __builtin_return_address(0),
    v.faultPtr = srcBegin,
    v.SourceFile = __FILE__,
    v.lineNo = __LINE__,
    v.PoolHandle = srcPool,
    v.dstSize = dstSize,
    v.srcSize = srcSize,
    v.copied = copied,
    v.dbgMetaData = NULL;

    ReportMemoryViolation(&v);
  }

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
  assert(pool_find(stringPool, stringBegin, stringEnd) && "String not found in pool!");

  if (stringBegin > stringEnd) {
    std::cout << "String pointer out of bounds!\n";

    OutOfBoundsViolation v;

    v.type = ViolationInfo::FAULT_OUT_OF_BOUNDS,
    v.faultPC = __builtin_return_address(0),
    v.faultPtr = stringBegin,
    v.dbgMetaData = NULL,
    v.PoolHandle = stringPool,
    v.SourceFile = __FILE__,
    v.lineNo = __LINE__,
    v.objStart = stringBegin,
    v.objLen = (unsigned)((char *)stringEnd - (char *)stringBegin) + 1;

    ReportMemoryViolation(&v);
  }

  maxlen = (char *)stringEnd - (char *)string;
  len = strnlen(string, maxlen);

  if (len >= maxlen || string[maxlen]) {
    std::cout << "String not terminated within bounds!\n";

    OutOfBoundsViolation v;

    v.type = ViolationInfo::FAULT_OUT_OF_BOUNDS,
    v.faultPC = __builtin_return_address(0),
    v.faultPtr = stringBegin,
    v.dbgMetaData = NULL,
    v.PoolHandle = stringPool,
    v.SourceFile = __FILE__,
    v.lineNo = __LINE__,
    v.objStart = stringBegin,
    v.objLen = (unsigned)((char *)stringEnd - (char *)stringBegin) + 1;

    ReportMemoryViolation(&v);
  }

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

  // Retrieve both the destination and source buffer's bounds from the pool handle.
  assert(pool_find(dstPool, dstBegin, dstEnd) && "Destination buffer not found in pool!");
  assert(pool_find(srcPool, srcBegin, srcEnd) && "Source string not found in pool!");

  // Check that both the destination and source pointers fall within their respective bounds.
  if (dstBegin > dstEnd) {
    std::cout << "Destination pointer out of bounds!\n";

    OutOfBoundsViolation v;

    v.type = ViolationInfo::FAULT_OUT_OF_BOUNDS,
    v.faultPC = __builtin_return_address(0),
    v.faultPtr = dstBegin,
    v.dbgMetaData = NULL,
    v.PoolHandle = dstPool,
    v.SourceFile = __FILE__,
    v.lineNo = __LINE__,
    v.objStart = dstBegin,
    v.objLen = (unsigned)((char *)dstEnd - (char *)dstBegin) + 1;

    ReportMemoryViolation(&v);
  }

  if (srcBegin > srcEnd) {
    std::cout << "Source pointer out of bounds!\n";

    OutOfBoundsViolation v;

    v.type = ViolationInfo::FAULT_OUT_OF_BOUNDS,
    v.faultPC = __builtin_return_address(0),
    v.faultPtr = srcBegin,
    v.dbgMetaData = NULL,
    v.PoolHandle = srcPool,
    v.SourceFile = __FILE__,
    v.lineNo = __LINE__,
    v.objStart = srcBegin,
    v.objLen = (unsigned)((char *)srcEnd - (char *)srcBegin) + 1;

    ReportMemoryViolation(&v);
  }

  // Calculate the maximum number of bytes to copy.
  dstSize = (char *)dstEnd - dst + 1;
  srcSize = (char *)srcEnd - (char *)src + 1;
  stop = std::min(dstSize, srcSize);

  if (stop < n) {
    copied = strncpy_asm(dst, src, stop);

    if (copied == stop) {
      std::cout << "Copy violated destination bounds!\n";

      WriteOOBViolation v;

      v.type = ViolationInfo::FAULT_WRITE_OUT_OF_BOUNDS,
      v.faultPC = __builtin_return_address(0),
      v.faultPtr = srcBegin,
      v.SourceFile = __FILE__,
      v.lineNo = __LINE__,
      v.PoolHandle = srcPool,
      v.dstSize = dstSize,
      v.srcSize = srcSize,
      v.copied = copied,
      v.dbgMetaData = NULL;

      ReportMemoryViolation(&v);
    }
  } else {
    copied = strncpy_asm(dst, src, n);

    // Possibly not NULL terminated
    if (copied > dstSize) {
      std::cout << "Copy violated destination bounds!\n";

      WriteOOBViolation v;

      v.type = ViolationInfo::FAULT_WRITE_OUT_OF_BOUNDS,
      v.faultPC = __builtin_return_address(0),
      v.faultPtr = srcBegin,
      v.SourceFile = __FILE__,
      v.lineNo = __LINE__,
      v.PoolHandle = srcPool,
      v.dstSize = dstSize,
      v.srcSize = srcSize,
      v.copied = copied,
      v.dbgMetaData = NULL;

      ReportMemoryViolation(&v);
    }
  }

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
  assert(pool_find(stringPool, stringBegin, stringEnd) && "String not found in pool!");

  if (stringBegin > stringEnd) {
    std::cout << "String pointer out of bounds!\n";

    OutOfBoundsViolation v;

    v.type = ViolationInfo::FAULT_OUT_OF_BOUNDS,
    v.faultPC = __builtin_return_address(0),
    v.faultPtr = stringBegin,
    v.dbgMetaData = NULL,
    v.PoolHandle = stringPool,
    v.SourceFile = __FILE__,
    v.lineNo = __LINE__,
    v.objStart = stringBegin,
    v.objLen = (unsigned)((char *)stringEnd - (char *)stringBegin) + 1;

    ReportMemoryViolation(&v);
  }

  difflen = (char *)stringEnd - (char *)string;

  if (difflen > maxlen) {
    std::cout << "String in pool longer than maxlen!\n";

    OutOfBoundsViolation v;

    v.type = ViolationInfo::FAULT_OUT_OF_BOUNDS,
    v.faultPC = __builtin_return_address(0),
    v.faultPtr = stringBegin,
    v.dbgMetaData = NULL,
    v.PoolHandle = stringPool,
    v.SourceFile = __FILE__,
    v.lineNo = __LINE__,
    v.objStart = stringBegin,
    v.objLen = (unsigned)((char *)stringEnd - (char *)stringBegin) + 1;

    ReportMemoryViolation(&v);
  }

  len = strnlen(string, difflen);

  if (len >= difflen || string[difflen]) {
    std::cout << "String not terminated within bounds!\n";

    OutOfBoundsViolation v;

    v.type = ViolationInfo::FAULT_OUT_OF_BOUNDS,
    v.faultPC = __builtin_return_address(0),
    v.faultPtr = stringBegin,
    v.dbgMetaData = NULL,
    v.PoolHandle = stringPool,
    v.SourceFile = __FILE__,
    v.lineNo = __LINE__,
    v.objStart = stringBegin,
    v.objLen = (unsigned)((char *)stringEnd - (char *)stringBegin) + 1;

    ReportMemoryViolation(&v);
  }

  return len;
}
