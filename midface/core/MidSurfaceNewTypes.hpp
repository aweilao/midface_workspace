#pragma once

#include "body.hxx"
#include "edge.hxx"
#include "face.hxx"
#include "logical.h"
#include "point.hxx"
#include "position.hxx"
#include "surface.hxx"
#include "unitvec.hxx"

#include <map>
#include <string>
#include <vector>

namespace midsurface_new
{
enum PipelineStep
{
    PIPELINE_STEP_UNKNOWN = 0,
    PIPELINE_STEP1_FACE_ANALYZE = 1,
    PIPELINE_STEP2_GROUP_BUILD = 2,
    PIPELINE_STEP3_PAIR_BUILD = 3,
    PIPELINE_STEP4_RELATION_BUILD = 4,
    PIPELINE_STEP5_MID_PATCH_BUILD = 5,
    PIPELINE_STEP6_TRIM_SELECT = 6,
    PIPELINE_STEP7_FINAL_EXPORT = 7
};

enum DiagnosticLevel
{
    DIAG_ERROR = 0,
    DIAG_WARN = 1,
    DIAG_INFO = 2,
    DIAG_DEBUG = 3
};

enum MidSurfaceNewFinalMode
{
    MIDFACE_NEW_FINAL_STEP6_SELECTED = 0,
    MIDFACE_NEW_FINAL_STEP7_EXPORTED = 1
};

struct IdPair
{
    IdPair();

    int first;
    int second;
};

struct StructuredEvent
{
    StructuredEvent();

    std::vector<std::string> tags;
    std::map<std::string, std::string> properties;
    std::map<std::string, std::string> json_properties;

    StructuredEvent Clone() const;
    StructuredEvent& AddTag(const std::string& tag);
    StructuredEvent& SetProperty(const std::string& name, const std::string& value);
    StructuredEvent& SetJsonProperty(const std::string& name, const std::string& json_value);
};

struct PointSample
{
    PointSample();

    int face_id;
    SPAposition point;
    SPAunit_vector normal;
    std::string role;
};

struct FaceRecord
{
    FaceRecord();

    int face_id;
    FACE* face;
    SURFACE* surface_geometry;
    std::string face_type;
    int color_r;
    int color_g;
    int color_b;
    int edge_count;
    int adjacency_degree;
    logical has_representative_point;
    logical has_representative_normal;
    logical has_area_proxy;
    logical has_edge_lengths;
    SPAposition representative_point;
    SPAunit_vector representative_normal;
    double area_proxy;
    double edge_min;
    double edge_max;
    logical valid;
    std::string invalid_reason;
    std::string sample_summary;
};

struct FaceAdjacency
{
    FaceAdjacency();

    int face_a;
    int face_b;
    int shared_edge_count;
    std::string relation;
};

struct ModelScaleContext
{
    ModelScaleContext();

    logical valid;
    double reference_length;
    double distance_unit;
    double normalized_reference;
    int top_edge_count_requested;
    int top_edge_count_used;
    int valid_edge_count;
    double min_valid_edge_length;
    double longest_edge_length;
    double shortest_used_edge_length;
};

struct GroupRecord
{
    GroupRecord();

    int group_id;
    std::vector<int> face_ids;
    std::vector<FACE*> faces;
    std::string type;
    std::vector<int> surface_ids;
    SPAposition seed_point;
    SPAunit_vector seed_normal;
    double area_sum;
    double local_scale;
    std::string source_rules;
    double confidence;
};

struct GroupMergeCandidate
{
    GroupMergeCandidate();

    int candidate_id;
    int face_a;
    int face_b;
    std::string source;
};

struct GroupMergeDecision
{
    GroupMergeDecision();

    int candidate_id;
    int face_a;
    int face_b;
    std::string decision;
    std::string reason;
    double score;
};

struct PairDecisionRecord
{
    PairDecisionRecord();

    int candidate_id;
    int group_a;
    int group_b;
    std::string decision;
    std::string reason;
    double score;
};

struct PairCandidate
{
    PairCandidate();

    int candidate_id;
    int group_a;
    int group_b;
    std::string source;
};

struct PairRecord
{
    PairRecord();

