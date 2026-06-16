#pragma once

#include "logical.h"

#include <string>

namespace midsurface_new
{
std::string NormalizePath(const std::string& path);
std::string JoinPath(const std::string& base, const std::string& child);
std::string ParentDirOfFile(const std::string& file_path);
logical EnsureDirectoryRecursive(const std::string& dir_path);
logical EnsureParentDirectoryForFile(const std::string& file_path);
} // namespace midsurface_new
