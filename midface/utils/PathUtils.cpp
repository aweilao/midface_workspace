#include "utils/PathUtils.hpp"

#include <cerrno>
#include <sys/stat.h>
#include <sys/types.h>

namespace midsurface_new
{
namespace
{
logical IsAbsolutePath(const std::string& path)
{
    return (!path.empty() && path[0] == '/') ? TRUE : FALSE;
}
}

std::string NormalizePath(const std::string& path)
{
    std::string out = path;
    int i = 0;
    for (i = 0; i < (int)out.size(); ++i)
    {
        if (out[i] == '\\')
            out[i] = '/';
    }

    while (out.size() > 1 && out[out.size() - 1] == '/')
        out.erase(out.size() - 1);
    return out;
}

std::string JoinPath(const std::string& base, const std::string& child)
{
    if (child.empty())
        return NormalizePath(base);
    if (IsAbsolutePath(child) || base.empty())
        return NormalizePath(child);

    std::string out = NormalizePath(base);
    if (!out.empty() && out[out.size() - 1] != '/')
        out += "/";
    out += child;
    return NormalizePath(out);
}

std::string ParentDirOfFile(const std::string& file_path)
{
    const std::string path = NormalizePath(file_path);
    const std::string::size_type pos = path.find_last_of('/');
    if (pos == std::string::npos)
        return std::string();
    if (pos == 0)
        return "/";
    return path.substr(0, pos);
}

logical EnsureDirectoryRecursive(const std::string& dir_path)
{
    if (dir_path.empty())
        return FALSE;

    const std::string path = NormalizePath(dir_path);
    struct stat st;
    if (stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode))
        return TRUE;

    std::string current;
    int start = 0;
    if (!path.empty() && path[0] == '/')
    {
        current = "/";
        start = 1;
    }

    while (start <= (int)path.size())
    {
        int next = start;
        while (next < (int)path.size() && path[next] != '/')
            ++next;

        const std::string part = path.substr(start, next - start);
        if (!part.empty())
        {
            current = JoinPath(current, part);
            if (mkdir(current.c_str(), 0755) != 0 && errno != EEXIST)
                return FALSE;
        }

        if (next >= (int)path.size())
            break;
        start = next + 1;
    }

    return (stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode)) ? TRUE : FALSE;
}

logical EnsureParentDirectoryForFile(const std::string& file_path)
{
    const std::string parent = ParentDirOfFile(file_path);
    if (parent.empty())
        return TRUE;
    return EnsureDirectoryRecursive(parent);
}
} // namespace midsurface_new
