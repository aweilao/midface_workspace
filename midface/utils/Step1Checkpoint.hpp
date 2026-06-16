#pragma once

#include "core/DiagnosticSink.hpp"
#include "steps/Step1FaceAnalyze.hpp"
#include "utils/CheckpointStore.hpp"

#include "body.hxx"
#include "logical.h"

namespace midsurface_new
{
logical SaveStep1Checkpoint(
    const CheckpointStore& store,
    int body_index,
    const Step1FaceAnalyzeResult& result,
    DiagnosticSink* diagnostics);

logical RestoreStep1Checkpoint(
    const CheckpointStore& store,
    int body_index,
    BODY*& out_body,
    Step1FaceAnalyzeResult& out_result,
    DiagnosticSink* diagnostics);
} // namespace midsurface_new
