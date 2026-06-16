#pragma once

#include "core/RunContext.hpp"
#include "steps/Step1FaceAnalyze.hpp"
#include "steps/Step2GroupBuild.hpp"
#include "steps/Step3PairBuild.hpp"
#include "steps/Step4RelationBuild.hpp"
#include "steps/Step5MidPatchBuild.hpp"
#include "steps/Step6TrimSelect.hpp"
#include "steps/Step7Stitch.hpp"

#include "body.hxx"
#include "logical.h"

namespace midsurface_new
{
struct MidSurfaceConfig
{
    MidSurfaceConfig();

    void EnableDebugSatOutput(const char* artifact_role);
    void EnableDefaultDebugSatOutputs();
    void EnableStep3PairColoredBodyDebugSat();

    RunContextOptions run_context;
    Step1FaceAnalyzeOptions step1;
    Step2GroupOptions step2;
    Step3PairOptions step3;
    Step4RelationOptions step4;
    Step5MidPatchOptions step5;
    Step6TrimSelectOptions step6;
};

struct MidSurfaceResult
{
    MidSurfaceResult();

    logical ok;
    int completed_step;
    Step1FaceAnalyzeResult step1;
    Step2GroupResult step2;
    Step3PairResult step3;
    Step4RelationResult step4;
    Step5MidPatchResult step5;
    Step6TrimSelectResult step6;
    Step7StitchResult step7;
};

logical RunMidSurface(
    BODY* body,
    const MidSurfaceConfig& config,
    MidSurfaceResult& result);

logical RunMidSurface(
    const MidSurfaceConfig& config,
    MidSurfaceResult& result);
} // namespace midsurface_new
