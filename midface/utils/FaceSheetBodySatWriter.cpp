#include "utils/FaceSheetBodySatWriter.hpp"

#include "utils/AcisSatWriter.hpp"

#include "cstrapi.hxx"
#include "kernapi.hxx"

namespace midsurface_new
{
namespace
{
logical CopyFaceDetached(FACE* src, FACE*& out_face)
{
    out_face = nullptr;
    if (src == nullptr)
        return FALSE;

    ENTITY* copied = nullptr;
    outcome rc = api_copy_entity((ENTITY*)src, copied);
    if (!rc.ok() || copied == nullptr || !is_FACE(copied))
        return FALSE;

    out_face = (FACE*)copied;
    return TRUE;
}
} // namespace

logical BuildSheetBodyFromFaceCopy(FACE* src_face, BODY*& out_body)
{
    out_body = nullptr;
    FACE* detached = nullptr;
    if (!CopyFaceDetached(src_face, detached) || detached == nullptr)
        return FALSE;

    FACE* one[1];
    one[0] = detached;
    BODY* body = nullptr;
    outcome rb = api_sheet_from_ff(1, one, body);
    if (!rb.ok() || body == nullptr)
        return FALSE;

    out_body = body;
    return TRUE;
}

logical SaveFacesAsSheetBodiesSat(
    const std::vector<FACE*>& faces,
    const char* output_sat_path)
{
    if (output_sat_path == nullptr)
        return FALSE;

    ENTITY_LIST entities;
    int i = 0;
    for (i = 0; i < (int)faces.size(); ++i)
    {
        BODY* body = nullptr;
        if (BuildSheetBodyFromFaceCopy(faces[i], body) != FALSE && body != nullptr)
            entities.add((ENTITY*)body);
    }

    AcisSatWriter writer;
    return writer.Save(output_sat_path, entities);
}
} // namespace midsurface_new
