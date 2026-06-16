#pragma once

#include "body.hxx"
#include "face.hxx"
#include "logical.h"

#include <vector>

namespace midsurface_new
{
logical BuildSheetBodyFromFaceCopy(FACE* src_face, BODY*& out_body);

logical SaveFacesAsSheetBodiesSat(
    const std::vector<FACE*>& faces,
    const char* output_sat_path);
} // namespace midsurface_new
