#include "steps/Step3PairBuild.hpp"

#include "utils/ColorUtils.hpp"
#include "utils/GroupUtils.hpp"
#include "utils/JsonUtils.hpp"
#include "utils/SamplingUtils.hpp"

#include "faceutil.hxx"
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

double Clamp01(double x)
{
    if (x < 0.0)
        return 0.0;
    if (x > 1.0)
        return 1.0;
    return x;
}

double DegToCos(double deg)
{
    return std::cos(deg * kPi / 180.0);
}

double RatioFromCount(int pass, int total)
{
    if (total <= 0)
        return 0.0;
    return ((double)pass) / ((double)total);
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

JsonValue IntListJson(const std::vector<int>& values)
{
    JsonValue arr = JsonValue::array();
    int i = 0;
    for (i = 0; i < (int)values.size(); ++i)
        arr.push_back(values[i]);
    return arr;
}

double Quantile(std::vector<double> vals, double q)
{
    if (vals.empty())
        return -1.0;
    std::sort(vals.begin(), vals.end());
    q = Clamp01(q);
    const double pos = q * (double)(vals.size() - 1);
    const int i0 = (int)std::floor(pos);
    const int i1 = (int)std::ceil(pos);
    if (i0 == i1)
        return vals[i0];
    const double t = pos - (double)i0;
    return vals[i0] * (1.0 - t) + vals[i1] * t;
}

double FacingRelaxFactor(double facing_score, const Step3PairOptions& options)
{
    if (options.pair_enable_facing_dist_relax == FALSE)
        return 1.0;
    if (options.pair_facing_dist_relax_max <= 0.0)
        return 1.0;

    double s = Clamp01(facing_score);
    const double start = Clamp01(options.pair_facing_relax_start);
    if (s <= start)
        return 1.0;

    s = (s - start) / std::max(1.0e-12, 1.0 - start);
    const double p = std::max(1.0, options.pair_facing_dist_relax_power);
    return 1.0 + options.pair_facing_dist_relax_max * std::pow(s, p);
}

StructuredEvent Step3EventTemplate(const char* schema)
{
    StructuredEvent event;
    event.AddTag("step3");
    event.AddTag("pairing");
    (void)schema;
    return event;
}

void EmitEvent(DiagnosticSink* diagnostics, const StructuredEvent& event)
{
    if (diagnostics != nullptr)
        (void)diagnostics->EmitEvent(event);
}

struct PairEvalSample
{
    PairEvalSample()
        : source_group(-1),
          target_group(-1),
          source_face(-1),
          target_face(-1),
          distance(0.0),
          normal_dot(0.0),
          u_dot_source_normal(0.0),
          u_dot_target_normal(0.0),
          normal_ok(FALSE),
          direction_ok(FALSE),
          pass(FALSE)
    {
    }

    int source_group;
    int target_group;
    int source_face;
    int target_face;
    SPAposition point;
    SPAposition closest_point;
    double distance;
    double normal_dot;
    double u_dot_source_normal;
    double u_dot_target_normal;
    logical normal_ok;
    logical direction_ok;
    logical pass;
};

struct DirectionEval
{
    DirectionEval()
        : total(0),
          pass(0),
          has_best(FALSE),
          best_distance(0.0)
    {
    }

    int total;
    int pass;
    std::vector<double> pass_distances;
    logical has_best;
    double best_distance;
    SPAposition best_source_point;
    SPAposition best_target_point;
    std::vector<PairEvalSample> samples;
};

struct RegionPairData
{
    RegionPairData()
        : ga(-1),
          gb(-1),
          distance(0.0),
          score(0.0),
          coverage_small(0.0),
          pass_balance(0.0),
          ratio_ab(0.0),
          ratio_ba(0.0),
          pass_ab(0),
          pass_ba(0),
          total_ab(0),
          total_ba(0),
          gate_pass(0),
          facing_score(0.0),
          adaptive_max(-1.0),
          adaptive_relax(1.0),
          width_a(-1.0),
          width_b(-1.0),
          width_min(-1.0)
    {
    }

    int ga;
    int gb;
    double distance;
    double score;
    double coverage_small;
    double pass_balance;
    double ratio_ab;
    double ratio_ba;
    int pass_ab;
    int pass_ba;
    int total_ab;
    int total_ba;
    int gate_pass;
    double facing_score;
    double adaptive_max;
    double adaptive_relax;
    double width_a;
    double width_b;
    double width_min;
    SPAposition pa;
    SPAposition pb;
    std::string reason;
    std::vector<PairEvalSample> samples;
};

InteriorSampleOptions MakePairInteriorSampleOptions(const Step3PairOptions& options)
{
    InteriorSampleOptions sample_options;
    sample_options.target.density = options.pair_interior_sample_density;
    sample_options.target.min_count = options.pair_interior_sample_min;
    sample_options.target.max_count = options.pair_interior_sample_max;
    sample_options.candidate_multiplier = 4;
    sample_options.candidate_max = 240;
    return sample_options;
}

void EvalDirectionalGroup(
    const GroupRecord& src,
    const GroupRecord& dst,
    const Step2GroupState& step2,
    const Step3PairOptions& options,
    double cos_opp,
    double cos_dir,
    DirectionEval& out)
{
    out = DirectionEval();
    std::vector<std::pair<const FaceRecord*, SPAposition> > samples;
    const InteriorSampleOptions sample_options = MakePairInteriorSampleOptions(options);
    CollectGroupInteriorSamples(src, step2, sample_options, samples);

    int i = 0;
    for (i = 0; i < (int)samples.size(); ++i)
    {
        const FaceRecord* source_record = samples[i].first;
        if (source_record == nullptr || source_record->face == nullptr)
            continue;

        const SPAposition p = samples[i].second;
        GroupClosestPointResult closest;
        if (FindClosestPointOnGroup(dst, step2, p, closest) == FALSE)
            continue;

        ++out.total;

        PairEvalSample eval;
        eval.source_group = src.group_id;
        eval.target_group = dst.group_id;
        eval.source_face = source_record->face_id;
        eval.target_face = closest.face_id;
        eval.point = p;
        eval.closest_point = closest.point;
        eval.distance = closest.distance;

        if (closest.distance <= 1.0e-12)
        {
            out.samples.push_back(eval);
            continue;
        }

        const SPAunit_vector na = sg_get_face_normal(source_record->face, p);
        const SPAunit_vector nb = sg_get_face_normal(closest.face, closest.point);
        eval.normal_dot = na % nb;
        eval.normal_ok = eval.normal_dot <= -cos_opp ? TRUE : FALSE;

        const SPAvector v = closest.point - p;
        eval.u_dot_source_normal = (v % na) / closest.distance;
        eval.u_dot_target_normal = (v % nb) / closest.distance;
        eval.direction_ok =
            (eval.u_dot_source_normal <= -cos_dir &&
             eval.u_dot_target_normal >= cos_dir)
                ? TRUE
                : FALSE;
        eval.pass = (eval.normal_ok != FALSE && eval.direction_ok != FALSE) ? TRUE : FALSE;

        if (eval.pass != FALSE)
        {
            ++out.pass;
            out.pass_distances.push_back(closest.distance);
            if (out.has_best == FALSE || closest.distance < out.best_distance)
            {
                out.has_best = TRUE;
                out.best_distance = closest.distance;
                out.best_source_point = p;
                out.best_target_point = closest.point;
            }
        }
        out.samples.push_back(eval);
    }
}

logical CoarsePairNormalPrefilter(const GroupRecord& a, const GroupRecord& b, double cos_opp)
{
    if (a.type == "plane" && b.type == "plane")
        return (a.seed_normal % b.seed_normal) <= -cos_opp ? TRUE : FALSE;
    return TRUE;
}

logical FastPairSurfacePrefilter(const GroupRecord& a, const GroupRecord& b, const Step3PairOptions& options)
{
    if (options.pair_same_type_only != FALSE)
    {
        if (a.type != b.type)
            return FALSE;
    }
    else
    {
        if (!a.type.empty() && !b.type.empty() && a.type != "unknown" && b.type != "unknown" && a.type != b.type)
            return FALSE;
    }

    const double s = std::max(1.0e-9, std::max(GroupScale(a), GroupScale(b)));
    const double seed_dist = (b.seed_point - a.seed_point).len();
    if (seed_dist > 18.0 * s)
        return FALSE;
    return TRUE;
}

logical EvaluateRegionPair(
    const GroupRecord& ga,
    const GroupRecord& gb,
    const Step2GroupState& step2,
    const Step3PairOptions& options,
    double cos_opp,
    RegionPairData& out)
{
    out = RegionPairData();
    out.ga = ga.group_id;
    out.gb = gb.group_id;
    const double cos_dir = DegToCos(options.pair_direction_angle_deg);

    DirectionEval ab;
    DirectionEval ba;
    EvalDirectionalGroup(ga, gb, step2, options, cos_opp, cos_dir, ab);
    EvalDirectionalGroup(gb, ga, step2, options, cos_opp, cos_dir, ba);

    const int gate_pass = std::max(ab.pass, ba.pass);
    if (gate_pass < std::max(1, options.pair_min_pass_count))
    {
        out.reason = "fail_min_pass_count";
        return FALSE;
    }

    const double ratio_ab = RatioFromCount(ab.pass, ab.total);
    const double ratio_ba = RatioFromCount(ba.pass, ba.total);
    const double pass_ratio_gate = std::max(ratio_ab, ratio_ba);
    if (options.pair_min_pass_ratio > 0.0 && pass_ratio_gate < options.pair_min_pass_ratio)
    {
        out.reason = "fail_min_pass_ratio";
        return FALSE;
    }

    std::vector<double> pass_dists;
    pass_dists.insert(pass_dists.end(), ab.pass_distances.begin(), ab.pass_distances.end());
    pass_dists.insert(pass_dists.end(), ba.pass_distances.begin(), ba.pass_distances.end());
    if (pass_dists.empty())
    {
        out.reason = "fail_empty_distance_set";
        return FALSE;
    }

    const double dist_rep = Quantile(pass_dists, options.pair_distance_quantile);
    if (dist_rep <= 0.0)
    {
        out.reason = "fail_invalid_thickness";
        return FALSE;
    }

    const double normal_score = Clamp01((-(ga.seed_normal % gb.seed_normal) - cos_opp) / (1.0 - cos_opp + 1.0e-12));
    if (options.pair_thickness_min > 0.0 && dist_rep < options.pair_thickness_min)
    {
        out.reason = "fail_thickness_min";
        return FALSE;
    }
    if (options.pair_thickness_max > 0.0 && dist_rep > options.pair_thickness_max)
    {
        out.reason = "fail_thickness_max";
        return FALSE;
    }

    if (options.pair_adaptive_dist_ratio > 0.0)
    {
        const double wa = GroupWidthScale(ga, step2);
        const double wb = GroupWidthScale(gb, step2);
        const double w = std::max(1.0e-12, std::min(wa, wb));
        out.width_a = wa;
        out.width_b = wb;
        out.width_min = w;
        double adaptive_max = options.pair_adaptive_dist_ratio * w;
        const double relax = FacingRelaxFactor(normal_score, options);
        adaptive_max *= relax;
        if (dist_rep > adaptive_max)
        {
            out.reason = "fail_adaptive_thickness";
            out.adaptive_max = adaptive_max;
            out.adaptive_relax = relax;
            return FALSE;
        }
        out.adaptive_max = adaptive_max;
        out.adaptive_relax = relax;
    }

    const logical a_is_small = GroupAreaSafe(ga) <= GroupAreaSafe(gb) ? TRUE : FALSE;
    const double coverage_small = a_is_small != FALSE ? ratio_ab : ratio_ba;
    if (options.pair_min_small_coverage > 0.0 && coverage_small < options.pair_min_small_coverage)
    {
        const int abs_keep_gate = std::max(6, 2 * std::max(1, options.pair_min_pass_count));
        if (gate_pass < abs_keep_gate)
        {
            out.reason = "fail_small_coverage";
            return FALSE;
        }
    }

    const int min_pass = std::min(ab.pass, ba.pass);
    const int max_pass = std::max(ab.pass, ba.pass);
    const double pass_balance = max_pass > 0 ? ((double)min_pass) / ((double)max_pass) : 0.0;

    const double thinness = dist_rep / std::max(1.0e-12, std::min(GroupScale(ga), GroupScale(gb)));
    if (options.pair_thinness_max > 0.0 && thinness > options.pair_thinness_max)
    {
        out.reason = "fail_thinness";
        return FALSE;
    }

    const double count_score = Clamp01(((double)gate_pass) / ((double)(std::max(1, options.pair_min_pass_count) * 2)));
    const double coverage_score = Clamp01(coverage_small);
    const double balance_score = Clamp01(pass_balance);
    const double score = 0.40 * count_score + 0.30 * coverage_score + 0.20 * normal_score + 0.10 * balance_score;
    if (score < options.pair_min_score)
    {
        out.reason = "fail_score";
        return FALSE;
    }

    out.distance = dist_rep;
    out.score = score;
    out.coverage_small = coverage_small;
    out.pass_balance = pass_balance;
    out.ratio_ab = ratio_ab;
    out.ratio_ba = ratio_ba;
    out.pass_ab = ab.pass;
    out.pass_ba = ba.pass;
    out.total_ab = ab.total;
    out.total_ba = ba.total;
    out.gate_pass = gate_pass;
    out.facing_score = normal_score;
    out.reason = "accept_region_pair";

    if (ab.has_best != FALSE && ba.has_best != FALSE)
    {
        if (ab.best_distance <= ba.best_distance)
        {
            out.pa = ab.best_source_point;
            out.pb = ab.best_target_point;
        }
        else
        {
            out.pa = ba.best_target_point;
            out.pb = ba.best_source_point;
        }
    }
    else if (ab.has_best != FALSE)
    {
        out.pa = ab.best_source_point;
        out.pb = ab.best_target_point;
    }
    else if (ba.has_best != FALSE)
    {
        out.pa = ba.best_target_point;
        out.pb = ba.best_source_point;
    }
    else
    {
        out.reason = "fail_missing_best_pass";
        return FALSE;
    }

    out.samples.reserve(ab.samples.size() + ba.samples.size());
    out.samples.insert(out.samples.end(), ab.samples.begin(), ab.samples.end());
    out.samples.insert(out.samples.end(), ba.samples.begin(), ba.samples.end());
    return TRUE;
}

bool PairBetter(const RegionPairData& a, const RegionPairData& b)
{
    if (a.score != b.score)
        return a.score > b.score;
    if (a.coverage_small != b.coverage_small)
        return a.coverage_small > b.coverage_small;
    if (a.gate_pass != b.gate_pass)
        return a.gate_pass > b.gate_pass;
    return a.distance < b.distance;
}

void AddDecision(
    const PairCandidate& candidate,
    const char* decision,
    const std::string& reason,
    double score,
    Step3PairState& state)
{
    PairDecisionRecord record;
    record.candidate_id = candidate.candidate_id;
    record.group_a = candidate.group_a;
    record.group_b = candidate.group_b;
    record.decision = decision == nullptr ? "" : decision;
    record.reason = reason;
    record.score = score;
    state.decisions.decisions.push_back(record);

    if (record.decision == "accept")
        ++state.stats.accepted_count;
    else
        ++state.stats.rejected_count;
}

JsonValue PairSamplesJson(const std::vector<PairEvalSample>& samples)
{
    JsonValue out = JsonValue::array();
    int limit = std::min((int)samples.size(), 80);
    int i = 0;
    for (i = 0; i < limit; ++i)
    {
        const PairEvalSample& s = samples[i];
        JsonValue item = JsonValue::object();
        item["i"] = i;
        item["source_group"] = s.source_group;
        item["target_group"] = s.target_group;
        item["source_face"] = s.source_face;
        item["target_face"] = s.target_face;
        item["p"] = JsonPoint(s.point);
        item["cp"] = JsonPoint(s.closest_point);
        item["dist"] = s.distance;
        item["normal_dot"] = s.normal_dot;
        item["u_dot_source_normal"] = s.u_dot_source_normal;
        item["u_dot_target_normal"] = s.u_dot_target_normal;
        item["normal_ok"] = s.normal_ok != FALSE;
        item["direction_ok"] = s.direction_ok != FALSE;
        item["pass"] = s.pass != FALSE;
        out.push_back(item);
    }
    return out;
}

void EmitCandidateEvent(
    DiagnosticSink* diagnostics,
    const PairCandidate& candidate,
    const char* stage,
    const char* result_text,
    const std::string& reason)
{
    StructuredEvent event = Step3EventTemplate("step3.pair_candidate.v1");
    event.AddTag("candidate");
    if (stage != nullptr)
        event.AddTag(stage);
    event.AddTag("detail");
    event.SetProperty("candidate_id", IntText(candidate.candidate_id));
    event.SetProperty("group_a", IntText(candidate.group_a));
    event.SetProperty("group_b", IntText(candidate.group_b));
    event.SetProperty("result", result_text == nullptr ? "" : result_text);
    event.SetProperty("source", candidate.source);
    if (!reason.empty())
        event.SetProperty("reason", reason);
    EmitEvent(diagnostics, event);
}

void EmitRefineEvent(
    DiagnosticSink* diagnostics,
    const PairCandidate& candidate,
    const RegionPairData& data,
    const char* result_text)
{
    StructuredEvent event = Step3EventTemplate("step3.pair_refine.v1");
    event.AddTag("refine");
    event.AddTag("detail");
    event.SetProperty("candidate_id", IntText(candidate.candidate_id));
    event.SetProperty("group_a", IntText(candidate.group_a));
    event.SetProperty("group_b", IntText(candidate.group_b));
    event.SetProperty("result", result_text == nullptr ? "" : result_text);
    event.SetProperty("reason", data.reason);
    event.SetProperty("thickness", DoubleText(data.distance));
    event.SetProperty("score", DoubleText(data.score));
    event.SetProperty("coverage", DoubleText(data.coverage_small));
    event.SetProperty("pass_ab", IntText(data.pass_ab));
    event.SetProperty("total_ab", IntText(data.total_ab));
    event.SetProperty("pass_ba", IntText(data.pass_ba));
    event.SetProperty("total_ba", IntText(data.total_ba));
    event.SetProperty("gate_pass", IntText(data.gate_pass));
    event.SetProperty("facing_score", DoubleText(data.facing_score));
    event.SetProperty("width_a", DoubleText(data.width_a));
    event.SetProperty("width_b", DoubleText(data.width_b));
    event.SetProperty("width_min", DoubleText(data.width_min));
    event.SetProperty("adaptive_max", DoubleText(data.adaptive_max));
    event.SetProperty("adaptive_relax", DoubleText(data.adaptive_relax));
    event.SetJsonProperty("samples", JsonDump(PairSamplesJson(data.samples)));
    EmitEvent(diagnostics, event);
}

void EmitPairEvent(DiagnosticSink* diagnostics, const PairRecord& pair)
{
    StructuredEvent event = Step3EventTemplate("step3.pair_record.v1");
    event.AddTag("pair");
    event.AddTag("record");
    event.AddTag("summary");
    event.AddTag("single");
    event.SetProperty("pair_id", IntText(pair.pair_id));
    event.SetProperty("group_a", IntText(pair.group_a));
    event.SetProperty("group_b", IntText(pair.group_b));
    event.SetProperty("thickness", DoubleText(pair.thickness));
    event.SetProperty("coverage", DoubleText(pair.coverage));
    event.SetProperty("score", DoubleText(pair.score));
    event.SetProperty("facing_score", DoubleText(pair.facing_score));
    event.SetProperty("pass_ratio", DoubleText(pair.pass_ratio));
    event.SetProperty("pass_balance", DoubleText(pair.pass_balance));
    event.SetProperty("uncertain", pair.uncertain != FALSE ? "true" : "false");
    event.SetProperty("rib_candidate", pair.rib_candidate != FALSE ? "true" : "false");
    event.SetProperty("reason", pair.reason);
    EmitEvent(diagnostics, event);
}

void EmitWallEvent(DiagnosticSink* diagnostics, const WallRecord& wall)
{
    StructuredEvent event = Step3EventTemplate("step3.wall_record.v1");
    event.AddTag("wall");
    event.AddTag("summary");
    event.AddTag("single");
    event.SetProperty("wall_id", IntText(wall.wall_id));
    event.SetProperty("group_id", IntText(wall.group_a));
    event.SetProperty("face_count", IntText((int)wall.source_face_ids.size()));
    event.SetProperty("source", wall.source);
    event.SetJsonProperty("face_ids", JsonDump(IntListJson(wall.source_face_ids)));
    EmitEvent(diagnostics, event);
}

void EmitGroupRoleEvents(DiagnosticSink* diagnostics, const Step2GroupState& step2, const Step3PairState& state)
{
    if (diagnostics == nullptr)
        return;

    int i = 0;
    for (i = 0; i < (int)step2.groups.groups.size(); ++i)
    {
        const GroupRecord& group = step2.groups.groups[i];
        StructuredEvent event = Step3EventTemplate("step3.group_role.v1");
        event.AddTag("group-role");
        event.AddTag("summary");
        event.AddTag("single");
        event.SetProperty("group_id", IntText(group.group_id));

        const std::map<int, std::vector<int> >::const_iterator pit =
            state.group_to_pairs.group_to_pairs.find(group.group_id);
        if (pit != state.group_to_pairs.group_to_pairs.end() && !pit->second.empty())
        {
            event.SetProperty("role", "pair");
            event.SetProperty("pair_count", IntText((int)pit->second.size()));
            event.SetJsonProperty("pair_ids", JsonDump(IntListJson(pit->second)));
        }
        else
        {
            event.SetProperty("role", "wall");
            event.SetProperty("pair_count", "0");
            const std::map<int, int>::const_iterator wit =
                state.group_to_walls.group_to_wall.find(group.group_id);
            event.SetProperty("wall_id", IntText(wit == state.group_to_walls.group_to_wall.end() ? -1 : wit->second));
        }
        EmitEvent(diagnostics, event);
    }
}

void EmitRunEvent(DiagnosticSink* diagnostics, const char* phase, const char* result_text, const Step3PairState& state)
{
    StructuredEvent event = Step3EventTemplate("step3.pair_run.v1");
    event.AddTag(phase == nullptr ? "run" : phase);
    if (phase != nullptr && std::string(phase) == "finish")
    {
        event.AddTag("summary");
        event.AddTag("all");
    }
    (void)result_text;
    event.SetProperty("candidate_count", IntText(state.stats.candidate_count));
    event.SetProperty("accepted_count", IntText(state.stats.accepted_count));
    event.SetProperty("rejected_count", IntText(state.stats.rejected_count));
    event.SetProperty("pair_count", IntText(state.stats.pair_count));
    event.SetProperty("coarse_prefilter_pass_count", IntText(state.stats.coarse_prefilter_pass_count));
    event.SetProperty("coarse_prefilter_reject_count", IntText(state.stats.coarse_prefilter_reject_count));
    event.SetProperty("surface_prefilter_pass_count", IntText(state.stats.surface_prefilter_pass_count));
    event.SetProperty("surface_prefilter_reject_count", IntText(state.stats.surface_prefilter_reject_count));
    event.SetProperty("refine_pass_count", IntText(state.stats.refine_pass_count));
    event.SetProperty("refine_reject_count", IntText(state.stats.refine_reject_count));
    event.SetProperty("thickness_filter_dropped_count", IntText(state.stats.thickness_filter_dropped_count));
    event.SetProperty("rib_candidate_count", IntText(state.stats.rib_candidate_count));
    event.SetProperty("uncertain_count", IntText(state.stats.uncertain_count));
    event.SetProperty("wall_count", IntText(state.stats.wall_count));
    event.SetProperty("pair_group_count", IntText(state.stats.pair_group_count));
    event.SetProperty("wall_group_count", IntText(state.stats.wall_group_count));
    EmitEvent(diagnostics, event);
}

void EmitStageSummaryEvents(DiagnosticSink* diagnostics, const Step3PairState& state)
{
    StructuredEvent candidate = Step3EventTemplate("step3.pair_candidate_stage.v1");
    candidate.AddTag("candidate");
    candidate.AddTag("summary");
    candidate.AddTag("stage");
    candidate.AddTag("finish");
    candidate.SetProperty("candidate_count", IntText(state.stats.candidate_count));
    candidate.SetProperty("coarse_prefilter_pass_count", IntText(state.stats.coarse_prefilter_pass_count));
    candidate.SetProperty("coarse_prefilter_reject_count", IntText(state.stats.coarse_prefilter_reject_count));
    candidate.SetProperty("surface_prefilter_pass_count", IntText(state.stats.surface_prefilter_pass_count));
    candidate.SetProperty("surface_prefilter_reject_count", IntText(state.stats.surface_prefilter_reject_count));
    EmitEvent(diagnostics, candidate);

    StructuredEvent refine = Step3EventTemplate("step3.pair_refine_stage.v1");
    refine.AddTag("refine");
    refine.AddTag("summary");
    refine.AddTag("stage");
    refine.AddTag("finish");
    refine.SetProperty("pass_count", IntText(state.stats.refine_pass_count));
    refine.SetProperty("reject_count", IntText(state.stats.refine_reject_count));
    refine.SetProperty("thickness_filter_dropped_count", IntText(state.stats.thickness_filter_dropped_count));
    EmitEvent(diagnostics, refine);

    StructuredEvent role = Step3EventTemplate("step3.group_role_stage.v1");
    role.AddTag("group-role");
    role.AddTag("summary");
    role.AddTag("stage");
    role.AddTag("finish");
    role.SetProperty("pair_count", IntText(state.stats.pair_count));
    role.SetProperty("wall_count", IntText(state.stats.wall_count));
    role.SetProperty("pair_group_count", IntText(state.stats.pair_group_count));
    role.SetProperty("wall_group_count", IntText(state.stats.wall_group_count));
    EmitEvent(diagnostics, role);
}

PairCandidate MakeCandidate(int candidate_id, const GroupRecord& a, const GroupRecord& b)
{
    PairCandidate candidate;
    candidate.candidate_id = candidate_id;
    candidate.group_a = std::min(a.group_id, b.group_id);
    candidate.group_b = std::max(a.group_id, b.group_id);
    candidate.source = "all_group_pair";
    return candidate;
}

PairRecord MakePairRecord(int pair_id, const RegionPairData& data, const Step2GroupState& step2, const Step3PairOptions& options)
{
    PairRecord pair;
    pair.pair_id = pair_id;
    pair.group_a = data.ga;
    pair.group_b = data.gb;
    pair.thickness = data.distance;
    pair.coverage = data.coverage_small;
    pair.score = data.score;
    pair.facing_score = data.facing_score;
    pair.pass_ratio = std::max(data.ratio_ab, data.ratio_ba);
    pair.pass_balance = data.pass_balance;
    pair.point_a = data.pa;
    pair.point_b = data.pb;
    pair.reason = data.reason;

    const SPAvector dir = data.pb - data.pa;
    const double len = dir.len();
    if (len > 1.0e-12)
        pair.pair_direction = SPAunit_vector(dir.x() / len, dir.y() / len, dir.z() / len);

    const GroupRecord& ga = step2.groups.groups[data.ga];
    const GroupRecord& gb = step2.groups.groups[data.gb];
    const double area_a = std::max(1.0e-12, GroupAreaSafe(ga));
    const double area_b = std::max(1.0e-12, GroupAreaSafe(gb));
    const double area_ratio = area_a < area_b ? area_a / area_b : area_b / area_a;
    pair.rib_candidate = area_ratio < options.rib_area_ratio ? TRUE : FALSE;
    pair.uncertain =
        (data.gate_pass < options.pair_min_pass_count || data.coverage_small < 0.25)
            ? TRUE
            : FALSE;
    return pair;
}

void FilterPairsByPerGroupThickness(
    std::vector<PairRecord>& pairs,
    int group_count,
    double thickness_ratio_max,
    DiagnosticSink* diagnostics,
    int& out_dropped)
{
    out_dropped = 0;
    if (pairs.empty() || group_count <= 0 || thickness_ratio_max <= 0.0)
        return;

    std::vector<int> pair_count_per_group(group_count, 0);
    std::vector<double> min_thickness_per_group(group_count, 1.0e300);
    int i = 0;
    for (i = 0; i < (int)pairs.size(); ++i)
    {
        const PairRecord& p = pairs[i];
        if (p.group_a >= 0 && p.group_a < group_count)
        {
            ++pair_count_per_group[p.group_a];
            if (p.thickness < min_thickness_per_group[p.group_a])
                min_thickness_per_group[p.group_a] = p.thickness;
        }
        if (p.group_b >= 0 && p.group_b < group_count)
        {
            ++pair_count_per_group[p.group_b];
            if (p.thickness < min_thickness_per_group[p.group_b])
                min_thickness_per_group[p.group_b] = p.thickness;
        }
    }

    std::vector<PairRecord> kept;
    kept.reserve(pairs.size());
    for (i = 0; i < (int)pairs.size(); ++i)
    {
        const PairRecord& p = pairs[i];
        logical keep = TRUE;
        logical drop_by_group_a = FALSE;
        logical drop_by_group_b = FALSE;
        if (p.group_a >= 0 && p.group_a < group_count && pair_count_per_group[p.group_a] > 1)
        {
            if (p.thickness > min_thickness_per_group[p.group_a] * thickness_ratio_max)
            {
                keep = FALSE;
                drop_by_group_a = TRUE;
            }
        }
        if (p.group_b >= 0 && p.group_b < group_count && pair_count_per_group[p.group_b] > 1)
        {
            if (p.thickness > min_thickness_per_group[p.group_b] * thickness_ratio_max)
            {
                keep = FALSE;
                drop_by_group_b = TRUE;
            }
        }
        if (keep != FALSE)
            kept.push_back(p);
        else
        {
            StructuredEvent event = Step3EventTemplate("step3.thickness_filter_detail.v1");
            event.AddTag("thickness-filter");
            event.AddTag("detail");
            event.SetProperty("result", "drop");
            event.SetProperty("reason", "fail_group_thickness_ratio");
            event.SetProperty("pair_id_before_filter", IntText(p.pair_id));
            event.SetProperty("group_a", IntText(p.group_a));
            event.SetProperty("group_b", IntText(p.group_b));
            event.SetProperty("thickness", DoubleText(p.thickness));
            event.SetProperty("ratio_max", DoubleText(thickness_ratio_max));
            event.SetProperty("group_a_pair_count", (p.group_a >= 0 && p.group_a < group_count) ? IntText(pair_count_per_group[p.group_a]) : "-1");
            event.SetProperty("group_b_pair_count", (p.group_b >= 0 && p.group_b < group_count) ? IntText(pair_count_per_group[p.group_b]) : "-1");
            event.SetProperty("group_a_min_thickness", (p.group_a >= 0 && p.group_a < group_count) ? DoubleText(min_thickness_per_group[p.group_a]) : "-1");
            event.SetProperty("group_b_min_thickness", (p.group_b >= 0 && p.group_b < group_count) ? DoubleText(min_thickness_per_group[p.group_b]) : "-1");
            event.SetProperty("group_a_limit", (p.group_a >= 0 && p.group_a < group_count) ? DoubleText(min_thickness_per_group[p.group_a] * thickness_ratio_max) : "-1");
            event.SetProperty("group_b_limit", (p.group_b >= 0 && p.group_b < group_count) ? DoubleText(min_thickness_per_group[p.group_b] * thickness_ratio_max) : "-1");
            event.SetProperty("drop_by_group_a", drop_by_group_a != FALSE ? "true" : "false");
            event.SetProperty("drop_by_group_b", drop_by_group_b != FALSE ? "true" : "false");
            EmitEvent(diagnostics, event);
            ++out_dropped;
        }
    }
    pairs.swap(kept);
}

void ReassignPairIds(std::vector<PairRecord>& pairs)
{
    int i = 0;
    for (i = 0; i < (int)pairs.size(); ++i)
        pairs[i].pair_id = i;
}

void BuildGroupToPairMap(const std::vector<PairRecord>& pairs, GroupToPairMap& out_map)
{
    out_map.group_to_pairs.clear();
    int i = 0;
    for (i = 0; i < (int)pairs.size(); ++i)
    {
        out_map.group_to_pairs[pairs[i].group_a].push_back(pairs[i].pair_id);
        out_map.group_to_pairs[pairs[i].group_b].push_back(pairs[i].pair_id);
    }
}

void BuildWallsFromUnpairedGroups(
    const Step2GroupState& step2,
    const GroupToPairMap& group_to_pairs,
    WallTable& out_walls,
    GroupToWallMap& out_map)
{
    out_walls.walls.clear();
    out_map.group_to_wall.clear();

    int gi = 0;
    for (gi = 0; gi < (int)step2.groups.groups.size(); ++gi)
    {
        const GroupRecord& group = step2.groups.groups[gi];
        const std::map<int, std::vector<int> >::const_iterator pit =
            group_to_pairs.group_to_pairs.find(group.group_id);
        if (pit != group_to_pairs.group_to_pairs.end() && !pit->second.empty())
            continue;

        WallRecord wall;
        wall.wall_id = (int)out_walls.walls.size();
        wall.group_a = group.group_id;
        wall.source = "step3_unpaired_group";
        wall.source_face_ids = group.face_ids;
        out_map.group_to_wall[group.group_id] = wall.wall_id;
        out_walls.walls.push_back(wall);
    }
}

void ColorPairs(const std::vector<PairRecord>& pairs, const Step2GroupState& step2)
{
    std::vector<ColorRgb255> palette = BuildDistinctColorPalette((int)pairs.size());
    int i = 0;
    for (i = 0; i < (int)pairs.size(); ++i)
    {
        const ColorRgb255 color = i < (int)palette.size() ? palette[i] : DistinctColorByIndex(i, (int)pairs.size());
        const PairRecord& pair = pairs[i];
        const int gids[2] = { pair.group_a, pair.group_b };
        int side = 0;
        for (side = 0; side < 2; ++side)
        {
            const int gid = gids[side];
            if (gid < 0 || gid >= (int)step2.groups.groups.size())
                continue;
            const GroupRecord& group = step2.groups.groups[gid];
            int j = 0;
            for (j = 0; j < (int)group.faces.size(); ++j)
                (void)ApplyFaceColor(group.faces[j], color);
        }
    }
}

void ColorWalls(const std::vector<WallRecord>& walls, const Step2GroupState& step2)
{
    std::vector<ColorRgb255> palette = BuildDistinctColorPalette((int)walls.size());
    int i = 0;
    for (i = 0; i < (int)walls.size(); ++i)
    {
        const ColorRgb255 color = i < (int)palette.size()
            ? palette[i]
            : DistinctColorByIndex(i, (int)walls.size());
        const WallRecord& wall = walls[i];
        const int gid = wall.group_a;
        if (gid < 0 || gid >= (int)step2.groups.groups.size())
            continue;
        const GroupRecord& group = step2.groups.groups[gid];
        int j = 0;
        for (j = 0; j < (int)group.faces.size(); ++j)
            (void)ApplyFaceColor(group.faces[j], color);
    }
}

JsonValue PairFaceIdsJson(const PairRecord& pair, const Step2GroupState& step2)
{
    JsonValue face_ids = JsonValue::array();
    std::set<int> seen;
    const int gids[2] = { pair.group_a, pair.group_b };
    int side = 0;
    for (side = 0; side < 2; ++side)
    {
        const int gid = gids[side];
        if (gid < 0 || gid >= (int)step2.groups.groups.size())
            continue;
        const GroupRecord& group = step2.groups.groups[gid];
        int i = 0;
        for (i = 0; i < (int)group.face_ids.size(); ++i)
        {
            if (seen.insert(group.face_ids[i]).second)
                face_ids.push_back(group.face_ids[i]);
        }
    }
    return face_ids;
}

JsonValue WallFaceIdsJson(const WallRecord& wall)
{
    return IntListJson(wall.source_face_ids);
}

void EmitStep3ColorMapEvents(
    DiagnosticSink* diagnostics,
    const std::vector<PairRecord>& pairs,
    const std::vector<WallRecord>& walls,
    const Step2GroupState& step2)
{
    if (diagnostics == nullptr)
        return;

    std::vector<std::string> tags;
    tags.push_back("step3");
    tags.push_back("pairing");

    const std::vector<ColorRgb255> palette = BuildDistinctColorPalette((int)pairs.size());
    int i = 0;
    for (i = 0; i < (int)pairs.size(); ++i)
    {
        const PairRecord& pair = pairs[i];
        const ColorRgb255 color = i < (int)palette.size()
            ? palette[i]
            : DistinctColorByIndex(i, (int)pairs.size());

        const JsonValue face_ids = PairFaceIdsJson(pair, step2);
        std::map<std::string, std::string> properties;
        std::map<std::string, std::string> json_properties;
        properties["pair_id"] = IntText(pair.pair_id);
        properties["group_a"] = IntText(pair.group_a);
        properties["group_b"] = IntText(pair.group_b);
        properties["face_count"] = IntText((int)face_ids.size());
        json_properties["rgb"] = JsonDump(ColorRgbJson(color));
        json_properties["face_ids"] = JsonDump(face_ids);

        (void)diagnostics->EmitColorIdMapEntryIfEnabled(
            tags,
            "step3.pair_colored_body",
            "pair_id",
            properties,
            json_properties);
    }

    std::vector<std::string> wall_tags;
    wall_tags.push_back("step3");
    wall_tags.push_back("pairing");
    wall_tags.push_back("wall");

    const std::vector<ColorRgb255> wall_palette = BuildDistinctColorPalette((int)walls.size());
    for (i = 0; i < (int)walls.size(); ++i)
    {
        const WallRecord& wall = walls[i];
        const ColorRgb255 color = i < (int)wall_palette.size()
            ? wall_palette[i]
            : DistinctColorByIndex(i, (int)walls.size());

        const JsonValue face_ids = WallFaceIdsJson(wall);
        std::map<std::string, std::string> properties;
        std::map<std::string, std::string> json_properties;
        properties["wall_id"] = IntText(wall.wall_id);
        properties["group_id"] = IntText(wall.group_a);
        properties["face_count"] = IntText((int)face_ids.size());
        json_properties["rgb"] = JsonDump(ColorRgbJson(color));
        json_properties["face_ids"] = JsonDump(face_ids);

        (void)diagnostics->EmitColorIdMapEntryIfEnabled(
            wall_tags,
            "step3.pair_colored_body",
            "wall_id",
            properties,
            json_properties);
    }
}
} // namespace

