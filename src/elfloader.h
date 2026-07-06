#pragma once

#include <Arduino.h>
#include <map>
#include <vector>
#include "filesystem.h"
#include "terminal.h"
#include "esp_heap_caps.h"

// Minimal ELF32 (little-endian) relocatable-object loader/linker for
// Xtensa - stage 3 of ESP-Nix's "run compiled code from SD" work.
//
// Handles, per loaded set of .o files:
//   - .text, .rodata, .data, .bss (not just .text as in the first
//     version) - so string literals, lookup tables, and global
//     variables work, not just pure-arithmetic functions.
//   - Calls into the firmware via a symbol whitelist (kRunmodSymbols in
//     commands.h).
//   - Calls and address-taken references between functions/data in the
//     SAME file (resolved via that file's own section base addresses).
//   - Calls and references between MULTIPLE loaded files (a simple
//     two-pass link: allocate every file's sections first, so every
//     final address is known, then build one combined table of every
//     file's externally-visible symbols, then apply every file's
//     relocations against that combined table, falling back to the
//     firmware whitelist for anything still undefined).
//
// Deliberately NOT a general linker: no program headers, only
// R_XTENSA_32 relocations are handled (the other types -mlongcalls
// emits are linker-relaxation metadata, irrelevant since we never
// relax), and there's no support for COMDAT/weak-symbol resolution,
// versioned symbols, or anything ELF supports beyond what a handful of
// plain C translation units compiled with -mtext-section-literals
// -mlongcalls actually produce.

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
#define R_XTENSA_SLOT0_OP 20
#define STT_FUNC 2
#define SHN_UNDEF 0

struct ExportedSymbol {
  const char* name;
  void* addr;
};

// One loaded object file: its raw bytes (kept only until this file's
// relocations are applied, then freed), section headers, and the
// separately-allocated final memory for each of .text/.rodata/.data/.bss.
struct LoadedObject {
  uint8_t* raw = nullptr;
  const Elf32_Ehdr* eh = nullptr;
  const Elf32_Shdr* shdrs = nullptr;
  const char* shstrtab = nullptr;
  int shnum = 0;

  int textIdx = -1, rodataIdx = -1, dataIdx = -1, bssIdx = -1;
  int relaTextIdx = -1, relaRodataIdx = -1, relaDataIdx = -1;
  int symtabIdx = -1, strtabIdx = -1;

  void* textMem = nullptr;
  void* rodataMem = nullptr;
  void* dataMem = nullptr;
  void* bssMem = nullptr;
  size_t textSize = 0, rodataSize = 0, dataSize = 0, bssSize = 0;

  // Every FUNC symbol defined in this file's .text, regardless of
  // binding (local/static functions are still callable by name within
  // this same file - just not exposed to other files being linked
  // alongside it).
  std::map<String, uint32_t> localFunctions;
};

class ElfModule {
public:
  ElfModule(FileSystem& f, Terminal& t) : fs(f), term(t) {}

  ~ElfModule() {
    for (auto& obj : objects) {
      if (obj.textMem) heap_caps_free(obj.textMem);
      if (obj.rodataMem) free(obj.rodataMem);
      if (obj.dataMem) free(obj.dataMem);
      if (obj.bssMem) free(obj.bssMem);
      if (obj.raw) free(obj.raw);
    }
  }

  // Loads and links one or more object files together. Undefined
  // symbols are resolved first against the other files in this same
  // set, then against the firmware whitelist. Prints a specific error
  // and returns false on any failure.
  bool load(const std::vector<String>& paths, const ExportedSymbol* symtab) {
    objects.resize(paths.size());

    for (size_t i = 0; i < paths.size(); i++) {
      if (!readAndParse(paths[i], objects[i])) return false;
    }

    // Allocate memory for every section of every file first, so every
    // final address is known before any relocation - needed for both
    // same-file and cross-file references.
    for (auto& obj : objects) {
      if (!allocateSections(obj)) return false;
    }

    // One combined table of every file's externally-visible symbols,
    // for resolving undefined references against each other before
    // falling back to the firmware whitelist.
    std::map<String, uint32_t> globalSymbols;
    for (auto& obj : objects) {
      collectSymbols(obj, globalSymbols);
    }

    for (auto& obj : objects) {
      if (!applyRelocations(obj, globalSymbols, symtab)) return false;
    }

    // Raw file bytes are no longer needed once everything's relocated
    // and copied into its final memory.
    for (auto& obj : objects) {
      free(obj.raw);
      obj.raw = nullptr;
    }

    return true;
  }

