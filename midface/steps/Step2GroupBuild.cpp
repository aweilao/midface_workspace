#include "steps/Step2GroupBuild.hpp"

#include "utils/ColorUtils.hpp"
#include "utils/JsonUtils.hpp"
#include "utils/SamplingUtils.hpp"

#include "cstrapi.hxx"
#include "faceutil.hxx"
#include "faceqry.hxx"
#include "intrapi.hxx"
#include "kernapi.hxx"
#include "queryapi.hxx"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace midsurface_new
{
namespace
{
const double kPi = 3.14159265358979323846;

std::string IntText(int value)
{
    char buf[64];
    sprintf(buf, "%d", value);
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

std::string PointText(const SPAposition& p)
{
    std::ostringstream ss;
    ss << p.x() << "," << p.y() << "," << p.z();
    return ss.str();
}

std::string FaceIdListText(const std::vector<int>& face_ids)
{
    std::ostringstream ss;
    int i = 0;
    for (i = 0; i < (int)face_ids.size(); ++i)
    {
        if (i > 0)
            ss << ",";
        ss << face_ids[i];
    }
    return ss.str();
}

std::string ColorText(const ColorRgb255& color)
{
    std::ostringstream ss;
    ss << color.r << "," << color.g << "," << color.b;
    return ss.str();
}

JsonValue ColorRgbJson(const ColorRgb255& color)
{
    JsonValue rgb = JsonValue::array();
    rgb.push_back(color.r);
    rgb.push_back(color.g);
    rgb.push_back(color.b);
    return rgb;
}

JsonValue FaceIdListJson(const std::vector<int>& face_ids)
{
    JsonValue out = JsonValue::array();
    int i = 0;
    for (i = 0; i < (int)face_ids.size(); ++i)
        out.push_back(face_ids[i]);
    return out;
}

std::string RejectStatsText(const std::map<std::string, int>& stats)
{
    std::ostringstream ss;
    std::map<std::string, int>::const_iterator it = stats.begin();
    int i = 0;
    for (; it != stats.end(); ++it)
    {
        if (i > 0)
            ss << ";";
        ss << it->first << "=" << it->second;
        ++i;
    }
    return ss.str();
}

double DegToCos(double deg)
{
    return std::cos(deg * kPi / 180.0);
}

double ClampPositive(double value, double fallback)
{
    return value > 0.0 ? value : fallback;
}

double SafeArea(const FaceRecord& record)
{
    return record.has_area_proxy != FALSE && record.area_proxy > 1.0e-12
        ? record.area_proxy
        : 1.0e-12;
}

double DistanceBetween(const SPAposition& a, const SPAposition& b)
{
    const SPAvector v = b - a;
    return v.len();
}

SPAunit_vector UnitFromComponents(double x, double y, double z, const SPAunit_vector& fallback)
{
    const double len = std::sqrt(x * x + y * y + z * z);
    if (len <= 1.0e-12)
        return fallback;
    return SPAunit_vector(x / len, y / len, z / len);
}

StructuredEvent Step2EventTemplate(const char* schema)
{
    StructuredEvent event;
    event.AddTag("step2");
    event.AddTag("grouping");
    (void)schema;
    return event;
}

void EmitEvent(DiagnosticSink* diagnostics, const StructuredEvent& event)
{
    if (diagnostics != nullptr)
        (void)diagnostics->EmitEvent(event);
}

struct ValidFaceIndex
{
    std::vector<const FaceRecord*> records;
    std::map<int, int> face_id_to_index;
};

struct ResolvedTolerances
{
    ResolvedTolerances()
        : normal_cos(DegToCos(15.0)),
          axis_cos(DegToCos(15.0)),
          plane_tol(1.0e-6),
          radius_tol(1.0e-6),
          edge_length_eps(1.0e-9)
    {
    }

    double normal_cos;
    double axis_cos;
    double plane_tol;
    double radius_tol;
    double edge_length_eps;
};

struct SurfacePrefilterResult
{
    SurfacePrefilterResult()
        : passed(FALSE),
          normal_dot(0.0),
          normal_angle_deg(0.0),
          plane_distance(0.0),
          radius_delta(0.0)
    {
    }

    logical passed;
    std::string result_text;
    std::string match_type;
    std::string matched_properties;
    std::string reason;
    double normal_dot;
    double normal_angle_deg;
    double plane_distance;
    double radius_delta;
};

struct SourceTargetPair
{
    SourceTargetPair()
        : source(nullptr),
          target(nullptr),
          swapped(FALSE)
    {
    }

    const FaceRecord* source;
    const FaceRecord* target;
    logical swapped;
};

struct FaceSampleSet
{
    FaceSampleSet()
        : face_id(-1),
          ok(FALSE)
    {
    }

    int face_id;
    std::vector<PointSample> samples;
    logical ok;
    std::string reason;
};

struct ClosestHit
{
    ClosestHit()
        : source_face_id(-1),
          target_face_id(-1),
          distance(0.0),
          source_normal_component(0.0),
          target_normal_component(0.0),
          closest_ok(FALSE),
          distance_ok(FALSE),
          normal_ok(FALSE),
          tangent_ok(FALSE),
          pass(FALSE)
    {
    }

    int source_face_id;
    int target_face_id;
    SPAposition source_point;
    SPAposition target_point;
    SPAunit_vector source_normal;
    SPAunit_vector target_normal;
    double distance;
    double source_normal_component;
    double target_normal_component;
    logical closest_ok;
    logical distance_ok;
    logical normal_ok;
    logical tangent_ok;
    logical pass;
    std::string fail_reason;
};

struct PairRefineResult
{
    PairRefineResult()
        : accepted(FALSE),
          sample_count(0),
          hit_count(0),
          nearest_count(0),
          pass_count(0),
          local_dist_gate(0.0),
          score(0.0)
    {
    }

    logical accepted;
    std::string reason;
    int sample_count;
    int hit_count;
    int nearest_count;
    int pass_count;
    double local_dist_gate;
    double score;
    std::vector<ClosestHit> nearest_hits;
};

class UnionFind
{
public:
    void Reset(const std::vector<int>& face_ids)
    {
        parent_.clear();
        rank_.clear();
        int i = 0;
        for (i = 0; i < (int)face_ids.size(); ++i)
        {
            parent_[face_ids[i]] = face_ids[i];
            rank_[face_ids[i]] = 0;
        }
    }

    int Find(int face_id)
    {
        std::map<int, int>::iterator it = parent_.find(face_id);
        if (it == parent_.end())
            return face_id;
        if (it->second != face_id)
            it->second = Find(it->second);
        return it->second;
    }

    void Unite(int face_a, int face_b)
    {
        const int root_a = Find(face_a);
        const int root_b = Find(face_b);
        if (root_a == root_b)
            return;

        if (rank_[root_a] < rank_[root_b])
            parent_[root_a] = root_b;
        else if (rank_[root_a] > rank_[root_b])
            parent_[root_b] = root_a;
        else
        {
            parent_[root_b] = root_a;
            ++rank_[root_a];
        }
    }

    void BuildComponents(std::map<int, std::vector<int> >& out_components)
    {
        out_components.clear();
        std::map<int, int>::iterator it = parent_.begin();
        for (; it != parent_.end(); ++it)
            out_components[Find(it->first)].push_back(it->first);

        std::map<int, std::vector<int> >::iterator comp_it = out_components.begin();
        for (; comp_it != out_components.end(); ++comp_it)
            std::sort(comp_it->second.begin(), comp_it->second.end());
    }

private:
    std::map<int, int> parent_;
    std::map<int, int> rank_;
};

bool HitLessByDistance(const ClosestHit& a, const ClosestHit& b)
{
    return a.distance < b.distance;
}

logical BuildSourceSamples(
    const FaceRecord& source,
    const Step2GroupOptions& options,
    const ResolvedTolerances& tol,
    FaceSampleSet& out_samples)
{
    out_samples = FaceSampleSet();
    out_samples.face_id = source.face_id;
    if (source.face == nullptr)
    {
        out_samples.reason = "missing_source_face";
        return FALSE;
    }

    BoundarySampleOptions sample_options;
    sample_options.target.density = options.sample_density;
    sample_options.target.min_count = options.sample_min_count;
    sample_options.target.max_count = options.sample_max_count;
    sample_options.min_samples_per_edge = options.edge_min_samples_per_edge;
    sample_options.extra_sample_alpha = options.edge_sample_alpha;
    sample_options.max_samples_per_face = options.max_samples_per_face;
    sample_options.edge_length_eps = tol.edge_length_eps;
    sample_options.dedup_tol2 = 1.0e-12;
    sample_options.role = "step2.source_boundary";

    out_samples.ok = BuildBoundaryPointSamples(source, sample_options, out_samples.samples, out_samples.reason);
    return out_samples.ok;
}

logical QueryLocalFaceNormal(
    const FaceRecord& record,
    const SPAposition& point,
    SPAunit_vector& out_normal)
{
    if (record.face == nullptr)
        return FALSE;

    if (record.face_type == "plane" && get_face_normal(record.face, out_normal) != FALSE)
        return TRUE;

    out_normal = sg_get_face_normal(record.face, point);
    return TRUE;

    if (record.has_representative_normal != FALSE)
    {
        out_normal = record.representative_normal;
        return TRUE;
    }
}

double ComputeLocalDistanceGate(const FaceRecord& source, const Step2GroupOptions& options)
{
    double base_dist = 0.0;
    if (source.has_edge_lengths != FALSE && source.edge_max > 1.0e-12)
        base_dist = SafeArea(source) / source.edge_max;
    if (base_dist <= 0.0 && source.has_edge_lengths != FALSE)
        base_dist = source.edge_min;
    if (base_dist <= 0.0)
        base_dist = std::sqrt(SafeArea(source));

    return base_dist * ClampPositive(options.local_distance_gate_scale, 1.0);
}

ClosestHit EvaluateOneHit(
    const FaceRecord& source,
    const FaceRecord& target,
    const PointSample& sample,
    double local_dist_gate,
    double normal_cos,
    double normal_component_max)
{
    ClosestHit hit;
    hit.source_face_id = source.face_id;
    hit.target_face_id = target.face_id;
    hit.source_point = sample.point;

    SPAposition closest;
    double distance = 0.0;
    SPAposition query_point = sample.point;
    outcome closest_result = api_entity_point_distance(target.face, query_point, closest, distance);
    if (!closest_result.ok())
    {
        hit.fail_reason = "closest_point_failed";
        return hit;
    }

    hit.closest_ok = TRUE;
    hit.target_point = closest;
    hit.distance = std::max(0.0, distance);
    hit.distance_ok = hit.distance <= local_dist_gate ? TRUE : FALSE;

    if (QueryLocalFaceNormal(source, sample.point, hit.source_normal) == FALSE ||
        QueryLocalFaceNormal(target, closest, hit.target_normal) == FALSE)
    {
        hit.fail_reason = "normal_unavailable";
        return hit;
    }

    hit.normal_ok = (hit.source_normal % hit.target_normal) >= normal_cos ? TRUE : FALSE;
    hit.tangent_ok = TRUE;
    if (hit.distance > 1.0e-12)
    {
        const SPAvector v = closest - sample.point;
        hit.source_normal_component = std::fabs((v % hit.source_normal) / hit.distance);
        hit.target_normal_component = std::fabs((v % hit.target_normal) / hit.distance);
        hit.tangent_ok =
            (hit.source_normal_component <= normal_component_max &&
             hit.target_normal_component <= normal_component_max)
                ? TRUE
                : FALSE;
    }

    hit.pass = (hit.distance_ok != FALSE && hit.normal_ok != FALSE && hit.tangent_ok != FALSE) ? TRUE : FALSE;
    if (hit.pass == FALSE)
    {
        if (hit.distance_ok == FALSE)
            hit.fail_reason = "distance_gate";
        else if (hit.normal_ok == FALSE)
            hit.fail_reason = "normal_gate";
        else if (hit.tangent_ok == FALSE)
            hit.fail_reason = "tangent_gate";
    }
    return hit;
}

SourceTargetPair ChooseSourceTarget(const FaceRecord& a, const FaceRecord& b)
{
    SourceTargetPair pair;
    pair.source = &a;
    pair.target = &b;
    pair.swapped = FALSE;
    if (SafeArea(a) > SafeArea(b) || (std::fabs(SafeArea(a) - SafeArea(b)) <= 1.0e-12 && a.face_id > b.face_id))
    {
        pair.source = &b;
        pair.target = &a;
        pair.swapped = TRUE;
    }
    return pair;
}

std::string SamplesJson(const std::vector<ClosestHit>& hits)
{
    JsonValue samples = JsonValue::array();
    int i = 0;
    for (i = 0; i < (int)hits.size(); ++i)
    {
        const ClosestHit& h = hits[i];
        JsonValue sample = JsonValue::object();
        sample["i"] = i;
        sample["p"] = JsonPoint(h.source_point);
        sample["cp"] = JsonPoint(h.target_point);
        sample["dist"] = h.distance;
        sample["distance_ok"] = h.distance_ok != FALSE;
        sample["normal_ok"] = h.normal_ok != FALSE;
        sample["tangent_ok"] = h.tangent_ok != FALSE;
        sample["pass"] = h.pass != FALSE;
        samples.push_back(sample);
    }
    return JsonDump(samples);
}

PairRefineResult EvaluatePairBySamples(
    const FaceRecord& a,
    const FaceRecord& b,
    const Step2GroupOptions& options,
    const ResolvedTolerances& tol)
{
    PairRefineResult result;
    SourceTargetPair pair = ChooseSourceTarget(a, b);
    if (pair.source == nullptr || pair.target == nullptr)
    {
        result.reason = "missing_source_target";
        return result;
    }

    FaceSampleSet samples;
    if (BuildSourceSamples(*pair.source, options, tol, samples) == FALSE)
    {
        result.reason = "no_samples";
        return result;
    }
    result.sample_count = (int)samples.samples.size();
    result.local_dist_gate = ComputeLocalDistanceGate(*pair.source, options);

    std::vector<ClosestHit> hits;
    int i = 0;
    for (i = 0; i < (int)samples.samples.size(); ++i)
    {
        ClosestHit hit = EvaluateOneHit(
            *pair.source,
            *pair.target,
            samples.samples[i],
            result.local_dist_gate,
            tol.normal_cos,
            std::max(0.0, options.local_normal_component_max));
        if (hit.closest_ok != FALSE)
            hits.push_back(hit);
    }

    result.hit_count = (int)hits.size();
    if (hits.empty())
    {
        result.reason = "closest_point_failed";
        return result;
    }

    std::sort(hits.begin(), hits.end(), HitLessByDistance);
    int nearest_count = options.local_nearest_count;
    if (nearest_count <= 0)
        nearest_count = 6;
    nearest_count = std::min(nearest_count, (int)hits.size());
    result.nearest_count = nearest_count;

    for (i = 0; i < nearest_count; ++i)
    {
        result.nearest_hits.push_back(hits[i]);
        if (hits[i].pass != FALSE)
            ++result.pass_count;
    }

    const int min_pass = std::min(std::max(1, options.local_min_pass_count), std::max(1, nearest_count));
    result.accepted = result.pass_count >= min_pass ? TRUE : FALSE;
    result.score = nearest_count > 0 ? ((double)result.pass_count) / ((double)nearest_count) : 0.0;
    result.reason = result.accepted != FALSE ? "accept_refine" : "fail_refine";
    return result;
}

void EmitPrefilterEvent(
    DiagnosticSink* diagnostics,
    const FaceRecord& a,
    const FaceRecord& b,
    const SurfacePrefilterResult& prefilter)
{
    StructuredEvent event = Step2EventTemplate("step2.group_prefilter.v1");
    event.AddTag("prefilter");
    event.AddTag("detail");
    event.SetProperty("face_a", IntText(a.face_id));
    event.SetProperty("face_b", IntText(b.face_id));
    event.SetProperty("face_a_type", a.face_type);
    event.SetProperty("face_b_type", b.face_type);
    event.SetProperty("result", prefilter.passed != FALSE ? "pass" : "fail");
    event.SetProperty("match_type", prefilter.match_type);
    event.SetProperty("normal_angle_deg", DoubleText(prefilter.normal_angle_deg));
    event.SetProperty("plane_distance", DoubleText(prefilter.plane_distance));
    event.SetProperty("radius_delta", DoubleText(prefilter.radius_delta));
    event.SetProperty("matched_properties", prefilter.matched_properties);
    if (!prefilter.reason.empty())
        event.SetProperty("reason", prefilter.reason);
    EmitEvent(diagnostics, event);
}

void EmitRefineEvent(
    DiagnosticSink* diagnostics,
    const FaceRecord& a,
    const FaceRecord& b,
    const PairRefineResult& refine)
{
    SourceTargetPair pair = ChooseSourceTarget(a, b);
    StructuredEvent event = Step2EventTemplate("step2.group_refine.v1");
    event.AddTag("refine");
    event.AddTag("detail");
    event.SetProperty("source_face", pair.source == nullptr ? "-1" : IntText(pair.source->face_id));
    event.SetProperty("target_face", pair.target == nullptr ? "-1" : IntText(pair.target->face_id));
    event.SetProperty("result", refine.accepted != FALSE ? "pass" : "fail");
    event.SetProperty("sample_count", IntText(refine.sample_count));
    event.SetProperty("pass_count", IntText(refine.pass_count));
    event.SetProperty("local_dist_gate", DoubleText(refine.local_dist_gate));
    event.SetJsonProperty("samples", SamplesJson(refine.nearest_hits));
    EmitEvent(diagnostics, event);
}

void EmitRunEvent(
    DiagnosticSink* diagnostics,
    const char* phase,
    const char* result_text,
    const Step2GroupState& state)
{
    StructuredEvent event = Step2EventTemplate("step2.group_run.v1");
    event.AddTag(phase == nullptr ? "run" : phase);
    if (phase != nullptr && std::string(phase) == "finish")
    {
        event.AddTag("summary");
        event.AddTag("all");
    }
    (void)result_text;
    event.SetProperty("valid_face_count", IntText(state.stats.valid_face_count));
    event.SetProperty("candidate_count", IntText(state.stats.candidate_count));
    event.SetProperty("accepted_count", IntText(state.stats.accepted_count));
    event.SetProperty("rejected_count", IntText(state.stats.rejected_count));
    event.SetProperty("group_count", IntText(state.stats.group_count));
    event.SetProperty("single_face_group_count", IntText(state.stats.single_face_group_count));
    event.SetProperty("surface_prefilter_pass_count", IntText(state.stats.surface_prefilter_pass_count));
    event.SetProperty("surface_prefilter_reject_count", IntText(state.stats.surface_prefilter_reject_count));
    event.SetProperty("sample_refine_pass_count", IntText(state.stats.sample_refine_pass_count));
    event.SetProperty("sample_refine_reject_count", IntText(state.stats.sample_refine_reject_count));
    event.SetProperty("reject_reason_stats", RejectStatsText(state.reject_reason_stats));
    EmitEvent(diagnostics, event);
}

void EmitGroupEvent(DiagnosticSink* diagnostics, const GroupRecord& group)
{
    StructuredEvent event = Step2EventTemplate("step2.group_record.v1");
    event.AddTag("group");
    event.AddTag("record");
    event.AddTag("summary");
    event.AddTag("single");
    event.SetProperty("group_id", IntText(group.group_id));
    event.SetProperty("face_count", IntText((int)group.face_ids.size()));
    event.SetProperty("face_ids", FaceIdListText(group.face_ids));
    event.SetProperty("type", group.type);
    event.SetProperty("area_sum", DoubleText(group.area_sum));
    event.SetProperty("local_scale", DoubleText(group.local_scale));
    event.SetProperty("confidence", DoubleText(group.confidence));
    event.SetProperty("source_rules", group.source_rules);
    EmitEvent(diagnostics, event);
}

void EmitStageSummaryEvents(DiagnosticSink* diagnostics, const Step2GroupState& state)
{
    StructuredEvent prefilter = Step2EventTemplate("step2.group_prefilter_stage.v1");
    prefilter.AddTag("prefilter");
    prefilter.AddTag("summary");
    prefilter.AddTag("stage");
    prefilter.AddTag("finish");
    prefilter.SetProperty("candidate_count", IntText(state.stats.candidate_count));
    prefilter.SetProperty("pass_count", IntText(state.stats.surface_prefilter_pass_count));
    prefilter.SetProperty("reject_count", IntText(state.stats.surface_prefilter_reject_count));
    EmitEvent(diagnostics, prefilter);

    StructuredEvent refine = Step2EventTemplate("step2.group_refine_stage.v1");
    refine.AddTag("refine");
    refine.AddTag("summary");
    refine.AddTag("stage");
    refine.AddTag("finish");
    refine.SetProperty("pass_count", IntText(state.stats.sample_refine_pass_count));
    refine.SetProperty("reject_count", IntText(state.stats.sample_refine_reject_count));
    refine.SetProperty("reject_reason_stats", RejectStatsText(state.reject_reason_stats));
    EmitEvent(diagnostics, refine);

    StructuredEvent group = Step2EventTemplate("step2.group_build_stage.v1");
    group.AddTag("group");
    group.AddTag("summary");
    group.AddTag("stage");
    group.AddTag("finish");
    group.SetProperty("group_count", IntText(state.stats.group_count));
    group.SetProperty("single_face_group_count", IntText(state.stats.single_face_group_count));
    EmitEvent(diagnostics, group);
}

void CollectValidFaces(const Step1FaceAnalyzeState& step1, ValidFaceIndex& out_index)
{
    out_index.records.clear();
    out_index.face_id_to_index.clear();

    int i = 0;
    for (i = 0; i < (int)step1.faces.records.size(); ++i)
    {
        const FaceRecord& record = step1.faces.records[i];
        if (record.valid != FALSE && record.face != nullptr)
        {
            out_index.face_id_to_index[record.face_id] = (int)out_index.records.size();
            out_index.records.push_back(&record);
        }
    }
}

const FaceRecord* FindFaceRecord(const ValidFaceIndex& index, int face_id)
{
    std::map<int, int>::const_iterator it = index.face_id_to_index.find(face_id);
    if (it == index.face_id_to_index.end())
        return nullptr;
    return index.records[it->second];
}

GroupMergeCandidate MakeCandidate(int candidate_id, const FaceRecord& a, const FaceRecord& b)
{
    GroupMergeCandidate candidate;
    candidate.candidate_id = candidate_id;
    candidate.face_a = std::min(a.face_id, b.face_id);
    candidate.face_b = std::max(a.face_id, b.face_id);
    candidate.source = "all_valid_pair";
    return candidate;
}

void AddDecision(
    const GroupMergeCandidate& candidate,
    const char* decision,
    const std::string& reason,
    double score,
    Step2GroupState& state)
{
    GroupMergeDecision record;
    record.candidate_id = candidate.candidate_id;
    record.face_a = candidate.face_a;
    record.face_b = candidate.face_b;
    record.decision = decision == nullptr ? "" : decision;
    record.reason = reason;
    record.score = score;
    state.decisions.decisions.push_back(record);

    if (record.decision == "accept")
        ++state.stats.accepted_count;
    else
    {
        ++state.stats.rejected_count;
        ++state.reject_reason_stats[reason.empty() ? "reject_unknown" : reason];
    }
}

ResolvedTolerances ResolveTolerances(const Step1FaceAnalyzeState& step1, const Step2GroupOptions& options)
{
    ResolvedTolerances tol;
    tol.normal_cos = DegToCos(ClampPositive(options.normal_angle_deg, 15.0));
    tol.axis_cos = DegToCos(ClampPositive(options.axis_angle_deg, 15.0));

    double diag = 1.0;
    if (step1.input_body != nullptr)
    {
        SPAposition lo;
        SPAposition hi;
        outcome box_result = api_get_entity_box((ENTITY*)step1.input_body, lo, hi);
        if (box_result.ok())
            diag = std::max(1.0e-9, DistanceBetween(lo, hi));
    }

    tol.plane_tol = options.plane_tol > 0.0 ? options.plane_tol : std::max(1.0e-6, diag * 1.0e-5);
    tol.radius_tol = options.radius_tol > 0.0 ? options.radius_tol : std::max(1.0e-6, diag * 1.0e-4);
    tol.edge_length_eps = options.edge_length_eps > 0.0 ? options.edge_length_eps : std::max(1.0e-9, diag * 1.0e-8);
    return tol;
}

SurfacePrefilterResult EvaluatePlanePrefilter(
    const FaceRecord& a,
    const FaceRecord& b,
    const ResolvedTolerances& tol)
{
    SurfacePrefilterResult result;
    result.match_type = "plane";

    SPAposition pa;
    SPAposition pb;
    SPAunit_vector na;
    SPAunit_vector nb;
    if (get_face_plane(a.face, pa, na) == FALSE || get_face_plane(b.face, pb, nb) == FALSE)
    {
        result.reason = "fail_plane_query";
        result.matched_properties = "plane_query:fail";
        return result;
    }

    result.normal_dot = na % nb;
    result.normal_angle_deg = std::acos(std::max(-1.0, std::min(1.0, result.normal_dot))) * 180.0 / kPi;
    result.plane_distance = std::fabs((pb - pa) % na);
    const logical normal_ok = result.normal_dot >= tol.normal_cos ? TRUE : FALSE;
    const logical distance_ok = result.plane_distance <= tol.plane_tol ? TRUE : FALSE;
    result.passed = (normal_ok != FALSE && distance_ok != FALSE) ? TRUE : FALSE;
    result.matched_properties =
        std::string("normal:") + (normal_ok != FALSE ? "pass" : "fail") +
        ";distance:" + (distance_ok != FALSE ? "pass" : "fail");
    result.reason = result.passed != FALSE ? "pass_plane" : (normal_ok == FALSE ? "fail_plane_normal" : "fail_plane_distance");
    return result;
}

SurfacePrefilterResult EvaluateRadiusPrefilter(
    const FaceRecord& a,
    const FaceRecord& b,
    const ResolvedTolerances& tol,
    const char* match_type)
{
    SurfacePrefilterResult result;
    result.match_type = match_type == nullptr ? "radius_surface" : match_type;
    double ra = 0.0;
    double rb = 0.0;
    if (get_face_radius(a.face, ra) == FALSE || get_face_radius(b.face, rb) == FALSE)
    {
        result.reason = "fail_radius_query";
        result.matched_properties = "radius:fail";
        return result;
    }

    result.radius_delta = std::fabs(ra - rb);
    result.passed = result.radius_delta <= tol.radius_tol ? TRUE : FALSE;
    result.matched_properties = std::string("radius:") + (result.passed != FALSE ? "pass" : "fail");
    result.reason = result.passed != FALSE ? "pass_radius" : "fail_radius_delta";
    return result;
}

SurfacePrefilterResult EvaluateSurfacePrefilter(
    const FaceRecord& a,
    const FaceRecord& b,
    const ResolvedTolerances& tol)
{
    SurfacePrefilterResult result;
    result.match_type = a.face_type;

    if (a.face == nullptr || b.face == nullptr)
    {
        result.reason = "fail_missing_face";
        return result;
    }

    if (a.face_type != b.face_type)
    {
        result.reason = "fail_type_mismatch";
        result.matched_properties = "type:fail";
        return result;
    }

    if (a.surface_geometry != nullptr && a.surface_geometry == b.surface_geometry)
    {
        result.passed = TRUE;
        result.reason = "pass_same_surface_pointer";
        result.matched_properties = "same_surface_pointer:pass";
        return result;
    }

    if (a.face_type == "plane")
        return EvaluatePlanePrefilter(a, b, tol);
    if (a.face_type == "cylinder")
        return EvaluateRadiusPrefilter(a, b, tol, "cylinder");
    if (a.face_type == "sphere")
        return EvaluateRadiusPrefilter(a, b, tol, "sphere");
    if (a.face_type == "cone" || a.face_type == "torus")
    {
        result.reason = "fail_unverified_parameter_api";
        result.matched_properties = "parameter_api:unverified";
        return result;
    }

    result.reason = "fail_unsupported_surface_type";
    result.matched_properties = "surface_type:unsupported";
    return result;
}

GroupRecord BuildOneGroupRecord(
    int group_id,
    const std::vector<int>& face_ids,
    const ValidFaceIndex& index,
    double confidence)
{
    GroupRecord group;
    group.group_id = group_id;
    group.face_ids = face_ids;
    group.source_rules = "surface_prefilter+three_gate_samples";
    group.confidence = confidence;

    double px = 0.0;
    double py = 0.0;
    double pz = 0.0;
    double nx = 0.0;
    double ny = 0.0;
    double nz = 0.0;
    double weight_sum = 0.0;
    SPAunit_vector fallback_normal(1.0, 0.0, 0.0);

    int i = 0;
    for (i = 0; i < (int)face_ids.size(); ++i)
    {
        const FaceRecord* record = FindFaceRecord(index, face_ids[i]);
        if (record == nullptr)
            continue;

        group.faces.push_back(record->face);
        if (group.type.empty())
            group.type = record->face_type;

        const double w = SafeArea(*record);
        group.area_sum += w;
        weight_sum += w;
        if (record->has_representative_point != FALSE)
        {
            px += record->representative_point.x() * w;
            py += record->representative_point.y() * w;
            pz += record->representative_point.z() * w;
        }
        if (record->has_representative_normal != FALSE)
        {
            fallback_normal = record->representative_normal;
            nx += record->representative_normal.x() * w;
            ny += record->representative_normal.y() * w;
            nz += record->representative_normal.z() * w;
        }
    }

    if (weight_sum > 0.0)
        group.seed_point = SPAposition(px / weight_sum, py / weight_sum, pz / weight_sum);
    group.seed_normal = UnitFromComponents(nx, ny, nz, fallback_normal);
    group.local_scale = std::sqrt(std::max(1.0e-12, group.area_sum));
    return group;
}

double AverageAcceptedScoreForGroup(const std::vector<int>& face_ids, const GroupDecisionTable& decisions)
{
    std::set<int> face_set(face_ids.begin(), face_ids.end());
    double score_sum = 0.0;
    int score_count = 0;
    int i = 0;
    for (i = 0; i < (int)decisions.decisions.size(); ++i)
    {
        const GroupMergeDecision& d = decisions.decisions[i];
        if (d.decision == "accept" &&
            face_set.find(d.face_a) != face_set.end() &&
            face_set.find(d.face_b) != face_set.end())
        {
            score_sum += d.score;
            ++score_count;
        }
    }
    if (score_count <= 0)
        return 1.0;
    return score_sum / (double)score_count;
}

void BuildGroupsFromUnionFind(
    const ValidFaceIndex& index,
    UnionFind& union_find,
    Step2GroupState& state)
{
    std::map<int, std::vector<int> > components;
    union_find.BuildComponents(components);

    std::vector<std::vector<int> > sorted_components;
    std::map<int, std::vector<int> >::const_iterator it = components.begin();
    for (; it != components.end(); ++it)
        sorted_components.push_back(it->second);
    std::sort(sorted_components.begin(), sorted_components.end());

    std::vector<ColorRgb255> palette = BuildDistinctColorPalette((int)sorted_components.size());
    int i = 0;
    for (i = 0; i < (int)sorted_components.size(); ++i)
    {
        const double confidence = AverageAcceptedScoreForGroup(sorted_components[i], state.decisions);
        GroupRecord group = BuildOneGroupRecord(i, sorted_components[i], index, confidence);
        int j = 0;
        for (j = 0; j < (int)group.face_ids.size(); ++j)
            state.face_to_group.face_to_group[group.face_ids[j]] = group.group_id;
        if ((int)group.face_ids.size() == 1)
            ++state.stats.single_face_group_count;

        if (i < (int)palette.size())
        {
            for (j = 0; j < (int)group.faces.size(); ++j)
                (void)ApplyFaceColor(group.faces[j], palette[i]);
        }
        state.groups.groups.push_back(group);
    }

    state.stats.group_count = (int)state.groups.groups.size();
}

void EmitAllGroupEvents(DiagnosticSink* diagnostics, const GroupTable& groups)
{
    int i = 0;
    for (i = 0; i < (int)groups.groups.size(); ++i)
        EmitGroupEvent(diagnostics, groups.groups[i]);
}

void EmitStep2ColorMapEvents(DiagnosticSink* diagnostics, const GroupTable& groups)
{
    if (diagnostics == nullptr)
        return;

    std::vector<std::string> tags;
    tags.push_back("step2");
    tags.push_back("grouping");

    const std::vector<ColorRgb255> palette = BuildDistinctColorPalette((int)groups.groups.size());
    int i = 0;
    for (i = 0; i < (int)groups.groups.size(); ++i)
    {
        const GroupRecord& group = groups.groups[i];
        const ColorRgb255 color = i < (int)palette.size()
            ? palette[i]
            : DistinctColorByIndex(i, (int)groups.groups.size());

        std::map<std::string, std::string> properties;
        std::map<std::string, std::string> json_properties;
        properties["group_id"] = IntText(group.group_id);
        properties["face_count"] = IntText((int)group.face_ids.size());
        properties["type"] = group.type;
        json_properties["rgb"] = JsonDump(ColorRgbJson(color));
        json_properties["face_ids"] = JsonDump(FaceIdListJson(group.face_ids));

        (void)diagnostics->EmitColorIdMapEntryIfEnabled(
            tags,
            "step2.group_colored_body",
            "group_id",
            properties,
            json_properties);
    }
}
} // namespace

Step2GroupOptions::Step2GroupOptions()
    : enable_surface_prefilter(TRUE),
      enable_sample_refine(TRUE),
      emit_prefilter_events(TRUE),
      emit_refine_events(TRUE),
      emit_group_events(TRUE),
      emit_group_artifacts(FALSE),
      emit_traced_pair_samples(TRUE),
      normal_angle_deg(15.0),
      axis_angle_deg(15.0),
      plane_tol(-1.0),
      radius_tol(-1.0),
      auto_tolerance(TRUE),
      sample_density(2.0),
      sample_min_count(9),
      sample_max_count(49),
      edge_min_samples_per_edge(8),
      edge_sample_alpha(0.5),
      max_samples_per_face(64),
      edge_length_eps(-1.0),
      auto_edge_length_eps(TRUE),
      local_nearest_count(6),
      local_min_pass_count(2),
      local_distance_gate_scale(1.2),
      local_normal_component_max(0.1)
{
}

GroupBuildStats::GroupBuildStats()
    : candidate_count(0),
      accepted_count(0),
      rejected_count(0),
      group_count(0),
      valid_face_count(0),
      surface_prefilter_pass_count(0),
      surface_prefilter_reject_count(0),
      sample_refine_pass_count(0),
      sample_refine_reject_count(0),
      single_face_group_count(0)
{
}

Step2GroupState::Step2GroupState()
    : input_step1(nullptr)
{
}

Step2GroupResult::Step2GroupResult()
    : ok(FALSE)
{
}

logical RunStep2GroupBuild(
    const Step1FaceAnalyzeState& step1,
    const Step2GroupOptions& options,
    DiagnosticSink* diagnostics,
    Step2GroupResult& result)
{
    result = Step2GroupResult();
    result.state.input_step1 = &step1;
    result.state.options_snapshot = options;

    ValidFaceIndex valid_faces;
    CollectValidFaces(step1, valid_faces);
    result.state.stats.valid_face_count = (int)valid_faces.records.size();
    EmitRunEvent(diagnostics, "start", nullptr, result.state);

    if (valid_faces.records.empty())
    {
        EmitStageSummaryEvents(diagnostics, result.state);
        EmitRunEvent(diagnostics, "finish", "fail", result.state);
        return FALSE;
    }

    std::vector<int> face_ids;
    int i = 0;
    for (i = 0; i < (int)valid_faces.records.size(); ++i)
        face_ids.push_back(valid_faces.records[i]->face_id);

    UnionFind union_find;
    union_find.Reset(face_ids);
    const ResolvedTolerances tol = ResolveTolerances(step1, options);

    int candidate_id = 0;
    for (i = 0; i < (int)valid_faces.records.size(); ++i)
    {
        int j = i + 1;
        for (; j < (int)valid_faces.records.size(); ++j)
        {
            const FaceRecord& a = *valid_faces.records[i];
            const FaceRecord& b = *valid_faces.records[j];
            GroupMergeCandidate candidate = MakeCandidate(candidate_id++, a, b);
            result.state.candidates.candidates.push_back(candidate);
            ++result.state.stats.candidate_count;

            SurfacePrefilterResult prefilter;
            if (options.enable_surface_prefilter != FALSE)
                prefilter = EvaluateSurfacePrefilter(a, b, tol);
            else
            {
                prefilter.passed = TRUE;
                prefilter.reason = "pass_prefilter_disabled";
                prefilter.match_type = "disabled";
                prefilter.matched_properties = "prefilter:disabled";
            }

            if (options.emit_prefilter_events != FALSE)
                EmitPrefilterEvent(diagnostics, a, b, prefilter);

            if (prefilter.passed == FALSE)
            {
                ++result.state.stats.surface_prefilter_reject_count;
                AddDecision(candidate, "reject", prefilter.reason, 0.0, result.state);
                continue;
            }
            ++result.state.stats.surface_prefilter_pass_count;

            PairRefineResult refine;
            if (options.enable_sample_refine != FALSE)
                refine = EvaluatePairBySamples(a, b, options, tol);
            else
            {
                refine.accepted = TRUE;
                refine.reason = "pass_refine_disabled";
                refine.score = 1.0;
            }

            if (options.emit_refine_events != FALSE)
                EmitRefineEvent(diagnostics, a, b, refine);

            if (refine.accepted == FALSE)
            {
                ++result.state.stats.sample_refine_reject_count;
                AddDecision(candidate, "reject", refine.reason, refine.score, result.state);
                continue;
            }

            ++result.state.stats.sample_refine_pass_count;
            AddDecision(candidate, "accept", refine.reason, refine.score, result.state);
            union_find.Unite(candidate.face_a, candidate.face_b);
        }
    }

    BuildGroupsFromUnionFind(valid_faces, union_find, result.state);
    if (options.emit_group_events != FALSE)
        EmitAllGroupEvents(diagnostics, result.state.groups);
    if (diagnostics != nullptr)
    {
        EmitStep2ColorMapEvents(diagnostics, result.state.groups);
        (void)diagnostics->EmitBodySatIfEnabled("step2.group_colored_body", step1.input_body);
    }

    result.ok = TRUE;
    EmitStageSummaryEvents(diagnostics, result.state);
    EmitRunEvent(diagnostics, "finish", "ok", result.state);
    return TRUE;
}
} // namespace midsurface_new