Step3PairOptions::Step3PairOptions()
    : enable_coarse_normal_prefilter(TRUE),
      enable_surface_prefilter(TRUE),
      enable_pair_refine(TRUE),
      allow_multi_pair_per_group(TRUE),
      emit_candidate_events(TRUE),
      emit_refine_events(TRUE),
      emit_pair_events(TRUE),
      pair_opposite_angle_deg(20.0),
      pair_direction_angle_deg(14.0),
      pair_min_pass_count(3),
      pair_min_pass_ratio(0.4),
      pair_min_small_coverage(0.20),
      pair_same_type_only(FALSE),
      pair_interior_sample_density(2.0),
      pair_interior_sample_min(9),
      pair_interior_sample_max(36),
      pair_distance_quantile(0.50),
      pair_thickness_min(-1.0),
      pair_thickness_max(-1.0),
      pair_adaptive_dist_ratio(1.0),
      pair_enable_facing_dist_relax(TRUE),
      pair_facing_relax_start(0.70),
      pair_facing_dist_relax_max(0.80),
      pair_facing_dist_relax_power(2.0),
      pair_thinness_max(-1.0),
      pair_min_score(0.35),
      pair_max_per_group(-1),
      pair_group_thickness_ratio_max(2.0),
      rib_area_ratio(0.35)
{
}

