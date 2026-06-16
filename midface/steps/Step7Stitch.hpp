#pragma once

#include "steps/Step7StitchInput.hpp"

#include "logical.h"

namespace midsurface_new
{
struct Step7StitchState
{
    Step7StitchInput input;
};

struct Step7StitchResult
{
    logical ok = FALSE;
    Step7StitchState state;
};

logical RunStep7Stitch(
    const Step7StitchInput& input,
    Step7StitchResult& result);

logical WriteStep7InputSplitFacesSat(
    const std::vector<Step7SplitRawFaceRecord>& split_to_raw_faces,
    const char* output_sat_path);
} // namespace midsurface_new
