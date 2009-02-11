//===------- StdLib.cpp - CStdLib transform pass runtime functions --------===//
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

#include "PoolAllocator.h"

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
