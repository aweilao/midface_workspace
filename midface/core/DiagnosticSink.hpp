#pragma once

#include "core/MidSurfaceNewTypes.hpp"
#include "core/RunContext.hpp"

#include "body.hxx"
#include "lists.hxx"
#include "logical.h"

#include <string>
#include <vector>
#include <map>

namespace midsurface_new
{
class DiagnosticSink
{
public:
    DiagnosticSink();

    void Configure(RunContext* context);
    void SetCurrentBodyIndex(int body_index);
    RunContext* context();
    const RunContext* context() const;
    int current_body_index() const;

    logical EmitEvent(const StructuredEvent& event);
    logical EmitPointSet(
        int step,
        const char* schema,
        const char* event_name,
        const std::vector<PointSample>& points);
    logical EmitCrossMarkerSet(
        int step,
        const char* schema,
        const char* event_name,
        const std::vector<PointSample>& points);
    logical EmitBodySatIfEnabled(const char* artifact_role, BODY* body);
    logical EmitEntityListSat(const char* artifact_role, const char* file_path, ENTITY_LIST& entities);
    logical EmitFaceSetSat(const char* artifact_role, const char* file_path, const std::vector<FACE*>& faces);
    logical EmitColorIdMapEntryIfEnabled(
        const std::vector<std::string>& tags,
        const char* artifact_role,
        const char* id_type,
        const std::map<std::string, std::string>& properties,
        const std::map<std::string, std::string>& json_properties);
    logical ShouldOutputDebugSat(const char* artifact_role) const;
    logical ShouldTraceFacePair(int face_a, int face_b) const;

private:
    RunContext* context_;
    int current_body_index_;
};
} // namespace midsurface_new
