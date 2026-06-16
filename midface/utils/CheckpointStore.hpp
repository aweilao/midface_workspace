#pragma once

#include "logical.h"

#include <string>

namespace midsurface_new
{
class CheckpointStore
{
public:
    CheckpointStore();

    void Configure(const char* root_dir);
    const std::string& root_dir() const;
    std::string StepDir(int step) const;
    std::string StepFile(int step, const char* file_name) const;
    logical EnsureStepDir(int step) const;
    logical WriteTextFile(int step, const char* file_name, const std::string& text) const;
    logical ReadTextFile(int step, const char* file_name, std::string& out_text) const;

private:
    std::string root_dir_;
};
} // namespace midsurface_new
