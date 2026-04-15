#pragma once

#include <filesystem>
#include <functional>
#include <string>

namespace romfetch {

using ProgressSink = std::function<void(const std::string& line)>;

#ifdef _MSC_VER
void EnsureDllSearchPath();
#endif

const char* AppVersion();

void Setup(ProgressSink sink = {});
void CheckForUpdates(ProgressSink sink = {});

bool SaveTextFromUrl(const char* url, const std::filesystem::path& path, const char* progress_label,
                     ProgressSink sink = {});
bool SaveBinaryFromUrl(const char* url, const std::filesystem::path& path, const char* progress_label,
                       ProgressSink sink = {});

/** Returns false if the name does not match a known ROM (error message optional). */
bool TryDownloadRom(const std::string& userSelection, ProgressSink sink = {});

} // namespace romfetch
