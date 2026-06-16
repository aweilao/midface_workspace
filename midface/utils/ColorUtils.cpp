#include "utils/ColorUtils.hpp"

#include "entity_color.hxx"
#include "rgbcolor.hxx"

namespace midsurface_new
{
namespace
{
double ClampUnit(double value)
{
    if (value < 0.0)
        return 0.0;
    if (value > 1.0)
        return 1.0;
    return value;
}

int ClampColorByte(int value)
{
    if (value < 0)
        return 0;
    if (value > 255)
        return 255;
    return value;
}

int UnitToColorByte(double value)
{
    return ClampColorByte((int)(ClampUnit(value) * 255.0 + 0.5));
}

double ColorByteToUnit(int value)
{
    return ((double)ClampColorByte(value)) / 255.0;
}

ColorRgb255 HsvToRgb(double hue, double saturation, double value)
{
    hue = ClampUnit(hue);
    saturation = ClampUnit(saturation);
    value = ClampUnit(value);

    const double h6 = hue * 6.0;
    int sector = (int)h6;
    if (sector >= 6)
        sector = 5;

    const double f = h6 - (double)sector;
    const double p = value * (1.0 - saturation);
    const double q = value * (1.0 - saturation * f);
    const double t = value * (1.0 - saturation * (1.0 - f));

    double r = 0.0;
    double g = 0.0;
    double b = 0.0;
    switch (sector)
    {
    case 0:
        r = value; g = t; b = p;
        break;
    case 1:
        r = q; g = value; b = p;
        break;
    case 2:
        r = p; g = value; b = t;
        break;
    case 3:
        r = p; g = q; b = value;
        break;
    case 4:
        r = t; g = p; b = value;
        break;
    case 5:
    default:
        r = value; g = p; b = q;
        break;
    }

    ColorRgb255 color;
    color.r = UnitToColorByte(r);
    color.g = UnitToColorByte(g);
    color.b = UnitToColorByte(b);
    return color;
}
} // namespace

ColorRgb255::ColorRgb255()
    : r(0),
      g(0),
      b(0)
{
}

ColorRgb255 StableColorByIndex(int index)
{
    if (index < 0)
        index = 0;

    ColorRgb255 c;
    c.r = (53 * (index + 1)) % 256;
    c.g = (97 * (index + 1)) % 256;
    c.b = (193 * (index + 1)) % 256;
    return c;
}

std::vector<ColorRgb255> BuildDistinctColorPalette(int count)
{
    std::vector<ColorRgb255> colors;
    if (count <= 0)
        return colors;

    colors.reserve((size_t)count);
    int i = 0;
    for (i = 0; i < count; ++i)
        colors.push_back(DistinctColorByIndex(i, count));
    return colors;
}

ColorRgb255 DistinctColorByIndex(int index, int count)
{
    if (count <= 0)
        return ColorRgb255();

    if (index < 0)
        index = 0;
    if (index >= count)
        index = index % count;

    const double hue = ((double)index) / ((double)count);
    return HsvToRgb(hue, 0.75, 0.95);
}

logical ApplyEntityColor(ENTITY* entity, const ColorRgb255& color)
{
    if (entity == nullptr)
        return FALSE;

    rgb_color acis_color(
        ColorByteToUnit(color.r),
        ColorByteToUnit(color.g),
        ColorByteToUnit(color.b));
    (void)set_entity_color(entity, acis_color);
    return TRUE;
}

logical ApplyFaceColor(FACE* face, const ColorRgb255& color)
{
    if (face == nullptr)
        return FALSE;
    return ApplyEntityColor((ENTITY*)face, color);
}
} // namespace midsurface_new
