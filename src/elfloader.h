#pragma once

#include <Arduino.h>
#include <map>
#include "filesystem.h"
#include "terminal.h"
#include "esp_heap_caps.h"

// Minimal ELF32 (little-endian) relocatable-object loader for Xtensa -
// stage 2 of ESP-Nix's "run compiled code from SD" work (see runelf in
// commands.h for stage 1). Loads a genuine .o file (not a bare objcopy'd
// .text dump), applies R_XTENSA_32 relocations against undefined symbols
// by resolving them against a small firmware-exported whitelist, and
// calls a named function within it.
//
// Deliberately NOT a general ELF/dynamic loader: no program headers, no
// multi-file linking, only .text/.rela.text/.symtab/.strtab are read,
// and only R_XTENSA_32 relocations against undefined external symbols
// are handled (the other relocation types Xtensa -mlongcalls produces -
// R_XTENSA_SLOT0_OP, R_XTENSA_ASM_EXPAND - are linker-relaxation
// metadata that only matters if the linker were relaxing longcalls,
// which we never do here, so they're safely ignored).

struct Elf32_Ehdr {
  uint8_t  e_ident[16];
  uint16_t e_type;
  uint16_t e_machine;
  uint32_t e_version;
  uint32_t e_entry;
  uint32_t e_phoff;
  uint32_t e_shoff;
  uint32_t e_flags;
  uint16_t e_ehsize;
  uint16_t e_phentsize;
  uint16_t e_phnum;
  uint16_t e_shentsize;
  uint16_t e_shnum;
  uint16_t e_shstrndx;
};

struct Elf32_Shdr {
  uint32_t sh_name;
  uint32_t sh_type;
  uint32_t sh_flags;
  uint32_t sh_addr;
  uint32_t sh_offset;
  uint32_t sh_size;
  uint32_t sh_link;
  uint32_t sh_info;
  uint32_t sh_addralign;
  uint32_t sh_entsize;
};

struct Elf32_Sym {
  uint32_t st_name;
  uint32_t st_value;
  uint32_t st_size;
  uint8_t  st_info;
  uint8_t  st_other;
  uint16_t st_shndx;
};

struct Elf32_Rela {
  uint32_t r_offset;
  uint32_t r_info;
  int32_t  r_addend;
};

#define ELF_R_SYM(info) ((info) >> 8)
#define ELF_R_TYPE(info) ((info) & 0xff)
#define R_XTENSA_32 1
#define STT_FUNC 2
#define SHN_UNDEF 0

struct ExportedSymbol {
  const char* name;
  void* addr;
};

class ElfModule {
public:
  ElfModule(FileSystem& f, Terminal& t) : fs(f), term(t) {}

  ~ElfModule() {
    if (execMem) heap_caps_free(execMem);
  }

  // Loads path, resolving undefined symbols against symtab (an array
  // terminated by a {nullptr, nullptr} entry). Prints a clear error and
  // returns false on any failure.
  bool load(const String& path, const ExportedSymbol* symtab) {
    uint8_t* data = nullptr;
    size_t dataLen = 0;
    if (!readWholeFile(path, data, dataLen)) {
      term.println("runmod: could not read " + path);
      return false;
    }

    bool ok = parseAndLoad(data, dataLen, symtab);
    free(data);
    return ok;
  }

  bool callInt2(const String& name, int a, int b, int& outResult) {
    if (!functions.count(name)) return false;
    typedef int (*Fn)(int, int);
    Fn fn = (Fn)((uint8_t*)execMem + functions[name]);
    outResult = fn(a, b);
    return true;
  }

  bool callInt0(const String& name, int& outResult) {
    if (!functions.count(name)) return false;
    typedef int (*Fn)();
    Fn fn = (Fn)((uint8_t*)execMem + functions[name]);
    outResult = fn();
    return true;
  }

  bool hasFunction(const String& name) {
    return functions.count(name) > 0;
  }

private:
  FileSystem& fs;
  Terminal& term;
  void* execMem = nullptr;
  std::map<String, uint32_t> functions;

  bool readWholeFile(const String& path, uint8_t*& outData, size_t& outLen) {
    File f = fs.openRaw(path, "r");
    if (!f) return false;
    size_t len = f.size();
    if (len == 0) {
      f.close();
      return false;
    }
    uint8_t* buf = (uint8_t*)malloc(len);
    if (!buf) {
      f.close();
      return false;
    }
    f.read(buf, len);
    f.close();
    outData = buf;
    outLen = len;
    return true;
  }

  void* lookupSymbol(const char* name, const ExportedSymbol* symtab) {
    for (int i = 0; symtab[i].name != nullptr; i++) {
      if (strcmp(symtab[i].name, name) == 0) return symtab[i].addr;
    }
    return nullptr;
  }

