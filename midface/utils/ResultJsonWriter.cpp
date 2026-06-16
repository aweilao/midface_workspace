#include "utils/ResultJsonWriter.hpp"

#include "utils/JsonUtils.hpp"
#include "utils/PathUtils.hpp"

#include <stdio.h>

namespace midsurface_new
{
namespace
{
JsonValue JsonBoolValue(logical value)
{
    return value != FALSE;
}

JsonValue JsonVector(const SPAunit_vector& value)
{
    JsonValue out = JsonValue::array();
    out.push_back(value.x());
    out.push_back(value.y());
    out.push_back(value.z());
    return out;
}

JsonValue JsonIntList(const std::vector<int>& values)
{
    JsonValue out = JsonValue::array();
    int i = 0;
    for (i = 0; i < (int)values.size(); ++i)
        out.push_back(values[i]);
    return out;
}

JsonValue FaceRecordJson(const FaceRecord& record)
{
    JsonValue out = JsonValue::object();
    out["face_id"] = record.face_id;
    out["face_type"] = record.face_type;
    out["color_rgb"] = JsonValue::array({ record.color_r, record.color_g, record.color_b });
    out["edge_count"] = record.edge_count;
    out["adjacency_degree"] = record.adjacency_degree;
    out["valid"] = JsonBoolValue(record.valid);
    out["invalid_reason"] = record.invalid_reason;
    out["has_representative_point"] = JsonBoolValue(record.has_representative_point);
    if (record.has_representative_point != FALSE)
        out["representative_point"] = JsonPoint(record.representative_point);
    out["has_representative_normal"] = JsonBoolValue(record.has_representative_normal);
    if (record.has_representative_normal != FALSE)
        out["representative_normal"] = JsonVector(record.representative_normal);
    out["has_area_proxy"] = JsonBoolValue(record.has_area_proxy);
    if (record.has_area_proxy != FALSE)
        out["area_proxy"] = record.area_proxy;
    out["has_edge_lengths"] = JsonBoolValue(record.has_edge_lengths);
    if (record.has_edge_lengths != FALSE)
    {
        out["edge_min"] = record.edge_min;
        out["edge_max"] = record.edge_max;
    }
    out["sample_summary"] = record.sample_summary;
    return out;
}

JsonValue FaceAdjacencyJson(const FaceAdjacency& edge)
{
    JsonValue out = JsonValue::object();
    out["face_a"] = edge.face_a;
    out["face_b"] = edge.face_b;
    out["shared_edge_count"] = edge.shared_edge_count;
    out["relation"] = edge.relation;
    return out;
}

JsonValue ModelScaleContextJson(const ModelScaleContext& scale)
{
    JsonValue out = JsonValue::object();
    out["valid"] = JsonBoolValue(scale.valid);
    out["reference_length"] = scale.reference_length;
    out["distance_unit"] = scale.distance_unit;
    out["normalized_reference"] = scale.normalized_reference;
    out["top_edge_count_requested"] = scale.top_edge_count_requested;
    out["top_edge_count_used"] = scale.top_edge_count_used;
    out["valid_edge_count"] = scale.valid_edge_count;
    out["min_valid_edge_length"] = scale.min_valid_edge_length;
    out["longest_edge_length"] = scale.longest_edge_length;
    out["shortest_used_edge_length"] = scale.shortest_used_edge_length;
    return out;
}

JsonValue GroupRecordJson(const GroupRecord& record)
{
    JsonValue out = JsonValue::object();
    out["group_id"] = record.group_id;
    out["face_ids"] = JsonIntList(record.face_ids);
    out["face_count"] = (int)record.face_ids.size();
    out["type"] = record.type;
    out["surface_ids"] = JsonIntList(record.surface_ids);
    out["seed_point"] = JsonPoint(record.seed_point);
    out["seed_normal"] = JsonVector(record.seed_normal);
    out["area_sum"] = record.area_sum;
    out["local_scale"] = record.local_scale;
    out["source_rules"] = record.source_rules;
    out["confidence"] = record.confidence;
    return out;
}

JsonValue PairRecordJson(const PairRecord& record)
{
    JsonValue out = JsonValue::object();
    out["pair_id"] = record.pair_id;
    out["group_a"] = record.group_a;
    out["group_b"] = record.group_b;
    out["thickness"] = record.thickness;
    out["coverage"] = record.coverage;
    out["score"] = record.score;
    out["facing_score"] = record.facing_score;
    out["pass_ratio"] = record.pass_ratio;
    out["pass_balance"] = record.pass_balance;
    out["point_a"] = JsonPoint(record.point_a);
    out["point_b"] = JsonPoint(record.point_b);
    out["pair_direction"] = JsonVector(record.pair_direction);
    out["uncertain"] = JsonBoolValue(record.uncertain);
    out["rib_candidate"] = JsonBoolValue(record.rib_candidate);
    out["reason"] = record.reason;
    return out;
}

JsonValue WallRecordJson(const WallRecord& record)
{
    JsonValue out = JsonValue::object();
    out["wall_id"] = record.wall_id;
    out["source_face_ids"] = JsonIntList(record.source_face_ids);
    out["source"] = record.source;
    out["pair_a"] = record.pair_a;
    out["pair_b"] = record.pair_b;
    out["group_a"] = record.group_a;
    out["group_b"] = record.group_b;
    out["bucket_slot"] = record.bucket_slot;
    out["coedge_len"] = record.coedge_len;
    out["half_profile"] = record.half_profile;
    out["hit_normal_cos_abs"] = record.hit_normal_cos_abs;
    out["has_body"] = JsonBoolValue(record.body != nullptr ? TRUE : FALSE);
    return out;
}

JsonValue PairRelationJson(const PairRelationRecord& record)
{
    JsonValue out = JsonValue::object();
    out["relation_id"] = record.relation_id;
    out["pair_a"] = record.pair_a;
    out["pair_b"] = record.pair_b;
    out["relation_type"] = record.relation_type;
    out["pair_mode"] = record.pair_mode;
    out["classify_source"] = record.classify_source;
    out["source"] = record.source;
    out["wall_mode"] = record.wall_mode;
    out["hits_aa"] = record.hits_aa;
    out["hits_ab"] = record.hits_ab;
    out["hits_ba"] = record.hits_ba;
    out["hits_bb"] = record.hits_bb;
    out["total_hits"] = record.total_hits;
    out["hit_normal_cos_abs"] = record.hit_normal_cos_abs;
    out["hit_normal_angle_deg"] = record.hit_normal_angle_deg;
    return out;
}

JsonValue PairWallRelationJson(const PairWallRelationRecord& record)
{
    JsonValue out = JsonValue::object();
    out["relation_id"] = record.relation_id;
    out["pair_id"] = record.pair_id;
    out["wall_group_id"] = record.wall_group_id;
    out["hit_group_a"] = record.hit_group_a;
    out["hit_group_b"] = record.hit_group_b;
    out["wall_mode"] = record.wall_mode;
    return out;
}

JsonValue MidPatchRecordJson(const MidPatchRecord& record)
{
    JsonValue out = JsonValue::object();
    out["patch_id"] = record.patch_id;
    out["source_pair_id"] = record.source_pair_id;
    out["group_a"] = record.group_a;
    out["group_b"] = record.group_b;
    out["anchor_group"] = record.anchor_group;
    out["component_id"] = record.component_id;
    out["merged_unit"] = JsonBoolValue(record.merged_unit);
    out["member_pairs"] = JsonIntList(record.member_pairs);
    out["kind"] = record.kind;
    out["type_a"] = record.type_a;
    out["type_b"] = record.type_b;
    out["side_type_a"] = record.side_type_a;
    out["side_type_b"] = record.side_type_b;
    out["valid"] = JsonBoolValue(record.valid);
    out["is_rib"] = JsonBoolValue(record.is_rib);
    out["is_uncertain"] = JsonBoolValue(record.is_uncertain);
    out["thickness"] = record.thickness;
    out["point_a"] = JsonPoint(record.point_a);
    out["point_b"] = JsonPoint(record.point_b);
    out["point_mid"] = JsonPoint(record.point_mid);
    out["normal_mid"] = JsonVector(record.normal_mid);
    out["has_face"] = JsonBoolValue(record.face != nullptr ? TRUE : FALSE);
    out["build_method"] = record.build_method;
    out["fail_reason"] = record.fail_reason;
    return out;
}

JsonValue MidPatchJunctionJson(const MidPatchJunctionRecord& record)
{
    JsonValue out = JsonValue::object();
    out["junction_id"] = record.junction_id;
    out["pair_i"] = record.pair_i;
    out["pair_j"] = record.pair_j;
    out["link_type"] = record.link_type;
    out["conn_class"] = record.conn_class;
    out["pair_mode"] = record.pair_mode;
    out["total_hits"] = record.total_hits;
    return out;
}

JsonValue TrimSliceRecordJson(const Step6SliceRecord& record)
{
    JsonValue out = JsonValue::object();
    out["slice_id"] = record.slice_id;
    out["selected_index"] = record.selected_index;
    out["source_patch_id"] = record.source_patch_id;
    out["source_pair_id"] = record.source_pair_id;
    out["source_seed_rep_pair"] = record.source_seed_rep_pair;
    out["area"] = record.area;
    out["has_face"] = JsonBoolValue(record.face != nullptr ? TRUE : FALSE);
    return out;
}

JsonValue TrimSelectionRecordJson(const TrimSelectionRecord& record)
{
    JsonValue out = JsonValue::object();
    out["selection_id"] = record.selection_id;
    out["source_patch_id"] = record.source_patch_id;
    out["source_pair_id"] = record.source_pair_id;
    out["source_seed_rep_pair"] = record.source_seed_rep_pair;
    out["source_split_index"] = record.source_split_index;
    out["area"] = record.area;
    out["reason"] = record.reason;
    out["has_source_split_face"] = JsonBoolValue(record.source_split_face != nullptr ? TRUE : FALSE);
    out["has_selected_face"] = JsonBoolValue(record.selected_face != nullptr ? TRUE : FALSE);
    return out;
}

JsonValue TrimSliceAdjacencyJson(const Step6SliceAdjacency& record)
{
    JsonValue out = JsonValue::object();
    out["adjacency_id"] = record.adjacency_id;
    out["slice_a"] = record.slice_a;
    out["slice_b"] = record.slice_b;
    out["a"] = record.slice_a;
    out["b"] = record.slice_b;
    out["imprint_edge_id"] = record.imprint_edge_id;
    out["shared_length"] = record.shared_length;
    return out;
}

JsonValue Step7SplitRawFaceJson(const Step7SplitRawFaceRecord& record)
{
    JsonValue out = JsonValue::object();
    out["split_id"] = record.split_id;
    out["raw_face_id"] = record.raw_face_id;
    out["has_split_face"] = JsonBoolValue(record.split_face != nullptr ? TRUE : FALSE);
    return out;
}

JsonValue Step7RawFaceRelationJson(const Step7RawFaceRelationRecord& record)
{
    JsonValue out = JsonValue::object();
    out["relation_id"] = record.relation_id;
    out["raw_face_a"] = record.raw_face_a;
    out["raw_face_b"] = record.raw_face_b;
    return out;
}

JsonValue Step7FaceRefJson(const Step7FaceRef& record)
{
    JsonValue out = JsonValue::object();
    out["face_id"] = record.face_id;
    out["has_face"] = JsonBoolValue(record.face != nullptr ? TRUE : FALSE);
    return out;
}

JsonValue Step7GroupRefJson(const Step7GroupRef& record)
{
    JsonValue out = JsonValue::object();
    out["group_id"] = record.group_id;

    JsonValue faces = JsonValue::array();
    int i = 0;
    for (i = 0; i < (int)record.faces.size(); ++i)
        faces.push_back(Step7FaceRefJson(record.faces[i]));
    out["faces"] = faces;
    return out;
}

JsonValue Step7PairRefJson(const Step7PairRef& record)
{
    JsonValue out = JsonValue::object();
    out["pair_id"] = record.pair_id;
    out["group_a"] = Step7GroupRefJson(record.group_a);
    out["group_b"] = Step7GroupRefJson(record.group_b);
    return out;
}

JsonValue Step7RawFaceRefJson(const Step7RawFaceRef& record)
{
    JsonValue out = JsonValue::object();
    out["raw_face_id"] = record.raw_face_id;
    out["has_raw_face"] = JsonBoolValue(record.raw_face != nullptr ? TRUE : FALSE);
    out["pair"] = Step7PairRefJson(record.pair);
    return out;
}

JsonValue IntVectorMapJson(const std::map<int, std::vector<int> >& values)
{
    JsonValue out = JsonValue::object();
    std::map<int, std::vector<int> >::const_iterator it = values.begin();
    for (; it != values.end(); ++it)
        out[std::to_string(it->first)] = JsonIntList(it->second);
    return out;
}

JsonValue IntIntMapJson(const std::map<int, int>& values)
{
    JsonValue out = JsonValue::object();
    std::map<int, int>::const_iterator it = values.begin();
    for (; it != values.end(); ++it)
        out[std::to_string(it->first)] = it->second;
    return out;
}

JsonValue Step1Json(const Step1FaceAnalyzeResult& result)
{
    JsonValue out = JsonValue::object();
    out["ok"] = JsonBoolValue(result.ok);
    out["stats"] = {
        { "face_count", result.state.stats.face_count },
        { "valid_face_count", result.state.stats.valid_face_count },
        { "invalid_face_count", result.state.stats.invalid_face_count },
        { "adjacency_count", result.state.stats.adjacency_count }
    };

    JsonValue faces = JsonValue::array();
    int i = 0;
    for (i = 0; i < (int)result.state.faces.records.size(); ++i)
        faces.push_back(FaceRecordJson(result.state.faces.records[i]));
    out["faces"] = faces;

    JsonValue adjacency = JsonValue::array();
    for (i = 0; i < (int)result.state.adjacency.edges.size(); ++i)
        adjacency.push_back(FaceAdjacencyJson(result.state.adjacency.edges[i]));
    out["adjacency"] = adjacency;
    out["model_scale"] = ModelScaleContextJson(result.state.model_scale);
    return out;
}

JsonValue Step2Json(const Step2GroupResult& result)
{
    JsonValue out = JsonValue::object();
    out["ok"] = JsonBoolValue(result.ok);
    out["stats"] = {
        { "candidate_count", result.state.stats.candidate_count },
        { "accepted_count", result.state.stats.accepted_count },
        { "rejected_count", result.state.stats.rejected_count },
        { "group_count", result.state.stats.group_count },
        { "valid_face_count", result.state.stats.valid_face_count },
        { "surface_prefilter_pass_count", result.state.stats.surface_prefilter_pass_count },
        { "surface_prefilter_reject_count", result.state.stats.surface_prefilter_reject_count },
        { "sample_refine_pass_count", result.state.stats.sample_refine_pass_count },
        { "sample_refine_reject_count", result.state.stats.sample_refine_reject_count },
        { "single_face_group_count", result.state.stats.single_face_group_count }
    };

    JsonValue groups = JsonValue::array();
    int i = 0;
    for (i = 0; i < (int)result.state.groups.groups.size(); ++i)
        groups.push_back(GroupRecordJson(result.state.groups.groups[i]));
    out["groups"] = groups;
    out["face_to_group"] = IntIntMapJson(result.state.face_to_group.face_to_group);

    JsonValue reject = JsonValue::object();
    std::map<std::string, int>::const_iterator it = result.state.reject_reason_stats.begin();
    for (; it != result.state.reject_reason_stats.end(); ++it)
        reject[it->first] = it->second;
    out["reject_reason_stats"] = reject;
    return out;
}

JsonValue Step3Json(const Step3PairResult& result)
{
    JsonValue out = JsonValue::object();
    out["ok"] = JsonBoolValue(result.ok);
    out["stats"] = {
        { "candidate_count", result.state.stats.candidate_count },
        { "accepted_count", result.state.stats.accepted_count },
        { "rejected_count", result.state.stats.rejected_count },
        { "pair_count", result.state.stats.pair_count },
        { "coarse_prefilter_pass_count", result.state.stats.coarse_prefilter_pass_count },
        { "coarse_prefilter_reject_count", result.state.stats.coarse_prefilter_reject_count },
        { "surface_prefilter_pass_count", result.state.stats.surface_prefilter_pass_count },
        { "surface_prefilter_reject_count", result.state.stats.surface_prefilter_reject_count },
        { "refine_pass_count", result.state.stats.refine_pass_count },
        { "refine_reject_count", result.state.stats.refine_reject_count },
        { "thickness_filter_dropped_count", result.state.stats.thickness_filter_dropped_count },
        { "rib_candidate_count", result.state.stats.rib_candidate_count },
        { "uncertain_count", result.state.stats.uncertain_count },
        { "wall_count", result.state.stats.wall_count },
        { "pair_group_count", result.state.stats.pair_group_count },
        { "wall_group_count", result.state.stats.wall_group_count }
    };

    JsonValue pairs = JsonValue::array();
    int i = 0;
    for (i = 0; i < (int)result.state.pairs.pairs.size(); ++i)
        pairs.push_back(PairRecordJson(result.state.pairs.pairs[i]));
    out["pairs"] = pairs;

    JsonValue walls = JsonValue::array();
    for (i = 0; i < (int)result.state.walls.walls.size(); ++i)
        walls.push_back(WallRecordJson(result.state.walls.walls[i]));
    out["walls"] = walls;
    out["group_to_pairs"] = IntVectorMapJson(result.state.group_to_pairs.group_to_pairs);
    out["group_to_walls"] = IntIntMapJson(result.state.group_to_walls.group_to_wall);
    return out;
}

JsonValue Step4Json(const Step4RelationResult& result)
{
    JsonValue out = JsonValue::object();
    out["ok"] = JsonBoolValue(result.ok);
    out["stats"] = {
        { "relation_count", result.state.stats.relation_count },
        { "wall_count", result.state.stats.wall_count },
        { "group_adjacency_count", result.state.stats.group_adjacency_count },
        { "coedge_total", result.state.stats.coedge_total },
        { "coedge_with_partner", result.state.stats.coedge_with_partner },
        { "coedge_partner_ring_gt2", result.state.stats.coedge_partner_ring_gt2 },
        { "unique_nonmanifold_edges", result.state.stats.unique_nonmanifold_edges },
        { "partner_ring_max", result.state.stats.partner_ring_max },
        { "tiny_faces_found", result.state.stats.tiny_faces_found },
        { "tiny_partner_coedges", result.state.stats.tiny_partner_coedges },
        { "tiny_patch_applied", result.state.stats.tiny_patch_applied },
        { "pair_wall_relation_count", result.state.stats.pair_wall_relation_count },
        { "orphan_wall_group_count", result.state.stats.orphan_wall_group_count },
        { "orphan_bridge_injected_count", result.state.stats.orphan_bridge_injected_count },
        { "direct_pair_link_count", result.state.stats.direct_pair_link_count },
        { "mm1_count", result.state.stats.mm1_count },
        { "mm2_count", result.state.stats.mm2_count },
        { "virtual_wall_try_count", result.state.stats.virtual_wall_try_count },
        { "virtual_wall_ok_count", result.state.stats.virtual_wall_ok_count }
    };

    JsonValue pair_relations = JsonValue::array();
    int i = 0;
    for (i = 0; i < (int)result.state.pair_relations.relations.size(); ++i)
        pair_relations.push_back(PairRelationJson(result.state.pair_relations.relations[i]));
    out["pair_relations"] = pair_relations;

    JsonValue pair_wall_relations = JsonValue::array();
    for (i = 0; i < (int)result.state.pair_wall_relations.relations.size(); ++i)
        pair_wall_relations.push_back(PairWallRelationJson(result.state.pair_wall_relations.relations[i]));
    out["pair_wall_relations"] = pair_wall_relations;

    JsonValue walls = JsonValue::array();
    for (i = 0; i < (int)result.state.walls.walls.size(); ++i)
        walls.push_back(WallRecordJson(result.state.walls.walls[i]));
    out["virtual_walls"] = walls;
    return out;
}

JsonValue Step5Json(const Step5MidPatchResult& result)
{
    JsonValue out = JsonValue::object();
    out["ok"] = JsonBoolValue(result.ok);
    out["stats"] = {
        { "input_pair_count", result.state.stats.input_pair_count },
        { "component_count", result.state.stats.component_count },
        { "requested_patch_count", result.state.stats.requested_patch_count },
        { "built_patch_count", result.state.stats.built_patch_count },
        { "failed_patch_count", result.state.stats.failed_patch_count },
        { "junction_count", result.state.stats.junction_count },
        { "merged_unit_count", result.state.stats.merged_unit_count },
        { "merged_pair_count", result.state.stats.merged_pair_count }
    };

    JsonValue patches = JsonValue::array();
    int i = 0;
    for (i = 0; i < (int)result.state.patches.patches.size(); ++i)
        patches.push_back(MidPatchRecordJson(result.state.patches.patches[i]));
    out["patches"] = patches;

    JsonValue components = JsonValue::array();
    for (i = 0; i < (int)result.state.components.components.size(); ++i)
        components.push_back(JsonIntList(result.state.components.components[i]));
    out["components"] = components;

    JsonValue junctions = JsonValue::array();
    for (i = 0; i < (int)result.state.junctions.junctions.size(); ++i)
        junctions.push_back(MidPatchJunctionJson(result.state.junctions.junctions[i]));
    out["junctions"] = junctions;
    return out;
}

JsonValue Step6Json(const Step6TrimSelectResult& result)
{
    JsonValue out = JsonValue::object();
    out["ok"] = JsonBoolValue(result.ok);
    out["stats"] = {
        { "input_patch_count", result.state.stats.input_patch_count },
        { "input_pair_count", result.state.stats.input_pair_count },
        { "mid_seed_try_count", result.state.stats.mid_seed_try_count },
        { "mid_seed_ok_count", result.state.stats.mid_seed_ok_count },
        { "mid_seed_reused_count", result.state.stats.mid_seed_reused_count },
        { "wall_seed_try_count", result.state.stats.wall_seed_try_count },
        { "wall_seed_ok_count", result.state.stats.wall_seed_ok_count },
        { "virtual_wall_seed_count", result.state.stats.virtual_wall_seed_count },
        { "virtual_wall_raw_count", result.state.stats.virtual_wall_raw_count },
        { "mm1_bidirectional_edge_count", result.state.stats.mm1_bidirectional_edge_count },
        { "mm1_directional_edge_count", result.state.stats.mm1_directional_edge_count },
        { "mm2_skipped_count", result.state.stats.mm2_skipped_count },
        { "trim_seed_group_count", result.state.stats.trim_seed_group_count },
        { "trim_tool_try_count", result.state.stats.trim_tool_try_count },
        { "trim_tool_ok_count", result.state.stats.trim_tool_ok_count },
        { "preselect_split_face_count", result.state.stats.preselect_split_face_count },
        { "selected_count", result.state.stats.selected_count },
        { "selected_face_count", result.state.stats.selected_count },
        { "rejected_count", result.state.stats.rejected_count },
        { "selected_copy_fail_count", result.state.stats.selected_copy_fail_count },
        { "selected_dedup_skip_count", result.state.stats.selected_dedup_skip_count },
        { "slice_count", result.state.stats.slice_count },
        { "side_edge_count", result.state.stats.side_edge_count },
        { "raw_imprint_edge_pair_count", result.state.stats.raw_imprint_edge_pair_count },
        { "ambiguous_edge_pair_count", result.state.stats.ambiguous_edge_pair_count },
        { "slice_edge_use_count", result.state.stats.slice_edge_use_count },
        { "slice_adjacency_count", result.state.stats.slice_adjacency_count }
    };

    JsonValue slices = JsonValue::array();
    int i = 0;
    for (i = 0; i < (int)result.state.slices.slices.size(); ++i)
        slices.push_back(TrimSliceRecordJson(result.state.slices.slices[i]));
    out["slices"] = slices;

    JsonValue selections = JsonValue::array();
    for (i = 0; i < (int)result.state.selections.selections.size(); ++i)
        selections.push_back(TrimSelectionRecordJson(result.state.selections.selections[i]));
    out["selections"] = selections;

    JsonValue slice_adjacencies = JsonValue::array();
    JsonValue selected_adjacency = JsonValue::array();
    for (i = 0; i < (int)result.state.slice_adjacencies.adjacencies.size(); ++i)
    {
        const Step6SliceAdjacency& adjacency = result.state.slice_adjacencies.adjacencies[i];
        slice_adjacencies.push_back(TrimSliceAdjacencyJson(adjacency));

        JsonValue selected_edge = JsonValue::object();
        selected_edge["a"] = adjacency.slice_a;
        selected_edge["b"] = adjacency.slice_b;
        selected_adjacency.push_back(selected_edge);
    }
    out["slice_adjacencies"] = slice_adjacencies;
    out["selected_adjacency"] = selected_adjacency;
    return out;
}

JsonValue Step7Json(const Step7StitchResult& result)
{
    JsonValue out = JsonValue::object();
    out["ok"] = JsonBoolValue(result.ok);

    int i = 0;
    JsonValue raw_relation_input = JsonValue::object();
    JsonValue split_to_raw_faces = JsonValue::array();
    for (i = 0; i < (int)result.state.input.split_to_raw_faces.size(); ++i)
        split_to_raw_faces.push_back(
            Step7SplitRawFaceJson(result.state.input.split_to_raw_faces[i]));
    raw_relation_input["split_to_raw_faces"] = split_to_raw_faces;

    JsonValue raw_face_relations = JsonValue::array();
    for (i = 0; i < (int)result.state.input.raw_face_relations.size(); ++i)
        raw_face_relations.push_back(
        Step7RawFaceRelationJson(result.state.input.raw_face_relations[i]));
    raw_relation_input["raw_face_relations"] = raw_face_relations;

    JsonValue raw_faces = JsonValue::array();
    for (i = 0; i < (int)result.state.input.raw_faces.size(); ++i)
        raw_faces.push_back(Step7RawFaceRefJson(result.state.input.raw_faces[i]));
    raw_relation_input["raw_faces"] = raw_faces;

    out["raw_relation_input"] = raw_relation_input;
    return out;
}

JsonValue ResultJson(int body_index, const MidSurfaceResult& result)
{
    JsonValue out = JsonValue::object();
    out["body_index"] = body_index;
    out["ok"] = JsonBoolValue(result.ok);
    out["completed_step"] = result.completed_step;

    if (result.completed_step >= PIPELINE_STEP1_FACE_ANALYZE)
        out["step1"] = Step1Json(result.step1);
    if (result.completed_step >= PIPELINE_STEP2_GROUP_BUILD)
        out["step2"] = Step2Json(result.step2);
    if (result.completed_step >= PIPELINE_STEP3_PAIR_BUILD)
        out["step3"] = Step3Json(result.step3);
    if (result.completed_step >= PIPELINE_STEP4_RELATION_BUILD)
        out["step4"] = Step4Json(result.step4);
    if (result.completed_step >= PIPELINE_STEP5_MID_PATCH_BUILD)
        out["step5"] = Step5Json(result.step5);
    if (result.completed_step >= PIPELINE_STEP6_TRIM_SELECT)
        out["step6"] = Step6Json(result.step6);
    if (result.completed_step >= PIPELINE_STEP7_FINAL_EXPORT)
        out["step7"] = Step7Json(result.step7);
    return out;
}
} // namespace

std::string ResultJsonPath(const RunContext& context, int body_index)
{
    char file_name[128];
    sprintf(file_name, "body_%d_result.json", body_index);
    return JoinPath(JoinPath(context.output_root(), "results"), file_name);
}

logical SaveMidSurfaceResultJson(
    const RunContext& context,
    int body_index,
    const MidSurfaceResult& result)
{
    const std::string path = ResultJsonPath(context, body_index);
    if (EnsureParentDirectoryForFile(path) == FALSE)
        return FALSE;

    FILE* fp = fopen(path.c_str(), "w");
    if (fp == nullptr)
        return FALSE;

    const std::string json = ResultJson(body_index, result).dump(2);
    const size_t written = fwrite(json.c_str(), 1, json.size(), fp);
    fputc('\n', fp);
    fclose(fp);
    return written == json.size() ? TRUE : FALSE;
}
} // namespace midsurface_new
