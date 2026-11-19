#pragma once

#include <QString>

#include <atomic>

// Decompresses a .bz2 file (also several concatenated streams, as pbzip2 writes) to `out`, with the
// bzip2 1.0.8 library built into the program. The tar.exe of Windows 10 reads zip and gzip but not
// bzip2, so the .tar.bz2 of the recommended model is decompressed here and tar.exe gets a plain .tar.
// Runs on any thread. Returns false with `error` set (out removed) on failure or when `cancel` is set.
bool Bunzip2File(const QString &in, const QString &out, const std::atomic<bool> &cancel, QString &error);
