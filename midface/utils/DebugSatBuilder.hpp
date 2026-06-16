#pragma once

#include "lists.hxx"
#include "logical.h"

class APOINT;
class BODY;
class EDGE;
class ENTITY;
class FACE;

namespace midsurface_new
{
class DebugSatBuilder
{
public:
    DebugSatBuilder();

    void Clear();
    void Add(ENTITY* entity);
    void Add(BODY* body);
    void Add(FACE* face);
    void Add(EDGE* edge);
    void Add(APOINT* point);
    int Count() const;
    ENTITY_LIST& entities();
    logical Save(const char* file_path);

private:
    ENTITY_LIST entities_;
};
} // namespace midsurface_new
