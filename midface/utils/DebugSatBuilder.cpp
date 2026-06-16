#include "utils/DebugSatBuilder.hpp"

#include "utils/AcisSatWriter.hpp"

#include "body.hxx"
#include "edge.hxx"
#include "entity.hxx"
#include "face.hxx"
#include "point.hxx"

namespace midsurface_new
{
DebugSatBuilder::DebugSatBuilder()
{
}

void DebugSatBuilder::Clear()
{
    entities_ = ENTITY_LIST();
}

void DebugSatBuilder::Add(ENTITY* entity)
{
    if (entity != nullptr)
        entities_.add(entity);
}

void DebugSatBuilder::Add(BODY* body)
{
    Add((ENTITY*)body);
}

void DebugSatBuilder::Add(FACE* face)
{
    Add((ENTITY*)face);
}

void DebugSatBuilder::Add(EDGE* edge)
{
    Add((ENTITY*)edge);
}

void DebugSatBuilder::Add(APOINT* point)
{
    Add((ENTITY*)point);
}

int DebugSatBuilder::Count() const
{
    return entities_.count();
}

ENTITY_LIST& DebugSatBuilder::entities()
{
    return entities_;
}

logical DebugSatBuilder::Save(const char* file_path)
{
    AcisSatWriter writer;
    return writer.Save(file_path, entities_);
}
} // namespace midsurface_new
