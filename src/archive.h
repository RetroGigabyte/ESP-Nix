#pragma once

#include <Arduino.h>
#include <SD_MMC.h>
#include "miniz/miniz.h"
#include <ESP32-targz.h>
#include "filesystem.h"
#include "terminal.h"

// Extracts .zip, .tar.gz/.tgz, .gz, and .tar archives. tar/gz formats go
// through ESP32-targz (bundled uzlib + TinyUntar); real PKZIP .zip files
// go through miniz, since no existing ESP32 library reads actual zip
// archives. Both stream through the archive rather than loading it into
// RAM - archives can be much larger than the 320KB available.
class Archiver {
public:
  bool extract(FileSystem& fs, Terminal& term, const String& archivePath, const String& destDirArg) {
    if (!fs.exists(archivePath)) {
      term.println("Archive not found: " + archivePath);
      return false;
    }

    String lower = archivePath;
    lower.toLowerCase();

    String destDir = destDirArg.length() > 0 ? destDirArg : parentDir(archivePath);
    ensureDirExists(fs, destDir);

    if (lower.endsWith(".tar.gz") || lower.endsWith(".tgz")) {
      return extractTarGz(term, archivePath, destDir);
    }
    if (lower.endsWith(".tar")) {
      return extractTar(term, archivePath, destDir);
    }
    if (lower.endsWith(".gz")) {
      return extractGz(term, archivePath, destDir);
    }
    if (lower.endsWith(".zip")) {
      return extractZip(fs, term, archivePath, destDir);
    }

    term.println("Unsupported archive type (.zip, .tar.gz, .tgz, .gz, .tar): " + archivePath);
    return false;
  }

  // Creates archivePath from sourcePath, format chosen by archivePath's
  // extension. .tar/.tar.gz sources must be a directory (tar always wraps
  // a directory tree); .gz sources must be a single file; .zip accepts
  // either.
  bool compress(FileSystem& fs, Terminal& term, const String& sourcePath, const String& archivePath) {
    if (!fs.exists(sourcePath)) {
      term.println("Source not found: " + sourcePath);
      return false;
    }

    String lower = archivePath;
    lower.toLowerCase();

    if (lower.endsWith(".tar.gz") || lower.endsWith(".tgz")) {
      return compressTarGz(fs, term, sourcePath, archivePath);
    }
    if (lower.endsWith(".tar")) {
      return compressTar(fs, term, sourcePath, archivePath);
    }
    if (lower.endsWith(".gz")) {
      return compressGz(fs, term, sourcePath, archivePath);
    }
    if (lower.endsWith(".zip")) {
      return compressZip(fs, term, sourcePath, archivePath);
    }

    term.println("Unsupported output type (.zip, .tar.gz, .tgz, .gz, .tar): " + archivePath);
    return false;
  }

private:
  String parentDir(const String& path) {
    int slash = path.lastIndexOf('/');
    if (slash <= 0) return "/";
    return path.substring(0, slash);
  }

  fs::FS& fsFor(const String& path) {
    if (path.startsWith("/sd")) return SD_MMC;
    return LittleFS;
  }

  String stripSd(const String& path) {
    if (path.startsWith("/sd")) {
      String rest = path.substring(3);
      return rest.length() == 0 ? "/" : rest;
    }
    return path;
  }

  // ESP32-targz's mkdir isn't recursive and LittleFS/SD_MMC won't create
  // missing parents on their own, so walk the path and create each level.
  void ensureDirExists(FileSystem& fs, const String& dirPath) {
    if (dirPath.length() == 0 || dirPath == "/" || fs.exists(dirPath)) return;

    int slash = dirPath.lastIndexOf('/');
    if (slash > 0) ensureDirExists(fs, dirPath.substring(0, slash));

    fs.createDir(dirPath);
  }

  bool extractTarGz(Terminal& term, const String& archivePath, const String& destDir) {
    TarGzUnpacker unpacker;
    unpacker.haltOnError(false);
    unpacker.setTarVerify(false);
    unpacker.setupFSCallbacks(targzTotalBytesFn, targzFreeBytesFn);
    unpacker.setTarProgressCallback(BaseUnpacker::defaultProgressCallback);
    unpacker.setTarStatusProgressCallback(BaseUnpacker::defaultTarStatusProgressCallback);
    unpacker.setTarMessageCallback(BaseUnpacker::targzPrintLoggerCallback);

    bool ok = unpacker.tarGzExpander(fsFor(archivePath), stripSd(archivePath).c_str(),
                                      fsFor(destDir), stripSd(destDir).c_str());
    if (!ok) {
      term.println("Extraction failed (code " + String(unpacker.tarGzGetError()) + "): " + archivePath);
      return false;
    }
    term.println("Extracted to " + destDir);
    return true;
  }

