#include "utils/AcisSatWriter.hpp"

#include "utils/PathUtils.hpp"

#include "errorbase.hxx"
#include "fileinfo.hxx"
#include "kernapi.hxx"

#include <stdio.h>

namespace midsurface_new
{
AcisSatWriter::AcisSatWriter()
{
}

logical AcisSatWriter::Save(const char* file_path, ENTITY_LIST& entities) const
{
    if (file_path == nullptr)
        return FALSE;
    if (entities.count() <= 0)
        return FALSE;
    if (EnsureParentDirectoryForFile(file_path) == FALSE)
        return FALSE;

    FileInfo fileinfo;
    fileinfo.set_units(1.0);
    fileinfo.set_product_id("ACIS (c) SPATIAL");
    outcome info_result = api_set_file_info(3, fileinfo);
    if (!info_result.ok())
        return FALSE;

    FILE* fp = fopen(file_path, "w");
    if (fp == nullptr)
        return FALSE;

    outcome result = api_save_entity_list(fp, TRUE, entities);
    fclose(fp);
    return result.ok() ? TRUE : FALSE;
}
} // namespace midsurface_new
