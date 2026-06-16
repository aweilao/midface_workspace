#include "core/MidSurfaceNewTypes.hpp"

namespace midsurface_new
{
IdPair::IdPair()
    : first(-1),
      second(-1)
{
}

StructuredEvent::StructuredEvent()
{
}

StructuredEvent StructuredEvent::Clone() const
{
    return *this;
}

StructuredEvent& StructuredEvent::AddTag(const std::string& tag)
{
    if (!tag.empty())
        tags.push_back(tag);
    return *this;
}

StructuredEvent& StructuredEvent::SetProperty(const std::string& name, const std::string& value)
{
    if (!name.empty())
        properties[name] = value;
    return *this;
}

StructuredEvent& StructuredEvent::SetJsonProperty(const std::string& name, const std::string& json_value)
{
    if (!name.empty())
        json_properties[name] = json_value;
    return *this;
}

PointSample::PointSample()
    : face_id(-1)
{
}

FaceRecord::FaceRecord()
    : face_id(-1),
      face(nullptr),
      surface_geometry(nullptr),
      color_r(0),
      color_g(0),
      color_b(0),
      edge_count(0),
      adjacency_degree(0),
      has_representative_point(FALSE),
      has_representative_normal(FALSE),
      has_area_proxy(FALSE),
      has_edge_lengths(FALSE),
      area_proxy(0.0),
      edge_min(0.0),
      edge_max(0.0),
      valid(FALSE)
{
}

FaceAdjacency::FaceAdjacency()
    : face_a(-1),
      face_b(-1),
      shared_edge_count(0)
{
}

ModelScaleContext::ModelScaleContext()
    : valid(FALSE),
      reference_length(0.0),
      distance_unit(0.0),
      normalized_reference(1000.0),
      top_edge_count_requested(10),
      top_edge_count_used(0),
      valid_edge_count(0),
      min_valid_edge_length(1.0e-9),
      longest_edge_length(0.0),
      shortest_used_edge_length(0.0)
{
}

GroupRecord::GroupRecord()
    : group_id(-1),
      area_sum(0.0),
      local_scale(0.0),
      confidence(0.0)
{
}

GroupMergeCandidate::GroupMergeCandidate()
    : candidate_id(-1),
      face_a(-1),
      face_b(-1)
{
}

GroupMergeDecision::GroupMergeDecision()
    : candidate_id(-1),
      face_a(-1),
      face_b(-1),
      score(0.0)
{
}

PairDecisionRecord::PairDecisionRecord()
    : candidate_id(-1),
      group_a(-1),
      group_b(-1),
      score(0.0)
{
}

PairCandidate::PairCandidate()
    : candidate_id(-1),
      group_a(-1),
      group_b(-1)
{
}

PairRecord::PairRecord()
    : pair_id(-1),
      group_a(-1),
      group_b(-1),
      thickness(0.0),
      coverage(0.0),
      score(0.0),
      facing_score(0.0),
      pass_ratio(0.0),
      pass_balance(0.0),
      uncertain(FALSE),
      rib_candidate(FALSE)
{
}

PairRelationRecord::PairRelationRecord()
    : relation_id(-1),
      pair_a(-1),
      pair_b(-1),
      hits_aa(0),
      hits_ab(0),
      hits_ba(0),
      hits_bb(0),
      total_hits(0),
      hit_normal_cos_abs(-1.0),
      hit_normal_angle_deg(-1.0)
{
}

PairWallRelationRecord::PairWallRelationRecord()
    : relation_id(-1),
      pair_id(-1),
      wall_group_id(-1),
      hit_group_a(0),
      hit_group_b(0)
{
}

WallRecord::WallRecord()
    : wall_id(-1),
      body(nullptr),
      pair_a(-1),
      pair_b(-1),
      group_a(-1),
      group_b(-1),
      bucket_slot(-1),
      coedge_len(0.0),
      half_profile(0.0),
      hit_normal_cos_abs(-1.0)
{
}

MidPatchRecord::MidPatchRecord()
    : patch_id(-1),
      source_pair_id(-1),
      group_a(-1),
      group_b(-1),
      anchor_group(-1),
      component_id(-1),
      merged_unit(FALSE),
      kind(MID_PATCH_UNKNOWN),
      side_type_a(MID_PATCH_SIDE_SINGLE),
      side_type_b(MID_PATCH_SIDE_SINGLE),
      valid(FALSE),
      is_rib(FALSE),
      is_uncertain(FALSE),
      thickness(0.0),
      face(nullptr),
      sheet_body(nullptr)
{
}

MidPatchJunctionRecord::MidPatchJunctionRecord()
    : junction_id(-1),
      pair_i(-1),
      pair_j(-1),
      link_type(MID_PATCH_LINK_UNKNOWN),
      conn_class(MID_PATCH_CONN_UNKNOWN),
      pair_mode(MID_PATCH_PAIR_UNKNOWN),
      total_hits(0)
{
}

TrimSelectionRecord::TrimSelectionRecord()
    : selection_id(-1),
      source_patch_id(-1),
      source_pair_id(-1),
      source_seed_rep_pair(-1),
      source_split_index(-1),
      source_split_face(nullptr),
      selected_face(nullptr),
      area(0.0)
{
}

FinalExportRecord::FinalExportRecord()
    : export_id(-1),
      entity(nullptr)
{
}
} // namespace midsurface_new
