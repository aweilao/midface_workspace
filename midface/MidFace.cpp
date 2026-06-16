#include "MidFace.h"

#include "utils/AcisSatWriter.hpp"

#include "kernapi.hxx"

#include <cstdio>

namespace midface_new
{
namespace
{
void CollectBodyEntities(ENTITY_LIST& entity_list, std::vector<BODY*>& out_bodies)
{
    out_bodies.clear();

    int i = 0;
    for (i = 0; i < entity_list.count(); ++i)
    {
        ENTITY* ent = entity_list[i];
        if (ent != nullptr && is_BODY(ent))
            out_bodies.push_back((BODY*)ent);
    }
}
}

MidFaceBuildOptions::MidFaceBuildOptions()
    : final_mode(midsurface_new::MIDFACE_NEW_FINAL_STEP7_EXPORTED)
{
    pipeline.run_context.enable_diagnostics = FALSE;
}

MidFaceBuildResult::MidFaceBuildResult()
    : ok(FALSE),
      input_body_count(0),
      processed_body_count(0),
      final_entity_count(0),
      single_entity(nullptr)
{
}

bool RestoreSatFileToEntityList(
    const char* file_path,
    ENTITY_LIST& out_entities)
{
    if (file_path == nullptr)
        return false;

    FILE* fp = fopen(file_path, "r");
    if (fp == nullptr)
        return false;

    outcome rc = api_restore_entity_list(fp, TRUE, out_entities);
    fclose(fp);
    return rc.ok() ? true : false;
}

bool BuildMidFaceSatFile(
    const char* input_sat_path,
    const char* output_sat_path)
{
    MidFaceBuildOptions options;
    return BuildMidFaceSatFileEx(input_sat_path, output_sat_path, options, nullptr);
}

bool BuildMidFaceSatFileEx(
    const char* input_sat_path,
    const char* output_sat_path,
    const MidFaceBuildOptions& options,
    MidFaceBuildResult* out_result)
{
    if (input_sat_path == nullptr || output_sat_path == nullptr)
        return false;

    ENTITY_LIST input_entities;
    if (!RestoreSatFileToEntityList(input_sat_path, input_entities))
        return false;

    ENTITY_LIST output_entities;
    MidFaceBuildResult result;
    const bool ok = BuildMidFaceEntitiesFromEntityList(
        input_entities,
        options,
        output_entities,
        &result);

    if (out_result != nullptr)
        *out_result = result;

    if (!ok || output_entities.count() <= 0)
        return false;

    midsurface_new::AcisSatWriter writer;
    return writer.Save(output_sat_path, output_entities) != FALSE;
}

bool BuildMidFaceEntitiesFromEntityList(
    ENTITY_LIST& input_entities,
    const MidFaceBuildOptions& options,
    ENTITY_LIST& out_entities,
    MidFaceBuildResult* out_result)
{
    out_entities = ENTITY_LIST();

    MidFaceBuildResult result;
    std::vector<BODY*> bodies;
    CollectBodyEntities(input_entities, bodies);
    result.input_body_count = (int)bodies.size();
    if (bodies.empty())
    {
        if (out_result != nullptr)
            *out_result = result;
        return false;
    }

    int i = 0;
    for (i = 0; i < (int)bodies.size(); ++i)
    {
        midsurface_new::MidSurfaceResult pipeline_result;
        if (midsurface_new::RunMidSurface(bodies[i], options.pipeline, pipeline_result) == FALSE)
        {
            result.pipeline_result = pipeline_result;
            continue;
        }

        ++result.processed_body_count;
        result.pipeline_result = pipeline_result;
    }

    result.final_entity_count = (int)result.final_entities.size();
    if (result.final_entity_count == 1)
        result.single_entity = result.final_entities[0];
    result.ok = result.final_entity_count > 0 ? TRUE : FALSE;

    if (out_result != nullptr)
        *out_result = result;
    return result.ok != FALSE;
}

bool BuildMidFaceEntityFromEntityList(
    ENTITY_LIST& input_entities,
    const MidFaceBuildOptions& options,
    ENTITY*& out_entity,
    MidFaceBuildResult* out_result)
{
    out_entity = nullptr;

    ENTITY_LIST out_entities;
    MidFaceBuildResult result;
    const bool ok = BuildMidFaceEntitiesFromEntityList(
        input_entities,
        options,
        out_entities,
        &result);

    if (out_result != nullptr)
        *out_result = result;

    if (!ok || out_entities.count() != 1)
        return false;

    out_entity = out_entities[0];
    return out_entity != nullptr;
}

bool BuildMidFaceBodyFromBody(BODY* input_body, BODY*& output_body)
{
    return BuildMidFaceBodyFromBodyEx(input_body, output_body, FALSE);
}

bool BuildMidFaceBodyFromBodyEx(
    BODY* input_body,
    BODY*& output_body,
    logical enable_output_files)
{
    MidFaceBuildOptions options;
    options.pipeline.run_context.enable_diagnostics = enable_output_files;
    return BuildMidFaceBodyFromBodyEx(input_body, options, output_body, nullptr);
}

bool BuildMidFaceBodyFromBodyEx(
    BODY* input_body,
    const MidFaceBuildOptions& options,
    BODY*& output_body,
    MidFaceBuildResult* out_result)
{
    output_body = nullptr;
    if (input_body == nullptr)
        return false;

    ENTITY_LIST input_entities;
    input_entities.add((ENTITY*)input_body);

    ENTITY* out_entity = nullptr;
    const bool ok = BuildMidFaceEntityFromEntityList(
        input_entities,
        options,
        out_entity,
        out_result);
    if (!ok || out_entity == nullptr || !is_BODY(out_entity))
        return false;

    output_body = (BODY*)out_entity;
    return true;
}
} // namespace midface_new
