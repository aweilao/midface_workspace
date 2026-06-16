#pragma once

#include "entity.hxx"
#include "face.hxx"
#include "logical.h"

#include <vector>

namespace midsurface_new
{
struct ColorRgb255
{
    ColorRgb255();

    int r;
    int g;
    int b;
};

ColorRgb255 StableColorByIndex(int index);
std::vector<ColorRgb255> BuildDistinctColorPalette(int count);
ColorRgb255 DistinctColorByIndex(int index, int count);
logical ApplyEntityColor(ENTITY* entity, const ColorRgb255& color);
logical ApplyFaceColor(FACE* face, const ColorRgb255& color);
} // namespace midsurface_new
