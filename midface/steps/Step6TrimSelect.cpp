#include "steps/Step6TrimSelect.hpp"

#include "utils/ColorUtils.hpp"
#include "utils/JsonUtils.hpp"

#include "boolapi.hxx"
#include "cstrapi.hxx"
#include "entity_color.hxx"
#include "faceqry.hxx"
#include "faceutil.hxx"
#include "geometry.hxx"
#include "getbox.hxx"
#include "intrapi.hxx"
#include "kernapi.hxx"
#include "lop_api.hxx"
#include "lop_opts.hxx"
#include "param.hxx"
#include "surdef.hxx"
#include "surface.hxx"
#include "cone.hxx"
#include "sphere.hxx"
#include "torus.hxx"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace midsurface_new
{
namespace
{
const char* kStep6PreselectSplitFaces = "step6.preselect_split_faces";
const char* kStep6SelectedFaces = "step6.selected_faces";
const char* kStep6ExtendedMidFaces = "step6.extended_mid_faces";
const char* kStep6ExtendedWallFaces = "step6.extended_wall_faces";

std::string IntText(int value)
{
    char buf[64];
    std::sprintf(buf, "%d", value);
    return std::string(buf);
}

std::string DoubleText(double value)
{
    std::ostringstream ss;
    ss << value;
    return ss.str();
}

std::string BoolText(logical value)
{
    return value != FALSE ? "true" : "false";
}

JsonValue ColorRgbJson(const ColorRgb255& color)
{
    JsonValue rgb = JsonValue::array();
    rgb.push_back(color.r);
    rgb.push_back(color.g);
    rgb.push_back(color.b);
    return rgb;
}

std::string PointerText(const void* ptr)
{
    if (ptr == nullptr)
        return "";
    std::ostringstream ss;
    ss << ptr;
    return ss.str();
}

std::string IntListJsonText(const std::vector<int>& values)
{
    JsonValue arr = JsonValue::array();
    int i = 0;
    for (i = 0; i < (int)values.size(); ++i)
        arr.push_back(values[i]);
    return JsonDump(arr);
}

StructuredEvent Step6EventTemplate()
{
    StructuredEvent event;
    event.AddTag("step6");
    event.AddTag("trim-select");
    return event;
}

StructuredEvent Step6DetailEvent()
{
    StructuredEvent event = Step6EventTemplate();
    event.AddTag("detail");
    return event;
}

StructuredEvent Step6StageSummaryEvent()
{
    StructuredEvent event = Step6EventTemplate();
    event.AddTag("summary");
    event.AddTag("stage");
    return event;
}

StructuredEvent Step6SingleSummaryEvent()
{
    StructuredEvent event = Step6EventTemplate();
    event.AddTag("summary");
    event.AddTag("single");
    return event;
}

StructuredEvent Step6AllSummaryEvent()
{
    StructuredEvent event = Step6EventTemplate();
    event.AddTag("summary");
    event.AddTag("all");
    return event;
}

void EmitEvent(DiagnosticSink* diagnostics, const StructuredEvent& event)
{
    if (diagnostics != nullptr)
        (void)diagnostics->EmitEvent(event);
}

void EmitStep7SplitRawFaceRecordEvent(
    DiagnosticSink* diagnostics,
    const Step7SplitRawFaceRecord& record)
{
    StructuredEvent event = Step6SingleSummaryEvent();
    event.AddTag("step7-input");
    event.AddTag("split-raw-face");
    event.SetProperty("split_id", IntText(record.split_id));
    event.SetProperty("raw_face_id", IntText(record.raw_face_id));
    event.SetProperty("has_split_face", BoolText(record.split_face != nullptr ? TRUE : FALSE));
    EmitEvent(diagnostics, event);
}

void EmitStep7RawFaceRelationRecordEvent(
    DiagnosticSink* diagnostics,
    const Step7RawFaceRelationRecord& record)
{
    StructuredEvent event = Step6SingleSummaryEvent();
    event.AddTag("step7-input");
    event.AddTag("raw-face-relation");
    event.SetProperty("relation_id", IntText(record.relation_id));
    event.SetProperty("raw_face_a", IntText(record.raw_face_a));
    event.SetProperty("raw_face_b", IntText(record.raw_face_b));
    EmitEvent(diagnostics, event);
}

void EmitStep7InputStageSummary(DiagnosticSink* diagnostics, const Step7StitchInput& input)
{
    StructuredEvent event = Step6StageSummaryEvent();
    event.AddTag("step7-input");
    event.AddTag("finish");
    event.SetProperty("split_to_raw_face_count", IntText((int)input.split_to_raw_faces.size()));
    event.SetProperty("raw_face_relation_count", IntText((int)input.raw_face_relations.size()));
    event.SetProperty("raw_face_count", IntText((int)input.raw_faces.size()));
    EmitEvent(diagnostics, event);
}

const PairRecord* FindStep7InputPair(const Step3PairState* step3, int pair_id)
{
    if (step3 == nullptr || pair_id < 0)
        return nullptr;
    if (pair_id < (int)step3->pairs.pairs.size() &&
        step3->pairs.pairs[pair_id].pair_id == pair_id)
        return &step3->pairs.pairs[pair_id];

    int i = 0;
    for (i = 0; i < (int)step3->pairs.pairs.size(); ++i)
    {
        if (step3->pairs.pairs[i].pair_id == pair_id)
            return &step3->pairs.pairs[i];
    }
    return nullptr;
}

const GroupRecord* FindStep7InputGroup(const Step2GroupState* step2, int group_id)
{
    if (step2 == nullptr || group_id < 0)
        return nullptr;
    if (group_id < (int)step2->groups.groups.size() &&
        step2->groups.groups[group_id].group_id == group_id)
        return &step2->groups.groups[group_id];

    int i = 0;
    for (i = 0; i < (int)step2->groups.groups.size(); ++i)
    {
        if (step2->groups.groups[i].group_id == group_id)
            return &step2->groups.groups[i];
    }
    return nullptr;
}

const MidPatchRecord* FindStep7InputPatch(
    const Step5MidPatchState& step5,
    int raw_face_id)
{
    if (raw_face_id < 0)
        return nullptr;
    if (raw_face_id < (int)step5.patches.patches.size() &&
        step5.patches.patches[raw_face_id].patch_id == raw_face_id)
        return &step5.patches.patches[raw_face_id];

    int i = 0;
    for (i = 0; i < (int)step5.patches.patches.size(); ++i)
    {
        if (step5.patches.patches[i].patch_id == raw_face_id)
            return &step5.patches.patches[i];
    }
    return nullptr;
}

Step7GroupRef BuildStep7GroupRef(const GroupRecord* group)
{
    Step7GroupRef out;
    if (group == nullptr)
        return out;

    out.group_id = group->group_id;
    const int face_count = (std::max)((int)group->face_ids.size(), (int)group->faces.size());
    int i = 0;
    for (i = 0; i < face_count; ++i)
    {
        Step7FaceRef face;
        face.face_id = i < (int)group->face_ids.size() ? group->face_ids[i] : -1;
        face.face = i < (int)group->faces.size() ? group->faces[i] : nullptr;
        out.faces.push_back(face);
    }
    return out;
}

Step7PairRef BuildStep7PairRef(
    const Step3PairState* step3,
    const Step2GroupState* step2,
    int pair_id)
{
    Step7PairRef out;
    const PairRecord* pair = FindStep7InputPair(step3, pair_id);
    if (pair == nullptr)
        return out;

    out.pair_id = pair->pair_id;
    out.group_a = BuildStep7GroupRef(FindStep7InputGroup(step2, pair->group_a));
    out.group_b = BuildStep7GroupRef(FindStep7InputGroup(step2, pair->group_b));
    return out;
}

Step7RawFaceRef BuildStep7RawFaceRef(
    const Step5MidPatchState& step5,
    int raw_face_id)
{
    Step7RawFaceRef out;
    const MidPatchRecord* patch = FindStep7InputPatch(step5, raw_face_id);
    if (patch == nullptr)
        return out;

    const Step4RelationState* step4 = step5.input_step4;
    const Step3PairState* step3 = step4 == nullptr ? nullptr : step4->input_step3;
    const Step2GroupState* step2 = step3 == nullptr ? nullptr : step3->input_step2;

    out.raw_face_id = patch->patch_id;
    out.raw_face = patch->face;

    int pair_id = patch->source_pair_id;
    if (pair_id < 0 && !patch->member_pairs.empty())
        pair_id = patch->member_pairs[0];
    out.pair = BuildStep7PairRef(step3, step2, pair_id);
    return out;
}

void EmitStep7RawFacePatchEvent(
    DiagnosticSink* diagnostics,
    const MidPatchRecord& patch)
{
    StructuredEvent event = Step6SingleSummaryEvent();
    event.AddTag("step7-input");
    event.AddTag("raw-face-patch");
    event.SetProperty("raw_face_id", IntText(patch.patch_id));
    event.SetProperty("patch_id", IntText(patch.patch_id));
    event.SetProperty("source_pair_id", IntText(patch.source_pair_id));
    event.SetJsonProperty("member_pairs", IntListJsonText(patch.member_pairs));
    event.SetProperty("group_a", IntText(patch.group_a));
    event.SetProperty("group_b", IntText(patch.group_b));
    event.SetProperty("component_id", IntText(patch.component_id));
    event.SetProperty("merged_unit", BoolText(patch.merged_unit));
    event.SetProperty("kind_id", IntText((int)patch.kind));
    event.SetProperty("type_a", patch.type_a);
    event.SetProperty("type_b", patch.type_b);
    event.SetProperty("valid", BoolText(patch.valid));
    event.SetProperty("has_raw_mid_face", BoolText(patch.face != nullptr ? TRUE : FALSE));
    event.SetProperty("raw_mid_face_ptr", PointerText(static_cast<const void*>(patch.face)));
    EmitEvent(diagnostics, event);
}

void EmitStep7RawFacePairEvent(
    DiagnosticSink* diagnostics,
    int raw_face_id,
    const PairRecord& pair)
{
    StructuredEvent event = Step6SingleSummaryEvent();
    event.AddTag("step7-input");
    event.AddTag("raw-face-pair");
    event.SetProperty("raw_face_id", IntText(raw_face_id));
    event.SetProperty("pair_id", IntText(pair.pair_id));
    event.SetProperty("group_a", IntText(pair.group_a));
    event.SetProperty("group_b", IntText(pair.group_b));
    event.SetProperty("thickness", DoubleText(pair.thickness));
    event.SetProperty("coverage", DoubleText(pair.coverage));
    event.SetProperty("score", DoubleText(pair.score));
    event.SetProperty("facing_score", DoubleText(pair.facing_score));
    event.SetProperty("pass_ratio", DoubleText(pair.pass_ratio));
    event.SetProperty("pass_balance", DoubleText(pair.pass_balance));
    event.SetProperty("uncertain", BoolText(pair.uncertain));
    event.SetProperty("rib_candidate", BoolText(pair.rib_candidate));
    event.SetProperty("reason", pair.reason);
    EmitEvent(diagnostics, event);
}

void EmitStep7RawFaceGroupEvent(
    DiagnosticSink* diagnostics,
    int raw_face_id,
    int pair_id,
    const char* pair_side,
    const GroupRecord& group)
{
    StructuredEvent event = Step6SingleSummaryEvent();
    event.AddTag("step7-input");
    event.AddTag("raw-face-group");
    event.SetProperty("raw_face_id", IntText(raw_face_id));
    event.SetProperty("pair_id", IntText(pair_id));
    event.SetProperty("pair_side", pair_side == nullptr ? "" : pair_side);
    event.SetProperty("group_id", IntText(group.group_id));
    event.SetProperty("group_type", group.type);
    event.SetProperty("face_count", IntText((int)group.face_ids.size()));
    event.SetJsonProperty("face_ids", IntListJsonText(group.face_ids));
    event.SetProperty("area_sum", DoubleText(group.area_sum));
    event.SetProperty("local_scale", DoubleText(group.local_scale));
    event.SetProperty("confidence", DoubleText(group.confidence));
    event.SetProperty("source_rules", group.source_rules);
    EmitEvent(diagnostics, event);
}

void EmitStep7RawFaceGroupFaceEvent(
    DiagnosticSink* diagnostics,
    int raw_face_id,
    int pair_id,
    const char* pair_side,
    const GroupRecord& group,
    int face_index,
    int face_id,
    FACE* face)
{
    StructuredEvent event = Step6SingleSummaryEvent();
    event.AddTag("step7-input");
    event.AddTag("raw-face-group-face");
    event.SetProperty("raw_face_id", IntText(raw_face_id));
    event.SetProperty("pair_id", IntText(pair_id));
    event.SetProperty("pair_side", pair_side == nullptr ? "" : pair_side);
    event.SetProperty("group_id", IntText(group.group_id));
    event.SetProperty("face_index", IntText(face_index));
    event.SetProperty("face_id", IntText(face_id));
    event.SetProperty("has_face", BoolText(face != nullptr ? TRUE : FALSE));
    event.SetProperty("face_ptr", PointerText(static_cast<const void*>(face)));
    EmitEvent(diagnostics, event);
}

void EmitStep7RawFaceGroupWithFaces(
    DiagnosticSink* diagnostics,
    int raw_face_id,
    int pair_id,
    const char* pair_side,
    const GroupRecord* group)
{
    if (group == nullptr)
        return;

    EmitStep7RawFaceGroupEvent(diagnostics, raw_face_id, pair_id, pair_side, *group);

    const int face_count = (std::max)((int)group->face_ids.size(), (int)group->faces.size());
    int i = 0;
    for (i = 0; i < face_count; ++i)
    {
        const int face_id = i < (int)group->face_ids.size() ? group->face_ids[i] : -1;
        FACE* face = i < (int)group->faces.size() ? group->faces[i] : nullptr;
        EmitStep7RawFaceGroupFaceEvent(
            diagnostics,
            raw_face_id,
            pair_id,
            pair_side,
            *group,
            i,
            face_id,
            face);
    }
}

void EmitStep7RawFaceUpstreamEvents(
    DiagnosticSink* diagnostics,
    const Step5MidPatchState& step5,
    int raw_face_id)
{
    const MidPatchRecord* patch = FindStep7InputPatch(step5, raw_face_id);
    if (patch == nullptr)
        return;

    const Step4RelationState* step4 = step5.input_step4;
    const Step3PairState* step3 = step4 == nullptr ? nullptr : step4->input_step3;
    const Step2GroupState* step2 = step3 == nullptr ? nullptr : step3->input_step2;

    EmitStep7RawFacePatchEvent(diagnostics, *patch);

    std::vector<int> pair_ids = patch->member_pairs;
    if (pair_ids.empty() && patch->source_pair_id >= 0)
        pair_ids.push_back(patch->source_pair_id);

    int i = 0;
    for (i = 0; i < (int)pair_ids.size(); ++i)
    {
        const PairRecord* pair = FindStep7InputPair(step3, pair_ids[i]);
        if (pair == nullptr)
            continue;

        EmitStep7RawFacePairEvent(diagnostics, raw_face_id, *pair);
        EmitStep7RawFaceGroupWithFaces(
            diagnostics,
            raw_face_id,
            pair->pair_id,
            "a",
            FindStep7InputGroup(step2, pair->group_a));
        EmitStep7RawFaceGroupWithFaces(
            diagnostics,
            raw_face_id,
            pair->pair_id,
            "b",
            FindStep7InputGroup(step2, pair->group_b));
    }
}

void ColorSelectedFacesAndEmitMap(
    DiagnosticSink* diagnostics,
    const TrimSelectionTable& selections)
{
    const int count = (int)selections.selections.size();
    const std::vector<ColorRgb255> palette = BuildDistinctColorPalette(count);

    std::vector<std::string> tags;
    tags.push_back("step6");
    tags.push_back("trim-select");
    tags.push_back("selection");

    int i = 0;
    for (i = 0; i < count; ++i)
    {
        const TrimSelectionRecord& record = selections.selections[i];
        const ColorRgb255 color = i < (int)palette.size()
            ? palette[i]
            : DistinctColorByIndex(i, count);

        if (record.selected_face != nullptr)
            (void)ApplyFaceColor(record.selected_face, color);

        std::map<std::string, std::string> properties;
        std::map<std::string, std::string> json_properties;
        properties["selection_id"] = IntText(record.selection_id);
        properties["selected_body_index"] = IntText(record.selection_id);
        properties["source_patch_id"] = IntText(record.source_patch_id);
        properties["source_pair_id"] = IntText(record.source_pair_id);
        properties["source_seed_rep_pair"] = IntText(record.source_seed_rep_pair);
        properties["source_split_index"] = IntText(record.source_split_index);
        properties["area"] = DoubleText(record.area);
        properties["reason"] = record.reason;
        properties["has_selected_face"] = BoolText(record.selected_face != nullptr ? TRUE : FALSE);
        json_properties["rgb"] = JsonDump(ColorRgbJson(color));

        if (diagnostics != nullptr)
        {
            (void)diagnostics->EmitColorIdMapEntryIfEnabled(
                tags,
                kStep6SelectedFaces,
                "selection_id",
                properties,
                json_properties);
        }
    }
}

void EmitStartEvent(DiagnosticSink* diagnostics, int patch_count, int pair_count)
{
    StructuredEvent event = Step6EventTemplate();
    event.AddTag("start");
    event.SetProperty("input_patch_count", IntText(patch_count));
    event.SetProperty("input_pair_count", IntText(pair_count));
    EmitEvent(diagnostics, event);
}

void EmitStageSummary(
    DiagnosticSink* diagnostics,
    const char* stage,
    const std::map<std::string, std::string>& props,
    const char* substage = nullptr)
{
    StructuredEvent event = Step6StageSummaryEvent();
    event.AddTag(stage);
    if (substage != nullptr)
        event.AddTag(substage);
    event.AddTag("finish");
    std::map<std::string, std::string>::const_iterator it = props.begin();
    for (; it != props.end(); ++it)
        event.SetProperty(it->first, it->second);
    EmitEvent(diagnostics, event);
}

void EmitFinishEvent(DiagnosticSink* diagnostics, const TrimSelectStats& stats, logical ok)
{
    StructuredEvent event = Step6AllSummaryEvent();
    event.AddTag("finish");
    event.SetProperty("ok", BoolText(ok));
    event.SetProperty("input_patch_count", IntText(stats.input_patch_count));
    event.SetProperty("input_pair_count", IntText(stats.input_pair_count));
    event.SetProperty("mid_seed_try_count", IntText(stats.mid_seed_try_count));
    event.SetProperty("mid_seed_ok_count", IntText(stats.mid_seed_ok_count));
    event.SetProperty("wall_seed_try_count", IntText(stats.wall_seed_try_count));
    event.SetProperty("wall_seed_ok_count", IntText(stats.wall_seed_ok_count));
    event.SetProperty("virtual_wall_seed_count", IntText(stats.virtual_wall_seed_count));
    event.SetProperty("mm1_bidirectional_edge_count", IntText(stats.mm1_bidirectional_edge_count));
    event.SetProperty("mm1_directional_edge_count", IntText(stats.mm1_directional_edge_count));
    event.SetProperty("mm2_skipped_count", IntText(stats.mm2_skipped_count));
    event.SetProperty("trim_tool_try_count", IntText(stats.trim_tool_try_count));
    event.SetProperty("trim_tool_ok_count", IntText(stats.trim_tool_ok_count));
    event.SetProperty("preselect_split_face_count", IntText(stats.preselect_split_face_count));
    event.SetProperty("selected_count", IntText(stats.selected_count));
    event.SetProperty("selected_face_count", IntText(stats.selected_count));
    event.SetProperty("rejected_count", IntText(stats.rejected_count));
    event.SetProperty("selected_copy_fail_count", IntText(stats.selected_copy_fail_count));
    event.SetProperty("selected_dedup_skip_count", IntText(stats.selected_dedup_skip_count));
    event.SetProperty("slice_adjacency_count", IntText(stats.slice_adjacency_count));
    EmitEvent(diagnostics, event);
}

double CpuSecondsNowLocal()
{
    return (double)std::clock() / (double)CLOCKS_PER_SEC;
}

double FaceAreaProxyFromBox(FACE* face)
{
    if (face == nullptr)
        return 0.0;
    const SPAbox b = get_face_box(face);
    double dx = std::fabs(b.high().x() - b.low().x());
    double dy = std::fabs(b.high().y() - b.low().y());
    double dz = std::fabs(b.high().z() - b.low().z());
    double dims[3];
    dims[0] = dx;
    dims[1] = dy;
    dims[2] = dz;
    std::sort(dims, dims + 3);
    return dims[1] * dims[2];
}

double FaceAreaEstimate(FACE* face)
{
    if (face == nullptr)
        return 0.0;
    double area = 0.0;
    double acc = 0.0;
    outcome ra = api_ent_area((ENTITY*)face, 1e-3, area, acc, nullptr);
    if (ra.ok() && area > 0.0)
        return area;
    return FaceAreaProxyFromBox(face);
}

logical CopyFaceDetached(FACE* src_face, FACE*& out_face)
{
    out_face = nullptr;
    if (src_face == nullptr)
        return FALSE;

    ENTITY* copied = nullptr;
    outcome rc = api_copy_entity((ENTITY*)src_face, copied);
    if (!rc.ok() || copied == nullptr)
        return FALSE;

    if (is_FACE(copied))
    {
        out_face = (FACE*)copied;
        return out_face != nullptr ? TRUE : FALSE;
    }

    ENTITY_LIST fl;
    outcome rf = api_get_faces(copied, fl, PAT_CAN_CREATE);
    if (!rf.ok() || fl.count() <= 0)
        return FALSE;
    fl.init();
    ENTITY* e = fl.next();
    if (e == nullptr || !is_FACE(e))
        return FALSE;
    out_face = (FACE*)e;
    return out_face != nullptr ? TRUE : FALSE;
}

logical CopyBodyEntity(BODY* src, BODY*& out_body)
{
    out_body = nullptr;
    if (src == nullptr)
        return FALSE;
    ENTITY* copied = nullptr;
    outcome rc = api_copy_entity((ENTITY*)src, copied);
    if (!rc.ok() || copied == nullptr || !is_BODY(copied))
        return FALSE;
    out_body = (BODY*)copied;
    return TRUE;
}

logical CollectFacesFromBodyLocal(BODY* body, std::vector<FACE*>& out_faces)
{
    out_faces.clear();
    if (body == nullptr)
        return FALSE;
    ENTITY_LIST fl;
    outcome r = api_get_faces(body, fl, PAT_CAN_CREATE);
    if (!r.ok() || fl.count() <= 0)
        return FALSE;
    fl.init();
    ENTITY* e = nullptr;
    while ((e = fl.next()) != nullptr)
    {
        if (is_FACE(e))
            out_faces.push_back((FACE*)e);
    }
    return out_faces.empty() ? FALSE : TRUE;
}

logical BuildRawSheetBodyFromFace(FACE* src_face, BODY*& out_body)
{
    out_body = nullptr;
    if (src_face == nullptr)
        return FALSE;

    FACE* detached = nullptr;
    FACE* one[1];
    if (CopyFaceDetached(src_face, detached) && detached != nullptr)
        one[0] = detached;
    else
        one[0] = src_face;

    BODY* b = nullptr;
    outcome rb = api_sheet_from_ff(1, one, b);
    if (!rb.ok() || b == nullptr)
        return FALSE;
    out_body = b;
    return TRUE;
}

logical BuildPlaneFaceByCoedgeUvBox(
    FACE* src_face,
    double uv_expand_scale,
    int sample_count,
    FACE*& out_face)
{
    out_face = nullptr;
    if (src_face == nullptr)
        return FALSE;
    if (get_face_type(src_face) != face_plane)
        return FALSE;

    std::vector<SPAposition> samples;
    LOOP* lp = src_face->loop();
    while (lp != nullptr)
    {
        COEDGE* start = lp->start();
        COEDGE* c = start;
        if (c != nullptr)
        {
            do
            {
                const double ts = coedge_start_param(c);
                const double te = coedge_end_param(c);
                const double tm = 0.5 * (ts + te);
                samples.push_back(coedge_param_pos(c, ts));
                samples.push_back(coedge_param_pos(c, tm));
                samples.push_back(coedge_param_pos(c, te));
                c = c->next();
            } while (c != nullptr && c != start);
        }
        lp = lp->next();
    }
    if (samples.empty())
        return FALSE;

    SURFACE* sg = src_face->geometry();
    if (sg == nullptr)
        return FALSE;
    const surface& sf = sg->equation();

    double umin = 1e100, umax = -1e100, vmin = 1e100, vmax = -1e100;
    int i = 0;
    for (i = 0; i < (int)samples.size(); ++i)
    {
        const SPApar_pos uv = sf.param(samples[i]);
        const double u = (double)uv.u;
        const double v = (double)uv.v;
        if (u < umin) umin = u;
        if (u > umax) umax = u;
        if (v < vmin) vmin = v;
        if (v > vmax) vmax = v;
    }
    if (umax <= umin || vmax <= vmin)
        return FALSE;

    const double scale = (std::max)(1.0, uv_expand_scale);
    const double new_umin = umin - (umax - umin) * 0.5 * (scale - 1.0);
    const double new_umax = umax + (umax - umin) * 0.5 * (scale - 1.0);
    const double new_vmin = vmin - (vmax - vmin) * 0.5 * (scale - 1.0);
    const double new_vmax = vmax + (vmax - vmin) * 0.5 * (scale - 1.0);

    SPAposition p11, p12, p21;
    sf.eval(SPApar_pos(new_umin, new_vmin), p11);
    sf.eval(SPApar_pos(new_umin, new_vmax), p12);
    sf.eval(SPApar_pos(new_umax, new_vmin), p21);

    FACE* f = nullptr;
    outcome rp = api_make_plface(p11, p12, p21, f);
    if (!rp.ok() || f == nullptr)
        return FALSE;
    out_face = f;
    (void)sample_count;
    return TRUE;
}

double ClampScalar(double value, double lo, double hi)
{
    if (value < lo)
        return lo;
    if (value > hi)
        return hi;
    return value;
}

SPAposition ProjectPointToAxisLine(
    const SPAposition& axis_origin,
    const SPAvector& axis_dir_unit,
    const SPAposition& point)
{
    const SPAvector offset = point - axis_origin;
    const double t = offset % axis_dir_unit;
    return SPAposition(
        axis_origin.x() + axis_dir_unit.x() * t,
        axis_origin.y() + axis_dir_unit.y() * t,
        axis_origin.z() + axis_dir_unit.z() * t);
}

logical BuildCylinderFaceByCoedgeUvBox(
    FACE* src_face,
    double uv_expand_scale,
    int sample_count,
    FACE*& out_face)
{
    out_face = nullptr;
    if (src_face == nullptr || get_face_type(src_face) != face_cylinder)
        return FALSE;

    SURFACE* sg = src_face->geometry();
    if (sg == nullptr || sg->identity() != CONE_TYPE)
        return FALSE;

    CONE* cyl = (CONE*)sg;
    const surface& sf = sg->equation();

    SPApar_box face_pb;
    if (!sg_get_face_par_box(src_face, face_pb))
        face_pb = sf.param_range();

    const SPAinterval ur = face_pb.u_range();
    const SPAinterval vr = face_pb.v_range();
    double umin = ur.start_pt();
    double umax = ur.end_pt();
    double vmin = vr.start_pt();
    double vmax = vr.end_pt();
    if (umax <= umin)
        return FALSE;

    const double scale = (std::max)(1.0, uv_expand_scale);
    const double u_span = umax - umin;
    const double u_delta = u_span * 0.5 * (scale - 1.0);
    umin -= u_delta;
    umax += u_delta;

    const double v_period = (std::fabs(sf.param_period_v()) > 1e-12)
        ? std::fabs(sf.param_period_v())
        : 2.0 * std::acos(-1.0);
    double v_span = vmax - vmin;
    if (v_span <= 0.0 && v_period > 1e-12)
        v_span += v_period;
    if (v_span <= 1e-12)
        return FALSE;

    const double v_delta = v_span * 0.5 * (scale - 1.0);
    const double vmin_exp = vmin - v_delta;
    const double vmax_exp = vmax + v_delta;
    if ((vmax_exp - vmin_exp) < v_period * 0.95)
    {
        vmin = vmin_exp;
        vmax = vmax_exp;
    }

    const double vmid = 0.5 * (vmin + vmax);
    SPAposition pmin;
    SPAposition pmax;
    sf.eval(SPApar_pos(umin, vmid), pmin);
    sf.eval(SPApar_pos(umax, vmid), pmax);

    const SPAvector axis_dir(cyl->direction().x(), cyl->direction().y(), cyl->direction().z());
    const double axis_len = axis_dir.len();
    if (axis_len <= 1e-12)
        return FALSE;
    const SPAvector axis_dir_unit(axis_dir.x() / axis_len, axis_dir.y() / axis_len, axis_dir.z() / axis_len);
    const SPAposition root = cyl->root_point();
    const SPAposition base_center = ProjectPointToAxisLine(root, axis_dir_unit, pmin);
    const SPAposition top_center = ProjectPointToAxisLine(root, axis_dir_unit, pmax);
    const double height = (top_center - base_center) % axis_dir_unit;
    if (std::fabs(height) <= 1e-12)
        return FALSE;

    const SPAvector major_axis = cyl->major_axis();
    if (major_axis.len() <= 1e-12)
        return FALSE;

    FACE* f = nullptr;
    outcome r = api_make_cnface(
        base_center,
        cyl->direction(),
        major_axis,
        cyl->radius_ratio(),
        cyl->sine_angle(),
        cyl->cosine_angle(),
        vmin,
        vmax,
        height,
        f);
    if (!r.ok() || f == nullptr)
        return FALSE;
    out_face = f;
    (void)sample_count;
    return TRUE;
}

logical BuildSphereFaceByCoedgeUvBox(
    FACE* src_face,
    double uv_expand_scale,
    int sample_count,
    FACE*& out_face)
{
    out_face = nullptr;
    if (src_face == nullptr || get_face_type(src_face) != face_sphere)
        return FALSE;

    SURFACE* sg = src_face->geometry();
    if (sg == nullptr || sg->identity() != SPHERE_TYPE)
        return FALSE;

    SPHERE* sph = (SPHERE*)sg;
    const surface& sf = sg->equation();
    const sphere& sph_eq = (const sphere&)sph->equation();

    SPApar_box face_pb;
    if (!sg_get_face_par_box(src_face, face_pb))
        face_pb = sf.param_range();

    const SPAinterval ur = face_pb.u_range();
    const SPAinterval vr = face_pb.v_range();
    double umin = ur.start_pt();
    double umax = ur.end_pt();
    double vmin = vr.start_pt();
    double vmax = vr.end_pt();
    if (umax <= umin)
        return FALSE;

    const double scale = (std::max)(1.0, uv_expand_scale);
    const double u_span = umax - umin;
    const double u_delta = u_span * 0.5 * (scale - 1.0);
    umin = ClampScalar(umin - u_delta, ur.start_pt(), ur.end_pt());
    umax = ClampScalar(umax + u_delta, ur.start_pt(), ur.end_pt());
    if (umax <= umin)
        return FALSE;

    const double v_period = (std::fabs(sf.param_period_v()) > 1e-12)
        ? std::fabs(sf.param_period_v())
        : 2.0 * std::acos(-1.0);
    double v_span = vmax - vmin;
    if (v_span <= 0.0 && v_period > 1e-12)
        v_span += v_period;
    if (v_span <= 1e-12)
        return FALSE;

    const double v_delta = v_span * 0.5 * (scale - 1.0);
    const double vmin_exp = vmin - v_delta;
    const double vmax_exp = vmax + v_delta;
    if ((vmax_exp - vmin_exp) < v_period * 0.95)
    {
        vmin = vmin_exp;
        vmax = vmax_exp;
    }

    SPAunit_vector lat_dir = sph_eq.uv_oridir;
    SPAunit_vector lon_dir = sph_eq.pole_dir;
    double slon = vmin;
    double elon = vmax;
    if (sph_eq.reverse_v)
    {
        const double old_slon = slon;
        const double old_elon = elon;
        slon = -old_elon;
        elon = -old_slon;
    }

    FACE* f = nullptr;
    outcome r = api_make_spface(
        sph->centre(),
        std::fabs(sph->radius()),
        lat_dir,
        lon_dir,
        umin,
        umax,
        slon,
        elon,
        f);
    if (!r.ok() || f == nullptr)
        return FALSE;
    out_face = f;
    (void)sample_count;
    return TRUE;
}

logical ExpandPeriodicRangeLocal(
    double& io_min,
    double& io_max,
    double expand_scale,
    double period)
{
    if (period <= 1.0e-12)
        return FALSE;

    double span = io_max - io_min;
    if (span <= 0.0)
        span += period;
    if (span <= 1.0e-12)
        return FALSE;
    if (span > period)
        span = period;

    const double scale = (std::max)(1.0, expand_scale);
    const double delta = span * 0.5 * (scale - 1.0);
    const double next_min = io_min - delta;
    const double next_max = io_min + span + delta;
    if ((next_max - next_min) < period * 0.95)
    {
        io_min = next_min;
        io_max = next_max;
    }
    else
    {
        io_max = io_min + span;
    }
    return TRUE;
}

logical BuildTorusFaceByCoedgeUvBox(
    FACE* src_face,
    double uv_expand_scale,
    int sample_count,
    FACE*& out_face)
{
    out_face = nullptr;
    if (src_face == nullptr || get_face_type(src_face) != face_torus)
        return FALSE;

    SURFACE* sg = src_face->geometry();
    if (sg == nullptr || sg->identity() != TORUS_TYPE)
        return FALSE;

    TORUS* tor = (TORUS*)sg;
    const surface& sf = sg->equation();

    SPApar_box face_pb;
    if (!sg_get_face_par_box(src_face, face_pb))
        face_pb = sf.param_range();

    const SPAinterval ur = face_pb.u_range();
    const SPAinterval vr = face_pb.v_range();
    double umin = ur.start_pt();
    double umax = ur.end_pt();
    double vmin = vr.start_pt();
    double vmax = vr.end_pt();

    const double period = 2.0 * std::acos(-1.0);
    if (!ExpandPeriodicRangeLocal(umin, umax, uv_expand_scale, period))
        return FALSE;
    if (!ExpandPeriodicRangeLocal(vmin, vmax, uv_expand_scale, period))
        return FALSE;

    SPAposition pnt;
    try
    {
        sf.eval(SPApar_pos(0.0, 0.0), pnt);
    }
    catch (...)
    {
        return FALSE;
    }

    FACE* f = nullptr;
    outcome r = api_make_trface(
        tor->centre(),
        tor->normal(),
        tor->major_radius(),
        tor->minor_radius(),
        pnt,
        umin,
        umax,
        vmin,
        vmax,
        f);
    if (!r.ok() || f == nullptr)
        return FALSE;
    out_face = f;
    (void)sample_count;
    return TRUE;
}

logical HasStep6CustomUvExpand(face_type face_t)
{
    return (face_t == face_plane ||
            face_t == face_cylinder ||
            face_t == face_sphere ||
            face_t == face_torus) ? TRUE : FALSE;
}

logical BuildFaceSheetBodyByApiExtendFailFallback(
    FACE* src_face,
    logical use_uv_expand,
    double uv_expand_scale,
    double cylinder_uv_expand_scale,
    double sphere_uv_expand_scale,
    double torus_uv_expand_scale,
    int sample_count,
    BODY*& out_body)
{
    out_body = nullptr;
    if (src_face == nullptr)
        return FALSE;

    if (use_uv_expand != FALSE && HasStep6CustomUvExpand(get_face_type(src_face)) != FALSE)
    {
        FACE* f_uv = nullptr;
        logical uv_ok = FALSE;
        const face_type face_t = get_face_type(src_face);
        if (face_t == face_plane)
            uv_ok = BuildPlaneFaceByCoedgeUvBox(src_face, uv_expand_scale, sample_count, f_uv);
        else if (face_t == face_cylinder)
            uv_ok = BuildCylinderFaceByCoedgeUvBox(src_face, cylinder_uv_expand_scale, sample_count, f_uv);
        else if (face_t == face_sphere)
            uv_ok = BuildSphereFaceByCoedgeUvBox(src_face, sphere_uv_expand_scale, sample_count, f_uv);
        else if (face_t == face_torus)
            uv_ok = BuildTorusFaceByCoedgeUvBox(src_face, torus_uv_expand_scale, sample_count, f_uv);

        if (uv_ok != FALSE && f_uv != nullptr)
        {
            FACE* one_uv[1];
            one_uv[0] = f_uv;
            BODY* b_uv = nullptr;
            outcome rb_uv = api_sheet_from_ff(1, one_uv, b_uv);
            if (rb_uv.ok() && b_uv != nullptr)
            {
                out_body = b_uv;
                return TRUE;
            }
        }
    }

    return BuildRawSheetBodyFromFace(src_face, out_body);
}

logical ExtendSheetBodyByDistanceDetailed(
    BODY* body,
    double ext_dist,
    double& out_t_get_edges,
    double& out_t_extend_api,
    int& out_edge_count,
    logical& out_api_ok)
{
    out_t_get_edges = 0.0;
    out_t_extend_api = 0.0;
    out_edge_count = 0;
    out_api_ok = FALSE;
    if (body == nullptr)
        return FALSE;
    if (ext_dist <= 0.0)
    {
        out_api_ok = TRUE;
        return TRUE;
    }

    const double t_edges0 = CpuSecondsNowLocal();
    ENTITY_LIST edges;
    outcome re = api_get_edges(body, edges, PAT_CAN_CREATE);
    const double t_edges1 = CpuSecondsNowLocal();
    out_t_get_edges = t_edges1 - t_edges0;
    out_edge_count = edges.count();
    if (!re.ok() || out_edge_count <= 0)
        return FALSE;

    const double ext_use = (std::max)(1e-4, ext_dist);
    const SPAbox b = get_body_box(body);
    const double pad = (std::max)(1e-3, 10.0 * ext_use);
    SPAposition lo(
        b.low().x() - pad,
        b.low().y() - pad,
        b.low().z() - pad);
    SPAposition hi(
        b.high().x() + pad,
        b.high().y() + pad,
        b.high().z() + pad);
    lop_options lopts;

    try
    {
        const double t_ext0 = CpuSecondsNowLocal();
        outcome rx = api_extend_sheetbody(edges, ext_use, lo, hi, &lopts);
        const double t_ext1 = CpuSecondsNowLocal();
        out_t_extend_api = t_ext1 - t_ext0;
        out_api_ok = rx.ok() ? TRUE : FALSE;
        return out_api_ok;
    }
    catch (...)
    {
        out_api_ok = FALSE;
        return FALSE;
    }
}

logical BuildSeedBodyFromFace(
    FACE* src_face,
    double extend_dist,
    const Step6TrimSelectOptions& options,
    double fallback_scale,
    logical do_extend,
    BODY*& out_body,
    double& out_t_copy,
    double& out_t_sheet,
    double& out_t_get_edges,
    double& out_t_extend_api,
    int& out_edge_count,
    logical& out_detach_used,
    logical& out_extend_ok)
{
    out_body = nullptr;
    out_t_copy = 0.0;
    out_t_sheet = 0.0;
    out_t_get_edges = 0.0;
    out_t_extend_api = 0.0;
    out_edge_count = 0;
    out_detach_used = FALSE;
    out_extend_ok = FALSE;
    if (src_face == nullptr)
        return FALSE;

    FACE* detached = nullptr;
    FACE* one[1];
    const double t_copy0 = CpuSecondsNowLocal();
    const logical copied_ok = CopyFaceDetached(src_face, detached) && detached != nullptr;
    const double t_copy1 = CpuSecondsNowLocal();
    out_t_copy = t_copy1 - t_copy0;
    if (copied_ok)
    {
        one[0] = detached;
        out_detach_used = TRUE;
    }
    else
    {
        one[0] = src_face;
    }

    BODY* b = nullptr;
    const double t_sheet0 = CpuSecondsNowLocal();
    outcome rb = api_sheet_from_ff(1, one, b);
    const double t_sheet1 = CpuSecondsNowLocal();
    out_t_sheet = t_sheet1 - t_sheet0;
    if (!rb.ok() || b == nullptr)
        return FALSE;

    const face_type seed_face_type = get_face_type(src_face);
    const logical has_custom_uv_expand =
        (options.api_extend_fail_fallback_use_uv_expand != FALSE &&
         HasStep6CustomUvExpand(seed_face_type) != FALSE) ? TRUE : FALSE;

    if (do_extend != FALSE &&
        (options.use_api_extend_fail_fallback_only == FALSE || has_custom_uv_expand == FALSE))
        (void)ExtendSheetBodyByDistanceDetailed(
            b, extend_dist, out_t_get_edges, out_t_extend_api, out_edge_count, out_extend_ok);

    if (do_extend != FALSE &&
        has_custom_uv_expand != FALSE &&
        (options.use_api_extend_fail_fallback_only != FALSE || out_extend_ok == FALSE))
    {
        BODY* fb = nullptr;
        if (BuildFaceSheetBodyByApiExtendFailFallback(
                src_face,
                options.api_extend_fail_fallback_use_uv_expand,
                fallback_scale,
                options.cylinder_uv_expand_scale,
                options.sphere_uv_expand_scale,
                options.torus_uv_expand_scale,
                options.extend_fail_fallback_sample_count,
                fb) &&
            fb != nullptr)
        {
            if (b != nullptr && b != fb)
                (void)api_del_entity((ENTITY*)b);
            b = fb;
            out_extend_ok = TRUE;
        }
        else if (options.use_api_extend_fail_fallback_only != FALSE)
        {
            out_extend_ok = FALSE;
        }
    }
    else if (do_extend == FALSE)
    {
        out_extend_ok = TRUE;
    }

    (void)fallback_scale;
    out_body = b;
    return TRUE;
}

logical ImprintToolOnBlankBody(BODY* tool_body, BODY* blank_body, int& out_edge_count)
{
    out_edge_count = 0;
    if (tool_body == nullptr || blank_body == nullptr)
        return FALSE;

    try
    {
        ENTITY_LIST tf;
        ENTITY_LIST bf;
        outcome rt = api_get_faces(tool_body, tf, PAT_CAN_CREATE);
        outcome rb = api_get_faces(blank_body, bf, PAT_CAN_CREATE);
        if (!rt.ok() || !rb.ok() || tf.count() <= 0 || bf.count() <= 0)
            return FALSE;

        ENTITY_LIST edges;
        outcome ri = api_selectively_imprint(tool_body, tf, blank_body, bf, FALSE, edges);
        if (!ri.ok())
            return FALSE;
        out_edge_count = edges.count();
        return TRUE;
    }
    catch (...)
    {
        return FALSE;
    }
}

logical ImprintToolSeedCopyOnBlankBody(
    BODY* tool_seed_body,
    BODY* blank_body,
    int& out_edge_count,
    logical& out_copy_ok)
{
    out_edge_count = 0;
    out_copy_ok = FALSE;
    if (tool_seed_body == nullptr || blank_body == nullptr)
        return FALSE;

    BODY* tool_copy = nullptr;
    if (!CopyBodyEntity(tool_seed_body, tool_copy) || tool_copy == nullptr)
        return FALSE;
    out_copy_ok = TRUE;

    const logical ok = ImprintToolOnBlankBody(tool_copy, blank_body, out_edge_count);
    (void)api_del_entity((ENTITY*)tool_copy);
    return ok;
}

void AddUniqueInt(std::vector<int>& arr, int v)
{
    int i = 0;
    for (i = 0; i < (int)arr.size(); ++i)
    {
        if (arr[i] == v)
            return;
    }
    arr.push_back(v);
}

logical GroupFacesById(const Step2GroupState& step2, int group_id, std::vector<FACE*>& out_faces)
{
    out_faces.clear();
    int i = 0;
    for (i = 0; i < (int)step2.groups.groups.size(); ++i)
    {
        const GroupRecord& group = step2.groups.groups[i];
        if (group.group_id == group_id)
        {
            out_faces = group.faces;
            return out_faces.empty() ? FALSE : TRUE;
        }
    }
    return FALSE;
}

double VecDot(const SPAvector& a, const SPAvector& b)
{
    return a.x() * b.x() + a.y() * b.y() + a.z() * b.z();
}

double VecLen(const SPAvector& v)
{
    const double d = VecDot(v, v);
    return d <= 0.0 ? 0.0 : std::sqrt(d);
}

double PosDist(const SPAposition& a, const SPAposition& b)
{
    return VecLen(a - b);
}

logical FindClosestPointOnFaceSet(
    const std::vector<FACE*>& faces,
    const SPAposition& query,
    SPAposition& out_cp,
    double& out_dist,
    FACE*& out_face)
{
    out_dist = 0.0;
    out_face = nullptr;
    if (faces.empty())
        return FALSE;

    logical found = FALSE;
    double best_d = 0.0;
    SPAposition best_p(0.0, 0.0, 0.0);
    FACE* best_f = nullptr;
    int i = 0;
    for (i = 0; i < (int)faces.size(); ++i)
    {
        FACE* f = faces[i];
        if (f == nullptr)
            continue;
        SPAposition cp;
        outcome r = api_find_cls_ptto_face(query, f, cp);
        if (!r.ok())
            continue;
        const double d = PosDist(query, cp);
        if (!found || d < best_d)
        {
            found = TRUE;
            best_d = d;
            best_p = cp;
            best_f = f;
        }
    }

    if (!found)
        return FALSE;
    out_cp = best_p;
    out_dist = best_d;
    out_face = best_f;
    return TRUE;
}

logical BuildFaceSeedPoint(FACE* face, SPAposition& out_p)
{
    out_p = SPAposition(0.0, 0.0, 0.0);
    if (face == nullptr)
        return FALSE;
    const SPAbox b = get_face_box(face);
    SPAposition c(
        0.5 * (b.low().x() + b.high().x()),
        0.5 * (b.low().y() + b.high().y()),
        0.5 * (b.low().z() + b.high().z()));
    SPAposition p;
    outcome r = api_find_cls_ptto_face(c, face, p);
    out_p = r.ok() ? p : c;
    return TRUE;
}

void CollectFaceSamplesByBboxProjection(
    FACE* face,
    int target,
    std::vector<SPAposition>& out_samples)
{
    out_samples.clear();
    if (face == nullptr || target <= 0)
        return;

    SPAposition c;
    if (BuildFaceSeedPoint(face, c))
        out_samples.push_back(c);

    const SPAbox b = get_face_box(face);
    const int max_iter = (std::max)(target * 4, 32);
    int i = 0;
    for (i = 0; i < max_iter && (int)out_samples.size() < target; ++i)
    {
        const int h = i + 1;
        double f2 = 1.0, r2 = 0.0; int x2 = h;
        while (x2 > 0) { f2 /= 2.0; r2 += f2 * (double)(x2 % 2); x2 /= 2; }
        double f3 = 1.0, r3 = 0.0; int x3 = h;
        while (x3 > 0) { f3 /= 3.0; r3 += f3 * (double)(x3 % 3); x3 /= 3; }
        double f5 = 1.0, r5 = 0.0; int x5 = h;
        while (x5 > 0) { f5 /= 5.0; r5 += f5 * (double)(x5 % 5); x5 /= 5; }

        SPAposition guess(
            b.low().x() + (b.high().x() - b.low().x()) * r2,
            b.low().y() + (b.high().y() - b.low().y()) * r3,
            b.low().z() + (b.high().z() - b.low().z()) * r5);
        SPAposition p;
        if (api_find_cls_ptto_face(guess, face, p).ok())
            out_samples.push_back(p);
    }
}

logical IsAnalyticTypeLocal(face_type t)
{
    return (t == face_plane ||
            t == face_cylinder ||
            t == face_cone ||
            t == face_sphere ||
            t == face_torus) ? TRUE : FALSE;
}

logical IsFreeformTypeLocal(face_type t)
{
    return IsAnalyticTypeLocal(t) ? FALSE : TRUE;
}

double Clamp01(double v)
{
    if (v < 0.0)
        return 0.0;
    if (v > 1.0)
        return 1.0;
    return v;
}

logical IsNearAnySample(
    const SPAposition& p,
    const std::vector<SPAposition>& samples,
    double tol2)
{
    int i = 0;
    for (i = 0; i < (int)samples.size(); ++i)
    {
        const SPAvector d = samples[i] - p;
        if ((d % d) <= tol2)
            return TRUE;
    }
    return FALSE;
}

logical AddSampleIfUnique(
    const SPAposition& p,
    std::vector<SPAposition>& out_samples,
    double tol2)
{
    if (IsNearAnySample(p, out_samples, tol2))
        return FALSE;
    out_samples.push_back(p);
    return TRUE;
}

logical CollectFaceSamplesByUvGrid(
    FACE* face,
    int target,
    std::vector<SPAposition>& out_samples)
{
    out_samples.clear();
    if (face == nullptr || target <= 0)
        return FALSE;

    SURFACE* sg = face->geometry();
    if (sg == nullptr)
        return FALSE;

    const surface& surf = sg->equation();
    SPApar_box face_pb;
    if (!sg_get_face_par_box(face, face_pb))
        face_pb = surf.param_range();

    const SPAinterval ur = face_pb.u_range();
    const SPAinterval vr = face_pb.v_range();
    const double u0 = ur.start_pt();
    const double u1 = ur.end_pt();
    const double v0 = vr.start_pt();
    const double v1 = vr.end_pt();
    const double ulen = u1 - u0;
    const double vlen = v1 - v0;
    if (std::fabs(ulen) <= 1e-15 || std::fabs(vlen) <= 1e-15)
        return FALSE;

    const SPAbox fb = get_face_box(face);
    const SPAvector diag_v = fb.high() - fb.low();
    const double diag2 = (diag_v % diag_v);
    const double dedup_tol2 = (std::max)(1e-16, diag2 * 1e-12);
    const double near_tol = (std::max)(1e-7, 1e-3 * std::sqrt((std::max)(1e-16, diag2)));

    int nu = (int)std::ceil(std::sqrt((double)target));
    if (nu < 3)
        nu = 3;
    int nv = nu;
    if (nu * nv < target)
        nv = (int)std::ceil((double)target / (double)nu);
    if (nv < 3)
        nv = 3;

    const double kOffsets[6][2] = {
        {0.5, 0.5},
        {0.25, 0.75},
        {0.75, 0.25},
        {0.15, 0.85},
        {0.35, 0.65},
        {0.65, 0.35}
    };
    int pass = 0;
    for (pass = 0; pass < 6 && (int)out_samples.size() < target; ++pass)
    {
        int i = 0;
        for (i = 0; i < nu && (int)out_samples.size() < target; ++i)
        {
            int j = 0;
            for (j = 0; j < nv && (int)out_samples.size() < target; ++j)
            {
                const double fu = ((double)i + kOffsets[pass][0]) / (double)nu;
                const double fv = ((double)j + kOffsets[pass][1]) / (double)nv;
                const double uu = u0 + ulen * fu;
                const double vv = v0 + vlen * fv;

                SPAposition pe;
                try
                {
                    pe = surf.eval_position(SPApar_pos(uu, vv));
                }
                catch (...)
                {
                    continue;
                }

                SPAposition onf;
                if (!api_find_cls_ptto_face(pe, face, onf).ok())
                    continue;
                if (PosDist(pe, onf) > near_tol)
                    continue;
                (void)AddSampleIfUnique(onf, out_samples, dedup_tol2);
            }
        }
    }

    return out_samples.empty() ? FALSE : TRUE;
}

logical TryGetFaceNormalAtPoint(FACE* face, const SPAposition& p, SPAvector& out_n)
{
    out_n = SPAvector(0.0, 0.0, 0.0);
    if (face == nullptr)
        return FALSE;
    try
    {
        const SPAunit_vector n = sg_get_face_normal(face, p);
        out_n = SPAvector(n.x(), n.y(), n.z());
        return TRUE;
    }
    catch (...)
    {
        return FALSE;
    }
}

logical NormalizeSafe(const SPAvector& v, SPAvector& out_unit)
{
    out_unit = SPAvector(0.0, 0.0, 0.0);
    const double len = VecLen(v);
    if (len <= 1e-15)
        return FALSE;
    out_unit = SPAvector(v.x() / len, v.y() / len, v.z() / len);
    return TRUE;
}

void CollectFaceSamplesForBetweenScore(FACE* face, int target, std::vector<SPAposition>& out_samples)
{
    if (CollectFaceSamplesByUvGrid(face, target, out_samples))
        return;
    CollectFaceSamplesByBboxProjection(face, target, out_samples);
    if (out_samples.empty())
    {
        SPAposition p0;
        if (BuildFaceSeedPoint(face, p0))
            out_samples.push_back(p0);
    }
}

struct FaceSelectGeomEval
{
    FaceSelectGeomEval()
        : face(nullptr),
          split_type(face_unknown),
          is_freeform(FALSE),
          sample_total(0),
          sample_valid(0),
          normal_support(0),
          freeform_eval(0),
          freeform_pass(0),
          success_ratio(0.0),
          mean_sum(0.0),
          std_sum(0.0),
          norm_std(1e30),
          mean_thickness_error(0.0),
          normal_support_ratio(0.0),
          freeform_pass_ratio(0.0),
          penalty(1e30),
          pass_geometry(FALSE)
    {
    }

    FACE* face;
    face_type split_type;
    logical is_freeform;
    int sample_total;
    int sample_valid;
    int normal_support;
    int freeform_eval;
    int freeform_pass;
    double success_ratio;
    double mean_sum;
    double std_sum;
    double norm_std;
    double mean_thickness_error;
    double normal_support_ratio;
    double freeform_pass_ratio;
    double penalty;
    logical pass_geometry;
};

logical EvaluateSplitFaceGeometrySupport(
    FACE* face,
    const std::vector<FACE*>& group_a_faces,
    const std::vector<FACE*>& group_b_faces,
    double pair_thickness,
    const Step6TrimSelectOptions& options,
    FaceSelectGeomEval& out_eval)
{
    out_eval = FaceSelectGeomEval();
    out_eval.face = face;
    if (face == nullptr || group_a_faces.empty() || group_b_faces.empty())
        return FALSE;

    out_eval.split_type = get_face_type(face);
    out_eval.is_freeform = IsFreeformTypeLocal(out_eval.split_type);

    std::vector<SPAposition> samples;
    CollectFaceSamplesForBetweenScore(face, (std::max)(4, options.select_sample_count), samples);
    if (samples.empty())
        return FALSE;
    out_eval.sample_total = (int)samples.size();

    const double kEps = 1e-12;
    const double normal_cos_min = Clamp01((std::max)(0.12, 0.60 * options.facing_min_cos));
    const double normal_cos_weak = Clamp01((std::max)(0.08, 0.50 * normal_cos_min));
    const double relax_factor = out_eval.is_freeform ? options.freeform_relax_factor : options.nonfree_relax_factor;
    const double pass_ratio_req = out_eval.is_freeform ? options.freeform_min_pass_ratio : options.nonfree_min_pass_ratio;
    const logical has_thickness_ref = pair_thickness > 1e-9 ? TRUE : FALSE;

    double sum_dist = 0.0;
    double sum_dist2 = 0.0;
    int i = 0;
    for (i = 0; i < (int)samples.size(); ++i)
    {
        const SPAposition& s = samples[i];
        SPAposition cp_a, cp_b;
        FACE* face_a = nullptr;
        FACE* face_b = nullptr;
        double d_a = 0.0;
        double d_b = 0.0;
        if (!FindClosestPointOnFaceSet(group_a_faces, s, cp_a, d_a, face_a))
            continue;
        if (!FindClosestPointOnFaceSet(group_b_faces, s, cp_b, d_b, face_b))
            continue;

        ++out_eval.sample_valid;
        const double sum_ab = d_a + d_b;
        sum_dist += sum_ab;
        sum_dist2 += (sum_ab * sum_ab);

        SPAvector v_ab_u;
        const logical has_dir = NormalizeSafe(cp_b - cp_a, v_ab_u);
        logical support_ok = FALSE;
        if (has_dir != FALSE)
        {
            SPAvector ns, na, nb;
            const logical has_ns = TryGetFaceNormalAtPoint(face, s, ns);
            const logical has_na = TryGetFaceNormalAtPoint(face_a, cp_a, na);
            const logical has_nb = TryGetFaceNormalAtPoint(face_b, cp_b, nb);
            const double ds = has_ns ? std::fabs(VecDot(v_ab_u, ns)) : 0.0;
            const double da = has_na ? std::fabs(VecDot(v_ab_u, na)) : 0.0;
            const double db = has_nb ? std::fabs(VecDot(v_ab_u, nb)) : 0.0;

            if (out_eval.is_freeform == FALSE)
            {
                support_ok = (has_ns && has_na && has_nb &&
                              ds >= normal_cos_min &&
                              da >= normal_cos_min &&
                              db >= normal_cos_min) ? TRUE : FALSE;
            }
            else
            {
                const logical weak_ns = (has_ns && ds >= normal_cos_weak) ? TRUE : FALSE;
                const logical weak_na = (has_na && da >= normal_cos_weak) ? TRUE : FALSE;
                const logical weak_nb = (has_nb && db >= normal_cos_weak) ? TRUE : FALSE;
                support_ok = (weak_ns || weak_na || weak_nb) ? TRUE : FALSE;
            }
        }
        if (support_ok != FALSE)
            ++out_eval.normal_support;

        SPAposition cp_ab, cp_ba;
        FACE* face_ab = nullptr;
        FACE* face_ba = nullptr;
        double d_ab = 0.0;
        double d_ba = 0.0;
        if (!FindClosestPointOnFaceSet(group_b_faces, cp_a, cp_ab, d_ab, face_ab))
            continue;
        if (!FindClosestPointOnFaceSet(group_a_faces, cp_b, cp_ba, d_ba, face_ba))
            continue;
        (void)face_ab;
        (void)face_ba;

        const double thick = (std::min)(d_ab, d_ba);
        if (thick <= kEps)
            continue;

        ++out_eval.freeform_eval;
        if (sum_ab <= relax_factor * thick)
            ++out_eval.freeform_pass;
    }

    if (out_eval.sample_total > 0)
        out_eval.success_ratio = (double)out_eval.sample_valid / (double)out_eval.sample_total;
    if (out_eval.sample_valid > 0)
    {
        out_eval.mean_sum = sum_dist / (double)out_eval.sample_valid;
        const double mean2 = sum_dist2 / (double)out_eval.sample_valid;
        const double var = mean2 - out_eval.mean_sum * out_eval.mean_sum;
        out_eval.std_sum = (var > 0.0) ? std::sqrt(var) : 0.0;
        out_eval.norm_std = out_eval.std_sum / (std::max)(kEps, out_eval.mean_sum);
        out_eval.normal_support_ratio = (double)out_eval.normal_support / (double)out_eval.sample_valid;
    }
    if (has_thickness_ref != FALSE)
        out_eval.mean_thickness_error =
            std::fabs(out_eval.mean_sum - pair_thickness) / (std::max)(kEps, pair_thickness);
    if (out_eval.freeform_eval > 0)
        out_eval.freeform_pass_ratio = (double)out_eval.freeform_pass / (double)out_eval.freeform_eval;

    double penalty = 0.0;
    penalty += (std::max)(0.0, options.min_sample_success_ratio - out_eval.success_ratio);
    penalty += (std::max)(0.0, pass_ratio_req - out_eval.freeform_pass_ratio);
    penalty += (std::max)(0.0, options.min_normal_support_ratio - out_eval.normal_support_ratio);
    out_eval.pass_geometry =
        (out_eval.success_ratio >= options.min_sample_success_ratio &&
         out_eval.freeform_pass_ratio >= pass_ratio_req &&
         out_eval.normal_support_ratio >= options.min_normal_support_ratio) ? TRUE : FALSE;
    out_eval.penalty = penalty;
    return TRUE;
}

void PickFacesByAreaRule(
    const std::vector<FACE*>& split_faces,
    double area_min_ratio,
    std::vector<FACE*>& out_selected)
{
    out_selected.clear();
    if (split_faces.empty())
        return;

    double amax = 0.0;
    int i = 0;
    for (i = 0; i < (int)split_faces.size(); ++i)
    {
        const double a = FaceAreaEstimate(split_faces[i]);
        if (a > amax)
            amax = a;
    }
    if (amax <= 0.0)
        return;

    const double area_min = (std::max)(0.0, area_min_ratio) * amax;
    for (i = 0; i < (int)split_faces.size(); ++i)
    {
        FACE* f = split_faces[i];
        if (f != nullptr && FaceAreaEstimate(f) >= area_min)
            out_selected.push_back(f);
    }
}

void FilterSmallFacesAmongPassed(
    const std::vector<FACE*>& passed_by_geometry,
    double small_face_ratio,
    std::vector<FACE*>& out_kept)
{
    out_kept.clear();
    if (passed_by_geometry.empty())
        return;
    double amax = 0.0;
    int i = 0;
    for (i = 0; i < (int)passed_by_geometry.size(); ++i)
    {
        const double a = FaceAreaEstimate(passed_by_geometry[i]);
        if (a > amax)
            amax = a;
    }
    if (amax <= 0.0)
    {
        out_kept = passed_by_geometry;
        return;
    }
    const double area_min = (std::max)(0.0, small_face_ratio) * amax;
    for (i = 0; i < (int)passed_by_geometry.size(); ++i)
    {
        FACE* f = passed_by_geometry[i];
        if (f != nullptr && FaceAreaEstimate(f) >= area_min)
            out_kept.push_back(f);
    }
    if (out_kept.empty())
        out_kept = passed_by_geometry;
}

void PickFacesByGeometryRule(
    const std::vector<FACE*>& split_faces,
    const Step2GroupState& step2,
    const PairRecord& pair,
    const Step6TrimSelectOptions& options,
    std::vector<FACE*>& out_selected,
    std::string& out_reason)
{
    out_selected.clear();
    out_reason = "geometry";
    if (split_faces.empty())
        return;

    std::vector<FACE*> ga;
    std::vector<FACE*> gb;
    if (!GroupFacesById(step2, pair.group_a, ga) ||
        !GroupFacesById(step2, pair.group_b, gb))
    {
        PickFacesByAreaRule(split_faces, options.area_min_ratio, out_selected);
        out_reason = "area_group_missing";
        return;
    }

    std::vector<FACE*> passed;
    FACE* best_fallback = nullptr;
    double best_penalty = 1e300;
    int i = 0;
    for (i = 0; i < (int)split_faces.size(); ++i)
    {
        FACE* f = split_faces[i];
        if (f == nullptr)
            continue;
        FaceSelectGeomEval ev;
        if (!EvaluateSplitFaceGeometrySupport(f, ga, gb, pair.thickness, options, ev))
            continue;
        if (ev.pass_geometry != FALSE)
            passed.push_back(f);
        if (best_fallback == nullptr || ev.penalty < best_penalty)
        {
            best_penalty = ev.penalty;
            best_fallback = f;
        }
    }

    if (!passed.empty())
    {
        FilterSmallFacesAmongPassed(passed, options.small_face_ratio, out_selected);
        out_reason = "geometry_small_filter";
        return;
    }

    if (best_fallback != nullptr)
    {
        out_selected.push_back(best_fallback);
        out_reason = "best_penalty";
        return;
    }

    PickFacesByAreaRule(split_faces, options.area_min_ratio, out_selected);
    out_reason = "area_fallback";
    if (out_selected.empty())
    {
        for (i = 0; i < (int)split_faces.size(); ++i)
        {
            if (split_faces[i] != nullptr)
            {
                out_selected.push_back(split_faces[i]);
                out_reason = "first_fallback";
                return;
            }
        }
    }
}

void BuildStep7StitchInputFromSelections(
    const Step5MidPatchState& step5,
    const std::vector<int>& pair_to_patch,
    Step6TrimSelectState& state,
    DiagnosticSink* diagnostics,
    logical emit_events)
{
    state.step7_stitch_input.split_to_raw_faces.clear();
    state.step7_stitch_input.raw_face_relations.clear();
    state.step7_stitch_input.raw_faces.clear();

    int i = 0;
    for (i = 0; i < (int)state.selections.selections.size(); ++i)
    {
        const TrimSelectionRecord& selection = state.selections.selections[i];
        Step7SplitRawFaceRecord record;
        record.split_id = selection.selection_id;
        record.raw_face_id = selection.source_patch_id;
        record.split_face = selection.selected_face;
        state.step7_stitch_input.split_to_raw_faces.push_back(record);
        if (emit_events != FALSE)
            EmitStep7SplitRawFaceRecordEvent(diagnostics, record);
    }

    std::set<std::pair<int, int> > emitted;
    for (i = 0; i < (int)step5.junctions.junctions.size(); ++i)
    {
        const MidPatchJunctionRecord& junction = step5.junctions.junctions[i];
        if (junction.pair_i < 0 || junction.pair_j < 0)
            continue;
        if (junction.pair_i >= (int)pair_to_patch.size() ||
            junction.pair_j >= (int)pair_to_patch.size())
            continue;

        int raw_a = pair_to_patch[junction.pair_i];
        int raw_b = pair_to_patch[junction.pair_j];
        if (raw_a < 0 || raw_b < 0 || raw_a == raw_b)
            continue;
        if (raw_b < raw_a)
            std::swap(raw_a, raw_b);

        const std::pair<int, int> key = std::make_pair(raw_a, raw_b);
        if (!emitted.insert(key).second)
            continue;

        Step7RawFaceRelationRecord record;
        record.relation_id = (int)state.step7_stitch_input.raw_face_relations.size();
        record.raw_face_a = raw_a;
        record.raw_face_b = raw_b;
        state.step7_stitch_input.raw_face_relations.push_back(record);
        if (emit_events != FALSE)
            EmitStep7RawFaceRelationRecordEvent(diagnostics, record);
    }

    std::set<int> step7_raw_face_ids;
    for (i = 0; i < (int)state.step7_stitch_input.split_to_raw_faces.size(); ++i)
        step7_raw_face_ids.insert(state.step7_stitch_input.split_to_raw_faces[i].raw_face_id);
    for (i = 0; i < (int)state.step7_stitch_input.raw_face_relations.size(); ++i)
    {
        const Step7RawFaceRelationRecord& relation =
            state.step7_stitch_input.raw_face_relations[i];
        step7_raw_face_ids.insert(relation.raw_face_a);
        step7_raw_face_ids.insert(relation.raw_face_b);
    }

    std::set<int>::const_iterator raw_face_it = step7_raw_face_ids.begin();
    for (; raw_face_it != step7_raw_face_ids.end(); ++raw_face_it)
    {
        Step7RawFaceRef raw_face = BuildStep7RawFaceRef(step5, *raw_face_it);
        if (raw_face.raw_face_id >= 0)
            state.step7_stitch_input.raw_faces.push_back(raw_face);
    }

    if (emit_events != FALSE)
    {
        for (i = 0; i < (int)state.step7_stitch_input.raw_faces.size(); ++i)
            EmitStep7RawFaceUpstreamEvents(
                diagnostics,
                step5,
                state.step7_stitch_input.raw_faces[i].raw_face_id);
        EmitStep7InputStageSummary(diagnostics, state.step7_stitch_input);
    }
}

logical EmitFaceDebugSatIfEnabled(
    DiagnosticSink* diagnostics,
    const char* role,
    const std::vector<FACE*>& faces)
{
    if (diagnostics == nullptr || diagnostics->context() == nullptr)
        return TRUE;
    if (diagnostics->ShouldOutputDebugSat(role) == FALSE)
        return TRUE;
    const std::string path =
        diagnostics->context()->DebugSatPath(role, diagnostics->current_body_index());
    return diagnostics->EmitFaceSetSat(role, path.c_str(), faces);
}

logical EmitBodyDebugSatIfEnabled(
    DiagnosticSink* diagnostics,
    const char* role,
    const std::vector<BODY*>& bodies)
{
    if (diagnostics == nullptr || diagnostics->context() == nullptr)
        return TRUE;
    if (diagnostics->ShouldOutputDebugSat(role) == FALSE)
        return TRUE;
    ENTITY_LIST entities;
    int i = 0;
    for (i = 0; i < (int)bodies.size(); ++i)
    {
        if (bodies[i] != nullptr)
            entities.add((ENTITY*)bodies[i]);
    }
    const std::string path =
        diagnostics->context()->DebugSatPath(role, diagnostics->current_body_index());
    return diagnostics->EmitEntityListSat(role, path.c_str(), entities);
}

logical EmitFaceSheetBodiesDebugSatIfEnabled(
    DiagnosticSink* diagnostics,
    const char* role,
    const std::vector<FACE*>& faces)
{
    if (diagnostics == nullptr || diagnostics->context() == nullptr)
        return TRUE;
    if (diagnostics->ShouldOutputDebugSat(role) == FALSE)
        return TRUE;

    ENTITY_LIST entities;
    int i = 0;
    for (i = 0; i < (int)faces.size(); ++i)
    {
        FACE* src = faces[i];
        if (src == nullptr)
            continue;
        FACE* detached = nullptr;
        if (!CopyFaceDetached(src, detached) || detached == nullptr)
            continue;
        FACE* one[1];
        one[0] = detached;
        BODY* b = nullptr;
        outcome rb = api_sheet_from_ff(1, one, b);
        if (rb.ok() && b != nullptr)
            entities.add((ENTITY*)b);
    }

    const std::string path =
        diagnostics->context()->DebugSatPath(role, diagnostics->current_body_index());
    return diagnostics->EmitEntityListSat(role, path.c_str(), entities);
}

void AddPairWallToolBuckets(
    std::map<int, std::vector<BODY*> >& wall_seed_buckets,
    const WallRecord& wall,
    BODY* body)
{
    if (body == nullptr)
        return;
    if (wall.group_a >= 0)
        wall_seed_buckets[wall.group_a].push_back(body);
    if (wall.wall_id >= 0)
        wall_seed_buckets[wall.wall_id].push_back(body);
}

std::pair<int, int> Step6OrderedPairKey(int a, int b)
{
    if (a <= b)
        return std::make_pair(a, b);
    return std::make_pair(b, a);
}

void AddRawPairAdjacencyCandidate(std::set<std::pair<int, int> >& candidates, int a, int b)
{
    if (a < 0 || b < 0 || a == b)
        return;
    candidates.insert(Step6OrderedPairKey(a, b));
}

void EmitSelectedAdjacencyEvent(
    DiagnosticSink* diagnostics,
    const Step6SliceAdjacency& adjacency,
    const char* source,
    const std::map<std::string, std::string>* extra_properties = nullptr)
{
    StructuredEvent event = Step6SingleSummaryEvent();
    event.AddTag("adjacency");
    event.AddTag("selected-adjacency");
    event.SetProperty("adjacency_id", IntText(adjacency.adjacency_id));
    event.SetProperty("a", IntText(adjacency.slice_a));
    event.SetProperty("b", IntText(adjacency.slice_b));
    event.SetProperty("selection_a", IntText(adjacency.slice_a));
    event.SetProperty("selection_b", IntText(adjacency.slice_b));
    event.SetProperty("imprint_edge_id", IntText(adjacency.imprint_edge_id));
    event.SetProperty("shared_length", DoubleText(adjacency.shared_length));
    event.SetProperty("source", source == nullptr ? "" : source);
    if (extra_properties != nullptr)
    {
        std::map<std::string, std::string>::const_iterator it = extra_properties->begin();
        for (; it != extra_properties->end(); ++it)
            event.SetProperty(it->first, it->second);
    }
    EmitEvent(diagnostics, event);
}

logical AddSelectedAdjacency(
    Step6SliceAdjacencyTable& table,
    std::set<std::pair<int, int> >& emitted,
    int selection_a,
    int selection_b,
    int imprint_edge_id,
    double shared_length,
    const char* source,
    DiagnosticSink* diagnostics,
    const std::map<std::string, std::string>* extra_properties = nullptr)
{
    if (selection_a < 0 || selection_b < 0 || selection_a == selection_b)
        return FALSE;
    const std::pair<int, int> key = Step6OrderedPairKey(selection_a, selection_b);
    if (!emitted.insert(key).second)
        return FALSE;

    Step6SliceAdjacency adjacency;
    adjacency.adjacency_id = (int)table.adjacencies.size();
    adjacency.slice_a = key.first;
    adjacency.slice_b = key.second;
    adjacency.imprint_edge_id = imprint_edge_id;
    adjacency.shared_length = shared_length;
    table.adjacencies.push_back(adjacency);
    EmitSelectedAdjacencyEvent(diagnostics, adjacency, source, extra_properties);
    return TRUE;
}

void CollectFaceCoedgesLocal(FACE* face, std::vector<COEDGE*>& out_coedges)
{
    out_coedges.clear();
    if (face == nullptr)
        return;

    LOOP* lp = face->loop();
    while (lp != nullptr)
    {
        COEDGE* start = lp->start();
        COEDGE* c = start;
        if (c != nullptr)
        {
            do
            {
                out_coedges.push_back(c);
                c = c->next();
            } while (c != nullptr && c != start);
        }
        lp = lp->next();
    }
}

logical BuildCoedgeSamplesIncludeEndsLocal(
    COEDGE* coedge,
    int sample_count,
    std::vector<SPAposition>& out_samples)
{
    out_samples.clear();
    if (coedge == nullptr)
        return FALSE;
    if (sample_count < 2)
        sample_count = 2;

    try
    {
        const double t0 = coedge_start_param(coedge);
        const double t1 = coedge_end_param(coedge);
        if (std::fabs(t1 - t0) <= 1.0e-15)
            return FALSE;

        int si = 0;
        for (si = 0; si < sample_count; ++si)
        {
            const double alpha = (sample_count <= 1)
                ? 0.0
                : ((double)si / (double)(sample_count - 1));
            const double t = t0 + (t1 - t0) * alpha;
            out_samples.push_back(coedge_param_pos(coedge, t));
        }
    }
    catch (...)
    {
        out_samples.clear();
        return FALSE;
    }
    return out_samples.empty() ? FALSE : TRUE;
}

double PolylineLengthLocal(const std::vector<SPAposition>& samples)
{
    if (samples.size() < 2)
        return 0.0;

    double length = 0.0;
    int i = 1;
    for (i = 1; i < (int)samples.size(); ++i)
        length += PosDist(samples[i - 1], samples[i]);
    return length;
}

struct Step6SelectedEdgeInfo
{
    Step6SelectedEdgeInfo()
        : edge(nullptr),
          coedge(nullptr),
          length(0.0)
    {
    }

    EDGE* edge;
    COEDGE* coedge;
    double length;
    std::vector<SPAposition> samples;
};

struct Step6SelectedFaceInfo
{
    Step6SelectedFaceInfo()
        : selection_id(-1),
          source_patch_id(-1),
          source_pair_id(-1),
          source_seed_rep_pair(-1),
          face(nullptr)
    {
    }

    int selection_id;
    int source_patch_id;
    int source_pair_id;
    int source_seed_rep_pair;
    FACE* face;
    std::vector<Step6SelectedEdgeInfo> edges;
};

void BuildSelectedFaceInfo(
    const TrimSelectionRecord& selection,
    const Step6TrimSelectOptions& options,
    Step6SelectedFaceInfo& out_info)
{
    out_info = Step6SelectedFaceInfo();
    out_info.selection_id = selection.selection_id;
    out_info.source_patch_id = selection.source_patch_id;
    out_info.source_pair_id = selection.source_pair_id;
    out_info.source_seed_rep_pair = selection.source_seed_rep_pair;
    out_info.face = selection.source_split_face;
    if (out_info.face == nullptr)
        return;

    std::vector<COEDGE*> coedges;
    CollectFaceCoedgesLocal(out_info.face, coedges);

    std::set<EDGE*> seen_edges;
    int ci = 0;
    for (ci = 0; ci < (int)coedges.size(); ++ci)
    {
        COEDGE* c = coedges[ci];
        if (c == nullptr)
            continue;
        EDGE* e = c->edge();
        if (e == nullptr || !seen_edges.insert(e).second)
            continue;

        Step6SelectedEdgeInfo edge_info;
        edge_info.edge = e;
        edge_info.coedge = c;
        const int edge_sample_count =
            (std::max)(2, (std::max)(options.edge_match_sample_count, options.adjacency_sample_count));
        if (!BuildCoedgeSamplesIncludeEndsLocal(
                c,
                edge_sample_count,
                edge_info.samples))
            continue;
        edge_info.length = PolylineLengthLocal(edge_info.samples);
        if (edge_info.length <= 1.0e-12)
            continue;
        out_info.edges.push_back(edge_info);
    }
}

logical SelectedFacesShareTopologicalEdge(
    const Step6SelectedFaceInfo& a,
    const Step6SelectedFaceInfo& b,
    double& out_shared_length)
{
    out_shared_length = 0.0;
    int ai = 0;
    for (ai = 0; ai < (int)a.edges.size(); ++ai)
    {
        if (a.edges[ai].edge == nullptr)
            continue;
        int bi = 0;
        for (bi = 0; bi < (int)b.edges.size(); ++bi)
        {
            if (b.edges[bi].edge == a.edges[ai].edge)
            {
                out_shared_length = (std::max)(a.edges[ai].length, b.edges[bi].length);
                return TRUE;
            }
        }
    }
    return FALSE;
}

double MaxAlignedSampleDistance(
    const std::vector<SPAposition>& a,
    const std::vector<SPAposition>& b,
    logical reversed)
{
    if (a.empty() || b.empty() || a.size() != b.size())
        return 1.0e300;

    double max_d = 0.0;
    int i = 0;
    for (i = 0; i < (int)a.size(); ++i)
    {
        const int j = reversed != FALSE ? ((int)b.size() - 1 - i) : i;
        const double d = PosDist(a[i], b[j]);
        if (d > max_d)
            max_d = d;
    }
    return max_d;
}

SPAvector ScaleVectorLocal(const SPAvector& v, double scale)
{
    return SPAvector(v.x() * scale, v.y() * scale, v.z() * scale);
}

SPAvector SubtractVectorsLocal(const SPAvector& a, const SPAvector& b)
{
    return SPAvector(a.x() - b.x(), a.y() - b.y(), a.z() - b.z());
}

double PointLineDistanceLocal(
    const SPAposition& p,
    const SPAposition& origin,
    const SPAvector& unit_dir)
{
    const SPAvector op = p - origin;
    const double t = VecDot(op, unit_dir);
    const SPAvector perpendicular = SubtractVectorsLocal(op, ScaleVectorLocal(unit_dir, t));
    return VecLen(perpendicular);
}

SPAposition AddScaledVectorLocal(const SPAposition& p, const SPAvector& v, double scale)
{
    return SPAposition(
        p.x() + v.x() * scale,
        p.y() + v.y() * scale,
        p.z() + v.z() * scale);
}

double PointSegmentDistanceLocal(
    const SPAposition& p,
    const SPAposition& a,
    const SPAposition& b)
{
    const SPAvector ab = b - a;
    const double len2 = VecDot(ab, ab);
    if (len2 <= 1.0e-24)
        return PosDist(p, a);

    const SPAvector ap = p - a;
    double t = VecDot(ap, ab) / len2;
    if (t < 0.0)
        t = 0.0;
    else if (t > 1.0)
        t = 1.0;

    return PosDist(p, AddScaledVectorLocal(a, ab, t));
}

double PointPolylineDistanceLocal(
    const SPAposition& p,
    const std::vector<SPAposition>& polyline)
{
    if (polyline.empty())
        return 1.0e300;
    if (polyline.size() == 1)
        return PosDist(p, polyline[0]);

    double best = 1.0e300;
    int i = 1;
    for (i = 1; i < (int)polyline.size(); ++i)
    {
        const double d = PointSegmentDistanceLocal(p, polyline[i - 1], polyline[i]);
        if (d < best)
            best = d;
    }
    return best;
}

double SegmentAlignedOverlapLengthLocal(
    const SPAposition& a0,
    const SPAposition& a1,
    const SPAposition& b0,
    const SPAposition& b1,
    double match_tol)
{
    const SPAvector va = a1 - a0;
    const SPAvector vb = b1 - b0;
    const double la = VecLen(va);
    const double lb = VecLen(vb);
    if (la <= 1.0e-12 || lb <= 1.0e-12)
        return 0.0;

    const SPAvector ua = ScaleVectorLocal(va, 1.0 / la);
    const SPAvector ub = ScaleVectorLocal(vb, 1.0 / lb);
    const double dir_cos_abs = std::fabs(VecDot(ua, ub));
    if (dir_cos_abs < 0.995)
        return 0.0;

    const double b0_line_d = PointLineDistanceLocal(b0, a0, ua);
    const double b1_line_d = PointLineDistanceLocal(b1, a0, ua);
    const double a0_line_d = PointLineDistanceLocal(a0, b0, ub);
    const double a1_line_d = PointLineDistanceLocal(a1, b0, ub);
    const double line_tol = (std::max)(match_tol, 1.0e-8);
    if (b0_line_d > line_tol || b1_line_d > line_tol ||
        a0_line_d > line_tol || a1_line_d > line_tol)
        return 0.0;

    const double a_lo = 0.0;
    const double a_hi = la;
    double b_t0 = VecDot(b0 - a0, ua);
    double b_t1 = VecDot(b1 - a0, ua);
    if (b_t0 > b_t1)
        std::swap(b_t0, b_t1);

    const double overlap_lo = (std::max)(a_lo, b_t0);
    const double overlap_hi = (std::min)(a_hi, b_t1);
    const double overlap = overlap_hi - overlap_lo;
    return overlap > 0.0 ? overlap : 0.0;
}

double PolylineCoveredLengthByOtherLocal(
    const std::vector<SPAposition>& samples,
    const std::vector<SPAposition>& other,
    double match_tol)
{
    if (samples.size() < 2 || other.size() < 2)
        return 0.0;

    double covered = 0.0;
    int i = 1;
    for (i = 1; i < (int)samples.size(); ++i)
    {
        const double seg_len = PosDist(samples[i - 1], samples[i]);
        if (seg_len <= 1.0e-12)
            continue;

        double seg_covered = 0.0;
        int j = 1;
        for (j = 1; j < (int)other.size(); ++j)
        {
            seg_covered += SegmentAlignedOverlapLengthLocal(
                samples[i - 1],
                samples[i],
                other[j - 1],
                other[j],
                match_tol);
        }
        if (seg_covered > seg_len)
            seg_covered = seg_len;
        covered += seg_covered;
    }

    return covered;
}

double MaxSampleDistanceToPolylineLocal(
    const std::vector<SPAposition>& samples,
    const std::vector<SPAposition>& polyline)
{
    if (samples.empty() || polyline.empty())
        return 1.0e300;

    double max_d = 0.0;
    int i = 0;
    for (i = 0; i < (int)samples.size(); ++i)
    {
        const double d = PointPolylineDistanceLocal(samples[i], polyline);
        if (d > max_d)
            max_d = d;
    }
    return max_d;
}

logical SelectedEdgesOverlapByGeometry(
    const Step6SelectedEdgeInfo& a,
    const Step6SelectedEdgeInfo& b,
    double match_tol,
    double min_overlap_len,
    double& out_shared_length)
{
    out_shared_length = 0.0;
    if (a.samples.empty() || b.samples.empty())
        return FALSE;
    if (a.length <= min_overlap_len || b.length <= min_overlap_len)
        return FALSE;

    const double covered_a =
        PolylineCoveredLengthByOtherLocal(a.samples, b.samples, match_tol);
    const double covered_b =
        PolylineCoveredLengthByOtherLocal(b.samples, a.samples, match_tol);
    double shared_length = (std::min)(covered_a, covered_b);
    const double max_shared = (std::min)(a.length, b.length);
    if (shared_length > max_shared)
        shared_length = max_shared;
    if (shared_length >= min_overlap_len)
    {
        out_shared_length = shared_length;
        return TRUE;
    }

    const Step6SelectedEdgeInfo* short_edge = &a;
    const Step6SelectedEdgeInfo* long_edge = &b;
    if (short_edge->length > long_edge->length)
        std::swap(short_edge, long_edge);
    const double max_d =
        MaxSampleDistanceToPolylineLocal(short_edge->samples, long_edge->samples);
    if (max_d > match_tol)
        return FALSE;

    out_shared_length = short_edge->length;
    return TRUE;
}

logical SelectedEdgesMatchByGeometry(
    const Step6SelectedEdgeInfo& a,
    const Step6SelectedEdgeInfo& b,
    const Step6TrimSelectOptions& options,
    double& out_shared_length)
{
    out_shared_length = 0.0;
    if (a.samples.empty() || b.samples.empty())
        return FALSE;
    if (a.length <= 1.0e-12 || b.length <= 1.0e-12)
        return FALSE;

    const double max_len = (std::max)(a.length, b.length);
    const double match_tol = (std::max)(options.edge_match_tolerance, max_len * 1.0e-7);
    const double len_tol_abs =
        (std::max)(options.edge_match_length_tolerance, 2.0 * options.edge_match_tolerance);
    const double len_tol = (std::max)(len_tol_abs, max_len * 1.0e-4);
    const double min_overlap_len = (std::max)(len_tol_abs, max_len * 1.0e-8);

    if (std::fabs(a.length - b.length) <= len_tol)
    {
        if (a.samples.size() == b.samples.size())
        {
            const double err_forward = MaxAlignedSampleDistance(a.samples, b.samples, FALSE);
            const double err_reverse = MaxAlignedSampleDistance(a.samples, b.samples, TRUE);
            if ((std::min)(err_forward, err_reverse) <= match_tol)
            {
                out_shared_length = (std::min)(a.length, b.length);
                return TRUE;
            }
        }
    }

    return SelectedEdgesOverlapByGeometry(
        a,
        b,
        match_tol,
        min_overlap_len,
        out_shared_length);
}

logical SelectedFacesMatchByGeometry(
    const Step6SelectedFaceInfo& a,
    const Step6SelectedFaceInfo& b,
    const Step6TrimSelectOptions& options,
    double& out_shared_length)
{
    out_shared_length = 0.0;
    int ai = 0;
    for (ai = 0; ai < (int)a.edges.size(); ++ai)
    {
        int bi = 0;
        for (bi = 0; bi < (int)b.edges.size(); ++bi)
        {
            double shared_length = 0.0;
            if (SelectedEdgesMatchByGeometry(a.edges[ai], b.edges[bi], options, shared_length) != FALSE)
            {
                out_shared_length = shared_length;
                return TRUE;
            }
        }
    }
    return FALSE;
}

logical PointNearFaceLocal(FACE* face, const SPAposition& p, double tolerance)
{
    if (face == nullptr)
        return FALSE;

    try
    {
        SPAposition cp;
        outcome r = api_find_cls_ptto_face(p, face, cp);
        if (!r.ok())
            return FALSE;
        return PosDist(p, cp) <= tolerance ? TRUE : FALSE;
    }
    catch (...)
    {
        return FALSE;
    }
}

double EdgeLengthOnFaceLocal(
    const Step6SelectedEdgeInfo& edge,
    FACE* face,
    double tolerance)
{
    if (face == nullptr || edge.samples.size() < 2)
        return 0.0;

    double covered = 0.0;
    int i = 1;
    for (i = 1; i < (int)edge.samples.size(); ++i)
    {
        const SPAposition& p0 = edge.samples[i - 1];
        const SPAposition& p1 = edge.samples[i];
        const double seg_len = PosDist(p0, p1);
        if (seg_len <= 1.0e-12)
            continue;

        const SPAposition pm = AddScaledVectorLocal(p0, p1 - p0, 0.5);
        if (PointNearFaceLocal(face, p0, tolerance) == FALSE)
            continue;
        if (PointNearFaceLocal(face, pm, tolerance) == FALSE)
            continue;
        if (PointNearFaceLocal(face, p1, tolerance) == FALSE)
            continue;
        covered += seg_len;
    }
    return covered;
}

logical SelectedEdgeLiesOnFaceByGeometry(
    const Step6SelectedEdgeInfo& edge,
    const Step6SelectedFaceInfo& target,
    const Step6TrimSelectOptions& options,
    double& out_shared_length)
{
    out_shared_length = 0.0;
    if (target.face == nullptr || edge.length <= 1.0e-12)
        return FALSE;

    const double match_tol =
        (std::max)(options.edge_match_tolerance, edge.length * 1.0e-7);
    const double min_support_len =
        (std::max)((std::max)(options.edge_match_length_tolerance, 2.0 * options.edge_match_tolerance),
                   edge.length * 1.0e-8);
    const double supported = EdgeLengthOnFaceLocal(edge, target.face, match_tol);
    if (supported < min_support_len)
        return FALSE;

    out_shared_length = supported;
    return TRUE;
}

logical SelectedFacesMatchByOneSidedEdgeOnFace(
    const Step6SelectedFaceInfo& a,
    const Step6SelectedFaceInfo& b,
    const Step6TrimSelectOptions& options,
    double& out_shared_length)
{
    out_shared_length = 0.0;
    int ai = 0;
    for (ai = 0; ai < (int)a.edges.size(); ++ai)
    {
        double shared_length = 0.0;
        if (SelectedEdgeLiesOnFaceByGeometry(a.edges[ai], b, options, shared_length) != FALSE)
        {
            out_shared_length = shared_length;
            return TRUE;
        }
    }

    int bi = 0;
    for (bi = 0; bi < (int)b.edges.size(); ++bi)
    {
        double shared_length = 0.0;
        if (SelectedEdgeLiesOnFaceByGeometry(b.edges[bi], a, options, shared_length) != FALSE)
        {
            out_shared_length = shared_length;
            return TRUE;
        }
    }

    return FALSE;
}

struct Step6FaceDistanceRefineResult
{
    Step6FaceDistanceRefineResult()
        : edge_count(0),
          checked_edge_count(0),
          best_edge_index(-1),
          best_edge_length(0.0),
          sample_count(0),
          valid_count(0),
          pass_count(0),
          pass_ratio(0.0),
          mean_distance(0.0),
          mean_pass_distance(0.0),
          max_pass_distance(0.0),
          mean_distance_units(0.0),
          mean_pass_distance_units(0.0),
          passed(FALSE)
    {
    }

    int edge_count;
    int checked_edge_count;
    int best_edge_index;
    double best_edge_length;
    int sample_count;
    int valid_count;
    int pass_count;
    double pass_ratio;
    double mean_distance;
    double mean_pass_distance;
    double max_pass_distance;
    double mean_distance_units;
    double mean_pass_distance_units;
    logical passed;
};

double ResolveAdjacencyDistanceScale(const Step6TrimSelectResult& result)
{
    const Step5MidPatchState* step5 = result.state.input_step5;
    const Step4RelationState* step4 = step5 == nullptr ? nullptr : step5->input_step4;
    const Step3PairState* step3 = step4 == nullptr ? nullptr : step4->input_step3;
    const Step2GroupState* step2 = step3 == nullptr ? nullptr : step3->input_step2;
    const Step1FaceAnalyzeState* step1 = step2 == nullptr ? nullptr : step2->input_step1;
    if (step1 != nullptr &&
        step1->model_scale.valid != FALSE &&
        step1->model_scale.distance_unit > 0.0)
        return step1->model_scale.distance_unit;
    return 0.0;
}

logical PreferEdgeDistanceResult(
    const Step6FaceDistanceRefineResult& candidate,
    const Step6FaceDistanceRefineResult& current)
{
    if (candidate.valid_count <= 0)
        return FALSE;
    if (current.valid_count <= 0)
        return TRUE;
    if (candidate.passed != current.passed)
        return candidate.passed;
    if (candidate.pass_ratio > current.pass_ratio + 1.0e-12)
        return TRUE;
    if (candidate.pass_ratio + 1.0e-12 < current.pass_ratio)
        return FALSE;
    if (candidate.pass_count != current.pass_count)
        return candidate.pass_count > current.pass_count ? TRUE : FALSE;
    if (candidate.mean_pass_distance > 0.0 && current.mean_pass_distance > 0.0)
        return candidate.mean_pass_distance < current.mean_pass_distance ? TRUE : FALSE;
    return candidate.mean_distance < current.mean_distance ? TRUE : FALSE;
}

logical EvaluateOneEdgeDistanceRefine(
    const Step6SelectedEdgeInfo& edge,
    int edge_index,
    FACE* target_face,
    const Step6TrimSelectOptions& options,
    double tolerance,
    double scale,
    Step6FaceDistanceRefineResult& out_result)
{
    out_result = Step6FaceDistanceRefineResult();
    out_result.best_edge_index = edge_index;
    out_result.best_edge_length = edge.length;
    out_result.sample_count = (int)edge.samples.size();
    if (target_face == nullptr || tolerance <= 0.0 || edge.samples.empty())
        return FALSE;

    double sum_distance = 0.0;
    double sum_pass_distance = 0.0;
    int i = 0;
    for (i = 0; i < (int)edge.samples.size(); ++i)
    {
        SPAposition cp;
        outcome r = api_find_cls_ptto_face(edge.samples[i], target_face, cp);
        if (!r.ok())
            continue;

        const double dist = PosDist(edge.samples[i], cp);
        ++out_result.valid_count;
        sum_distance += dist;
        if (dist < tolerance)
        {
            ++out_result.pass_count;
            sum_pass_distance += dist;
            if (dist > out_result.max_pass_distance)
                out_result.max_pass_distance = dist;
        }
    }

    if (out_result.valid_count <= 0)
        return FALSE;

    out_result.pass_ratio = (double)out_result.pass_count / (double)out_result.valid_count;
    out_result.mean_distance = sum_distance / (double)out_result.valid_count;
    if (out_result.pass_count > 0)
        out_result.mean_pass_distance = sum_pass_distance / (double)out_result.pass_count;
    if (scale > 0.0)
    {
        out_result.mean_distance_units = out_result.mean_distance / scale;
        out_result.mean_pass_distance_units = out_result.mean_pass_distance / scale;
    }
    out_result.passed =
        out_result.pass_ratio >= options.adjacency_min_pass_ratio ? TRUE : FALSE;
    return TRUE;
}

logical EvaluateEdgeDistanceRefineDirection(
    const Step6SelectedFaceInfo& source,
    const Step6SelectedFaceInfo& target,
    const Step6TrimSelectOptions& options,
    double tolerance,
    double scale,
    Step6FaceDistanceRefineResult& out_result)
{
    out_result = Step6FaceDistanceRefineResult();
    out_result.edge_count = (int)source.edges.size();
    if (target.face == nullptr || tolerance <= 0.0 || source.edges.empty())
        return FALSE;

    logical any_valid = FALSE;
    int ei = 0;
    for (ei = 0; ei < (int)source.edges.size(); ++ei)
    {
        Step6FaceDistanceRefineResult edge_result;
        if (EvaluateOneEdgeDistanceRefine(
                source.edges[ei],
                ei,
                target.face,
                options,
                tolerance,
                scale,
                edge_result) == FALSE)
            continue;
        edge_result.edge_count = (int)source.edges.size();
        edge_result.checked_edge_count = out_result.checked_edge_count + 1;
        ++out_result.checked_edge_count;
        any_valid = TRUE;
        if (PreferEdgeDistanceResult(edge_result, out_result) != FALSE)
        {
            const int checked_edges = out_result.checked_edge_count;
            out_result = edge_result;
            out_result.edge_count = (int)source.edges.size();
            out_result.checked_edge_count = checked_edges;
        }
    }
    return any_valid;
}

std::string DirectionText(logical a_to_b_pass, logical b_to_a_pass)
{
    if (a_to_b_pass != FALSE && b_to_a_pass != FALSE)
        return "both";
    if (a_to_b_pass != FALSE)
        return "a_to_b";
    if (b_to_a_pass != FALSE)
        return "b_to_a";
    return "none";
}

void AddDistanceRefineProperties(
    const Step6SelectedFaceInfo& a,
    const Step6SelectedFaceInfo& b,
    double scale,
    double tolerance_units,
    double tolerance,
    double min_pass_ratio,
    const Step6FaceDistanceRefineResult& a_to_b,
    const Step6FaceDistanceRefineResult& b_to_a,
    std::map<std::string, std::string>& props)
{
    props["source_pair_a"] = IntText(a.source_pair_id);
    props["source_pair_b"] = IntText(b.source_pair_id);
    props["source_patch_a"] = IntText(a.source_patch_id);
    props["source_patch_b"] = IntText(b.source_patch_id);
    props["source_seed_rep_pair_a"] = IntText(a.source_seed_rep_pair);
    props["source_seed_rep_pair_b"] = IntText(b.source_seed_rep_pair);
    props["scale"] = DoubleText(scale);
    props["tolerance_units"] = DoubleText(tolerance_units);
    props["tolerance"] = DoubleText(tolerance);
    props["min_pass_ratio"] = DoubleText(min_pass_ratio);
    props["refine_mode"] = "edge";
    props["a_to_b_edge_count"] = IntText(a_to_b.edge_count);
    props["a_to_b_checked_edge_count"] = IntText(a_to_b.checked_edge_count);
    props["a_to_b_best_edge_index"] = IntText(a_to_b.best_edge_index);
    props["a_to_b_best_edge_length"] = DoubleText(a_to_b.best_edge_length);
    props["a_to_b_sample_count"] = IntText(a_to_b.sample_count);
    props["a_to_b_valid_count"] = IntText(a_to_b.valid_count);
    props["a_to_b_pass_count"] = IntText(a_to_b.pass_count);
    props["a_to_b_pass_ratio"] = DoubleText(a_to_b.pass_ratio);
    props["a_to_b_mean_distance"] = DoubleText(a_to_b.mean_distance);
    props["a_to_b_mean_distance_units"] = DoubleText(a_to_b.mean_distance_units);
    props["a_to_b_mean_pass_distance"] = DoubleText(a_to_b.mean_pass_distance);
    props["a_to_b_mean_pass_distance_units"] = DoubleText(a_to_b.mean_pass_distance_units);
    props["a_to_b_max_pass_distance"] = DoubleText(a_to_b.max_pass_distance);
    props["b_to_a_edge_count"] = IntText(b_to_a.edge_count);
    props["b_to_a_checked_edge_count"] = IntText(b_to_a.checked_edge_count);
    props["b_to_a_best_edge_index"] = IntText(b_to_a.best_edge_index);
    props["b_to_a_best_edge_length"] = DoubleText(b_to_a.best_edge_length);
    props["b_to_a_sample_count"] = IntText(b_to_a.sample_count);
    props["b_to_a_valid_count"] = IntText(b_to_a.valid_count);
    props["b_to_a_pass_count"] = IntText(b_to_a.pass_count);
    props["b_to_a_pass_ratio"] = DoubleText(b_to_a.pass_ratio);
    props["b_to_a_mean_distance"] = DoubleText(b_to_a.mean_distance);
    props["b_to_a_mean_distance_units"] = DoubleText(b_to_a.mean_distance_units);
    props["b_to_a_mean_pass_distance"] = DoubleText(b_to_a.mean_pass_distance);
    props["b_to_a_mean_pass_distance_units"] = DoubleText(b_to_a.mean_pass_distance_units);
    props["b_to_a_max_pass_distance"] = DoubleText(b_to_a.max_pass_distance);
    props["accepted_direction"] = DirectionText(a_to_b.passed, b_to_a.passed);
}

logical RawMidfaceAdjacencyCandidate(
    const Step6SelectedFaceInfo& a,
    const Step6SelectedFaceInfo& b,
    const std::set<std::pair<int, int> >& raw_pair_adjacency_candidates)
{
    if (a.selection_id < 0 || b.selection_id < 0 || a.selection_id == b.selection_id)
        return FALSE;
    if (a.source_seed_rep_pair >= 0 && a.source_seed_rep_pair == b.source_seed_rep_pair)
        return TRUE;
    if (a.source_patch_id >= 0 && a.source_patch_id == b.source_patch_id)
        return TRUE;
    if (a.source_pair_id >= 0 && a.source_pair_id == b.source_pair_id)
        return TRUE;
    if (a.source_pair_id >= 0 && b.source_pair_id >= 0)
    {
        const std::pair<int, int> key = Step6OrderedPairKey(a.source_pair_id, b.source_pair_id);
        if (raw_pair_adjacency_candidates.find(key) != raw_pair_adjacency_candidates.end())
            return TRUE;
    }
    return FALSE;
}

void BuildSelectedSliceAdjacency(
    Step6TrimSelectResult& result,
    const std::set<std::pair<int, int> >& raw_pair_adjacency_candidates,
    const Step6TrimSelectOptions& options,
    DiagnosticSink* diagnostics)
{
    result.state.slice_adjacencies.adjacencies.clear();

    std::vector<Step6SelectedFaceInfo> infos;
    infos.reserve(result.state.selections.selections.size());
    int si = 0;
    for (si = 0; si < (int)result.state.selections.selections.size(); ++si)
    {
        Step6SelectedFaceInfo info;
        BuildSelectedFaceInfo(result.state.selections.selections[si], options, info);
        infos.push_back(info);
    }

    std::set<std::pair<int, int> > emitted;
    int topology_hit_count = 0;
    int raw_candidate_count = 0;
    int distance_checked_count = 0;
    int distance_hit_count = 0;
    int distance_one_way_hit_count = 0;
    int distance_both_way_hit_count = 0;
    const double adjacency_scale = ResolveAdjacencyDistanceScale(result);
    const double adjacency_tolerance =
        adjacency_scale > 0.0
            ? options.adjacency_distance_units * adjacency_scale
            : options.edge_match_tolerance;

    int ai = 0;
    for (ai = 0; ai < (int)infos.size(); ++ai)
    {
        int bi = ai + 1;
        for (; bi < (int)infos.size(); ++bi)
        {
            double shared_length = 0.0;
            if (infos[ai].source_seed_rep_pair >= 0 &&
                infos[ai].source_seed_rep_pair == infos[bi].source_seed_rep_pair &&
                SelectedFacesShareTopologicalEdge(infos[ai], infos[bi], shared_length) != FALSE)
            {
                if (AddSelectedAdjacency(
                    result.state.slice_adjacencies,
                    emitted,
                    infos[ai].selection_id,
                    infos[bi].selection_id,
                    -1,
                    shared_length,
                    "topology",
                    diagnostics) != FALSE)
                    ++topology_hit_count;
                continue;
            }

            if (RawMidfaceAdjacencyCandidate(infos[ai], infos[bi], raw_pair_adjacency_candidates) == FALSE)
                continue;
            ++raw_candidate_count;
            ++distance_checked_count;

            Step6FaceDistanceRefineResult a_to_b;
            Step6FaceDistanceRefineResult b_to_a;
            (void)EvaluateEdgeDistanceRefineDirection(
                infos[ai],
                infos[bi],
                options,
                adjacency_tolerance,
                adjacency_scale,
                a_to_b);
            (void)EvaluateEdgeDistanceRefineDirection(
                infos[bi],
                infos[ai],
                options,
                adjacency_tolerance,
                adjacency_scale,
                b_to_a);

            if (a_to_b.passed != FALSE || b_to_a.passed != FALSE)
            {
                std::map<std::string, std::string> distance_props;
                AddDistanceRefineProperties(
                    infos[ai],
                    infos[bi],
                    adjacency_scale,
                    options.adjacency_distance_units,
                    adjacency_tolerance,
                    options.adjacency_min_pass_ratio,
                    a_to_b,
                    b_to_a,
                    distance_props);

                const logical both_pass =
                    (a_to_b.passed != FALSE && b_to_a.passed != FALSE) ? TRUE : FALSE;
                double stored_distance = 0.0;
                if (both_pass != FALSE)
                    stored_distance = 0.5 * (a_to_b.mean_pass_distance + b_to_a.mean_pass_distance);
                else if (a_to_b.passed != FALSE)
                    stored_distance = a_to_b.mean_pass_distance;
                else
                    stored_distance = b_to_a.mean_pass_distance;

                if (AddSelectedAdjacency(
                    result.state.slice_adjacencies,
                    emitted,
                    infos[ai].selection_id,
                    infos[bi].selection_id,
                    -1,
                    stored_distance,
                    "distance",
                    diagnostics,
                    &distance_props) != FALSE)
                {
                    ++distance_hit_count;
                    if (both_pass != FALSE)
                        ++distance_both_way_hit_count;
                    else
                        ++distance_one_way_hit_count;
                }
            }
        }
    }

    result.state.stats.slice_adjacency_count =
        (int)result.state.slice_adjacencies.adjacencies.size();

    std::map<std::string, std::string> props;
    props["selected_count"] = IntText((int)result.state.selections.selections.size());
    props["raw_pair_candidate_count"] = IntText((int)raw_pair_adjacency_candidates.size());
    props["raw_prefilter_pair_count"] = IntText(raw_candidate_count);
    props["topology_hit_count"] = IntText(topology_hit_count);
    props["distance_checked_pair_count"] = IntText(distance_checked_count);
    props["distance_hit_count"] = IntText(distance_hit_count);
    props["distance_one_way_hit_count"] = IntText(distance_one_way_hit_count);
    props["distance_both_way_hit_count"] = IntText(distance_both_way_hit_count);
    props["adjacency_scale"] = DoubleText(adjacency_scale);
    props["adjacency_tolerance_units"] = DoubleText(options.adjacency_distance_units);
    props["adjacency_tolerance"] = DoubleText(adjacency_tolerance);
    props["adjacency_min_pass_ratio"] = DoubleText(options.adjacency_min_pass_ratio);
    props["adjacency_sample_count"] = IntText(options.adjacency_sample_count);
    props["slice_adjacency_count"] = IntText(result.state.stats.slice_adjacency_count);
    EmitStageSummary(diagnostics, "adjacency", props);
}

} // namespace

Step6TrimSelectOptions::Step6TrimSelectOptions()
    : enable_trim(TRUE),
      emit_selection_events(TRUE),
      build_slice_adjacency(FALSE),
      edge_match_sample_count(8),
      edge_match_tolerance(1.0e-5),
      edge_match_length_tolerance(1.0e-5),
      adjacency_distance_units(10.0),
      adjacency_sample_count(24),
      adjacency_min_pass_ratio(0.60),
      enable_source_trim(FALSE),
      enable_body_prewall_extend(TRUE),
      mid_extend_scale(1.50),
      mid_extend_min(1e-4),
      wall_extend_scale(3.0),
      wall_extend_min(1e-4),
      wall_sphere_radius_boost_ratio(0.05),
      use_api_extend_fail_fallback_only(TRUE),
      api_extend_fail_fallback_use_uv_expand(TRUE),
      cylinder_uv_expand_scale(3.0),
      sphere_uv_expand_scale(3.0),
      torus_uv_expand_scale(3.0),
      extend_fail_fallback_sample_count(64),
      uniform_sample_count(24),
      pass_ratio_required(0.70),
      facing_min_cos(0.80),
      area_min_ratio(0.06),
      use_between_score_pick(TRUE),
      between_score_min(1.60),
      between_area_floor_ratio(0.0),
      between_sample_count(24),
      select_sample_count(24),
      min_sample_success_ratio(0.2),
      max_norm_std(0.28),
      max_mean_thickness_error(0.45),
      min_normal_support_ratio(0.0),
      nonfree_relax_factor(4.0),
      nonfree_min_pass_ratio(0.20),
      freeform_relax_factor(2.20),
      freeform_min_pass_ratio(0.55),
      small_face_ratio(0.06)
{
}

Step6EdgeGeom::Step6EdgeGeom()
    : edge(nullptr),
      coedge(nullptr),
      p0(0.0, 0.0, 0.0),
      p1(0.0, 0.0, 0.0),
      mid(0.0, 0.0, 0.0),
      length(0.0),
      curve_type(-1),
      valid(FALSE)
{
}

Step6SliceRecord::Step6SliceRecord()
    : slice_id(-1),
      face(nullptr),
      selected_index(-1),
      source_patch_id(-1),
      source_pair_id(-1),
      source_seed_rep_pair(-1),
      area(0.0)
{
}

Step6RawImprintSideEdge::Step6RawImprintSideEdge()
    : side_edge_id(-1),
      raw_id(-1),
      side(STEP6_IMPRINT_SIDE_A)
{
}

Step6RawImprintEdgePair::Step6RawImprintEdgePair()
    : imprint_edge_id(-1),
      raw_a(-1),
      raw_b(-1),
      side_edge_a_id(-1),
      side_edge_b_id(-1),
      edge_a(nullptr),
      edge_b(nullptr),
      reversed(FALSE),
      endpoint_error(0.0),
      sample_error(0.0),
      length_error(0.0),
      ambiguous(FALSE)
{
}

Step6SliceEdgeUse::Step6SliceEdgeUse()
    : use_id(-1),
      slice_id(-1),
      slice_edge(nullptr),
      imprint_edge_id(-1),
      side(STEP6_IMPRINT_SIDE_A),
      coverage_ratio(0.0)
{
}

Step6SliceAdjacency::Step6SliceAdjacency()
    : adjacency_id(-1),
      slice_a(-1),
      slice_b(-1),
      imprint_edge_id(-1),
      shared_length(0.0)
{
}

TrimSelectStats::TrimSelectStats()
    : input_patch_count(0),
      input_pair_count(0),
      mid_seed_try_count(0),
      mid_seed_ok_count(0),
      mid_seed_reused_count(0),
      wall_seed_try_count(0),
      wall_seed_ok_count(0),
      virtual_wall_seed_count(0),
      virtual_wall_raw_count(0),
      mm1_bidirectional_edge_count(0),
      mm1_directional_edge_count(0),
      mm2_skipped_count(0),
      trim_seed_group_count(0),
      trim_tool_try_count(0),
      trim_tool_ok_count(0),
      preselect_split_face_count(0),
      selected_count(0),
      rejected_count(0),
      selected_copy_fail_count(0),
      selected_dedup_skip_count(0),
      slice_count(0),
      side_edge_count(0),
      raw_imprint_edge_pair_count(0),
      ambiguous_edge_pair_count(0),
      slice_edge_use_count(0),
      slice_adjacency_count(0)
{
}

Step6TrimSelectState::Step6TrimSelectState()
    : input_step5(nullptr)
{
}

Step6TrimSelectResult::Step6TrimSelectResult()
    : ok(FALSE)
{
}

logical RunStep6TrimSelect(
    const Step5MidPatchState& step5,
    const Step6TrimSelectOptions& options,
    DiagnosticSink* diagnostics,
    Step6TrimSelectResult& result)
{
    result = Step6TrimSelectResult();
    result.state.input_step5 = &step5;
    result.state.options_snapshot = options;

    const Step4RelationState* step4 = step5.input_step4;
    const Step3PairState* step3 = step4 == nullptr ? nullptr : step4->input_step3;
    const Step2GroupState* step2 = step3 == nullptr ? nullptr : step3->input_step2;
    const Step1FaceAnalyzeState* step1 = step2 == nullptr ? nullptr : step2->input_step1;
    BODY* source_body = step1 == nullptr ? nullptr : step1->input_body;

    if (step4 == nullptr || step3 == nullptr || step2 == nullptr || step1 == nullptr || source_body == nullptr)
    {
        StructuredEvent event = Step6EventTemplate();
        event.AddTag("invalid_input");
        event.SetProperty("reason", "missing_upstream_state");
        EmitEvent(diagnostics, event);
        return FALSE;
    }

    const int pair_count = (int)step3->pairs.pairs.size();
    result.state.stats.input_patch_count = (int)step5.patches.patches.size();
    result.state.stats.input_pair_count = pair_count;
    EmitStartEvent(diagnostics, result.state.stats.input_patch_count, pair_count);

    std::vector<FACE*> pair_raw_face(pair_count, nullptr);
    std::vector<BODY*> pair_seed_bodies(pair_count, nullptr);
    std::vector<double> pair_seed_ext(pair_count, 0.0);
    std::vector<int> pair_to_patch(pair_count, -1);
    std::vector<BODY*> ext_mid_bodies;
    std::map<FACE*, BODY*> unique_mid_seed_by_face;
    std::map<FACE*, double> unique_mid_ext_by_face;

    int patch_i = 0;
    for (patch_i = 0; patch_i < (int)step5.patches.patches.size(); ++patch_i)
    {
        const MidPatchRecord& patch = step5.patches.patches[patch_i];
        if (patch.face == nullptr)
            continue;

        std::vector<int> members = patch.member_pairs;
        if (members.empty() && patch.source_pair_id >= 0)
            members.push_back(patch.source_pair_id);
        if (members.empty())
            continue;

        BODY* seed_body = nullptr;
        double ext_dist = 0.0;
        std::map<FACE*, BODY*>::const_iterator reuse = unique_mid_seed_by_face.find(patch.face);
        if (reuse != unique_mid_seed_by_face.end())
        {
            seed_body = reuse->second;
            std::map<FACE*, double>::const_iterator eit = unique_mid_ext_by_face.find(patch.face);
            if (eit != unique_mid_ext_by_face.end())
                ext_dist = eit->second;
            ++result.state.stats.mid_seed_reused_count;
        }
        else
        {
            ++result.state.stats.mid_seed_try_count;
            const double area = (std::max)(1e-12, FaceAreaEstimate(patch.face));
            ext_dist = (std::max)(options.mid_extend_min, options.mid_extend_scale * std::sqrt(area));
            double t_copy = 0.0, t_sheet = 0.0, t_get_edges = 0.0, t_extend_api = 0.0;
            int edge_count = 0;
            logical detach_used = FALSE;
            logical extend_ok = FALSE;
            if (BuildSeedBodyFromFace(
                    patch.face,
                    ext_dist,
                    options,
                    options.mid_extend_scale,
                    options.enable_body_prewall_extend,
                    seed_body,
                    t_copy,
                    t_sheet,
                    t_get_edges,
                    t_extend_api,
                    edge_count,
                    detach_used,
                    extend_ok) &&
                seed_body != nullptr)
            {
                ++result.state.stats.mid_seed_ok_count;
                unique_mid_seed_by_face[patch.face] = seed_body;
                unique_mid_ext_by_face[patch.face] = ext_dist;
                ext_mid_bodies.push_back(seed_body);
            }

            if (options.emit_selection_events != FALSE)
            {
                StructuredEvent event = Step6DetailEvent();
                event.AddTag("extend");
                event.AddTag("mid");
                event.SetProperty("source_patch_id", IntText(patch.patch_id));
                event.SetProperty("area", DoubleText(area));
                event.SetProperty("extend_distance", DoubleText(ext_dist));
                event.SetProperty("ok", BoolText(seed_body != nullptr ? TRUE : FALSE));
                event.SetProperty("detach_used", BoolText(detach_used));
                event.SetProperty("extend_ok", BoolText(extend_ok));
                event.SetProperty("edge_count", IntText(edge_count));
                EmitEvent(diagnostics, event);
            }
        }

        int mi = 0;
        for (mi = 0; mi < (int)members.size(); ++mi)
        {
            const int pid = members[mi];
            if (pid < 0 || pid >= pair_count)
                continue;
            pair_raw_face[pid] = patch.face;
            pair_seed_bodies[pid] = seed_body;
            pair_seed_ext[pid] = ext_dist;
            pair_to_patch[pid] = patch.patch_id;
        }
    }

    {
        std::map<std::string, std::string> props;
        props["try_count"] = IntText(result.state.stats.mid_seed_try_count);
        props["ok_count"] = IntText(result.state.stats.mid_seed_ok_count);
        props["reused_count"] = IntText(result.state.stats.mid_seed_reused_count);
        EmitStageSummary(diagnostics, "extend", props, "mid");
    }
    (void)EmitBodyDebugSatIfEnabled(diagnostics, kStep6ExtendedMidFaces, ext_mid_bodies);

    std::map<int, std::vector<BODY*> > wall_seed_buckets;
    std::vector<BODY*> ext_wall_bodies;
    int wi = 0;
    for (wi = 0; wi < (int)step3->walls.walls.size(); ++wi)
    {
        const WallRecord& wall = step3->walls.walls[wi];
        std::vector<FACE*> wall_faces;
        if (!GroupFacesById(*step2, wall.group_a, wall_faces))
            continue;
        int fi = 0;
        for (fi = 0; fi < (int)wall_faces.size(); ++fi)
        {
            FACE* wf = wall_faces[fi];
            if (wf == nullptr)
                continue;
            ++result.state.stats.wall_seed_try_count;
            const double area = (std::max)(1e-12, FaceAreaEstimate(wf));
            const double ext_dist = (std::max)(options.wall_extend_min, options.wall_extend_scale * std::sqrt(area));
            BODY* wb = nullptr;
            double t_copy = 0.0, t_sheet = 0.0, t_get_edges = 0.0, t_extend_api = 0.0;
            int edge_count = 0;
            logical detach_used = FALSE;
            logical extend_ok = FALSE;
            if (BuildSeedBodyFromFace(
                    wf,
                    ext_dist,
                    options,
                    options.wall_extend_scale,
                    options.enable_body_prewall_extend,
                    wb,
                    t_copy,
                    t_sheet,
                    t_get_edges,
                    t_extend_api,
                    edge_count,
                    detach_used,
                    extend_ok) &&
                wb != nullptr)
            {
                ++result.state.stats.wall_seed_ok_count;
                AddPairWallToolBuckets(wall_seed_buckets, wall, wb);
                ext_wall_bodies.push_back(wb);
            }
        }
    }

    for (wi = 0; wi < (int)step4->walls.walls.size(); ++wi)
    {
        const WallRecord& wall = step4->walls.walls[wi];
        if (wall.body == nullptr)
            continue;
        BODY* wb = nullptr;
        if (CopyBodyEntity(wall.body, wb) && wb != nullptr)
        {
            AddPairWallToolBuckets(wall_seed_buckets, wall, wb);
            ext_wall_bodies.push_back(wb);
            ++result.state.stats.virtual_wall_seed_count;
        }
    }

    {
        std::map<std::string, std::string> props;
        props["try_count"] = IntText(result.state.stats.wall_seed_try_count);
        props["ok_count"] = IntText(result.state.stats.wall_seed_ok_count);
        props["virtual_wall_seed_count"] = IntText(result.state.stats.virtual_wall_seed_count);
        EmitStageSummary(diagnostics, "extend", props, "wall");
    }
    (void)EmitBodyDebugSatIfEnabled(diagnostics, kStep6ExtendedWallFaces, ext_wall_bodies);

    std::vector< std::vector<int> > pair_neighbors(pair_count);
    std::vector< std::vector<int> > pair_mm1_dir_tools(pair_count);
    std::vector< std::vector<int> > pair_mm1_split_tools(pair_count);
    std::set<std::pair<int, int> > raw_pair_adjacency_candidates;
    int mm1_split_tool_edge_count = 0;
    int ri = 0;
    for (ri = 0; ri < (int)step4->pair_relations.relations.size(); ++ri)
    {
        const PairRelationRecord& rel = step4->pair_relations.relations[ri];
        if (rel.pair_a < 0 || rel.pair_b < 0 || rel.pair_a >= pair_count || rel.pair_b >= pair_count)
            continue;
        AddRawPairAdjacencyCandidate(raw_pair_adjacency_candidates, rel.pair_a, rel.pair_b);
        if (rel.pair_mode == "MM2")
        {
            ++result.state.stats.mm2_skipped_count;
            continue;
        }
        if (rel.pair_mode != "MM1")
            continue;

        AddUniqueInt(pair_mm1_split_tools[rel.pair_a], rel.pair_b);
        AddUniqueInt(pair_mm1_split_tools[rel.pair_b], rel.pair_a);
        mm1_split_tool_edge_count += 2;

        const logical a_side_a = (rel.hits_aa > 0 || rel.hits_ab > 0) ? TRUE : FALSE;
        const logical a_side_b = (rel.hits_ba > 0 || rel.hits_bb > 0) ? TRUE : FALSE;
        const logical b_side_a = (rel.hits_aa > 0 || rel.hits_ba > 0) ? TRUE : FALSE;
        const logical b_side_b = (rel.hits_ab > 0 || rel.hits_bb > 0) ? TRUE : FALSE;
        const int a_side_cnt = (a_side_a ? 1 : 0) + (a_side_b ? 1 : 0);
        const int b_side_cnt = (b_side_a ? 1 : 0) + (b_side_b ? 1 : 0);

        if (a_side_cnt == 2 && b_side_cnt == 1)
        {
            AddUniqueInt(pair_mm1_dir_tools[rel.pair_a], rel.pair_b);
            ++result.state.stats.mm1_directional_edge_count;
        }
        else if (b_side_cnt == 2 && a_side_cnt == 1)
        {
            AddUniqueInt(pair_mm1_dir_tools[rel.pair_b], rel.pair_a);
            ++result.state.stats.mm1_directional_edge_count;
        }
        else
        {
            AddUniqueInt(pair_neighbors[rel.pair_a], rel.pair_b);
            AddUniqueInt(pair_neighbors[rel.pair_b], rel.pair_a);
            result.state.stats.mm1_bidirectional_edge_count += 2;
        }
    }

    std::map<int, std::vector<int> > pair_wall_groups;
    for (ri = 0; ri < (int)step4->pair_wall_relations.relations.size(); ++ri)
    {
        const PairWallRelationRecord& rel = step4->pair_wall_relations.relations[ri];
        if (rel.pair_id < 0 || rel.pair_id >= pair_count)
            continue;
        AddUniqueInt(pair_wall_groups[rel.pair_id], rel.wall_group_id);
    }

    {
        std::map<std::string, std::string> props;
        props["mm1_bidirectional_edge_count"] = IntText(result.state.stats.mm1_bidirectional_edge_count);
        props["mm1_directional_edge_count"] = IntText(result.state.stats.mm1_directional_edge_count);
        props["mm1_split_tool_edge_count"] = IntText(mm1_split_tool_edge_count);
        props["mm2_skipped_count"] = IntText(result.state.stats.mm2_skipped_count);
        props["raw_pair_candidate_count"] = IntText((int)raw_pair_adjacency_candidates.size());
        props["pair_wall_relation_count"] = IntText((int)step4->pair_wall_relations.relations.size());
        EmitStageSummary(diagnostics, "trim-graph", props);
    }

    std::vector< std::vector<FACE*> > pair_split_faces(pair_count);
    std::map<BODY*, std::vector<int> > seed_pair_groups;
    int pidx = 0;
    for (pidx = 0; pidx < pair_count; ++pidx)
    {
        BODY* seed = pair_seed_bodies[pidx];
        if (seed != nullptr)
            seed_pair_groups[seed].push_back(pidx);
    }
    result.state.stats.trim_seed_group_count = (int)seed_pair_groups.size();

    std::map<BODY*, int> seed_rep_pair;
    std::set<FACE*> preselect_seen;
    std::map<BODY*, std::vector<int> >::const_iterator it_group = seed_pair_groups.begin();
    for (; it_group != seed_pair_groups.end(); ++it_group)
    {
        BODY* seed = it_group->first;
        const std::vector<int>& members = it_group->second;
        if (seed == nullptr || members.empty())
            continue;
        const int rep_pair = members[0];
        seed_rep_pair[seed] = rep_pair;

        BODY* blank = nullptr;
        if (!CopyBodyEntity(seed, blank) || blank == nullptr)
            continue;

        int mi = 0;
        for (mi = 0; mi < (int)members.size(); ++mi)
        {
            const int pid = members[mi];
            if (pid < 0 || pid >= pair_count)
                continue;

            int ni = 0;
            for (ni = 0; ni < (int)pair_mm1_split_tools[pid].size(); ++ni)
            {
                const int qidx = pair_mm1_split_tools[pid][ni];
                if (qidx < 0 || qidx >= pair_count)
                    continue;
                BODY* tool = pair_seed_bodies[qidx];
                if (tool == nullptr || tool == seed)
                    continue;
                int edges = 0;
                logical copy_ok = FALSE;
                ++result.state.stats.trim_tool_try_count;
                const logical ok_trim = ImprintToolSeedCopyOnBlankBody(tool, blank, edges, copy_ok);
                if (ok_trim != FALSE)
                    ++result.state.stats.trim_tool_ok_count;
            }

            std::map<int, std::vector<int> >::const_iterator wit = pair_wall_groups.find(pid);
            if (wit != pair_wall_groups.end())
            {
                int wg_i = 0;
                for (wg_i = 0; wg_i < (int)wit->second.size(); ++wg_i)
                {
                    const int wall_group_id = wit->second[wg_i];
                    std::map<int, std::vector<BODY*> >::const_iterator bit = wall_seed_buckets.find(wall_group_id);
                    if (bit == wall_seed_buckets.end())
                        continue;
                    int bi = 0;
                    for (bi = 0; bi < (int)bit->second.size(); ++bi)
                    {
                        BODY* tool = bit->second[bi];
                        if (tool == nullptr)
                            continue;
                        int edges = 0;
                        logical copy_ok = FALSE;
                        ++result.state.stats.trim_tool_try_count;
                        const logical ok_trim = ImprintToolSeedCopyOnBlankBody(tool, blank, edges, copy_ok);
                        if (ok_trim != FALSE)
                            ++result.state.stats.trim_tool_ok_count;
                    }
                }
            }

            if (options.enable_source_trim != FALSE)
            {
                int edges = 0;
                logical copy_ok = FALSE;
                ++result.state.stats.trim_tool_try_count;
                const logical ok_trim = ImprintToolSeedCopyOnBlankBody(source_body, blank, edges, copy_ok);
                if (ok_trim != FALSE)
                    ++result.state.stats.trim_tool_ok_count;
            }
        }

        std::vector<FACE*> fs;
        if (CollectFacesFromBodyLocal(blank, fs))
        {
            for (mi = 0; mi < (int)members.size(); ++mi)
            {
                const int pid = members[mi];
                if (pid >= 0 && pid < pair_count)
                    pair_split_faces[pid] = fs;
            }

            int sf = 0;
            for (sf = 0; sf < (int)fs.size(); ++sf)
            {
                FACE* srcf = fs[sf];
                if (srcf == nullptr || !preselect_seen.insert(srcf).second)
                    continue;
                FACE* detached = nullptr;
                if (!CopyFaceDetached(srcf, detached) || detached == nullptr)
                    continue;
                Step6SliceRecord slice;
                slice.slice_id = (int)result.state.slices.slices.size();
                slice.face = detached;
                slice.source_pair_id = rep_pair;
                slice.source_seed_rep_pair = rep_pair;
                slice.source_patch_id = (rep_pair >= 0 && rep_pair < pair_count) ? pair_to_patch[rep_pair] : -1;
                slice.area = FaceAreaEstimate(srcf);
                result.state.slices.slices.push_back(slice);
            }
        }
    }

    result.state.stats.preselect_split_face_count = (int)result.state.slices.slices.size();
    result.state.stats.slice_count = (int)result.state.slices.slices.size();
    {
        std::map<std::string, std::string> props;
        props["trim_seed_group_count"] = IntText(result.state.stats.trim_seed_group_count);
        props["trim_tool_try_count"] = IntText(result.state.stats.trim_tool_try_count);
        props["trim_tool_ok_count"] = IntText(result.state.stats.trim_tool_ok_count);
        props["preselect_split_face_count"] = IntText(result.state.stats.preselect_split_face_count);
        EmitStageSummary(diagnostics, "split", props);
    }

    std::vector<FACE*> preselect_faces;
    int si = 0;
    for (si = 0; si < (int)result.state.slices.slices.size(); ++si)
        preselect_faces.push_back(result.state.slices.slices[si].face);
    (void)EmitFaceSheetBodiesDebugSatIfEnabled(diagnostics, kStep6PreselectSplitFaces, preselect_faces);

    std::set<FACE*> selected_src_seen;
    std::vector<FACE*> selected_faces;
    for (pidx = 0; pidx < pair_count; ++pidx)
    {
        const std::vector<FACE*>& fs = pair_split_faces[pidx];
        if (fs.empty())
            continue;
        std::string reason;
        std::vector<FACE*> picked;
        PickFacesByGeometryRule(fs, *step2, step3->pairs.pairs[pidx], options, picked, reason);
        int k = 0;
        for (k = 0; k < (int)picked.size(); ++k)
        {
            FACE* srcf = picked[k];
            if (srcf == nullptr)
                continue;
            if (!selected_src_seen.insert(srcf).second)
            {
                ++result.state.stats.selected_dedup_skip_count;
                continue;
            }
            FACE* detached = nullptr;
            if (!CopyFaceDetached(srcf, detached) || detached == nullptr)
            {
                ++result.state.stats.selected_copy_fail_count;
                continue;
            }
            TrimSelectionRecord record;
            record.selection_id = (int)result.state.selections.selections.size();
            record.source_patch_id = (pidx >= 0 && pidx < pair_count) ? pair_to_patch[pidx] : -1;
            record.source_pair_id = pidx;
            std::map<BODY*, int>::const_iterator rep_it = seed_rep_pair.find(pair_seed_bodies[pidx]);
            record.source_seed_rep_pair = rep_it != seed_rep_pair.end() ? rep_it->second : pidx;
            record.source_split_index = k;
            record.source_split_face = srcf;
            record.selected_face = detached;
            record.area = FaceAreaEstimate(srcf);
            record.reason = reason;
            result.state.selections.selections.push_back(record);
            selected_faces.push_back(detached);

            if (options.emit_selection_events != FALSE)
            {
                StructuredEvent event = Step6SingleSummaryEvent();
                event.AddTag("selection");
                event.SetProperty("selection_id", IntText(record.selection_id));
                event.SetProperty("source_patch_id", IntText(record.source_patch_id));
                event.SetProperty("source_pair_id", IntText(record.source_pair_id));
                event.SetProperty("source_seed_rep_pair", IntText(record.source_seed_rep_pair));
                event.SetProperty("area", DoubleText(record.area));
                event.SetProperty("reason", record.reason);
                EmitEvent(diagnostics, event);
            }
        }
    }

    result.state.stats.selected_count = (int)result.state.selections.selections.size();
    result.state.stats.rejected_count =
        (std::max)(0, result.state.stats.preselect_split_face_count - result.state.stats.selected_count);
    BuildStep7StitchInputFromSelections(
        step5,
        pair_to_patch,
        result.state,
        diagnostics,
        options.emit_selection_events);
    {
        std::map<std::string, std::string> props;
        props["selected_count"] = IntText(result.state.stats.selected_count);
        props["selected_face_count"] = IntText(result.state.stats.selected_count);
        props["rejected_count"] = IntText(result.state.stats.rejected_count);
        props["selected_copy_fail_count"] = IntText(result.state.stats.selected_copy_fail_count);
        props["selected_dedup_skip_count"] = IntText(result.state.stats.selected_dedup_skip_count);
        EmitStageSummary(diagnostics, "select", props);
    }

    if (options.build_slice_adjacency != FALSE)
        BuildSelectedSliceAdjacency(result, raw_pair_adjacency_candidates, options, diagnostics);

    ColorSelectedFacesAndEmitMap(diagnostics, result.state.selections);
    (void)EmitFaceSheetBodiesDebugSatIfEnabled(diagnostics, kStep6SelectedFaces, selected_faces);

    result.ok = result.state.stats.selected_count > 0 ? TRUE : FALSE;
    EmitFinishEvent(diagnostics, result.state.stats, result.ok);
    return result.ok;
}
} // namespace midsurface_new
