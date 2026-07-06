#pragma once

#include <Arduino.h>
#include "elfloader.h"
#include "version.h"

// The firmware-exported symbol table for runmod (and Retron's CALL
// command, which uses the same loader): functions a loaded module's
// undefined external references can be resolved against. Shared by
// commands.h and retron.h so both can load/call compiled modules against
// the same whitelist - previously this lived only in commands.h, but
// Retron's CALL command needs it too.
//
// Each must be declared extern "C" so the linker uses the plain,
// unmangled name a C object file would reference (e.g. "host_print",
// not some C++-mangled form) - this is what lets a module's
// `extern void host_print(int);` resolve correctly.
extern "C" void host_print(int value) {
  Serial.println("[runmod host_print] " + String(value));
}

// Returns the OS version string (see version.h) - lets a loaded module
// print/check it via a single call, the same way Retron scripts read it
// through the "version" pseudo-variable.
extern "C" const char* host_os_version() {
  return ESP_NIX_VERSION;
}

// mbedTLS's mbedtls_platform_zeroize(): a secure-zero memset that won't
// get optimized away as a "dead store" the way a plain memset() might
// be, since the buffer being zeroed is never read again afterward from
// the compiler's point of view - real mbedTLS provides its own via
// platform_util.c, but for a standalone loaded module this is a
// semantically-compatible substitute (volatile pointer defeats the
// dead-store elimination that's the whole reason this function exists).
extern "C" void mbedtls_platform_zeroize(void* buf, size_t len) {
  volatile unsigned char* p = (volatile unsigned char*)buf;
  while (len--) *p++ = 0;
}

// libgcc's __bswapsi2(): a 32-bit byte-swap helper GCC calls when it
// can't emit a direct hardware instruction for the swap on this target -
// real mbedTLS code (SHA-256 among others) relies on this for
// endianness conversion. libgcc.a is already linked into this firmware,
// but its own __bswapsi2 isn't necessarily reachable/exported the way a
// loaded module needs (no guarantee the linker kept it, or that its
// address is knowable without digging through the link map) - providing
// our own trivial, ABI-compatible implementation sidesteps that entirely.
extern "C" unsigned int __bswapsi2(unsigned int x) {
  return ((x & 0x000000ffu) << 24) |
         ((x & 0x0000ff00u) << 8)  |
         ((x & 0x00ff0000u) >> 8)  |
         ((x & 0xff000000u) >> 24);
}

// libc functions loaded modules can call for their own memory
// management and basic string/buffer work - a genuine library (rather
// than a pure-arithmetic test function) needs these almost immediately.
// Taking their addresses directly works with no wrapper needed, since
// they're already plain C-linkage symbols matching the exact names a
// compiled module's own `extern` declarations would reference.
static const ExportedSymbol kRunmodSymbols[] = {
  {"host_print", (void*)host_print},
  {"host_os_version", (void*)host_os_version},
  {"mbedtls_platform_zeroize", (void*)mbedtls_platform_zeroize},
  {"__bswapsi2", (void*)__bswapsi2},
  {"malloc", (void*)malloc},
  {"free", (void*)free},
  {"calloc", (void*)calloc},
  {"realloc", (void*)realloc},
  {"memcpy", (void*)memcpy},
  {"memset", (void*)memset},
  {"memmove", (void*)memmove},
  {"memcmp", (void*)memcmp},
  {"strlen", (void*)strlen},
  {"strcpy", (void*)strcpy},
  {"strncpy", (void*)strncpy},
  {"strcmp", (void*)strcmp},
  {"strncmp", (void*)strncmp},
  {"strcat", (void*)strcat},
  {"strchr", (void*)strchr},
  {nullptr, nullptr}
};
