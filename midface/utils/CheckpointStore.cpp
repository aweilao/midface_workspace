#include "utils/CheckpointStore.hpp"

#include "utils/PathUtils.hpp"

#include <cstdio>
#include <fstream>
#include <sstream>

namespace midsurface_new
{
CheckpointStore::CheckpointStore()
{
}

void CheckpointStore::Configure(const char* root_dir)
{
    root_dir_ = root_dir == nullptr ? std::string() : NormalizePath(root_dir);
}

const std::string& CheckpointStore::root_dir() const
{
    return root_dir_;
}

std::string CheckpointStore::StepDir(int step) const
{
    char name[64];
    sprintf(name, "step%d", step);
    return JoinPath(root_dir_, name);
}

std::string CheckpointStore::StepFile(int step, const char* file_name) const
{
    return JoinPath(StepDir(step), file_name == nullptr ? "" : file_name);
}

logical CheckpointStore::EnsureStepDir(int step) const
{
    if (root_dir_.empty())
        return FALSE;
    return EnsureDirectoryRecursive(StepDir(step));
}

logical CheckpointStore::WriteTextFile(int step, const char* file_name, const std::string& text) const
{
    const std::string path = StepFile(step, file_name);
    if (EnsureParentDirectoryForFile(path) == FALSE)
        return FALSE;

    std::ofstream out(path.c_str());
    if (!out)
        return FALSE;
    out << text;
    return out.good() ? TRUE : FALSE;
}

logical CheckpointStore::ReadTextFile(int step, const char* file_name, std::string& out_text) const
{
    out_text.clear();
    const std::string path = StepFile(step, file_name);
    std::ifstream in(path.c_str());
    if (!in)
        return FALSE;

    std::ostringstream ss;
    ss << in.rdbuf();
    out_text = ss.str();
    return TRUE;
}
} // namespace midsurface_new