PairBuildStats::PairBuildStats()
    : candidate_count(0),
      accepted_count(0),
      rejected_count(0),
      pair_count(0),
      coarse_prefilter_pass_count(0),
      coarse_prefilter_reject_count(0),
      surface_prefilter_pass_count(0),
      surface_prefilter_reject_count(0),
      refine_pass_count(0),
      refine_reject_count(0),
      thickness_filter_dropped_count(0),
      rib_candidate_count(0),
      uncertain_count(0),
      wall_count(0),
      pair_group_count(0),
      wall_group_count(0)
{
}

Step3PairState::Step3PairState()
    : input_step2(nullptr)
{
}

Step3PairResult::Step3PairResult()
    : ok(FALSE)
{
}

logical RunStep3PairBuild(
    const Step2GroupState& step2,
    const Step3PairOptions& options,
    DiagnosticSink* diagnostics,
    Step3PairResult& result)
{
    result = Step3PairResult();
    result.state.input_step2 = &step2;
    result.state.options_snapshot = options;
    EmitRunEvent(diagnostics, "start", nullptr, result.state);

    if (step2.groups.groups.empty())
    {
        EmitStageSummaryEvents(diagnostics, result.state);
        EmitRunEvent(diagnostics, "finish", "fail", result.state);
        return FALSE;
    }

    const double cos_opp = DegToCos(options.pair_opposite_angle_deg);
    std::vector<RegionPairData> accepted_candidates;
    std::vector<PairCandidate> accepted_pair_candidates;

    int candidate_id = 0;
    int a = 0;
    for (a = 0; a < (int)step2.groups.groups.size(); ++a)
    {
        int b = a + 1;
        for (; b < (int)step2.groups.groups.size(); ++b)
        {
            const GroupRecord& ga = step2.groups.groups[a];
            const GroupRecord& gb = step2.groups.groups[b];
            PairCandidate candidate = MakeCandidate(candidate_id++, ga, gb);
            result.state.candidates.candidates.push_back(candidate);
            ++result.state.stats.candidate_count;

            if (options.enable_coarse_normal_prefilter != FALSE &&
                CoarsePairNormalPrefilter(ga, gb, cos_opp) == FALSE)
            {
                ++result.state.stats.coarse_prefilter_reject_count;
                AddDecision(candidate, "reject", "fail_coarse_normal_prefilter", 0.0, result.state);
                if (options.emit_candidate_events != FALSE)
                    EmitCandidateEvent(diagnostics, candidate, "coarse_normal", "fail", "fail_coarse_normal_prefilter");
                continue;
            }
            ++result.state.stats.coarse_prefilter_pass_count;

            if (options.enable_surface_prefilter != FALSE &&
                FastPairSurfacePrefilter(ga, gb, options) == FALSE)
            {
                ++result.state.stats.surface_prefilter_reject_count;
                AddDecision(candidate, "reject", "fail_surface_prefilter", 0.0, result.state);
                if (options.emit_candidate_events != FALSE)
                    EmitCandidateEvent(diagnostics, candidate, "surface", "fail", "fail_surface_prefilter");
                continue;
            }
            ++result.state.stats.surface_prefilter_pass_count;
            if (options.emit_candidate_events != FALSE)
                EmitCandidateEvent(diagnostics, candidate, "surface", "pass", "pass_prefilter");

            RegionPairData data;
            logical refine_ok = TRUE;
            if (options.enable_pair_refine != FALSE)
                refine_ok = EvaluateRegionPair(ga, gb, step2, options, cos_opp, data);
            else
            {
                data.ga = ga.group_id;
                data.gb = gb.group_id;
                data.score = 1.0;
                data.reason = "pass_refine_disabled";
            }

            if (options.emit_refine_events != FALSE)
                EmitRefineEvent(diagnostics, candidate, data, refine_ok != FALSE ? "pass" : "fail");

            if (refine_ok == FALSE)
            {
                ++result.state.stats.refine_reject_count;
                AddDecision(candidate, "reject", data.reason.empty() ? "fail_refine" : data.reason, data.score, result.state);
                continue;
            }

            ++result.state.stats.refine_pass_count;
            accepted_candidates.push_back(data);
            accepted_pair_candidates.push_back(candidate);
        }
    }

    std::vector<int> order(accepted_candidates.size(), 0);
    int i = 0;
    for (i = 0; i < (int)order.size(); ++i)
        order[i] = i;
    std::sort(order.begin(), order.end(), [&accepted_candidates](int ia, int ib) {
        return PairBetter(accepted_candidates[ia], accepted_candidates[ib]);
    });

    int max_per_group = options.pair_max_per_group;
    if (options.allow_multi_pair_per_group == FALSE)
        max_per_group = 1;
    std::vector<int> used(step2.groups.groups.size(), 0);
    std::set<std::pair<int, int> > seen;

    std::vector<PairRecord> selected_pairs;
    for (i = 0; i < (int)order.size(); ++i)
    {
        const int idx = order[i];
        const RegionPairData& data = accepted_candidates[idx];
        const PairCandidate& candidate = accepted_pair_candidates[idx];
        if (max_per_group > 0)
        {
            if (data.ga < 0 || data.ga >= (int)used.size() ||
                data.gb < 0 || data.gb >= (int)used.size() ||
                used[data.ga] >= max_per_group ||
                used[data.gb] >= max_per_group)
            {
                AddDecision(candidate, "reject", "fail_max_per_group", data.score, result.state);
                continue;
            }
        }

        const int g0 = std::min(data.ga, data.gb);
        const int g1 = std::max(data.ga, data.gb);
        const std::pair<int, int> key(g0, g1);
        if (seen.find(key) != seen.end())
        {
            AddDecision(candidate, "reject", "fail_duplicate_pair", data.score, result.state);
            continue;
        }
        seen.insert(key);
        if (max_per_group > 0)
        {
            ++used[data.ga];
            ++used[data.gb];
        }

        PairRecord pair = MakePairRecord((int)selected_pairs.size(), data, step2, options);
        if (pair.rib_candidate != FALSE)
            ++result.state.stats.rib_candidate_count;
        if (pair.uncertain != FALSE)
            ++result.state.stats.uncertain_count;
        selected_pairs.push_back(pair);
        AddDecision(candidate, "accept", data.reason, data.score, result.state);
    }

    int dropped = 0;
    FilterPairsByPerGroupThickness(
        selected_pairs,
        (int)step2.groups.groups.size(),
        options.pair_group_thickness_ratio_max,
        diagnostics,
        dropped);
    result.state.stats.thickness_filter_dropped_count = dropped;
    ReassignPairIds(selected_pairs);
    result.state.pairs.pairs = selected_pairs;
    result.state.stats.pair_count = (int)selected_pairs.size();
    BuildGroupToPairMap(result.state.pairs.pairs, result.state.group_to_pairs);
    BuildWallsFromUnpairedGroups(
        step2,
        result.state.group_to_pairs,
        result.state.walls,
        result.state.group_to_walls);
    result.state.stats.wall_count = (int)result.state.walls.walls.size();
    result.state.stats.wall_group_count = (int)result.state.group_to_walls.group_to_wall.size();
    result.state.stats.pair_group_count = (int)result.state.group_to_pairs.group_to_pairs.size();
    ColorPairs(result.state.pairs.pairs, step2);
    ColorWalls(result.state.walls.walls, step2);

    if (options.emit_pair_events != FALSE)
    {
        for (i = 0; i < (int)result.state.pairs.pairs.size(); ++i)
            EmitPairEvent(diagnostics, result.state.pairs.pairs[i]);
        for (i = 0; i < (int)result.state.walls.walls.size(); ++i)
            EmitWallEvent(diagnostics, result.state.walls.walls[i]);
        EmitGroupRoleEvents(diagnostics, step2, result.state);
    }
    if (diagnostics != nullptr)
    {
        EmitStep3ColorMapEvents(diagnostics, result.state.pairs.pairs, result.state.walls.walls, step2);
        (void)diagnostics->EmitBodySatIfEnabled(
            "step3.pair_colored_body",
            step2.input_step1 == nullptr ? nullptr : step2.input_step1->input_body);
    }

    result.ok = TRUE;
    EmitStageSummaryEvents(diagnostics, result.state);
    EmitRunEvent(diagnostics, "finish", "ok", result.state);
    return TRUE;
}
} // namespace midsurface_new
