#include "steps/Step7Stitch.hpp"

#include "utils/FaceSheetBodySatWriter.hpp"

namespace midsurface_new
{
namespace
{
const Step7RawFaceRef* FindRawFaceRef(
    const std::vector<Step7RawFaceRef>& raw_faces,
    int raw_face_id)
{
    int i = 0;
    for (i = 0; i < (int)raw_faces.size(); ++i)
    {
        if (raw_faces[i].raw_face_id == raw_face_id)
            return &raw_faces[i];
    }
    return nullptr;
}

void AppendGroupFaces(const Step7GroupRef& group, std::vector<FACE*>& faces)
{
    int i = 0;
    for (i = 0; i < (int)group.faces.size(); ++i)
    {
        if (group.faces[i].face != nullptr)
            faces.push_back(group.faces[i].face);
    }
}

logical WriteFirstSplitWithUpstreamFacesSat(const Step7StitchInput& input)
{
    if (input.split_to_raw_faces.empty())
        return TRUE;

    const Step7SplitRawFaceRecord& split = input.split_to_raw_faces[5];
    std::vector<FACE*> faces;
    if (split.split_face != nullptr)
        faces.push_back(split.split_face);

    const Step7RawFaceRef* raw_face = FindRawFaceRef(input.raw_faces, split.raw_face_id);
    if (raw_face != nullptr)
    {
        AppendGroupFaces(raw_face->pair.group_a, faces);
        AppendGroupFaces(raw_face->pair.group_b, faces);
    }

    if (faces.empty())
        return TRUE;

    return SaveFacesAsSheetBodiesSat(
        faces,
        "output/step7_first_split_with_origin_faces_body_0.sat");
}
} // namespace

logical RunStep7Stitch(
    const Step7StitchInput& input,
    Step7StitchResult& result)
{

    if (WriteStep7InputSplitFacesSat(
            input.split_to_raw_faces,
            "output/step7_input_split_faces_body_0.sat") == FALSE)
    {
        result.ok = FALSE;
        return FALSE;
    }

    // TODO
    (void)WriteFirstSplitWithUpstreamFacesSat(input);

    result.state.input = input;
    result.ok = TRUE;
    return result.ok;
}

logical WriteStep7InputSplitFacesSat(
    const std::vector<Step7SplitRawFaceRecord>& split_to_raw_faces,
    const char* output_sat_path)
{
    if (output_sat_path == nullptr)
        return FALSE;

    std::vector<FACE*> faces;
    int i = 0;
    for (i = 0; i < (int)split_to_raw_faces.size(); ++i)
        faces.push_back(split_to_raw_faces[i].split_face);

    return SaveFacesAsSheetBodiesSat(faces, output_sat_path);
}
} // namespace midsurface_new