  bool extractTar(Terminal& term, const String& archivePath, const String& destDir) {
    TarUnpacker unpacker;
    unpacker.haltOnError(false);
    unpacker.setTarVerify(false);
    unpacker.setupFSCallbacks(targzTotalBytesFn, targzFreeBytesFn);
    unpacker.setTarProgressCallback(BaseUnpacker::defaultProgressCallback);
    unpacker.setTarStatusProgressCallback(BaseUnpacker::defaultTarStatusProgressCallback);
    unpacker.setTarMessageCallback(BaseUnpacker::targzPrintLoggerCallback);

    bool ok = unpacker.tarExpander(fsFor(archivePath), stripSd(archivePath).c_str(),
                                    fsFor(destDir), stripSd(destDir).c_str());
    if (!ok) {
      term.println("Extraction failed (code " + String(unpacker.tarGzGetError()) + "): " + archivePath);
      return false;
    }
    term.println("Extracted to " + destDir);
    return true;
  }

  bool extractGz(Terminal& term, const String& archivePath, const String& destDir) {
    GzUnpacker unpacker;
    unpacker.haltOnError(false);
    unpacker.setupFSCallbacks(targzTotalBytesFn, targzFreeBytesFn);
    unpacker.setGzProgressCallback(BaseUnpacker::defaultProgressCallback);

    // gzExpander wants a full destination filename, not a folder - derive
    // one by stripping ".gz" from the source's basename
    String base = archivePath;
    int slash = base.lastIndexOf('/');
    if (slash >= 0) base = base.substring(slash + 1);
    if (base.endsWith(".gz")) base = base.substring(0, base.length() - 3);

    String destFile = destDir;
    if (!destFile.endsWith("/")) destFile += "/";
    destFile += base;

    bool ok = unpacker.gzExpander(fsFor(archivePath), stripSd(archivePath).c_str(),
                                   fsFor(destFile), stripSd(destFile).c_str());
    if (!ok) {
      term.println("Extraction failed (code " + String(unpacker.tarGzGetError()) + "): " + archivePath);
      return false;
    }
    term.println("Extracted to " + destFile);
    return true;
  }

  // --- ZIP (real PKZIP format, via vendored miniz) ---

  static size_t zipReadCb(void* pOpaque, mz_uint64 file_ofs, void* pBuf, size_t n) {
    File* f = (File*)pOpaque;
    if (!f->seek(file_ofs)) return 0;
    int r = f->read((uint8_t*)pBuf, n);
    return r < 0 ? 0 : (size_t)r;
  }

  static size_t zipWriteCb(void* pOpaque, mz_uint64 file_ofs, const void* pBuf, size_t n) {
    File* outFile = (File*)pOpaque;
    return outFile->write((const uint8_t*)pBuf, n);
  }

  bool extractZip(FileSystem& fs, Terminal& term, const String& archivePath, const String& destDir) {
    File archiveFile = fs.openRaw(archivePath);
    if (!archiveFile) {
      term.println("Failed to open " + archivePath);
      return false;
    }

    mz_zip_archive zip;
    memset(&zip, 0, sizeof(zip));
    zip.m_pRead = zipReadCb;
    zip.m_pIO_opaque = &archiveFile;

    if (!mz_zip_reader_init(&zip, archiveFile.size(), 0)) {
      term.println("Not a valid zip file: " + archivePath);
      archiveFile.close();
      return false;
    }

    mz_uint fileCount = mz_zip_reader_get_num_files(&zip);
    int extracted = 0;

    for (mz_uint i = 0; i < fileCount; i++) {
      mz_zip_archive_file_stat stat;
      if (!mz_zip_reader_file_stat(&zip, i, &stat)) continue;

      String name = String(stat.m_filename);
      if (name.indexOf("..") >= 0) {
        term.println("Skipping unsafe entry: " + name);
        continue;
      }

      String entryPath = destDir;
      if (!entryPath.endsWith("/")) entryPath += "/";
      entryPath += name;
      while (entryPath.endsWith("/") && entryPath.length() > 1) {
        entryPath.remove(entryPath.length() - 1);
      }

      if (mz_zip_reader_is_file_a_directory(&zip, i)) {
        ensureDirExists(fs, entryPath);
        continue;
      }

      int slash = entryPath.lastIndexOf('/');
      if (slash > 0) ensureDirExists(fs, entryPath.substring(0, slash));

      File outFile = fs.openRaw(entryPath, "w");
      if (!outFile) {
        term.println("Failed to create: " + entryPath);
        continue;
      }

      bool ok = mz_zip_reader_extract_to_callback(&zip, i, zipWriteCb, &outFile, 0);
      outFile.close();

      if (ok) {
        extracted++;
      } else {
        term.println("Failed to extract: " + entryPath);
      }
    }

    mz_zip_reader_end(&zip);
    archiveFile.close();

    term.println("Extracted " + String(extracted) + " file(s) to " + destDir);
    return true;
  }

  bool compressTarGz(FileSystem& fs, Terminal& term, const String& sourcePath, const String& archivePath) {
    if (!fs.isDir(sourcePath)) {
      term.println("Source must be a directory for .tar.gz (use .gz for a single file): " + sourcePath);
      return false;
    }

    ensureDirExists(fs, parentDir(archivePath));

    int result = TarGzPacker::compress(&fsFor(sourcePath), stripSd(sourcePath).c_str(),
                                        &fsFor(archivePath), stripSd(archivePath).c_str());
    if (result != 0) {
      term.println("Compression failed (code " + String(result) + "): " + sourcePath);
      return false;
    }
    term.println("Created " + archivePath);
    return true;
  }

