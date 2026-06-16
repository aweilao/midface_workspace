#include "utils/StructuredEventWriter.hpp"

#include "utils/JsonUtils.hpp"
#include "utils/PathUtils.hpp"

#include <stdio.h>

namespace midsurface_new
{
StructuredEventWriter::StructuredEventWriter()
    : fp_(nullptr)
{
}

StructuredEventWriter::~StructuredEventWriter()
{
    Close();
}

logical StructuredEventWriter::Open(const char* file_path, logical append)
{
    Close();
    if (file_path == nullptr)
        return FALSE;

    if (EnsureParentDirectoryForFile(file_path) == FALSE)
        return FALSE;

    fp_ = fopen(file_path, append != FALSE ? "a" : "w");
    return fp_ != nullptr ? TRUE : FALSE;
}

void StructuredEventWriter::Close()
{
    if (fp_ != nullptr)
    {
        fclose(fp_);
        fp_ = nullptr;
    }
}

logical StructuredEventWriter::IsOpen() const
{
    return fp_ != nullptr ? TRUE : FALSE;
}

logical StructuredEventWriter::Write(const StructuredEvent& event)
{
    if (fp_ == nullptr)
        return FALSE;

    fprintf(fp_, "{");
    fprintf(fp_, "\"tags\":[");
    int i = 0;
    for (i = 0; i < (int)event.tags.size(); ++i)
    {
        if (i > 0)
            fprintf(fp_, ",");
        fprintf(fp_, "%s", JsonString(event.tags[i]).c_str());
    }
    fprintf(fp_, "]");

    fprintf(fp_, ",\"properties\":{");
    std::map<std::string, std::string>::const_iterator it = event.properties.begin();
    int count = 0;
    for (; it != event.properties.end(); ++it)
    {
        if (count > 0)
            fprintf(fp_, ",");
        fprintf(fp_, "%s:%s", JsonString(it->first).c_str(), JsonString(it->second).c_str());
        ++count;
    }
    std::map<std::string, std::string>::const_iterator jit = event.json_properties.begin();
    for (; jit != event.json_properties.end(); ++jit)
    {
        if (IsLikelyJsonValue(jit->second) == FALSE)
            continue;
        if (count > 0)
            fprintf(fp_, ",");
        fprintf(fp_, "%s:%s", JsonString(jit->first).c_str(), jit->second.c_str());
        ++count;
    }
    fprintf(fp_, "}");
    fprintf(fp_, "}\n");
    fflush(fp_);
    return TRUE;
}
} // namespace midsurface_new
