#pragma once

#include "MidSurface.h"

#include "body.hxx"
#include "entity.hxx"
#include "lists.hxx"
#include "logical.h"

#include <vector>

namespace midface_new
{
struct MidFaceBuildOptions
{
    MidFaceBuildOptions();

    midsurface_new::MidSurfaceNewFinalMode final_mode;
    midsurface_new::MidSurfaceConfig pipeline;
};

struct MidFaceBuildResult
{
    MidFaceBuildResult();

    logical ok;
    int input_body_count;
    int processed_body_count;
    int final_entity_count;
    ENTITY* single_entity;
    std::vector<ENTITY*> final_entities;
    midsurface_new::MidSurfaceResult pipeline_result;
};

bool RestoreSatFileToEntityList(
    const char* file_path,
    ENTITY_LIST& out_entities);

bool BuildMidFaceSatFile(
    const char* input_sat_path,
    const char* output_sat_path);

bool BuildMidFaceSatFileEx(
    const char* input_sat_path,
    const char* output_sat_path,
    const MidFaceBuildOptions& options,
    MidFaceBuildResult* out_result);

bool BuildMidFaceEntitiesFromEntityList(
    ENTITY_LIST& input_entities,
    const MidFaceBuildOptions& options,
    ENTITY_LIST& out_entities,
    MidFaceBuildResult* out_result);

bool BuildMidFaceEntityFromEntityList(
    ENTITY_LIST& input_entities,
    const MidFaceBuildOptions& options,
    ENTITY*& out_entity,
    MidFaceBuildResult* out_result);

bool BuildMidFaceBodyFromBody(BODY* input_body, BODY*& output_body);

bool BuildMidFaceBodyFromBodyEx(
    BODY* input_body,
    BODY*& output_body,
    logical enable_output_files);

bool BuildMidFaceBodyFromBodyEx(
    BODY* input_body,
    const MidFaceBuildOptions& options,
    BODY*& output_body,
    MidFaceBuildResult* out_result);
} // namespace midface_new