  // Calls a loaded function by name with up to 6 register-sized (32-bit
  // int or pointer) arguments - the practical ceiling for a shell
  // command to express, since Xtensa's windowed ABI passes the first 6
  // scalar/pointer args in registers regardless of the callee's real
  // signature; a callee that takes fewer simply ignores the rest.
  bool call(const String& name, const std::vector<uint32_t>& args, uint32_t& outResult) {
    void* fn = findFunction(name);
    if (!fn) return false;

    uint32_t a[6] = {0, 0, 0, 0, 0, 0};
    for (size_t i = 0; i < args.size() && i < 6; i++) a[i] = args[i];

    typedef uint32_t (*Fn6)(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
    Fn6 f = (Fn6)fn;
    outResult = f(a[0], a[1], a[2], a[3], a[4], a[5]);
    return true;
  }

  bool hasFunction(const String& name) {
    return findFunction(name) != nullptr;
  }

  void* textBase() {
    return objects.empty() ? nullptr : objects[0].textMem;
  }

private:
  FileSystem& fs;
  Terminal& term;
  std::vector<LoadedObject> objects;

  void* findFunction(const String& name) {
    for (auto& obj : objects) {
      auto it = obj.localFunctions.find(name);
      if (it != obj.localFunctions.end()) {
        return (uint8_t*)obj.textMem + it->second;
      }
    }
    return nullptr;
  }

  bool readAndParse(const String& path, LoadedObject& obj) {
    String resolved = fs.resolvePath(path);
    if (!fs.exists(resolved)) {
      term.println("runmod: no such file: " + resolved);
      return false;
    }
    File f = fs.openRaw(resolved, "r");
    if (!f) {
      term.println("runmod: could not open " + resolved);
      return false;
    }
    size_t len = f.size();
    if (len == 0) {
      term.println("runmod: empty file: " + resolved);
      f.close();
      return false;
    }
    uint8_t* buf = (uint8_t*)malloc(len);
    if (!buf) {
      term.println("runmod: could not allocate buffer for " + resolved);
      f.close();
      return false;
    }
    f.read(buf, len);
    f.close();

    if (len < sizeof(Elf32_Ehdr)) {
      term.println("runmod: " + resolved + " is too small to be an ELF object");
      free(buf);
      return false;
    }
    const Elf32_Ehdr* eh = (const Elf32_Ehdr*)buf;
    if (eh->e_ident[0] != 0x7f || eh->e_ident[1] != 'E' || eh->e_ident[2] != 'L' || eh->e_ident[3] != 'F') {
      term.println("runmod: " + resolved + " is not an ELF file");
      free(buf);
      return false;
    }

    obj.raw = buf;
    obj.eh = eh;
    obj.shdrs = (const Elf32_Shdr*)(buf + eh->e_shoff);
    obj.shnum = eh->e_shnum;
    obj.shstrtab = (const char*)(buf + obj.shdrs[eh->e_shstrndx].sh_offset);

    // .rodata specifically is matched by prefix, not exact name: gas
    // commonly emits string literals into a mergeable-strings subsection
    // like ".rodata.str1.4" rather than plain ".rodata" - confirmed by
    // compiling an actual string-literal test case, not assumed from
    // the ELF spec alone. Only the first matching section is tracked;
    // a file with more than one distinct rodata-like section isn't
    // supported (a limitation worth hitting a real error on rather than
    // silently mis-loading, if it ever comes up).
    for (int i = 0; i < obj.shnum; i++) {
      const char* name = obj.shstrtab + obj.shdrs[i].sh_name;
      if (strcmp(name, ".text") == 0) obj.textIdx = i;
      else if (obj.rodataIdx < 0 && strncmp(name, ".rodata", 7) == 0) obj.rodataIdx = i;
      else if (strcmp(name, ".data") == 0) obj.dataIdx = i;
      else if (strcmp(name, ".bss") == 0) obj.bssIdx = i;
      else if (strcmp(name, ".rela.text") == 0) obj.relaTextIdx = i;
      else if (obj.relaRodataIdx < 0 && strncmp(name, ".rela.rodata", 12) == 0) obj.relaRodataIdx = i;
      else if (strcmp(name, ".rela.data") == 0) obj.relaDataIdx = i;
      else if (strcmp(name, ".symtab") == 0) obj.symtabIdx = i;
      else if (strcmp(name, ".strtab") == 0) obj.strtabIdx = i;
    }

    if (obj.textIdx < 0) {
      term.println("runmod: " + resolved + " has no .text section");
      free(buf);
      return false;
    }

    return true;
  }

  size_t wordAlign(size_t n) {
    return (n + 3) & ~((size_t)3);
  }

  bool allocateSections(LoadedObject& obj) {
    obj.textSize = wordAlign(obj.shdrs[obj.textIdx].sh_size);
    if (obj.textSize > 0) {
      obj.textMem = heap_caps_malloc(obj.textSize, MALLOC_CAP_EXEC | MALLOC_CAP_32BIT);
      if (!obj.textMem) {
        term.println("runmod: could not allocate executable RAM for .text");
        return false;
      }
    }

    if (obj.rodataIdx >= 0 && obj.shdrs[obj.rodataIdx].sh_size > 0) {
      obj.rodataSize = wordAlign(obj.shdrs[obj.rodataIdx].sh_size);
      obj.rodataMem = calloc(1, obj.rodataSize);
      if (!obj.rodataMem) {
        term.println("runmod: could not allocate RAM for .rodata");
        return false;
      }
      memcpy(obj.rodataMem, obj.raw + obj.shdrs[obj.rodataIdx].sh_offset, obj.shdrs[obj.rodataIdx].sh_size);
    }

    if (obj.dataIdx >= 0 && obj.shdrs[obj.dataIdx].sh_size > 0) {
      obj.dataSize = wordAlign(obj.shdrs[obj.dataIdx].sh_size);
      obj.dataMem = calloc(1, obj.dataSize);
      if (!obj.dataMem) {
        term.println("runmod: could not allocate RAM for .data");
        return false;
      }
      memcpy(obj.dataMem, obj.raw + obj.shdrs[obj.dataIdx].sh_offset, obj.shdrs[obj.dataIdx].sh_size);
    }

    if (obj.bssIdx >= 0 && obj.shdrs[obj.bssIdx].sh_size > 0) {
      obj.bssSize = wordAlign(obj.shdrs[obj.bssIdx].sh_size);
      obj.bssMem = calloc(1, obj.bssSize);  // .bss has no file content - just zeroed memory
      if (!obj.bssMem) {
        term.println("runmod: could not allocate RAM for .bss");
        return false;
      }
    }

    return true;
  }

  void collectSymbols(LoadedObject& obj, std::map<String, uint32_t>& globalSymbols) {
    if (obj.symtabIdx < 0 || obj.strtabIdx < 0) return;
    const Elf32_Sym* syms = (const Elf32_Sym*)(obj.raw + obj.shdrs[obj.symtabIdx].sh_offset);
    const char* strtab = (const char*)(obj.raw + obj.shdrs[obj.strtabIdx].sh_offset);
    int symCount = obj.shdrs[obj.symtabIdx].sh_size / sizeof(Elf32_Sym);

    for (int i = 0; i < symCount; i++) {
      const Elf32_Sym& sym = syms[i];
      if (sym.st_shndx == SHN_UNDEF) continue;

      uint32_t addr;
      if (sym.st_shndx == obj.textIdx) addr = (uint32_t)obj.textMem + sym.st_value;
      else if (sym.st_shndx == obj.rodataIdx) addr = (uint32_t)obj.rodataMem + sym.st_value;
      else if (sym.st_shndx == obj.dataIdx) addr = (uint32_t)obj.dataMem + sym.st_value;
      else if (sym.st_shndx == obj.bssIdx) addr = (uint32_t)obj.bssMem + sym.st_value;
      else continue;  // some section we don't track (e.g. .comment)

      // Callable/addressable by name within this same file regardless
      // of binding - a static/local function still needs to be found
      // when this file's own relocations reference it, and still
      // needs to be callable directly by name from the shell.
      if ((sym.st_info & 0xf) == STT_FUNC && sym.st_shndx == obj.textIdx) {
        obj.localFunctions[String(strtab + sym.st_name)] = sym.st_value;
      }

      // Only externally-visible (non-local) symbols get exposed for
      // OTHER files in a multi-file load to link against.
      int bind = sym.st_info >> 4;
      if (bind != 0) {
        globalSymbols[String(strtab + sym.st_name)] = addr;
      }
    }
  }

  bool resolveSymbolAddress(const LoadedObject& obj, const Elf32_Sym& sym, const char* strtab,
                             const std::map<String, uint32_t>& globalSymbols,
                             const ExportedSymbol* symtab, uint32_t& outAddr) {
    if (sym.st_shndx == SHN_UNDEF) {
      String name = String(strtab + sym.st_name);

      auto it = globalSymbols.find(name);
      if (it != globalSymbols.end()) {
        outAddr = it->second;
        return true;
      }
      for (int i = 0; symtab[i].name != nullptr; i++) {
        if (name == symtab[i].name) {
          outAddr = (uint32_t)symtab[i].addr;
          return true;
        }
      }
      term.println("runmod: unresolved symbol: " + name);
      return false;
    }

    if (sym.st_shndx == obj.textIdx)   { outAddr = (uint32_t)obj.textMem + sym.st_value;   return true; }
    if (sym.st_shndx == obj.rodataIdx) { outAddr = (uint32_t)obj.rodataMem + sym.st_value; return true; }
    if (sym.st_shndx == obj.dataIdx)   { outAddr = (uint32_t)obj.dataMem + sym.st_value;   return true; }
    if (sym.st_shndx == obj.bssIdx)    { outAddr = (uint32_t)obj.bssMem + sym.st_value;    return true; }

    term.println("runmod: relocation against an unsupported section");
    return false;
  }

  // Patches a 3-byte Xtensa CALLn instruction's PC-relative offset field
  // (bits [23:6] of the instruction read as one little-endian 24-bit
  // value; bits [5:0] are the opcode+n fields and must be preserved).
  // gas only fills this in directly when it can resolve the callee at
  // assembly time (a local/static symbol); a call to a symbol with
  // external/global linkage - even one defined in the very same file,
  // e.g. one public mbedTLS function calling another - gets a real
  // R_XTENSA_SLOT0_OP relocation instead, deliberately left as a 0
  // placeholder for a linker to patch. Confirmed empirically: skipping
  // these (as an earlier version of this loader did, having only been
  // tested against static-function-to-static-function calls) leaves the
  // call targeting 2 bytes past itself, crashing with an
  // IllegalInstruction the moment it's reached - found while stress-
  // testing against real mbedTLS source rather than synthetic tests.
  void patchCall(uint8_t* destMem, uint32_t offset, uint32_t selfAddr, uint32_t targetAddr) {
    uint32_t word = destMem[offset] | (destMem[offset + 1] << 8) | (destMem[offset + 2] << 16);
    uint32_t lowBits = word & 0x3F;

    uint32_t base = (selfAddr + 3) & ~((uint32_t)3);
    int32_t offset18 = ((int32_t)targetAddr - (int32_t)base) >> 2;

    uint32_t newWord = ((uint32_t)offset18 & 0x3FFFF) << 6 | lowBits;
    destMem[offset] = newWord & 0xFF;
    destMem[offset + 1] = (newWord >> 8) & 0xFF;
    destMem[offset + 2] = (newWord >> 16) & 0xFF;
  }

  // Applies every relocation this loader understands in one (section,
  // .rela.<section>) pair, writing into destMem (already holding that
  // section's raw pre-relocation bytes). selfBase is the address destMem
  // will actually live at once copied to its final home - needed to
  // compute CALLn's PC-relative offset correctly; R_XTENSA_32 doesn't
  // need it. gas emits R_XTENSA_32 against local/static symbols
  // targeting the enclosing SECTION symbol (whose st_value is always 0)
  // rather than the specific symbol, with the real intra-section offset
  // baked into the placeholder bytes already at the relocation site -
  // confirmed empirically against this toolchain's actual output, not
  // just the ELF spec on paper - so both the placeholder and r_addend
  // are added on top of the resolved base.
  bool applyRelocaSection(LoadedObject& obj, int relaIdx, uint8_t* destMem, size_t destSize,
                           uint32_t selfBase, const std::map<String, uint32_t>& globalSymbols,
                           const ExportedSymbol* symtab) {
    if (relaIdx < 0 || obj.symtabIdx < 0 || obj.strtabIdx < 0) return true;

    const Elf32_Shdr& relaSh = obj.shdrs[relaIdx];
    const Elf32_Rela* relas = (const Elf32_Rela*)(obj.raw + relaSh.sh_offset);
    int relaCount = relaSh.sh_size / sizeof(Elf32_Rela);
    const Elf32_Sym* syms = (const Elf32_Sym*)(obj.raw + obj.shdrs[obj.symtabIdx].sh_offset);
    const char* strtab = (const char*)(obj.raw + obj.shdrs[obj.strtabIdx].sh_offset);

    for (int i = 0; i < relaCount; i++) {
      const Elf32_Rela& rel = relas[i];
      int type = ELF_R_TYPE(rel.r_info);

      if (type == R_XTENSA_SLOT0_OP) {
        if (rel.r_offset + 3 > destSize) continue;
        uint32_t word = destMem[rel.r_offset] | (destMem[rel.r_offset + 1] << 8) | (destMem[rel.r_offset + 2] << 16);
        // Only a CALLn instruction (opcode 0101 in bits[3:0]) needs real
        // target patching - an L32R's SLOT0_OP (opcode 0001) references
        // a literal pool entry at a fixed, always-local offset already
        // correctly encoded by gas, needing no patching (proven safe by
        // every prior test on this branch).
        if ((word & 0xF) != 0x5) continue;

        const Elf32_Sym& sym = syms[ELF_R_SYM(rel.r_info)];
        uint32_t resolved;
        if (!resolveSymbolAddress(obj, sym, strtab, globalSymbols, symtab, resolved)) return false;

        uint32_t targetAddr = resolved + rel.r_addend;
        uint32_t selfAddr = selfBase + rel.r_offset;
        patchCall(destMem, rel.r_offset, selfAddr, targetAddr);
        continue;
      }

      if (type != R_XTENSA_32) continue;

      const Elf32_Sym& sym = syms[ELF_R_SYM(rel.r_info)];
      uint32_t resolved;
      if (!resolveSymbolAddress(obj, sym, strtab, globalSymbols, symtab, resolved)) return false;

      // The placeholder-bytes-carry-the-real-offset quirk only applies
      // to the local/static-symbol case (gas targets the enclosing
      // SECTION symbol, whose st_value is always 0, baking the true
      // offset into the bytes instead). For a genuinely external/
      // undefined symbol (memcpy, memset, etc.) there's no local offset
      // to encode there, and the placeholder isn't guaranteed to be
      // zero - confirmed the hard way: unconditionally adding it here
      // corrupted an otherwise-correctly-resolved external address once
      // a file had enough diverse external references (real mbedTLS
      // source, not the small hand-written tests this was first found
      // against), producing a bad pointer dereference at runtime.
      uint32_t placeholder = 0;
      if (sym.st_shndx != SHN_UNDEF && rel.r_offset + 4 <= destSize) {
        memcpy(&placeholder, destMem + rel.r_offset, 4);
      }

      uint32_t value = resolved + rel.r_addend + placeholder;
      if (rel.r_offset + 4 <= destSize) memcpy(destMem + rel.r_offset, &value, 4);
    }
    return true;
  }

  bool applyRelocations(LoadedObject& obj, const std::map<String, uint32_t>& globalSymbols, const ExportedSymbol* symtab) {
    // .text is relocated in a normal-RAM staging buffer first, then
    // word-copied into its final executable-RAM home, since IRAM
    // (MALLOC_CAP_EXEC memory) only supports 32-bit-aligned accesses on
    // the ESP32 - a byte-wise write into it faults with a LoadStoreError.
    uint8_t* textStaging = (uint8_t*)calloc(1, obj.textSize);
    if (!textStaging) {
      term.println("runmod: could not allocate staging buffer for .text");
      return false;
    }
    memcpy(textStaging, obj.raw + obj.shdrs[obj.textIdx].sh_offset, obj.shdrs[obj.textIdx].sh_size);

    if (!applyRelocaSection(obj, obj.relaTextIdx, textStaging, obj.textSize, (uint32_t)obj.textMem, globalSymbols, symtab)) {
      free(textStaging);
      return false;
    }

    const uint32_t* src = (const uint32_t*)textStaging;
    uint32_t* dst = (uint32_t*)obj.textMem;
    for (size_t i = 0; i < obj.textSize / 4; i++) dst[i] = src[i];
    free(textStaging);

    if (obj.rodataMem && !applyRelocaSection(obj, obj.relaRodataIdx, (uint8_t*)obj.rodataMem, obj.rodataSize, (uint32_t)obj.rodataMem, globalSymbols, symtab)) {
      return false;
    }
    if (obj.dataMem && !applyRelocaSection(obj, obj.relaDataIdx, (uint8_t*)obj.dataMem, obj.dataSize, (uint32_t)obj.dataMem, globalSymbols, symtab)) {
      return false;
    }

    return true;
  }
};