    int pair_id;
    int group_a;
    int group_b;
    double thickness;
    double coverage;
    double score;
    double facing_score;
    double pass_ratio;
    double pass_balance;
    SPAposition point_a;
    SPAposition point_b;
    SPAunit_vector pair_direction;
    logical uncertain;
    logical rib_candidate;
    std::string reason;
};

struct PairRelationRecord
{
    PairRelationRecord();

    int relation_id;
    int pair_a;
    int pair_b;
    std::string relation_type;
    std::string pair_mode;
    std::string classify_source;
    std::string source;
    std::string wall_mode;
    int hits_aa;
    int hits_ab;
    int hits_ba;
    int hits_bb;
    int total_hits;
    double hit_normal_cos_abs;
    double hit_normal_angle_deg;
};

struct PairWallRelationRecord
{
    PairWallRelationRecord();

    int relation_id;
    int pair_id;
    int wall_group_id;
    int hit_group_a;
    int hit_group_b;
    std::string wall_mode;
};

enum MidPatchKind
{
    MID_PATCH_UNKNOWN = 0,
    MID_PATCH_PLANE = 1,
    MID_PATCH_CYLINDER = 2,
    MID_PATCH_SPHERE = 3,
    MID_PATCH_ANALYTIC = 4,
    MID_PATCH_SPLINE = 5,
    MID_PATCH_MIXED = 6
};

enum MidPatchSideGroupType
{
    MID_PATCH_SIDE_SINGLE = 0,
    MID_PATCH_SIDE_MIXED = 1
};

enum MidPatchLinkType
{
    MID_PATCH_LINK_UNKNOWN = 0,
    MID_PATCH_LINK_SAME_SIDE = 1,
    MID_PATCH_LINK_CROSS_SIDE = 2,
    MID_PATCH_LINK_MIXED = 3,
    MID_PATCH_LINK_RIB_T = 4
};

enum MidPatchConnectClass
{
    MID_PATCH_CONN_UNKNOWN = 0,
    MID_PATCH_CONN_MM1 = 1,
    MID_PATCH_CONN_MM2 = 2
};

enum MidPatchPairMode
{
    MID_PATCH_PAIR_UNKNOWN = 0,
    MID_PATCH_PAIR_MM1 = 1,
    MID_PATCH_PAIR_MM2 = 2
};

struct WallRecord
{
    WallRecord();

    int wall_id;
    std::vector<int> source_face_ids;
    BODY* body;
    std::string source;
    int pair_a;
    int pair_b;
    int group_a;
    int group_b;
    int bucket_slot;
    double coedge_len;
    double half_profile;
    double hit_normal_cos_abs;
};

struct MidPatchRecord
{
    MidPatchRecord();

    int patch_id;
    int source_pair_id;
    int group_a;
    int group_b;
    int anchor_group;
    int component_id;
    logical merged_unit;
    std::vector<int> member_pairs;
    MidPatchKind kind;
    std::string type_a;
    std::string type_b;
    MidPatchSideGroupType side_type_a;
    MidPatchSideGroupType side_type_b;
    logical valid;
    logical is_rib;
    logical is_uncertain;
    double thickness;
    SPAposition point_a;
    SPAposition point_b;
    SPAposition point_mid;
    SPAunit_vector normal_mid;
    FACE* face;
    BODY* sheet_body;
    std::string build_method;
    std::string fail_reason;
};

struct MidPatchJunctionRecord
{
    MidPatchJunctionRecord();

    int junction_id;
    int pair_i;
    int pair_j;
    MidPatchLinkType link_type;
    MidPatchConnectClass conn_class;
    MidPatchPairMode pair_mode;
    int total_hits;
};

struct TrimSelectionRecord
{
    TrimSelectionRecord();

    int selection_id;
    int source_patch_id;
    int source_pair_id;
    int source_seed_rep_pair;
    int source_split_index;
    FACE* source_split_face;
    FACE* selected_face;
    double area;
    std::string reason;
};

struct FinalExportRecord
{
    FinalExportRecord();

    int export_id;
    ENTITY* entity;
    std::string source;
};
} // namespace midsurface_new
