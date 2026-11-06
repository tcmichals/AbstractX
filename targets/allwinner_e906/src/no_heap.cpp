/*
 * Copyright (C) 2026 Tim Michals
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * No heap in E906 images ([SPEC-E906-MEM-03]):
 * - global operator delete is a trap, so libstdc++'s version (which calls free and links newlib's
 *   allocator) is never pulled in; it is called only from deleting destructors that are never used.
 *   operator new is deliberately not defined.
 * - C stdout functions discard their text: the E906 has no console for them, and newlib's stdout
 *   allocates a buffer on its first write.
 * @impl [SPEC-E906-MEM-03] targets/allwinner_e906/SPECIFICATION.md
 */

#include <cstdarg>
#include <cstddef>

void operator delete(void*) noexcept { __builtin_trap(); }
void operator delete(void*, std::size_t) noexcept { __builtin_trap(); }

extern "C" {
int printf(const char*, ...) { return 0; }
int iprintf(const char*, ...) { return 0; }
int vprintf(const char*, va_list) { return 0; }
int puts(const char*) { return 0; }
int putchar(int c) { return c; }
}