  bool parseAndLoad(const uint8_t* data, size_t dataLen, const ExportedSymbol* symtab) {
    if (dataLen < sizeof(Elf32_Ehdr)) {
      term.println("runmod: file too small to be an ELF object");
      return false;
    }
    const Elf32_Ehdr* eh = (const Elf32_Ehdr*)data;
    if (eh->e_ident[0] != 0x7f || eh->e_ident[1] != 'E' || eh->e_ident[2] != 'L' || eh->e_ident[3] != 'F') {
      term.println("runmod: not an ELF file");
      return false;
    }

    const Elf32_Shdr* shdrs = (const Elf32_Shdr*)(data + eh->e_shoff);
    int shnum = eh->e_shnum;
    const char* shstrtab = (const char*)(data + shdrs[eh->e_shstrndx].sh_offset);

    int textIdx = -1, relaTextIdx = -1, symtabIdx = -1, strtabIdx = -1;
    for (int i = 0; i < shnum; i++) {
      const char* name = shstrtab + shdrs[i].sh_name;
      if (strcmp(name, ".text") == 0) textIdx = i;
      else if (strcmp(name, ".rela.text") == 0) relaTextIdx = i;
      else if (strcmp(name, ".symtab") == 0) symtabIdx = i;
      else if (strcmp(name, ".strtab") == 0) strtabIdx = i;
    }

    if (textIdx < 0) {
      term.println("runmod: no .text section found");
      return false;
    }

    const Elf32_Shdr& textSh = shdrs[textIdx];
    size_t textLen = textSh.sh_size;
    size_t wordLen = (textLen + 3) & ~((size_t)3);

    // Staging buffer: normal byte-addressable RAM, where relocations get
    // patched in before the final word-copy into executable memory
    // (IRAM only supports 32-bit-aligned accesses on the ESP32).
    uint8_t* staging = (uint8_t*)calloc(1, wordLen);
    if (!staging) {
      term.println("runmod: could not allocate staging buffer");
      return false;
    }
    memcpy(staging, data + textSh.sh_offset, textLen);

    const Elf32_Sym* syms = nullptr;
    const char* strtab = nullptr;
    if (symtabIdx >= 0 && strtabIdx >= 0) {
      syms = (const Elf32_Sym*)(data + shdrs[symtabIdx].sh_offset);
      strtab = (const char*)(data + shdrs[strtabIdx].sh_offset);
    }

    if (relaTextIdx >= 0 && syms && strtab) {
      const Elf32_Shdr& relaSh = shdrs[relaTextIdx];
      const Elf32_Rela* relas = (const Elf32_Rela*)(data + relaSh.sh_offset);
      int relaCount = relaSh.sh_size / sizeof(Elf32_Rela);

      for (int i = 0; i < relaCount; i++) {
        const Elf32_Rela& rel = relas[i];
        if (ELF_R_TYPE(rel.r_info) != R_XTENSA_32) continue;

        const Elf32_Sym& sym = syms[ELF_R_SYM(rel.r_info)];
        if (sym.st_shndx != SHN_UNDEF) {
          // Defined within this same object file (e.g. a self-reference
          // to .text) - not supported yet, since only one section is
          // loaded; skip rather than guess at a wrong address.
          continue;
        }

        const char* symName = strtab + sym.st_name;
        void* addr = lookupSymbol(symName, symtab);
        if (!addr) {
          term.println("runmod: unresolved symbol: " + String(symName));
          free(staging);
          return false;
        }

        uint32_t value = (uint32_t)addr + rel.r_addend;
        if (rel.r_offset + 4 <= wordLen) {
          memcpy(staging + rel.r_offset, &value, 4);
        }
      }
    }

    execMem = heap_caps_malloc(wordLen, MALLOC_CAP_EXEC | MALLOC_CAP_32BIT);
    if (!execMem) {
      term.println("runmod: could not allocate " + String(wordLen) + " bytes of executable RAM");
      free(staging);
      return false;
    }
    const uint32_t* src = (const uint32_t*)staging;
    uint32_t* dst = (uint32_t*)execMem;
    for (size_t i = 0; i < wordLen / 4; i++) dst[i] = src[i];
    free(staging);

    if (syms && strtab) {
      int symCount = shdrs[symtabIdx].sh_size / sizeof(Elf32_Sym);
      for (int i = 0; i < symCount; i++) {
        const Elf32_Sym& sym = syms[i];
        if ((sym.st_info & 0xf) == STT_FUNC && sym.st_shndx == textIdx) {
          functions[String(strtab + sym.st_name)] = sym.st_value;
        }
      }
    }

    return true;
  }
};
