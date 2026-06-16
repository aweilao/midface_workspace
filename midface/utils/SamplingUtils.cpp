#include "utils/SamplingUtils.hpp"

#include "box.hxx"
#include "cstrapi.hxx"
#include "curveq.hxx"
#include "getbox.hxx"
#include "intrapi.hxx"
#include "kernapi.hxx"
#include "queryapi.hxx"
#include "wire_qry.hxx"

#include <algorithm>
#include <cmath>

namespace midsurface_new
{
namespace
{
double PositiveOr(double value, double fallback)
{
    return value > 0.0 ? value : fallback;
}

double SafeArea(double area_proxy)
{
    return area_proxy > 1.0e-12 ? area_proxy : 1.0e-12;
}

void AddProjectedEdgeSamples(
    const FaceRecord& source,
    EDGE* edge,
    int sample_count,
    double dedup_tol2,
    const char* role,
    std::vector<PointSample>& out_samples)
{
    if (edge == nullptr || sample_count <= 0)
        return;

    SPAposition p0;
    SPAposition p1;
    if (get_curve_ends(edge, p0, p1) == FALSE)
        return;

    int k = 0;
    for (k = 0; k < sample_count; ++k)
    {
        const double u = ((double)k + 0.5) / (double)sample_count;
        const SPAposition guess(
            p0.x() + (p1.x() - p0.x()) * u,
            p0.y() + (p1.y() - p0.y()) * u,
            p0.z() + (p1.z() - p0.z()) * u);

        SPAposition on_edge;
        double d = 0.0;
        SPAposition query = guess;
        outcome edge_result = api_entity_point_distance(edge, query, on_edge, d);
        if (!edge_result.ok())
            continue;

        SPAposition on_face;
        if (ProjectPointToFace(source.face, on_edge, on_face) == FALSE)
            on_face = on_edge;

        if (IsNearAnyPointSample(on_face, out_samples, dedup_tol2) == FALSE)
            AddPointSample(source.face_id, on_face, source.representative_normal, role, out_samples);
    }
}

logical AddArcLengthEdgeSamples(
    const FaceRecord& source,
    EDGE* edge,
    int sample_count,
    double dedup_tol2,
    const char* role,
    std::vector<PointSample>& out_samples)
{
    if (edge == nullptr || sample_count <= 0)
        return FALSE;

    ENTITY_LIST edge_list;
    edge_list.add((ENTITY*)edge);

    const int request_count = sample_count + 2;
    std::vector<SPAposition> points((size_t)request_count);
    outcome sample_result = api_arc_len_samples_from_edges(edge_list, request_count, &points[0]);
    if (!sample_result.ok())
        return FALSE;

    int added = 0;
    int i = 0;
    for (i = 1; i < request_count - 1; ++i)
    {
        SPAposition on_face;
        if (ProjectPointToFace(source.face, points[i], on_face) == FALSE)
            on_face = points[i];

        if (IsNearAnyPointSample(on_face, out_samples, dedup_tol2) == FALSE)
        {
            AddPointSample(source.face_id, on_face, source.representative_normal, role, out_samples);
            ++added;
        }
    }

    return added > 0 ? TRUE : FALSE;
}
} // namespace

SampleTargetOptions::SampleTargetOptions()
    : density(1.0),
      min_count(1),
      max_count(1)
{
}

BoundarySampleOptions::BoundarySampleOptions()
    : min_samples_per_edge(1),
      extra_sample_alpha(0.0),
      max_samples_per_face(1),
      edge_length_eps(1.0e-9),
      dedup_tol2(1.0e-12),
      role("boundary")
{
}

InteriorSampleOptions::InteriorSampleOptions()
    : candidate_multiplier(4),
      candidate_max(240)
{
}

int ComputeSampleTarget(double area_proxy, const SampleTargetOptions& options)
{
    const int lo = std::max(1, options.min_count);
    const int hi = std::max(lo, options.max_count);
    const double density = PositiveOr(options.density, 1.0);
    const int n = (int)std::ceil(density * std::sqrt(SafeArea(area_proxy)));
    return std::max(lo, std::min(hi, n));
}

logical CollectFaceEdges(FACE* face, std::vector<EDGE*>& out_edges)
{
    out_edges.clear();
    if (face == nullptr)
        return FALSE;

    ENTITY_LIST edge_entities;
    outcome result = api_get_edges((ENTITY*)face, edge_entities);
    if (!result.ok())
        return FALSE;

    edge_entities.init();
    ENTITY* entity = nullptr;
    while ((entity = edge_entities.next()) != nullptr)
    {
        if (entity != nullptr && is_EDGE(entity))
            out_edges.push_back((EDGE*)entity);
    }
    return TRUE;
}

logical ProjectPointToFace(FACE* face, const SPAposition& guess, SPAposition& out_point)
{
    if (face == nullptr)
        return FALSE;
    outcome result = api_find_cls_ptto_face(guess, face, out_point);
    return result.ok() ? TRUE : FALSE;
}

logical IsNearAnyPoint(const SPAposition& p, const std::vector<SPAposition>& samples, double tol2)
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

logical IsNearAnyPointSample(const SPAposition& p, const std::vector<PointSample>& samples, double tol2)
{
    int i = 0;
    for (i = 0; i < (int)samples.size(); ++i)
    {
        const SPAvector d = samples[i].point - p;
        if ((d % d) <= tol2)
            return TRUE;
    }
    return FALSE;
}

void AddPointSample(
    int face_id,
    const SPAposition& point,
    const SPAunit_vector& normal,
    const char* role,
    std::vector<PointSample>& out_samples)
{
    PointSample sample;
    sample.face_id = face_id;
    sample.point = point;
    sample.normal = normal;
    sample.role = role == nullptr ? "" : role;
    out_samples.push_back(sample);
}

logical BuildBoundaryPointSamples(
    const FaceRecord& source,
    const BoundarySampleOptions& options,
    std::vector<PointSample>& out_samples,
    std::string& out_reason)
{
    out_samples.clear();
    out_reason.clear();
    if (source.face == nullptr)
    {
        out_reason = "missing_source_face";
        return FALSE;
    }

    std::vector<EDGE*> edges;
    if (CollectFaceEdges(source.face, edges) == FALSE || edges.empty())
    {
        if (source.has_representative_point != FALSE)
        {
            AddPointSample(source.face_id, source.representative_point, source.representative_normal, "fallback_representative", out_samples);
            out_reason = "fallback_representative";
            return TRUE;
        }
        out_reason = "no_edges";
        return FALSE;
    }

    const int target_n = ComputeSampleTarget(SafeArea(source.area_proxy), options.target);
    const int min_per_edge = std::max(1, options.min_samples_per_edge);
    int max_samples = std::max(1, options.max_samples_per_face);
    max_samples = std::max(max_samples, min_per_edge * (int)edges.size());

    int extra_budget = (int)std::floor(std::max(0.0, options.extra_sample_alpha) * (double)target_n + 0.5);
    const int base_total = min_per_edge * (int)edges.size();
    if (base_total + extra_budget > max_samples)
        extra_budget = std::max(0, max_samples - base_total);

    std::vector<double> edge_lengths(edges.size(), 0.0);
    double total_length = 0.0;
    int i = 0;
    for (i = 0; i < (int)edges.size(); ++i)
    {
        const double length = edges[i] == nullptr ? 0.0 : edges[i]->length(TRUE);
        if (length > options.edge_length_eps)
        {
            edge_lengths[i] = length;
            total_length += length;
        }
    }
    if (total_length <= 1.0e-12)
        total_length = 1.0;

    for (i = 0; i < (int)edges.size(); ++i)
    {
        int extra = 0;
        if (extra_budget > 0 && edge_lengths[i] > 0.0)
            extra = (int)std::floor((double)extra_budget * edge_lengths[i] / total_length + 0.5);
        const int sample_count = min_per_edge + extra;
        if (AddArcLengthEdgeSamples(source, edges[i], sample_count, options.dedup_tol2, options.role, out_samples) == FALSE)
            AddProjectedEdgeSamples(source, edges[i], sample_count, options.dedup_tol2, options.role, out_samples);
    }

    if (out_samples.empty() && source.has_representative_point != FALSE)
    {
        AddPointSample(source.face_id, source.representative_point, source.representative_normal, "fallback_representative", out_samples);
        out_reason = "fallback_representative";
    }

    if (out_reason.empty())
        out_reason = out_samples.empty() ? "no_samples" : "ok";
    return out_samples.empty() ? FALSE : TRUE;
}

logical BuildInteriorPositionSamples(
    const FaceRecord& record,
    const InteriorSampleOptions& options,
    std::vector<SPAposition>& out_samples,
    std::string& out_reason)
{
    out_samples.clear();
    out_reason.clear();
    if (record.face == nullptr)
    {
        out_reason = "missing_face";
        return FALSE;
    }

    SPAbox box = get_face_box(record.face);
    SPAposition lo = box.low();
    SPAposition hi = box.high();
    const double cx = 0.5 * (lo.x() + hi.x());
    const double cy = 0.5 * (lo.y() + hi.y());
    const double cz = 0.5 * (lo.z() + hi.z());

    const SPAvector diag_v = hi - lo;
    const double diag2 = diag_v % diag_v;
    const double dedup_tol2 = std::max(1.0e-16, diag2 * 1.0e-12);

    const int target_n = ComputeSampleTarget(SafeArea(record.area_proxy), options.target);
    const int candidate_target = std::min(target_n * std::max(1, options.candidate_multiplier), std::max(target_n, options.candidate_max));

    SPAposition center_sample;
    if (ProjectPointToFace(record.face, SPAposition(cx, cy, cz), center_sample) != FALSE)
        out_samples.push_back(center_sample);

    int i = 0;
    for (i = 0; i < candidate_target * 3 && (int)out_samples.size() < candidate_target; ++i)
    {
        const int hidx = i + 1;
        double f2 = 1.0, r2 = 0.0;
        int x2 = hidx;
        while (x2 > 0)
        {
            f2 /= 2.0;
            r2 += f2 * (double)(x2 % 2);
            x2 /= 2;
        }
        double f3 = 1.0, r3 = 0.0;
        int x3 = hidx;
        while (x3 > 0)
        {
            f3 /= 3.0;
            r3 += f3 * (double)(x3 % 3);
            x3 /= 3;
        }
        double f5 = 1.0, r5 = 0.0;
        int x5 = hidx;
        while (x5 > 0)
        {
            f5 /= 5.0;
            r5 += f5 * (double)(x5 % 5);
            x5 /= 5;
        }

        const SPAposition guess(
            lo.x() + (hi.x() - lo.x()) * r2,
            lo.y() + (hi.y() - lo.y()) * r3,
            lo.z() + (hi.z() - lo.z()) * r5);

        SPAposition p;
        if (ProjectPointToFace(record.face, guess, p) == FALSE)
            continue;
        if (IsNearAnyPoint(p, out_samples, dedup_tol2) == FALSE)
            out_samples.push_back(p);
    }

    if ((int)out_samples.size() > target_n)
    {
        std::vector<SPAposition> reduced;
        reduced.reserve((size_t)target_n);
        const double stride = ((double)out_samples.size()) / ((double)target_n);
        for (i = 0; i < target_n; ++i)
        {
            int idx = (int)std::floor(((double)i + 0.5) * stride);
            if (idx < 0)
                idx = 0;
            if (idx >= (int)out_samples.size())
                idx = (int)out_samples.size() - 1;
            reduced.push_back(out_samples[idx]);
        }
        out_samples.swap(reduced);
    }

    if (out_samples.empty() && record.has_representative_point != FALSE)
    {
        out_samples.push_back(record.representative_point);
        out_reason = "fallback_representative";
        return TRUE;
    }

    out_reason = out_samples.empty() ? "no_samples" : "ok";
    return out_samples.empty() ? FALSE : TRUE;
}
} // namespace midsurface_new
