#include "MidSurface.h"

#include "core/DiagnosticSink.hpp"
#include "utils/PathUtils.hpp"
#include "utils/ResultJsonWriter.hpp"
#include "utils/Step1Checkpoint.hpp"

#include "kernapi.hxx"

#include <cstdio>
#include <set>
#include <vector>

namespace midsurface_new
{
namespace
{
const char* kStartMethodBodyPipeline = "body_pipeline";
const char* kStartMethodSatPipeline = "sat_pipeline";
const char* kStartMethodStep2FromStep1Checkpoint = "step2_from_step1_checkpoint";
const char* kCheckpointStep1Result = "step1.result";
const char* kDebugStep1ColoredBody = "step1.colored_body";
const char* kDebugStep2GroupColoredBody = "step2.group_colored_body";
const char* kDebugStep3PairColoredBody = "step3.pair_colored_body";

StructuredEvent PipelineEventTemplate()
{
    StructuredEvent event;
    event.AddTag("pipeline");
    return event;
}

std::string IntText(int value)
{
    char buf[64];
    sprintf(buf, "%d", value);
    return std::string(buf);
}

logical ShouldStopAfter(const MidSurfaceConfig& config, int step)
{
    return config.run_context.stop_after_step > PIPELINE_STEP_UNKNOWN &&
        config.run_context.stop_after_step <= step ? TRUE : FALSE;
}

logical ShouldCheckpoint(const MidSurfaceConfig& config, const char* checkpoint_role)
{
    if (config.run_context.enable_checkpoints == FALSE || checkpoint_role == nullptr)
        return FALSE;

    int i = 0;
    for (i = 0; i < (int)config.run_context.checkpoint_outputs.size(); ++i)
    {
        if (config.run_context.checkpoint_outputs[i] == checkpoint_role)
            return TRUE;
    }
    return FALSE;
}

void EmitResultJsonOutputEvent(
    RunContext& context,
    int body_index,
    const MidSurfaceResult& result,
    logical ok)
{
    StructuredEvent event = PipelineEventTemplate();
    event.AddTag("result-json");
    event.AddTag("output");
    event.AddTag(ok != FALSE ? "ok" : "fail");
    event.SetProperty("body_index", IntText(body_index));
    event.SetProperty("path", ResultJsonPath(context, body_index));
    event.SetProperty("completed_step", IntText(result.completed_step));
    (void)context.EmitForBody(body_index, event);
}

logical SaveResultJsonForBody(
    RunContext& context,
    int body_index,
    const MidSurfaceResult& result)
{
    const logical ok = SaveMidSurfaceResultJson(context, body_index, result);
    EmitResultJsonOutputEvent(context, body_index, result, ok);
    return ok;
}

logical RunMidSurfaceBodyWithContext(
    BODY* body,
    const MidSurfaceConfig& config,
    RunContext& context,
    DiagnosticSink& diagnostics,
    int body_index,
    MidSurfaceResult& result)
{
    result = MidSurfaceResult();
    diagnostics.SetCurrentBodyIndex(body_index);

    StructuredEvent start_event = PipelineEventTemplate();
    start_event.SetProperty("body_index", IntText(body_index));
    (void)context.EmitSimpleForBody(
        body_index,
        start_event,
        "event:start",
        DIAG_INFO,
        nullptr);

    if (RunStep1FaceAnalyze(body, config.step1, &diagnostics, result.step1) == FALSE)
    {
        result.completed_step = PIPELINE_STEP_UNKNOWN;
        result.ok = FALSE;
        (void)SaveResultJsonForBody(context, body_index, result);
        (void)context.EmitSimpleForBody(
            body_index,
            PipelineEventTemplate(),
            "event:stop_after_step1",
            DIAG_WARN,
            nullptr);
        return FALSE;
    }
    result.completed_step = PIPELINE_STEP1_FACE_ANALYZE;
    if (ShouldCheckpoint(config, kCheckpointStep1Result) != FALSE)
        (void)SaveStep1Checkpoint(context.checkpoints(), body_index, result.step1, &diagnostics);
    if (ShouldStopAfter(config, PIPELINE_STEP1_FACE_ANALYZE) != FALSE)
    {
        result.ok = result.step1.ok;
        (void)SaveResultJsonForBody(context, body_index, result);
        (void)context.EmitSimpleForBody(
            body_index,
            PipelineEventTemplate(),
            "event:stop_after_step1",
            DIAG_INFO,
            nullptr);
        return result.ok;
    }

    if (RunStep2GroupBuild(result.step1.state, config.step2, &diagnostics, result.step2) == FALSE)
    {
        result.ok = FALSE;
        (void)SaveResultJsonForBody(context, body_index, result);
        return FALSE;
    }
    result.completed_step = PIPELINE_STEP2_GROUP_BUILD;
    if (ShouldStopAfter(config, PIPELINE_STEP2_GROUP_BUILD) != FALSE)
    {
        result.ok = result.step2.ok;
        (void)SaveResultJsonForBody(context, body_index, result);
        (void)context.EmitSimpleForBody(
            body_index,
            PipelineEventTemplate(),
            "event:stop_after_step2",
            DIAG_INFO,
            nullptr);
        return result.ok;
    }

    if (RunStep3PairBuild(result.step2.state, config.step3, &diagnostics, result.step3) == FALSE)
    {
        result.ok = FALSE;
        (void)SaveResultJsonForBody(context, body_index, result);
        return FALSE;
    }
    result.completed_step = PIPELINE_STEP3_PAIR_BUILD;
    if (ShouldStopAfter(config, PIPELINE_STEP3_PAIR_BUILD) != FALSE)
    {
        result.ok = result.step3.ok;
        (void)SaveResultJsonForBody(context, body_index, result);
        return result.ok;
    }

    if (RunStep4RelationBuild(result.step3.state, config.step4, &diagnostics, result.step4) == FALSE)
    {
        result.ok = FALSE;
        (void)SaveResultJsonForBody(context, body_index, result);
        return FALSE;
    }
    result.completed_step = PIPELINE_STEP4_RELATION_BUILD;
    if (ShouldStopAfter(config, PIPELINE_STEP4_RELATION_BUILD) != FALSE)
    {
        result.ok = result.step4.ok;
        (void)SaveResultJsonForBody(context, body_index, result);
        return result.ok;
    }

    if (RunStep5MidPatchBuild(result.step4.state, config.step5, &diagnostics, result.step5) == FALSE)
    {
        result.ok = FALSE;
        (void)SaveResultJsonForBody(context, body_index, result);
        return FALSE;
    }
    result.completed_step = PIPELINE_STEP5_MID_PATCH_BUILD;
    if (ShouldStopAfter(config, PIPELINE_STEP5_MID_PATCH_BUILD) != FALSE)
    {
        result.ok = result.step5.ok;
        (void)SaveResultJsonForBody(context, body_index, result);
        return result.ok;
    }

    if (RunStep6TrimSelect(result.step5.state, config.step6, &diagnostics, result.step6) == FALSE)
    {
        result.ok = FALSE;
        (void)SaveResultJsonForBody(context, body_index, result);
        return FALSE;
    }
    result.completed_step = PIPELINE_STEP6_TRIM_SELECT;
    if (ShouldStopAfter(config, PIPELINE_STEP6_TRIM_SELECT) != FALSE)
    {
        result.ok = result.step6.ok;
        (void)SaveResultJsonForBody(context, body_index, result);
        return result.ok;
    }

    if (RunStep7Stitch(result.step6.state.step7_stitch_input, result.step7) == FALSE)
    {
        result.ok = FALSE;
        (void)SaveResultJsonForBody(context, body_index, result);
        return FALSE;
    }
    result.completed_step = PIPELINE_STEP7_FINAL_EXPORT;

    result.ok = result.step7.ok;
    (void)SaveResultJsonForBody(context, body_index, result);
    return result.ok;
}

logical RunMidSurfaceFromStep1CheckpointWithContext(
    const MidSurfaceConfig& config,
    RunContext& context,
    DiagnosticSink& diagnostics,
    int body_index,
    MidSurfaceResult& result)
{
    result = MidSurfaceResult();
    diagnostics.SetCurrentBodyIndex(body_index);

    BODY* body = nullptr;
    if (RestoreStep1Checkpoint(context.checkpoints(), body_index, body, result.step1, &diagnostics) == FALSE)
    {
        result.ok = FALSE;
        (void)SaveResultJsonForBody(context, body_index, result);
        return FALSE;
    }
    result.completed_step = PIPELINE_STEP1_FACE_ANALYZE;

    if (ShouldStopAfter(config, PIPELINE_STEP1_FACE_ANALYZE) != FALSE)
    {
        result.ok = result.step1.ok;
        (void)SaveResultJsonForBody(context, body_index, result);
        return result.ok;
    }

    if (RunStep2GroupBuild(result.step1.state, config.step2, &diagnostics, result.step2) == FALSE)
    {
        result.ok = FALSE;
        (void)SaveResultJsonForBody(context, body_index, result);
        return FALSE;
    }
    result.completed_step = PIPELINE_STEP2_GROUP_BUILD;
    if (ShouldStopAfter(config, PIPELINE_STEP2_GROUP_BUILD) != FALSE)
    {
        result.ok = result.step2.ok;
        (void)SaveResultJsonForBody(context, body_index, result);
        (void)context.EmitSimpleForBody(
            body_index,
            PipelineEventTemplate(),
            "event:stop_after_step2",
            DIAG_INFO,
            nullptr);
        return result.ok;
    }

    if (RunStep3PairBuild(result.step2.state, config.step3, &diagnostics, result.step3) == FALSE)
    {
        result.ok = FALSE;
        (void)SaveResultJsonForBody(context, body_index, result);
        return FALSE;
    }
    result.completed_step = PIPELINE_STEP3_PAIR_BUILD;
    result.ok = result.step3.ok;
    (void)SaveResultJsonForBody(context, body_index, result);
    return result.ok;
}

logical RestoreSatFileToEntityList(const char* file_path, ENTITY_LIST& out_entities)
{
    if (file_path == nullptr)
        return FALSE;

    FILE* fp = fopen(file_path, "r");
    if (fp == nullptr)
        return FALSE;

    outcome rc = api_restore_entity_list(fp, TRUE, out_entities);
    fclose(fp);
    return rc.ok() ? TRUE : FALSE;
}

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
} // namespace

MidSurfaceConfig::MidSurfaceConfig()
{
}

void MidSurfaceConfig::EnableDebugSatOutput(const char* artifact_role)
{
    if (artifact_role == nullptr || artifact_role[0] == '\0')
        return;

    int i = 0;
    for (i = 0; i < (int)run_context.debug_sat_outputs.size(); ++i)
    {
        if (run_context.debug_sat_outputs[i] == artifact_role)
            return;
    }
    run_context.debug_sat_outputs.push_back(artifact_role);
}

void MidSurfaceConfig::EnableDefaultDebugSatOutputs()
{
    EnableDebugSatOutput(kDebugStep1ColoredBody);
    EnableDebugSatOutput(kDebugStep2GroupColoredBody);
}

void MidSurfaceConfig::EnableStep3PairColoredBodyDebugSat()
{
    EnableDebugSatOutput(kDebugStep3PairColoredBody);
}

MidSurfaceResult::MidSurfaceResult()
    : ok(FALSE),
      completed_step(PIPELINE_STEP_UNKNOWN)
{
}

logical RunMidSurface(
    BODY* body,
    const MidSurfaceConfig& config,
    MidSurfaceResult& result)
{
    RunContext context;
    if (context.Configure(config.run_context) == FALSE)
        return FALSE;

    DiagnosticSink diagnostics;
    diagnostics.Configure(&context);

    if (config.run_context.start_method == kStartMethodStep2FromStep1Checkpoint)
    {
        const logical ok = RunMidSurfaceFromStep1CheckpointWithContext(config, context, diagnostics, 0, result);
        (void)context.EmitSimple(
            PipelineEventTemplate(),
            ok != FALSE ? "event:finish" : "event:finish_without_result",
            ok != FALSE ? DIAG_INFO : DIAG_WARN,
            nullptr);
        return ok;
    }

    const logical ok = RunMidSurfaceBodyWithContext(body, config, context, diagnostics, 0, result);
    (void)context.EmitSimple(
        PipelineEventTemplate(),
        ok != FALSE ? "event:finish" : "event:finish_without_result",
        ok != FALSE ? DIAG_INFO : DIAG_WARN,
        nullptr);
    return ok;
}

logical RunMidSurface(
    const MidSurfaceConfig& config,
    MidSurfaceResult& result)
{
    result = MidSurfaceResult();
    if (config.run_context.start_method != kStartMethodSatPipeline &&
        config.run_context.start_method != kStartMethodStep2FromStep1Checkpoint)
        return FALSE;
    if (config.run_context.start_method == kStartMethodSatPipeline &&
        config.run_context.input_sat_path.empty())
        return FALSE;

    RunContext context;
    if (context.Configure(config.run_context) == FALSE)
        return FALSE;

    DiagnosticSink diagnostics;
    diagnostics.Configure(&context);

    if (config.run_context.start_method == kStartMethodStep2FromStep1Checkpoint)
    {
        MidSurfaceResult body_result;
        const logical ok = RunMidSurfaceFromStep1CheckpointWithContext(config, context, diagnostics, 0, body_result);
        result = body_result;
        result.ok = ok;
        (void)context.EmitSimple(
            PipelineEventTemplate(),
            result.ok != FALSE ? "event:finish" : "event:finish_without_result",
            result.ok != FALSE ? DIAG_INFO : DIAG_WARN,
            nullptr);
        return result.ok;
    }

    ENTITY_LIST input_entities;
    if (RestoreSatFileToEntityList(config.run_context.input_sat_path.c_str(), input_entities) == FALSE)
        return FALSE;

    std::vector<BODY*> bodies;
    CollectBodyEntities(input_entities, bodies);
    if (bodies.empty())
        return FALSE;

    logical any_ok = FALSE;
    int i = 0;
    for (i = 0; i < (int)bodies.size(); ++i)
    {
        MidSurfaceResult body_result;
        if (RunMidSurfaceBodyWithContext(bodies[i], config, context, diagnostics, i, body_result) != FALSE)
            any_ok = TRUE;
        result = body_result;
    }

    result.ok = any_ok;
    (void)context.EmitSimple(
        PipelineEventTemplate(),
        result.ok != FALSE ? "event:finish" : "event:finish_without_result",
        result.ok != FALSE ? DIAG_INFO : DIAG_WARN,
        nullptr);
    return result.ok;
}
} // namespace midsurface_new
