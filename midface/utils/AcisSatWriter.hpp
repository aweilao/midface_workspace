#pragma once

#include "lists.hxx"
#include "logical.h"

namespace midsurface_new
{
class AcisSatWriter
{
public:
    AcisSatWriter();

    logical Save(const char* file_path, ENTITY_LIST& entities) const;
};
} // namespace midsurface_new
