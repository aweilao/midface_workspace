#pragma once

#include "face.hxx"

#include <vector>

namespace midsurface_new
{
struct Step7SplitRawFaceRecord
{
    int split_id = -1;
    int raw_face_id = -1;
    FACE* split_face = nullptr;
};

struct Step7RawFaceRelationRecord
{
    int relation_id = -1;
    int raw_face_a = -1;
    int raw_face_b = -1;
};

struct Step7FaceRef
{
    int face_id = -1;
    FACE* face = nullptr;
};

struct Step7GroupRef
{
    int group_id = -1;
    std::vector<Step7FaceRef> faces;
};

struct Step7PairRef
{
    int pair_id = -1;
    Step7GroupRef group_a;
    Step7GroupRef group_b;
};

struct Step7RawFaceRef
{
    int raw_face_id = -1;
    FACE* raw_face = nullptr;
    Step7PairRef pair;
};

struct Step7StitchInput
{
    std::vector<Step7SplitRawFaceRecord> split_to_raw_faces;
    std::vector<Step7RawFaceRelationRecord> raw_face_relations;
    std::vector<Step7RawFaceRef> raw_faces;
};
} // namespace midsurface_new