  bool compressTar(FileSystem& fs, Terminal& term, const String& sourcePath, const String& archivePath) {
    if (!fs.isDir(sourcePath)) {
      term.println("Source must be a directory for .tar (use .gz for a single file): " + sourcePath);
      return false;
    }

    ensureDirExists(fs, parentDir(archivePath));

    std::vector<TAR::dir_entity_t> entities;
    TAR::collectDirEntities<fs::FS>(&entities, &fsFor(sourcePath), stripSd(sourcePath).c_str());

    int result = TarPacker::pack_files(&fsFor(sourcePath), entities,
                                        &fsFor(archivePath), stripSd(archivePath).c_str());
    if (result != 0) {
      term.println("Compression failed (code " + String(result) + "): " + sourcePath);
      return false;
    }
    term.println("Created " + archivePath);
    return true;
  }

  bool compressGz(FileSystem& fs, Terminal& term, const String& sourcePath, const String& archivePath) {
    if (fs.isDir(sourcePath)) {
      term.println("Source must be a single file for .gz (use .tar.gz for a directory): " + sourcePath);
      return false;
    }

    ensureDirExists(fs, parentDir(archivePath));

    size_t written = LZPacker::compress(&fsFor(sourcePath), stripSd(sourcePath).c_str(),
                                         &fsFor(archivePath), stripSd(archivePath).c_str());
    if (written == 0) {
      term.println("Compression failed: " + sourcePath);
      return false;
    }
    term.println("Created " + archivePath + " (" + String(written) + " bytes)");
    return true;
  }

  // --- ZIP compression (via vendored miniz) ---

  bool compressZip(FileSystem& fs, Terminal& term, const String& sourcePath, const String& archivePath) {
    ensureDirExists(fs, parentDir(archivePath));

    File outFile = fs.openRaw(archivePath, "w");
    if (!outFile) {
      term.println("Failed to create " + archivePath);
      return false;
    }

    mz_zip_archive zip;
    memset(&zip, 0, sizeof(zip));
    zip.m_pWrite = zipWriteCb;
    zip.m_pIO_opaque = &outFile;

    if (!mz_zip_writer_init(&zip, 0)) {
      term.println("Failed to start zip archive: " + archivePath);
      outFile.close();
      return false;
    }

    String baseName = sourcePath;
    int slash = baseName.lastIndexOf('/');
    if (slash >= 0) baseName = baseName.substring(slash + 1);

    int added = 0;
    bool ok;
    if (fs.isDir(sourcePath)) {
      ok = addDirToZip(fs, term, zip, sourcePath, baseName, added);
    } else {
      ok = addFileToZip(fs, term, zip, sourcePath, baseName, added);
    }

    if (ok) {
      ok = mz_zip_writer_finalize_archive(&zip);
      if (!ok) term.println("Failed to finalize zip archive: " + archivePath);
    }

    mz_zip_writer_end(&zip);
    outFile.close();

    if (!ok) return false;

    term.println("Created " + archivePath + " (" + String(added) + " file(s))");
    return true;
  }

  bool addDirToZip(FileSystem& fs, Terminal& term, mz_zip_archive& zip,
                    const String& dirPath, const String& zipPrefix, int& added) {
    String dirEntry = zipPrefix + "/";
    if (!mz_zip_writer_add_mem(&zip, dirEntry.c_str(), nullptr, 0, MZ_NO_COMPRESSION)) {
      term.println("Failed to add directory entry: " + dirEntry);
      return false;
    }

    for (const auto& name : fs.listDir(dirPath)) {
      String childPath = dirPath;
      if (!childPath.endsWith("/")) childPath += "/";
      childPath += name;

      String childZipName = zipPrefix + "/" + name;

      bool ok = fs.isDir(childPath)
        ? addDirToZip(fs, term, zip, childPath, childZipName, added)
        : addFileToZip(fs, term, zip, childPath, childZipName, added);

      if (!ok) return false;
    }
    return true;
  }

  bool addFileToZip(FileSystem& fs, Terminal& term, mz_zip_archive& zip,
                     const String& filePath, const String& zipName, int& added) {
    File file = fs.openRaw(filePath);
    if (!file) {
      term.println("Failed to open: " + filePath);
      return false;
    }

    // MZ_NO_COMPRESSION (stored, not deflated): deflate's compressor
    // state (tdefl_compressor) needs a single ~300KB allocation for its
    // hash tables, which doesn't fit in the ESP32's ~250KB free heap.
    // Storing entries uncompressed avoids that allocation entirely -
    // still a valid zip, just without space savings.
    bool ok = mz_zip_writer_add_read_buf_callback(
      &zip, zipName.c_str(), zipReadCb, &file, file.size(),
      nullptr, nullptr, 0, MZ_NO_COMPRESSION, nullptr, 0, nullptr, 0);

    file.close();

    if (!ok) {
      term.println("Failed to add: " + filePath);
      return false;
    }

    added++;
    return true;
  }
};
