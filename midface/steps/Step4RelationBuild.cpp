#include "steps/Step4RelationBuild.hpp"

#include "utils/GroupUtils.hpp"
#include "utils/JsonUtils.hpp"

#include "coedge.hxx"
#include "cstrapi.hxx"
#include "edge.hxx"
#include "faceutil.hxx"
#include "geometry.hxx"
#include "kernapi.hxx"
#include "loop.hxx"
#include "queryapi.hxx"
#include "sweepapi.hxx"
#include "wire_qry.hxx"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <map>
#include <set>
#include <sstream>
#include <vector>

namespace midsurface_new
{
namespace
{
const double kPi = 3.14159265358979323846;
const double kTinyFaceLongEdgeAreaRatio = 1000.0;
const int kTinyFaceEdgeLenSamples = 9;
const int kTinyPartnerSampleCount = 7;
const int kHitNormalSampleCount = 5;
logical g_step4_sweep_init_done = FALSE;

struct PairLinkAccum
{
    PairLinkAccum()
        : aa(0),
          ab(0),
          ba(0),
          bb(0),
          direct_hits(0),
          orphan_hits(0)
    {
    }

    int aa;
    int ab;
    int ba;
    int bb;
    int direct_hits;
    int orphan_hits;
};

struct PairDirOverride
{
    PairDirOverride()
        : dir_i(1.0, 0.0, 0.0),
          dir_j(1.0, 0.0, 0.0),
          has_i(FALSE),
          has_j(FALSE)
    {
    }

    SPAunit_vector dir_i;
    SPAunit_vector dir_j;
    logical has_i;
    logical has_j;
};

typedef std::map<std::pair<int, int>, std::map<int, std::vector<COEDGE*> > > PairSlotHitCoedgeMap;

struct GroupAdjBuildStatsLocal
{
    GroupAdjBuildStatsLocal()
        : coedge_total(0),
          coedge_with_partner(0),
          coedge_partner_ring_gt2(0),
          unique_nonmanifold_edges(0),
          partner_ring_max(0),
          tiny_faces_found(0),
          coedges_with_tiny_partner(0),
          tiny_partner_patch_applied(0)
    {
    }

    int coedge_total;
    int coedge_with_partner;
    int coedge_partner_ring_gt2;
    int unique_nonmanifold_edges;
    int partner_ring_max;
    int tiny_faces_found;
    int coedges_with_tiny_partner;
    int tiny_partner_patch_applied;
};

struct WallCoedgeTouch
{
    WallCoedgeTouch()
        : pair_id(-1),
          side(-1),
          edge_len(0.0),
          src_coedge(nullptr),
          has_dir(FALSE),
          dir(1.0, 0.0, 0.0)
    {
    }

    int pair_id;
    int side;
    double edge_len;
    COEDGE* src_coedge;
    logical has_dir;
    SPAunit_vector dir;
};

struct OrphanBridgeStats
{
    OrphanBridgeStats()
        : orphan_wall_groups(0),
          wall_faces_total(0),
          injected_links(0),
          face_dedup_skip(0)
    {
    }

    int orphan_wall_groups;
    int wall_faces_total;
    int injected_links;
    int face_dedup_skip;
};

struct Mm2HitBucket
{
    Mm2HitBucket()
        : slot(-1),
          group_i(-1),
          group_j(-1),
          from_orphan_recorded(FALSE),
          from_partner_collected(FALSE),
          hit_weight(0),
          len_sum(0.0),
          longest_len(0.0)
    {
    }

    int slot;
    int group_i;
    int group_j;
    logical from_orphan_recorded;
    logical from_partner_collected;
    int hit_weight;
    double len_sum;
    double longest_len;
    std::vector<COEDGE*> coedges;
    std::map<COEDGE*, int> coedge_owner_mask;
};

struct Mm2SweepWallBuild
{
    Mm2SweepWallBuild()
        : body(nullptr),
          group_i(-1),
          group_j(-1),
          bucket_slot(-1),
          from_orphan_recorded(FALSE),
          from_partner_collected(FALSE),
          coedge_len(0.0),
          half_profile(0.0),
          hit_normal_cos_abs(-1.0)
    {
    }

    BODY* body;
    int group_i;
    int group_j;
    int bucket_slot;
    logical from_orphan_recorded;
    logical from_partner_collected;
    double coedge_len;
    double half_profile;
    double hit_normal_cos_abs;
};

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

void AddUniqueCoedgePtr(std::vector<COEDGE*>& values, COEDGE* c)
{
    if (c == nullptr)
        return;
    int i = 0;
    for (i = 0; i < (int)values.size(); ++i)
    {
        if (values[i] == c)
            return;
    }
    values.push_back(c);
}

StructuredEvent Step4EventTemplate()
{
    StructuredEvent event;
    event.AddTag("step4");
    event.AddTag("relation");
    return event;
}

void EmitEvent(DiagnosticSink* diagnostics, const StructuredEvent& event)
{
    if (diagnostics != nullptr)
        (void)diagnostics->EmitEvent(event);
}

StructuredEvent Step4DetailEvent()
{
    StructuredEvent event = Step4EventTemplate();
    event.AddTag("detail");
    return event;
}

StructuredEvent Step4SingleSummaryEvent()
{
    StructuredEvent event = Step4EventTemplate();
    event.AddTag("summary");
    event.AddTag("single");
    return event;
}

StructuredEvent Step4StageSummaryEvent()
{
    StructuredEvent event = Step4EventTemplate();
    event.AddTag("summary");
    event.AddTag("stage");
    return event;
}

StructuredEvent Step4AllSummaryEvent()
{
    StructuredEvent event = Step4EventTemplate();
    event.AddTag("summary");
    event.AddTag("all");
    return event;
}

std::pair<int, int> GroupKey(int a, int b)
{
    if (a <= b)
        return std::make_pair(a, b);
    return std::make_pair(b, a);
}

int GetGroupAdjHit(const std::map<std::pair<int, int>, int>& group_hits, int g0, int g1)
{
    if (g0 < 0 || g1 < 0 || g0 == g1)
        return 0;
    const std::map<std::pair<int, int>, int>::const_iterator it = group_hits.find(GroupKey(g0, g1));
    return it == group_hits.end() ? 0 : it->second;
}

const GroupRecord* FindGroupById(const Step2GroupState& step2, int group_id)
{
    int i = 0;
    for (i = 0; i < (int)step2.groups.groups.size(); ++i)
    {
        if (step2.groups.groups[i].group_id == group_id)
            return &step2.groups.groups[i];
    }
    return nullptr;
}

const PairRecord* FindPairById(const Step3PairState& step3, int pair_id)
{
    int i = 0;
    for (i = 0; i < (int)step3.pairs.pairs.size(); ++i)
    {
        if (step3.pairs.pairs[i].pair_id == pair_id)
            return &step3.pairs.pairs[i];
    }
    return nullptr;
}

void BuildFaceToGroupMap(const Step2GroupState& step2, std::map<FACE*, int>& face_to_group)
{
    face_to_group.clear();
    int gi = 0;
    for (gi = 0; gi < (int)step2.groups.groups.size(); ++gi)
    {
        const GroupRecord& group = step2.groups.groups[gi];
        int fi = 0;
        for (fi = 0; fi < (int)group.faces.size(); ++fi)
        {
            if (group.faces[fi] != nullptr)
                face_to_group[group.faces[fi]] = group.group_id;
        }
    }
}

double CoedgeLengthSafe(COEDGE* c)
{
    if (c == nullptr)
        return 0.0;

    const double t0 = coedge_start_param(c);
    const double t1 = coedge_end_param(c);
    SPAposition prev = coedge_param_pos(c, t0);
    double len = 0.0;

    int si = 1;
    for (si = 1; si < kTinyFaceEdgeLenSamples; ++si)
    {
        const double alpha = (double)si / (double)(kTinyFaceEdgeLenSamples - 1);
        const double t = t0 + (t1 - t0) * alpha;
        const SPAposition cur = coedge_param_pos(c, t);
        const SPAvector d = cur - prev;
        len += d.len();
        prev = cur;
    }
    return len > 0.0 ? len : 0.0;
}

logical BuildCoedgeDirection(COEDGE* c, SPAunit_vector& out_dir)
{
    out_dir = SPAunit_vector(1.0, 0.0, 0.0);
    if (c == nullptr)
        return FALSE;

    const double t0 = coedge_start_param(c);
    const double t1 = coedge_end_param(c);
    const double dt = t1 - t0;
    if (std::fabs(dt) <= 1.0e-12)
        return FALSE;

    const double eps = 1.0e-3;
    const SPAposition pa = coedge_param_pos(c, t0 + eps * dt);
    const SPAposition pb = coedge_param_pos(c, t1 - eps * dt);
    const SPAvector v = pb - pa;
    const double n = v.len();
    if (n <= 1.0e-12)
        return FALSE;
    out_dir = SPAunit_vector(v.x() / n, v.y() / n, v.z() / n);
    return TRUE;
}

logical BuildCoedgeSamplePoints(COEDGE* c, int sample_count, std::vector<SPAposition>& out_pts)
{
    out_pts.clear();
    if (c == nullptr)
        return FALSE;
    if (sample_count < 1)
        sample_count = 1;

    const double t0 = coedge_start_param(c);
    const double t1 = coedge_end_param(c);
    if (std::fabs(t1 - t0) <= 1.0e-15)
        return FALSE;

    int si = 0;
    for (si = 0; si < sample_count; ++si)
    {
        const double u = 0.1 + 0.8 * (((double)si + 0.5) / (double)sample_count);
        const double t = t0 + (t1 - t0) * u;
        out_pts.push_back(coedge_param_pos(c, t));
    }
    return out_pts.empty() ? FALSE : TRUE;
}

void SampleCoedgePoints(COEDGE* c, int sample_count, std::vector<SPAposition>& out_pts)
{
    out_pts.clear();
    if (c == nullptr)
        return;
    if (sample_count < 2)
        sample_count = 2;

    const double t0 = coedge_start_param(c);
    const double t1 = coedge_end_param(c);
    int si = 0;
    for (si = 0; si < sample_count; ++si)
    {
        const double alpha = (sample_count <= 1) ? 0.0 : ((double)si / (double)(sample_count - 1));
        const double t = t0 + (t1 - t0) * alpha;
        out_pts.push_back(coedge_param_pos(c, t));
    }
}

logical TryGetFaceNormalAtPoint(FACE* face, const SPAposition& p, SPAunit_vector& out_n)
{
    out_n = SPAunit_vector(1.0, 0.0, 0.0);
    if (face == nullptr)
        return FALSE;
    try
    {
        out_n = sg_get_face_normal(face, p);
        return TRUE;
    }
    catch (...)
    {
        return FALSE;
    }
}

void CollectGroupCoedges(const Step2GroupState& step2, int group_id, std::vector<COEDGE*>& out_coedges)
{
    out_coedges.clear();
    const GroupRecord* group = FindGroupById(step2, group_id);
    if (group == nullptr)
        return;

    int fi = 0;
    for (fi = 0; fi < (int)group->faces.size(); ++fi)
    {
        FACE* f = group->faces[fi];
        if (f == nullptr)
            continue;
        LOOP* lp = f->loop();
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
}

logical CoedgeTouchesGroupByPartnerRing(COEDGE* c, int target_group, const std::map<FACE*, int>& face_to_group)
{
    if (c == nullptr || target_group < 0)
        return FALSE;
    std::set<COEDGE*> seen;
    COEDGE* p = c->partner();
    while (p != nullptr && seen.insert(p).second)
    {
        FACE* pf = (p->loop() != nullptr) ? p->loop()->face() : nullptr;
        const std::map<FACE*, int>::const_iterator it = face_to_group.find(pf);
        if (it != face_to_group.end() && it->second == target_group)
            return TRUE;
        p = p->partner();
    }
    return FALSE;
}

logical SelectLongestCoedge(const std::vector<COEDGE*>& coedges, COEDGE*& out_best)
{
    out_best = nullptr;
    double best_len = -1.0;
    int i = 0;
    for (i = 0; i < (int)coedges.size(); ++i)
    {
        const double len = CoedgeLengthSafe(coedges[i]);
        if (len > best_len)
        {
            best_len = len;
            out_best = coedges[i];
        }
    }
    return out_best != nullptr ? TRUE : FALSE;
}

logical FindClosestPointOnCoedgeSet(
    const SPAposition& query,
    const std::vector<COEDGE*>& coedges,
    SPAposition& out_cp,
    FACE*& out_face,
    double& out_dist)
{
    out_cp = SPAposition(0.0, 0.0, 0.0);
    out_face = nullptr;
    out_dist = DBL_MAX;

    logical found = FALSE;
    int i = 0;
    for (i = 0; i < (int)coedges.size(); ++i)
    {
        COEDGE* c = coedges[i];
        if (c == nullptr || c->edge() == nullptr)
            continue;

        SPAposition in_p = query;
        SPAposition cp;
        double d = DBL_MAX;
        if (!api_entity_point_distance((ENTITY*)c->edge(), in_p, cp, d).ok())
            continue;

        if (found == FALSE || d < out_dist)
        {
            found = TRUE;
            out_dist = d;
            out_cp = cp;
            out_face = (c->loop() != nullptr) ? c->loop()->face() : nullptr;
        }
    }
    return found;
}

logical GetFaceAreaSafe(FACE* f, double& out_area)
{
    out_area = 0.0;
    if (f == nullptr)
        return FALSE;
    double accuracy = 0.0;
    outcome r = api_ent_area((ENTITY*)f, 1.0e-6, out_area, accuracy, nullptr);
    return (r.ok() && out_area > 0.0) ? TRUE : FALSE;
}

void BuildTinyFaceSet(const Step2GroupState& step2, std::set<FACE*>& out_tiny_faces)
{
    out_tiny_faces.clear();
    int gi = 0;
    for (gi = 0; gi < (int)step2.groups.groups.size(); ++gi)
    {
        const GroupRecord& group = step2.groups.groups[gi];
        int fi = 0;
        for (fi = 0; fi < (int)group.faces.size(); ++fi)
        {
            FACE* f = group.faces[fi];
            double area = 0.0;
            if (GetFaceAreaSafe(f, area) == FALSE)
                continue;

            double longest = 0.0;
            LOOP* lp = f->loop();
            while (lp != nullptr)
            {
                COEDGE* start = lp->start();
                COEDGE* c = start;
                if (c != nullptr)
                {
                    do
                    {
                        longest = std::max(longest, CoedgeLengthSafe(c));
                        c = c->next();
                    } while (c != nullptr && c != start);
                }
                lp = lp->next();
            }

            if (longest > 0.0 && longest * longest >= kTinyFaceLongEdgeAreaRatio * area)
                out_tiny_faces.insert(f);
        }
    }
}

void CollectTinyFaceNeighborGroups(
    FACE* tiny_face,
    int src_group,
    const std::map<FACE*, int>& face_to_group,
    const std::set<FACE*>& tiny_faces,
    std::set<int>& out_groups)
{
    if (tiny_face == nullptr)
        return;

    LOOP* lp = tiny_face->loop();
    while (lp != nullptr)
    {
        COEDGE* start = lp->start();
        COEDGE* c = start;
        if (c != nullptr)
        {
            do
            {
                std::set<COEDGE*> seen_ring;
                COEDGE* p = c->partner();
                while (p != nullptr && seen_ring.insert(p).second)
                {
                    FACE* pf = (p->loop() != nullptr) ? p->loop()->face() : nullptr;
                    if (pf != nullptr && tiny_faces.find(pf) == tiny_faces.end())
                    {
                        const std::map<FACE*, int>::const_iterator it = face_to_group.find(pf);
                        if (it != face_to_group.end() && it->second >= 0 && it->second != src_group)
                            out_groups.insert(it->second);
                    }
                    p = p->partner();
                }
                c = c->next();
            } while (c != nullptr && c != start);
        }
        lp = lp->next();
    }
}

logical AvgSampleDistanceToGroup(
    const std::vector<SPAposition>& samples,
    const GroupRecord& group,
    double& out_avg_dist)
{
    out_avg_dist = DBL_MAX;
    if (samples.empty())
        return FALSE;

    double sum = 0.0;
    int count = 0;
    int si = 0;
    for (si = 0; si < (int)samples.size(); ++si)
    {
        int fi = 0;
        double best = DBL_MAX;
        logical found = FALSE;
        for (fi = 0; fi < (int)group.faces.size(); ++fi)
        {
            FACE* f = group.faces[fi];
            if (f == nullptr)
                continue;
            SPAposition in_p = samples[si];
            SPAposition cp;
            double d = DBL_MAX;
            outcome r = api_entity_point_distance((ENTITY*)f, in_p, cp, d);
            if (!r.ok())
                continue;
            found = TRUE;
            if (d < best)
                best = d;
        }
        if (found != FALSE)
        {
            sum += best;
            ++count;
        }
    }
    if (count <= 0)
        return FALSE;
    out_avg_dist = sum / (double)count;
    return TRUE;
}

logical ResolveTinyPartnerHitGroup(
    COEDGE* src_coedge,
    int src_group,
    const std::set<FACE*>& tiny_partner_faces,
    const Step2GroupState& step2,
    const std::map<FACE*, int>& face_to_group,
    const std::set<FACE*>& tiny_faces,
    int& out_group,
    double& out_avg_dist)
{
    out_group = -1;
    out_avg_dist = DBL_MAX;
    if (src_coedge == nullptr || tiny_partner_faces.empty())
        return FALSE;

    std::set<int> candidate_groups;
    std::set<FACE*>::const_iterator fit = tiny_partner_faces.begin();
    for (; fit != tiny_partner_faces.end(); ++fit)
        CollectTinyFaceNeighborGroups(*fit, src_group, face_to_group, tiny_faces, candidate_groups);
    if (candidate_groups.empty())
        return FALSE;

    std::vector<SPAposition> samples;
    SampleCoedgePoints(src_coedge, kTinyPartnerSampleCount, samples);
    if (samples.empty())
        return FALSE;

    std::set<int>::const_iterator git = candidate_groups.begin();
    for (; git != candidate_groups.end(); ++git)
    {
        const GroupRecord* group = FindGroupById(step2, *git);
        if (group == nullptr || group->group_id == src_group)
            continue;

        double avg_dist = DBL_MAX;
        if (AvgSampleDistanceToGroup(samples, *group, avg_dist) == FALSE)
            continue;
        if (avg_dist < out_avg_dist)
        {
            out_avg_dist = avg_dist;
            out_group = group->group_id;
        }
    }
    return out_group >= 0 ? TRUE : FALSE;
}

void BuildGroupAdjacencyHitsByPartner(
    const Step2GroupState& step2,
    const Step4RelationOptions& options,
    DiagnosticSink* diagnostics,
    const std::map<FACE*, int>& face_to_group,
    std::map<std::pair<int, int>, int>& out_group_hits,
    GroupAdjBuildStatsLocal& out_stats)
{
    out_group_hits.clear();
    out_stats = GroupAdjBuildStatsLocal();

    std::set<FACE*> tiny_faces;
    if (options.enable_tiny_face_partner_patch != FALSE)
        BuildTinyFaceSet(step2, tiny_faces);
    out_stats.tiny_faces_found = (int)tiny_faces.size();

    std::set<EDGE*> nonmanifold_edges;
    int gi = 0;
    for (gi = 0; gi < (int)step2.groups.groups.size(); ++gi)
    {
        const GroupRecord& group = step2.groups.groups[gi];
        int fi = 0;
        for (fi = 0; fi < (int)group.faces.size(); ++fi)
        {
            FACE* f = group.faces[fi];
            if (f == nullptr)
                continue;

            LOOP* lp = f->loop();
            while (lp != nullptr)
            {
                COEDGE* start = lp->start();
                COEDGE* c = start;
                if (c != nullptr)
                {
                    do
                    {
                        out_stats.coedge_total += 1;
                        std::set<COEDGE*> seen_partner_ring;
                        std::set<int> edge_hit_groups;
                        std::set<FACE*> tiny_partner_faces;

                        COEDGE* p = c->partner();
                        while (p != nullptr && seen_partner_ring.insert(p).second)
                        {
                            FACE* nf = (p->loop() != nullptr) ? p->loop()->face() : nullptr;
                            const std::map<FACE*, int>::const_iterator it = face_to_group.find(nf);
                            if (it != face_to_group.end())
                            {
                                if (options.enable_tiny_face_partner_patch != FALSE &&
                                    tiny_faces.find(nf) != tiny_faces.end())
                                {
                                    tiny_partner_faces.insert(nf);
                                }
                                else
                                {
                                    const int gj = it->second;
                                    if (gj != group.group_id && group.group_id < gj &&
                                        edge_hit_groups.insert(gj).second)
                                    {
                                        out_group_hits[GroupKey(group.group_id, gj)] += 1;
                                        StructuredEvent event = Step4DetailEvent();
                                        event.AddTag("group-adjacency");
                                        event.SetProperty("group_a", IntText(group.group_id));
                                        event.SetProperty("group_b", IntText(gj));
                                        event.SetProperty("source", "coedge_partner");
                                        event.SetProperty("ring_size", IntText((int)seen_partner_ring.size()));
                                        event.SetProperty("hit_delta", "1");
                                        EmitEvent(diagnostics, event);
                                    }
                                }
                            }
                            p = p->partner();
                        }

                        if (options.enable_tiny_face_partner_patch != FALSE && !tiny_partner_faces.empty())
                        {
                            out_stats.coedges_with_tiny_partner += 1;
                            int tiny_hit_group = -1;
                            double tiny_avg_dist = DBL_MAX;
                            if (ResolveTinyPartnerHitGroup(
                                    c,
                                    group.group_id,
                                    tiny_partner_faces,
                                    step2,
                                    face_to_group,
                                    tiny_faces,
                                    tiny_hit_group,
                                    tiny_avg_dist) != FALSE)
                            {
                                if (tiny_hit_group != group.group_id &&
                                    group.group_id < tiny_hit_group &&
                                    edge_hit_groups.insert(tiny_hit_group).second)
                                {
                                    out_group_hits[GroupKey(group.group_id, tiny_hit_group)] += 1;
                                }
                                out_stats.tiny_partner_patch_applied += 1;
                                StructuredEvent event = Step4DetailEvent();
                                event.AddTag("tiny-face-patch");
                                event.SetProperty("source_group", IntText(group.group_id));
                                event.SetProperty("target_group", IntText(tiny_hit_group));
                                event.SetProperty("tiny_partner_face_count", IntText((int)tiny_partner_faces.size()));
                                event.SetProperty("avg_dist", DoubleText(tiny_avg_dist));
                                event.SetProperty("result", "pass");
                                EmitEvent(diagnostics, event);
                            }
                            else
                            {
                                StructuredEvent event = Step4DetailEvent();
                                event.AddTag("tiny-face-patch");
                                event.SetProperty("source_group", IntText(group.group_id));
                                event.SetProperty("target_group", "-1");
                                event.SetProperty("tiny_partner_face_count", IntText((int)tiny_partner_faces.size()));
                                event.SetProperty("result", "fail");
                                EmitEvent(diagnostics, event);
                            }
                        }

                        const int ring_size = (int)seen_partner_ring.size();
                        if (ring_size > 0)
                            out_stats.coedge_with_partner += 1;
                        if (ring_size > 2)
                        {
                            out_stats.coedge_partner_ring_gt2 += 1;
                            EDGE* e = c->edge();
                            if (e != nullptr)
                                nonmanifold_edges.insert(e);
                        }
                        if (ring_size > out_stats.partner_ring_max)
                            out_stats.partner_ring_max = ring_size;

                        c = c->next();
                    } while (c != nullptr && c != start);
                }
                lp = lp->next();
            }
        }
    }
    out_stats.unique_nonmanifold_edges = (int)nonmanifold_edges.size();
}

int PairSide(const PairRecord& pair, int group_id)
{
    if (pair.group_a == group_id)
        return 0;
    if (pair.group_b == group_id)
        return 1;
    return -1;
}

int PairSideSlotFromTwoSides(int s0, int s1)
{
    if (s0 == 0 && s1 == 0) return 0;
    if (s0 == 0 && s1 == 1) return 1;
    if (s0 == 1 && s1 == 0) return 2;
    if (s0 == 1 && s1 == 1) return 3;
    return -1;
}

void AddSlotHit(PairLinkAccum& acc, int s0, int s1, int delta)
{
    if (s0 == 0 && s1 == 0) acc.aa += delta;
    else if (s0 == 0 && s1 == 1) acc.ab += delta;
    else if (s0 == 1 && s1 == 0) acc.ba += delta;
    else if (s0 == 1 && s1 == 1) acc.bb += delta;
}

logical IsNeighborCoedgeIndex(int i, int j, int n)
{
    if (n <= 1)
        return TRUE;
    if (i == j)
        return TRUE;
    if (std::abs(i - j) == 1)
        return TRUE;
    if ((i == 0 && j == n - 1) || (j == 0 && i == n - 1))
        return TRUE;
    return FALSE;
}

void CollectCoedgeTouchesByPartnerRing(
    COEDGE* c,
    const Step3PairState& step3,
    const std::map<FACE*, int>& face_to_group,
    const std::map<int, std::vector<int> >& group_to_pairs,
    std::vector<WallCoedgeTouch>& out_touches)
{
    out_touches.clear();
    if (c == nullptr)
        return;

    const double edge_len = CoedgeLengthSafe(c);
    SPAunit_vector dir(1.0, 0.0, 0.0);
    const logical has_dir = BuildCoedgeDirection(c, dir);
    std::set<COEDGE*> seen;
    std::set<std::pair<int, int> > seen_pair_side;

    COEDGE* p = c->partner();
    while (p != nullptr && seen.insert(p).second)
    {
        FACE* pf = (p->loop() != nullptr) ? p->loop()->face() : nullptr;
        const std::map<FACE*, int>::const_iterator it = face_to_group.find(pf);
        if (it != face_to_group.end())
        {
            const int g = it->second;
            const std::map<int, std::vector<int> >::const_iterator owners_it = group_to_pairs.find(g);
            if (owners_it != group_to_pairs.end())
            {
                const std::vector<int>& owners = owners_it->second;
                int oi = 0;
                for (oi = 0; oi < (int)owners.size(); ++oi)
                {
                    const int pair_id = owners[oi];
                    const PairRecord* pair = FindPairById(step3, pair_id);
                    if (pair == nullptr)
                        continue;
                    const int side = PairSide(*pair, g);
                    if (side < 0)
                        continue;

                    const std::pair<int, int> key(pair_id, side);
                    if (!seen_pair_side.insert(key).second)
                        continue;

                    WallCoedgeTouch touch;
                    touch.pair_id = pair_id;
                    touch.side = side;
                    touch.edge_len = edge_len;
                    touch.src_coedge = c;
                    touch.has_dir = has_dir;
                    touch.dir = dir;
                    out_touches.push_back(touch);
                }
            }
        }
        p = p->partner();
    }
}

JsonValue HitDeltaJson(int aa, int ab, int ba, int bb)
{
    JsonValue obj = JsonValue::object();
    obj["aa"] = aa;
    obj["ab"] = ab;
    obj["ba"] = ba;
    obj["bb"] = bb;
    return obj;
}

void EmitOrphanBridgeEvent(
    DiagnosticSink* diagnostics,
    int wall_group_id,
    int wall_face_index,
    int pair_a,
    int pair_b,
    int side_a,
    int side_b,
    int aa,
    int ab,
    int ba,
    int bb,
    double edge_sum,
    const char* mode)
{
    StructuredEvent event = Step4DetailEvent();
    event.AddTag("orphan-bridge");
    event.SetProperty("wall_group_id", IntText(wall_group_id));
    event.SetProperty("wall_face_index", IntText(wall_face_index));
    event.SetProperty("pair_a", IntText(pair_a));
    event.SetProperty("pair_b", IntText(pair_b));
    event.SetProperty("side_a", IntText(side_a));
    event.SetProperty("side_b", IntText(side_b));
    event.SetProperty("edge_sum", DoubleText(edge_sum));
    event.SetProperty("mode", mode == nullptr ? "" : mode);
    event.SetJsonProperty("hit_delta", JsonDump(HitDeltaJson(aa, ab, ba, bb)));
    EmitEvent(diagnostics, event);
}

void BuildSyntheticPairHitsFromOrphanWalls(
    const Step3PairState& step3,
    const std::map<FACE*, int>& face_to_group,
    const std::map<int, std::vector<int> >& group_to_pairs,
    const std::set<int>& mw1_wall_groups,
    std::map<std::pair<int, int>, PairLinkAccum>& pair_accum,
    std::map<std::pair<int, int>, PairDirOverride>& dir_override,
    PairSlotHitCoedgeMap* pair_slot_hit_coedges,
    DiagnosticSink* diagnostics,
    OrphanBridgeStats& out_stats)
{
    out_stats = OrphanBridgeStats();
    const Step2GroupState* step2 = step3.input_step2;
    if (step2 == nullptr)
        return;

    int wall_index = 0;
    for (wall_index = 0; wall_index < (int)step3.walls.walls.size(); ++wall_index)
    {
        const WallRecord& wall = step3.walls.walls[wall_index];
        const int gw = wall.group_a;
        if (gw < 0 || gw >= (int)step2->groups.groups.size())
            continue;
        const GroupRecord& wall_group = step2->groups.groups[gw];
        if (mw1_wall_groups.find(gw) != mw1_wall_groups.end())
            continue;

        ++out_stats.orphan_wall_groups;
        int fi = 0;
        for (fi = 0; fi < (int)wall_group.faces.size(); ++fi)
        {
            FACE* wf = wall_group.faces[fi];
            if (wf == nullptr)
                continue;
            ++out_stats.wall_faces_total;
            std::set<std::pair<int, int> > face_injected_pairs;

            LOOP* lp = wf->loop();
            while (lp != nullptr)
            {
                std::vector<WallCoedgeTouch> touches;
                COEDGE* start = lp->start();
                COEDGE* c = start;
                if (c != nullptr)
                {
                    do
                    {
                        std::vector<WallCoedgeTouch> one_touches;
                        CollectCoedgeTouchesByPartnerRing(c, step3, face_to_group, group_to_pairs, one_touches);
                        touches.insert(touches.end(), one_touches.begin(), one_touches.end());
                        c = c->next();
                    } while (c != nullptr && c != start);
                }

                const int n = (int)touches.size();
                if (n >= 2)
                {
                    logical used_adjacent_fallback = FALSE;
                    double best_sum = -1.0;
                    int best_i = -1;
                    int best_j = -1;
                    int i = 0, j = 0;
                    for (i = 0; i < n; ++i)
                    {
                        for (j = i + 1; j < n; ++j)
                        {
                            if (IsNeighborCoedgeIndex(i, j, n) != FALSE)
                                continue;
                            if (touches[i].pair_id < 0 || touches[j].pair_id < 0 ||
                                touches[i].pair_id == touches[j].pair_id)
                                continue;
                            const double s = touches[i].edge_len + touches[j].edge_len;
                            if (s > best_sum)
                            {
                                best_sum = s;
                                best_i = i;
                                best_j = j;
                            }
                        }
                    }
                    if (best_i < 0 || best_j < 0)
                    {
                        used_adjacent_fallback = TRUE;
                        for (i = 0; i < n; ++i)
                        {
                            for (j = i + 1; j < n; ++j)
                            {
                                if (touches[i].pair_id < 0 || touches[j].pair_id < 0 ||
                                    touches[i].pair_id == touches[j].pair_id)
                                    continue;
                                const double s = touches[i].edge_len + touches[j].edge_len;
                                if (s > best_sum)
                                {
                                    best_sum = s;
                                    best_i = i;
                                    best_j = j;
                                }
                            }
                        }
                    }

                    if (best_i >= 0 && best_j >= 0)
                    {
                        const WallCoedgeTouch& a = touches[best_i];
                        const WallCoedgeTouch& b = touches[best_j];
                        int p0 = a.pair_id;
                        int p1 = b.pair_id;
                        int s0 = a.side;
                        int s1 = b.side;
                        SPAunit_vector d0 = a.dir;
                        SPAunit_vector d1 = b.dir;
                        logical has_d0 = a.has_dir;
                        logical has_d1 = b.has_dir;
                        if (p0 > p1)
                        {
                            std::swap(p0, p1);
                            std::swap(s0, s1);
                            std::swap(d0, d1);
                            std::swap(has_d0, has_d1);
                        }

                        if (p0 >= 0 && p1 >= 0 && p0 != p1 && s0 >= 0 && s1 >= 0)
                        {
                            const std::pair<int, int> pk(p0, p1);
                            if (!face_injected_pairs.insert(pk).second)
                            {
                                ++out_stats.face_dedup_skip;
                            }
                            else
                            {
                                PairLinkAccum& acc = pair_accum[pk];
                                int aa = 0, ab = 0, ba = 0, bb = 0;
                                if (s0 == 0 && s1 == 0) aa = 1;
                                else if (s0 == 0 && s1 == 1) ab = 1;
                                else if (s0 == 1 && s1 == 0) ba = 1;
                                else if (s0 == 1 && s1 == 1) bb = 1;
                                AddSlotHit(acc, s0, s1, 1);
                                acc.orphan_hits += 1;
                                const int slot = PairSideSlotFromTwoSides(s0, s1);
                                if (pair_slot_hit_coedges != nullptr && slot >= 0)
                                {
                                    std::vector<COEDGE*>& slot_edges = (*pair_slot_hit_coedges)[pk][slot];
                                    AddUniqueCoedgePtr(slot_edges, a.src_coedge);
                                    AddUniqueCoedgePtr(slot_edges, b.src_coedge);
                                }

                                PairDirOverride& override_dir = dir_override[pk];
                                if (has_d0 != FALSE)
                                {
                                    override_dir.dir_i = d0;
                                    override_dir.has_i = TRUE;
                                }
                                if (has_d1 != FALSE)
                                {
                                    override_dir.dir_j = d1;
                                    override_dir.has_j = TRUE;
                                }

                                ++out_stats.injected_links;
                                EmitOrphanBridgeEvent(
                                    diagnostics,
                                    gw,
                                    fi,
                                    p0,
                                    p1,
                                    s0,
                                    s1,
                                    aa,
                                    ab,
                                    ba,
                                    bb,
                                    best_sum,
                                    used_adjacent_fallback != FALSE ? "adjacent_fallback" : "non_adjacent");
                            }
                        }
                    }
                }

                lp = lp->next();
            }
        }
    }
}

logical SelectDominantHitGroupPair(
    const Step3PairState& step3,
    int pair_i,
    int pair_j,
    const PairLinkAccum& acc,
    int& out_group_i,
    int& out_group_j)
{
    out_group_i = -1;
    out_group_j = -1;
    const PairRecord* pi = FindPairById(step3, pair_i);
    const PairRecord* pj = FindPairById(step3, pair_j);
    if (pi == nullptr || pj == nullptr)
        return FALSE;

    const int hits[4] = { acc.aa, acc.ab, acc.ba, acc.bb };
    int best_kind = -1;
    int best_hit = 0;
    int k = 0;
    for (k = 0; k < 4; ++k)
    {
        if (hits[k] > best_hit)
        {
            best_hit = hits[k];
            best_kind = k;
        }
    }
    if (best_kind < 0 || best_hit <= 0)
        return FALSE;

    if (best_kind == 0) { out_group_i = pi->group_a; out_group_j = pj->group_a; return TRUE; }
    if (best_kind == 1) { out_group_i = pi->group_a; out_group_j = pj->group_b; return TRUE; }
    if (best_kind == 2) { out_group_i = pi->group_b; out_group_j = pj->group_a; return TRUE; }
    if (best_kind == 3) { out_group_i = pi->group_b; out_group_j = pj->group_b; return TRUE; }
    return FALSE;
}

logical ComputeHitNormalCosForGroupPair(
    const Step2GroupState& step2,
    const std::map<FACE*, int>& face_to_group,
    int group_i,
    int group_j,
    double& out_cos_abs)
{
    out_cos_abs = 0.0;
    if (group_i < 0 || group_j < 0 || group_i == group_j)
        return FALSE;

    std::vector<COEDGE*> coedges_i;
    std::vector<COEDGE*> coedges_j;
    CollectGroupCoedges(step2, group_i, coedges_i);
    CollectGroupCoedges(step2, group_j, coedges_j);
    if (coedges_i.empty() || coedges_j.empty())
        return FALSE;

    std::vector<COEDGE*> hit_i;
    std::vector<COEDGE*> hit_j;
    int ci = 0;
    for (ci = 0; ci < (int)coedges_i.size(); ++ci)
    {
        if (CoedgeTouchesGroupByPartnerRing(coedges_i[ci], group_j, face_to_group) != FALSE)
            hit_i.push_back(coedges_i[ci]);
    }
    int cj = 0;
    for (cj = 0; cj < (int)coedges_j.size(); ++cj)
    {
        if (CoedgeTouchesGroupByPartnerRing(coedges_j[cj], group_i, face_to_group) != FALSE)
            hit_j.push_back(coedges_j[cj]);
    }

    const std::vector<COEDGE*>& src_set = hit_i.empty() ? coedges_i : hit_i;
    const std::vector<COEDGE*>& dst_set = hit_j.empty() ? coedges_j : hit_j;

    COEDGE* src = nullptr;
    if (SelectLongestCoedge(src_set, src) == FALSE || src == nullptr)
        return FALSE;

    std::vector<SPAposition> samples;
    if (BuildCoedgeSamplePoints(src, kHitNormalSampleCount, samples) == FALSE || samples.empty())
        return FALSE;

    FACE* fa = (src->loop() != nullptr) ? src->loop()->face() : nullptr;
    if (fa == nullptr)
        return FALSE;

    double sum_cos = 0.0;
    int valid_count = 0;
    int si = 0;
    for (si = 0; si < (int)samples.size(); ++si)
    {
        SPAposition b1;
        FACE* fb = nullptr;
        double dmin = DBL_MAX;
        if (FindClosestPointOnCoedgeSet(samples[si], dst_set, b1, fb, dmin) == FALSE || fb == nullptr)
            continue;

        SPAunit_vector na(1.0, 0.0, 0.0);
        SPAunit_vector nb(1.0, 0.0, 0.0);
        if (TryGetFaceNormalAtPoint(fa, samples[si], na) == FALSE)
            continue;
        if (TryGetFaceNormalAtPoint(fb, b1, nb) == FALSE)
            continue;

        double cosv = na.x() * nb.x() + na.y() * nb.y() + na.z() * nb.z();
        if (cosv < 0.0)
            cosv = -cosv;
        if (cosv < 0.0) cosv = 0.0;
        if (cosv > 1.0) cosv = 1.0;
        sum_cos += cosv;
        ++valid_count;
    }

    if (valid_count <= 0)
        return FALSE;
    out_cos_abs = sum_cos / (double)valid_count;
    return TRUE;
}

logical BuildPairDirection(
    const Step3PairState& step3,
    int pair_id,
    SPAunit_vector& out_dir)
{
    out_dir = SPAunit_vector(1.0, 0.0, 0.0);
    const PairRecord* pair = FindPairById(step3, pair_id);
    if (pair == nullptr)
        return FALSE;

    SPAvector v = pair->point_b - pair->point_a;
    if (v.len() <= 1.0e-12)
        v = SPAvector(pair->pair_direction.x(), pair->pair_direction.y(), pair->pair_direction.z());
    if (v.len() <= 1.0e-12)
        return FALSE;

    const double inv = 1.0 / v.len();
    out_dir = SPAunit_vector(v.x() * inv, v.y() * inv, v.z() * inv);
    return TRUE;
}

std::string ClassifyPairModeByDirections(
    const SPAunit_vector& a,
    const SPAunit_vector& b,
    double mm2_angle_deg)
{
    double cosv = a.x() * b.x() + a.y() * b.y() + a.z() * b.z();
    if (cosv < 0.0)
        cosv = -cosv;
    if (cosv < 0.0) cosv = 0.0;
    if (cosv > 1.0) cosv = 1.0;
    const double threshold = std::cos(std::max(0.0, mm2_angle_deg) * kPi / 180.0);
    return cosv >= threshold ? "MM2" : "MM1";
}

std::string ClassifyPairModeByAngleFallback(
    const Step3PairState& step3,
    int pair_i,
    int pair_j,
    const PairDirOverride* override_dir,
    double mm2_angle_deg)
{
    SPAunit_vector ni(1.0, 0.0, 0.0);
    SPAunit_vector nj(1.0, 0.0, 0.0);
    logical has_i = FALSE;
    logical has_j = FALSE;
    if (override_dir != nullptr && override_dir->has_i != FALSE)
    {
        ni = override_dir->dir_i;
        has_i = TRUE;
    }
    else
    {
        has_i = BuildPairDirection(step3, pair_i, ni);
    }
    if (override_dir != nullptr && override_dir->has_j != FALSE)
    {
        nj = override_dir->dir_j;
        has_j = TRUE;
    }
    else
    {
        has_j = BuildPairDirection(step3, pair_j, nj);
    }
    if (has_i == FALSE || has_j == FALSE)
        return "UNKNOWN";
    return ClassifyPairModeByDirections(ni, nj, mm2_angle_deg);
}

JsonValue IntListJson(const std::vector<int>& values)
{
    JsonValue arr = JsonValue::array();
    int i = 0;
    for (i = 0; i < (int)values.size(); ++i)
        arr.push_back(values[i]);
    return arr;
}

void EmitStartEvent(DiagnosticSink* diagnostics, const Step3PairState& step3)
{
    StructuredEvent event = Step4EventTemplate();
    event.AddTag("start");
    event.SetProperty("pair_count", IntText((int)step3.pairs.pairs.size()));
    if (step3.input_step2 != nullptr)
        event.SetProperty("group_count", IntText((int)step3.input_step2->groups.groups.size()));
    EmitEvent(diagnostics, event);
}

void EmitAdjacencySummary(
    DiagnosticSink* diagnostics,
    const std::map<std::pair<int, int>, int>& group_hits,
    const GroupAdjBuildStatsLocal& stats)
{
    StructuredEvent event = Step4StageSummaryEvent();
    event.AddTag("group-adjacency");
    event.AddTag("finish");
    event.SetProperty("group_adjacency_count", IntText((int)group_hits.size()));
    event.SetProperty("coedge_total", IntText(stats.coedge_total));
    event.SetProperty("coedge_with_partner", IntText(stats.coedge_with_partner));
    event.SetProperty("coedge_partner_ring_gt2", IntText(stats.coedge_partner_ring_gt2));
    event.SetProperty("unique_nonmanifold_edges", IntText(stats.unique_nonmanifold_edges));
    event.SetProperty("partner_ring_max", IntText(stats.partner_ring_max));
    event.SetProperty("tiny_faces_found", IntText(stats.tiny_faces_found));
    event.SetProperty("tiny_partner_coedges", IntText(stats.coedges_with_tiny_partner));
    event.SetProperty("tiny_patch_applied", IntText(stats.tiny_partner_patch_applied));
    EmitEvent(diagnostics, event);

    if (stats.coedges_with_tiny_partner > 0 || stats.tiny_partner_patch_applied > 0)
    {
        StructuredEvent tiny = Step4StageSummaryEvent();
        tiny.AddTag("tiny-face-patch");
        tiny.SetProperty("tiny_faces_found", IntText(stats.tiny_faces_found));
        tiny.SetProperty("tiny_partner_coedges", IntText(stats.coedges_with_tiny_partner));
        tiny.SetProperty("tiny_patch_applied", IntText(stats.tiny_partner_patch_applied));
        EmitEvent(diagnostics, tiny);
    }
}

void EmitPairWallEvent(DiagnosticSink* diagnostics, const PairWallRelationRecord& record)
{
    StructuredEvent event = Step4SingleSummaryEvent();
    event.AddTag("pair-wall");
    event.SetProperty("relation_id", IntText(record.relation_id));
    event.SetProperty("pair_id", IntText(record.pair_id));
    event.SetProperty("wall_group_id", IntText(record.wall_group_id));
    event.SetProperty("hit_group_a", IntText(record.hit_group_a));
    event.SetProperty("hit_group_b", IntText(record.hit_group_b));
    event.SetProperty("wall_mode", record.wall_mode);
    EmitEvent(diagnostics, event);
}

void EmitPairLinkEvent(DiagnosticSink* diagnostics, const PairRelationRecord& record)
{
    StructuredEvent event = Step4SingleSummaryEvent();
    event.AddTag("pair-link");
    event.SetProperty("relation_id", IntText(record.relation_id));
    event.SetProperty("pair_a", IntText(record.pair_a));
    event.SetProperty("pair_b", IntText(record.pair_b));
    event.SetProperty("pair_mode", record.pair_mode);
    event.SetProperty("classify_source", record.classify_source);
    event.SetProperty("source", record.source);
    event.SetProperty("hits_aa", IntText(record.hits_aa));
    event.SetProperty("hits_ab", IntText(record.hits_ab));
    event.SetProperty("hits_ba", IntText(record.hits_ba));
    event.SetProperty("hits_bb", IntText(record.hits_bb));
    event.SetProperty("total_hits", IntText(record.total_hits));
    event.SetProperty("hit_normal_cos_abs", DoubleText(record.hit_normal_cos_abs));
    event.SetProperty("hit_normal_angle_deg", DoubleText(record.hit_normal_angle_deg));
    EmitEvent(diagnostics, event);
}

void EmitPairLinkCandidateDetail(
    DiagnosticSink* diagnostics,
    int pair_a,
    int pair_b,
    int group_a,
    int group_b,
    int side_a,
    int side_b,
    int hit_delta,
    const PairLinkAccum& acc)
{
    StructuredEvent event = Step4DetailEvent();
    event.AddTag("pair-link");
    event.SetProperty("pair_a", IntText(pair_a));
    event.SetProperty("pair_b", IntText(pair_b));
    event.SetProperty("group_a", IntText(group_a));
    event.SetProperty("group_b", IntText(group_b));
    event.SetProperty("side_a", IntText(side_a));
    event.SetProperty("side_b", IntText(side_b));
    event.SetProperty("hit_delta", IntText(hit_delta));
    event.SetProperty("hits_aa", IntText(acc.aa));
    event.SetProperty("hits_ab", IntText(acc.ab));
    event.SetProperty("hits_ba", IntText(acc.ba));
    event.SetProperty("hits_bb", IntText(acc.bb));
    event.SetProperty("source", "group_adjacency");
    EmitEvent(diagnostics, event);
}

void EmitPairLinkStageSummary(
    DiagnosticSink* diagnostics,
    int candidate_link_count,
    int accepted_link_count,
    int rejected_link_count,
    int direct_hit_count,
    int orphan_hit_count,
    int mm1_count,
    int mm2_count)
{
    StructuredEvent event = Step4StageSummaryEvent();
    event.AddTag("pair-link");
    event.AddTag("finish");
    event.SetProperty("candidate_link_count", IntText(candidate_link_count));
    event.SetProperty("accepted_link_count", IntText(accepted_link_count));
    event.SetProperty("rejected_link_count", IntText(rejected_link_count));
    event.SetProperty("direct_hit_count", IntText(direct_hit_count));
    event.SetProperty("orphan_hit_count", IntText(orphan_hit_count));
    event.SetProperty("mm1_count", IntText(mm1_count));
    event.SetProperty("mm2_count", IntText(mm2_count));
    EmitEvent(diagnostics, event);
}

void EmitPairWallStageSummary(
    DiagnosticSink* diagnostics,
    int checked_count,
    int accepted_count,
    int rejected_count,
    int wall_group_count)
{
    StructuredEvent event = Step4StageSummaryEvent();
    event.AddTag("pair-wall");
    event.AddTag("finish");
    event.SetProperty("checked_count", IntText(checked_count));
    event.SetProperty("accepted_count", IntText(accepted_count));
    event.SetProperty("rejected_count", IntText(rejected_count));
    event.SetProperty("wall_group_count", IntText(wall_group_count));
    EmitEvent(diagnostics, event);
}

void EmitOrphanBridgeStageSummary(DiagnosticSink* diagnostics, const OrphanBridgeStats& stats)
{
    StructuredEvent event = Step4StageSummaryEvent();
    event.AddTag("orphan-bridge");
    event.AddTag("finish");
    event.SetProperty("orphan_wall_group_count", IntText(stats.orphan_wall_groups));
    event.SetProperty("wall_faces_total", IntText(stats.wall_faces_total));
    event.SetProperty("injected_link_count", IntText(stats.injected_links));
    event.SetProperty("face_dedup_skip", IntText(stats.face_dedup_skip));
    EmitEvent(diagnostics, event);
}

void EmitVirtualWallStageSummary(
    DiagnosticSink* diagnostics,
    int try_count,
    int ok_count,
    int fail_count)
{
    StructuredEvent event = Step4StageSummaryEvent();
    event.AddTag("virtual-wall");
    event.AddTag("finish");
    event.SetProperty("try_count", IntText(try_count));
    event.SetProperty("ok_count", IntText(ok_count));
    event.SetProperty("fail_count", IntText(fail_count));
    EmitEvent(diagnostics, event);
}

void EmitFinishEvent(DiagnosticSink* diagnostics, const RelationBuildStats& stats)
{
    StructuredEvent event = Step4AllSummaryEvent();
    event.AddTag("finish");
    event.SetProperty("relation_count", IntText(stats.relation_count));
    event.SetProperty("pair_wall_relation_count", IntText(stats.pair_wall_relation_count));
    event.SetProperty("wall_count", IntText(stats.wall_count));
    event.SetProperty("group_adjacency_count", IntText(stats.group_adjacency_count));
    event.SetProperty("tiny_faces_found", IntText(stats.tiny_faces_found));
    event.SetProperty("tiny_patch_applied", IntText(stats.tiny_patch_applied));
    event.SetProperty("orphan_wall_group_count", IntText(stats.orphan_wall_group_count));
    event.SetProperty("orphan_bridge_injected_count", IntText(stats.orphan_bridge_injected_count));
    event.SetProperty("direct_pair_link_count", IntText(stats.direct_pair_link_count));
    event.SetProperty("mm1_count", IntText(stats.mm1_count));
    event.SetProperty("mm2_count", IntText(stats.mm2_count));
    event.SetProperty("virtual_wall_try_count", IntText(stats.virtual_wall_try_count));
    event.SetProperty("virtual_wall_ok_count", IntText(stats.virtual_wall_ok_count));
    EmitEvent(diagnostics, event);
}

void AddUniqueInt(std::vector<int>& values, int v)
{
    int i = 0;
    for (i = 0; i < (int)values.size(); ++i)
    {
        if (values[i] == v)
            return;
    }
    values.push_back(v);
}

logical BuildCoedgeSamplePointsUniformIncludeEnds(COEDGE* c, int sample_count, std::vector<SPAposition>& out_pts)
{
    out_pts.clear();
    if (c == nullptr)
        return FALSE;
    if (sample_count < 2)
        sample_count = 2;
    const double t0 = coedge_start_param(c);
    const double t1 = coedge_end_param(c);
    if (std::fabs(t1 - t0) <= 1.0e-15)
        return FALSE;
    int si = 0;
    for (si = 0; si < sample_count; ++si)
    {
        const double u = (double)si / (double)(sample_count - 1);
        const double t = t0 + (t1 - t0) * u;
        out_pts.push_back(coedge_param_pos(c, t));
    }
    return out_pts.empty() ? FALSE : TRUE;
}

logical CollectHitCoedgesBetweenGroupsStrictNoFallback(
    const Step2GroupState& step2,
    const std::map<FACE*, int>& face_to_group,
    int group_i,
    int group_j,
    std::vector<COEDGE*>& out_hit_i,
    std::vector<COEDGE*>& out_hit_j)
{
    out_hit_i.clear();
    out_hit_j.clear();
    if (group_i < 0 || group_j < 0 || group_i == group_j)
        return FALSE;

    std::vector<COEDGE*> all_i;
    std::vector<COEDGE*> all_j;
    CollectGroupCoedges(step2, group_i, all_i);
    CollectGroupCoedges(step2, group_j, all_j);
    if (all_i.empty() || all_j.empty())
        return FALSE;

    int ci = 0;
    for (ci = 0; ci < (int)all_i.size(); ++ci)
    {
        if (CoedgeTouchesGroupByPartnerRing(all_i[ci], group_j, face_to_group) != FALSE)
            out_hit_i.push_back(all_i[ci]);
    }
    int cj = 0;
    for (cj = 0; cj < (int)all_j.size(); ++cj)
    {
        if (CoedgeTouchesGroupByPartnerRing(all_j[cj], group_i, face_to_group) != FALSE)
            out_hit_j.push_back(all_j[cj]);
    }

    return (!out_hit_i.empty() || !out_hit_j.empty()) ? TRUE : FALSE;
}

logical CollectHitCoedgesBetweenGroups(
    const Step2GroupState& step2,
    const std::map<FACE*, int>& face_to_group,
    int group_i,
    int group_j,
    std::vector<COEDGE*>& out_hit_i,
    std::vector<COEDGE*>& out_hit_j)
{
    if (CollectHitCoedgesBetweenGroupsStrictNoFallback(step2, face_to_group, group_i, group_j, out_hit_i, out_hit_j) != FALSE)
    {
        if (!out_hit_i.empty() && !out_hit_j.empty())
            return TRUE;
    }

    if (out_hit_i.empty())
        CollectGroupCoedges(step2, group_i, out_hit_i);
    if (out_hit_j.empty())
        CollectGroupCoedges(step2, group_j, out_hit_j);
    return (!out_hit_i.empty() && !out_hit_j.empty()) ? TRUE : FALSE;
}

int InferBucketCoedgeOwnerMask(
    COEDGE* c,
    int group_i,
    int group_j,
    const std::map<FACE*, int>& face_to_group)
{
    if (c == nullptr)
        return 0;

    int mask = 0;
    FACE* f = (c->loop() != nullptr) ? c->loop()->face() : nullptr;
    const std::map<FACE*, int>::const_iterator itf = face_to_group.find(f);
    if (itf != face_to_group.end())
    {
        if (itf->second == group_i)
            mask |= 1;
        if (itf->second == group_j)
            mask |= 2;
    }
    if (mask == 0)
    {
        if (CoedgeTouchesGroupByPartnerRing(c, group_i, face_to_group) != FALSE)
            mask |= 1;
        if (CoedgeTouchesGroupByPartnerRing(c, group_j, face_to_group) != FALSE)
            mask |= 2;
    }
    return mask;
}

void CollectMm2HitBuckets(
    const Step3PairState& step3,
    const std::map<FACE*, int>& face_to_group,
    int pair_i,
    int pair_j,
    const PairLinkAccum& acc,
    const PairSlotHitCoedgeMap* pair_slot_hit_coedges,
    std::vector<Mm2HitBucket>& out_buckets)
{
    out_buckets.clear();
    const Step2GroupState* step2 = step3.input_step2;
    const PairRecord* pi = FindPairById(step3, pair_i);
    const PairRecord* pj = FindPairById(step3, pair_j);
    if (step2 == nullptr || pi == nullptr || pj == nullptr)
        return;

    const int gi_bucket[4] = { pi->group_a, pi->group_a, pi->group_b, pi->group_b };
    const int gj_bucket[4] = { pj->group_a, pj->group_b, pj->group_a, pj->group_b };
    const int hv_bucket[4] = { acc.aa, acc.ab, acc.ba, acc.bb };

    int p0 = pair_i;
    int p1 = pair_j;
    if (p0 > p1)
        std::swap(p0, p1);
    const std::map<int, std::vector<COEDGE*> >* slot_map_ptr = nullptr;
    if (pair_slot_hit_coedges != nullptr)
    {
        const PairSlotHitCoedgeMap::const_iterator it_pair = pair_slot_hit_coedges->find(std::make_pair(p0, p1));
        if (it_pair != pair_slot_hit_coedges->end())
            slot_map_ptr = &it_pair->second;
    }

    int bk = 0;
    for (bk = 0; bk < 4; ++bk)
    {
        if (hv_bucket[bk] <= 0)
            continue;
        const int gi = gi_bucket[bk];
        const int gj = gj_bucket[bk];
        if (gi < 0 || gj < 0 || gi == gj)
            continue;

        Mm2HitBucket b;
        b.slot = bk;
        b.group_i = gi;
        b.group_j = gj;
        b.hit_weight = hv_bucket[bk];
        std::set<COEDGE*> seen;

        if (slot_map_ptr != nullptr)
        {
            const std::map<int, std::vector<COEDGE*> >::const_iterator it_slot = slot_map_ptr->find(bk);
            if (it_slot != slot_map_ptr->end())
            {
                int ri = 0;
                for (ri = 0; ri < (int)it_slot->second.size(); ++ri)
                {
                    COEDGE* c = it_slot->second[ri];
                    if (c == nullptr)
                        continue;
                    b.coedge_owner_mask[c] |= InferBucketCoedgeOwnerMask(c, gi, gj, face_to_group);
                    if (!seen.insert(c).second)
                        continue;
                    b.coedges.push_back(c);
                    const double len = CoedgeLengthSafe(c);
                    b.len_sum += len;
                    b.longest_len = std::max(b.longest_len, len);
                }
                if (!b.coedges.empty())
                    b.from_orphan_recorded = TRUE;
            }
        }

        std::vector<COEDGE*> hit_i;
        std::vector<COEDGE*> hit_j;
        if (CollectHitCoedgesBetweenGroupsStrictNoFallback(*step2, face_to_group, gi, gj, hit_i, hit_j) != FALSE)
        {
            int ci = 0;
            for (ci = 0; ci < (int)hit_i.size(); ++ci)
            {
                COEDGE* c = hit_i[ci];
                if (c == nullptr)
                    continue;
                int owner_mask = InferBucketCoedgeOwnerMask(c, gi, gj, face_to_group);
                if (owner_mask == 0)
                    owner_mask = 1;
                b.coedge_owner_mask[c] |= owner_mask;
                if (!seen.insert(c).second)
                    continue;
                b.coedges.push_back(c);
                const double len = CoedgeLengthSafe(c);
                b.len_sum += len;
                b.longest_len = std::max(b.longest_len, len);
                b.from_partner_collected = TRUE;
            }
            int cj = 0;
            for (cj = 0; cj < (int)hit_j.size(); ++cj)
            {
                COEDGE* c = hit_j[cj];
                if (c == nullptr)
                    continue;
                int owner_mask = InferBucketCoedgeOwnerMask(c, gi, gj, face_to_group);
                if (owner_mask == 0)
                    owner_mask = 2;
                b.coedge_owner_mask[c] |= owner_mask;
                if (!seen.insert(c).second)
                    continue;
                b.coedges.push_back(c);
                const double len = CoedgeLengthSafe(c);
                b.len_sum += len;
                b.longest_len = std::max(b.longest_len, len);
                b.from_partner_collected = TRUE;
            }
        }
        if (!b.coedges.empty())
            out_buckets.push_back(b);
    }
}

int SelectPrimaryMm2Bucket(const std::vector<Mm2HitBucket>& buckets)
{
    if (buckets.empty())
        return -1;
    int best = 0;
    int i = 1;
    for (i = 1; i < (int)buckets.size(); ++i)
    {
        if (buckets[i].len_sum > buckets[best].len_sum)
            best = i;
        else if (std::fabs(buckets[i].len_sum - buckets[best].len_sum) <= 1.0e-12 &&
                 buckets[i].hit_weight > buckets[best].hit_weight)
            best = i;
    }
    return best;
}

void CollectUniqueBucketEdges(
    const Mm2HitBucket& bucket,
    logical owner1_or3_only,
    std::vector<EDGE*>& out_edges,
    std::vector<int>& out_owner_masks)
{
    out_edges.clear();
    out_owner_masks.clear();
    std::map<EDGE*, int> edge_to_index;
    int i = 0;
    for (i = 0; i < (int)bucket.coedges.size(); ++i)
    {
        COEDGE* c = bucket.coedges[i];
        EDGE* e = c == nullptr ? nullptr : c->edge();
        if (e == nullptr)
            continue;
        int owner_mask = 0;
        const std::map<COEDGE*, int>::const_iterator it = bucket.coedge_owner_mask.find(c);
        if (it != bucket.coedge_owner_mask.end())
            owner_mask = it->second;
        if (owner1_or3_only != FALSE && (owner_mask & 1) == 0)
            continue;

        const std::map<EDGE*, int>::const_iterator eit = edge_to_index.find(e);
        if (eit == edge_to_index.end())
        {
            const int idx = (int)out_edges.size();
            edge_to_index[e] = idx;
            out_edges.push_back(e);
            out_owner_masks.push_back(owner_mask);
        }
        else
        {
            out_owner_masks[eit->second] |= owner_mask;
        }
    }
}

struct BridgeCandidate
{
    BridgeCandidate()
        : e0(-1),
          end0(0),
          e1(-1),
          end1(0),
          dist(0.0)
    {
    }

    int e0;
    int end0;
    int e1;
    int end1;
    double dist;
};

bool BridgeCandidateLess(const BridgeCandidate& a, const BridgeCandidate& b)
{
    return a.dist < b.dist;
}

void BuildBridgeEdgesForBucket(
    const std::vector<EDGE*>& base_edges,
    const std::vector<int>& base_owner_masks,
    double gap_tol,
    logical endpoint_single_use,
    std::vector<EDGE*>& out_bridge_edges)
{
    out_bridge_edges.clear();
    if (base_edges.size() < 2 || gap_tol <= 0.0)
        return;

    std::vector<BridgeCandidate> candidates;
    int i = 0;
    for (i = 0; i < (int)base_edges.size(); ++i)
    {
        EDGE* ei = base_edges[i];
        if (ei == nullptr)
            continue;
        const SPAposition pi[2] = { ei->start_pos(), ei->end_pos() };
        int j = i + 1;
        for (j = i + 1; j < (int)base_edges.size(); ++j)
        {
            EDGE* ej = base_edges[j];
            if (ej == nullptr)
                continue;
            const int own_i = i < (int)base_owner_masks.size() ? base_owner_masks[i] : 0;
            const int own_j = j < (int)base_owner_masks.size() ? base_owner_masks[j] : 0;
            if ((own_i & own_j) == 0)
                continue;
            const SPAposition pj[2] = { ej->start_pos(), ej->end_pos() };
            int ai = 0;
            for (ai = 0; ai < 2; ++ai)
            {
                int bj = 0;
                for (bj = 0; bj < 2; ++bj)
                {
                    const double dist = (pj[bj] - pi[ai]).len();
                    if (dist > gap_tol || dist <= 1.0e-12)
                        continue;
                    BridgeCandidate c;
                    c.e0 = i;
                    c.end0 = ai;
                    c.e1 = j;
                    c.end1 = bj;
                    c.dist = dist;
                    candidates.push_back(c);
                }
            }
        }
    }

    std::sort(candidates.begin(), candidates.end(), BridgeCandidateLess);
    std::vector<int> used(base_edges.size() * 2, 0);
    int k = 0;
    for (k = 0; k < (int)candidates.size(); ++k)
    {
        const BridgeCandidate& c = candidates[k];
        const int id0 = c.e0 * 2 + c.end0;
        const int id1 = c.e1 * 2 + c.end1;
        if (endpoint_single_use != FALSE && (used[id0] || used[id1]))
            continue;
        EDGE* e0 = base_edges[c.e0];
        EDGE* e1 = base_edges[c.e1];
        if (e0 == nullptr || e1 == nullptr)
            continue;
        const SPAposition p0 = c.end0 == 0 ? e0->start_pos() : e0->end_pos();
        const SPAposition p1 = c.end1 == 0 ? e1->start_pos() : e1->end_pos();
        EDGE* bridge = nullptr;
        outcome r = api_curve_line(p0, p1, bridge);
        if (!r.ok() || bridge == nullptr)
            continue;
        out_bridge_edges.push_back(bridge);
        if (endpoint_single_use != FALSE)
        {
            used[id0] = 1;
            used[id1] = 1;
        }
    }
}

double PairThicknessHint(const Step3PairState& step3, int pair_id)
{
    const PairRecord* pair = FindPairById(step3, pair_id);
    return pair == nullptr ? 0.0 : std::fabs(pair->thickness);
}

double ComputeMm2SweepHalfProfile(
    const Step3PairState& step3,
    int pair_i,
    int pair_j,
    const Step4RelationOptions& options,
    double path_len_hint)
{
    double base = std::max(PairThicknessHint(step3, pair_i), PairThicknessHint(step3, pair_j));
    if (base <= 1.0e-12 && path_len_hint > 0.0)
        base = 0.1 * path_len_hint;
    return std::max(options.mm2_sweep_thickness_min, options.mm2_sweep_thickness_scale * std::max(0.0, base));
}

logical FindClosestPointAndNormalOnGroup(
    const Step2GroupState& step2,
    int group_id,
    const SPAposition& query,
    SPAposition& out_cp,
    SPAunit_vector& out_n)
{
    out_cp = query;
    out_n = SPAunit_vector(1.0, 0.0, 0.0);
    const GroupRecord* group = FindGroupById(step2, group_id);
    if (group == nullptr || group->faces.empty())
        return FALSE;

    logical found = FALSE;
    double best_d = DBL_MAX;
    int i = 0;
    for (i = 0; i < (int)group->faces.size(); ++i)
    {
        FACE* f = group->faces[i];
        if (f == nullptr)
            continue;
        SPAposition cp;
        outcome rc = api_find_cls_ptto_face(query, f, cp);
        if (!rc.ok())
            continue;
        SPAunit_vector n;
        if (TryGetFaceNormalAtPoint(f, cp, n) == FALSE)
            continue;
        const double d = (cp - query).len();
        if (found == FALSE || d < best_d)
        {
            found = TRUE;
            best_d = d;
            out_cp = cp;
            out_n = n;
        }
    }
    return found;
}

logical BuildPathReferenceFromEntity(ENTITY* path_entity, SPAposition& out_pos, SPAunit_vector& out_tan, double& out_len)
{
    out_pos = SPAposition(0.0, 0.0, 0.0);
    out_tan = SPAunit_vector(1.0, 0.0, 0.0);
    out_len = 0.0;
    if (path_entity == nullptr)
        return FALSE;
    if (is_EDGE(path_entity))
    {
        EDGE* e = (EDGE*)path_entity;
        const SPAposition p0 = e->start_pos();
        const SPAposition p1 = e->end_pos();
        const SPAvector v = p1 - p0;
        out_len = v.len();
        if (out_len <= 1.0e-12)
            return FALSE;
        out_pos = p0;
        out_tan = SPAunit_vector(v.x() / out_len, v.y() / out_len, v.z() / out_len);
        return TRUE;
    }
    if (is_BODY(path_entity))
    {
        BODY* body = (BODY*)path_entity;
        double wire_len = 0.0;
        if (api_wire_len(body, wire_len).ok())
            out_len = wire_len;
        ENTITY_LIST edges;
        outcome re = api_get_edges(body, edges, PAT_CAN_CREATE);
        if (!re.ok() || edges.count() <= 0)
            return FALSE;
        EDGE* best = nullptr;
        double best_len = -1.0;
        edges.init();
        ENTITY* ent = nullptr;
        while ((ent = edges.next()) != nullptr)
        {
            if (!is_EDGE(ent))
                continue;
            EDGE* e = (EDGE*)ent;
            const double len = (e->end_pos() - e->start_pos()).len();
            if (len > best_len)
            {
                best_len = len;
                best = e;
            }
        }
        if (best == nullptr || best_len <= 1.0e-12)
            return FALSE;
        const SPAvector v = best->end_pos() - best->start_pos();
        out_pos = best->start_pos();
        if (out_len <= 1.0e-12)
            out_len = best_len;
        out_tan = SPAunit_vector(v.x() / best_len, v.y() / best_len, v.z() / best_len);
        return TRUE;
    }
    return FALSE;
}

logical SweepWallFromPathEntity(
    const Step3PairState& step3,
    const Step4RelationOptions& options,
    int pair_i,
    int pair_j,
    int group_i,
    int group_j,
    ENTITY* path_entity,
    BODY*& out_body,
    double& out_half_profile)
{
    out_body = nullptr;
    out_half_profile = 0.0;
    const Step2GroupState* step2 = step3.input_step2;
    if (step2 == nullptr || path_entity == nullptr)
        return FALSE;

    SPAposition p_ref;
    SPAunit_vector t_ref;
    double path_len = 0.0;
    if (BuildPathReferenceFromEntity(path_entity, p_ref, t_ref, path_len) == FALSE)
        return FALSE;

    SPAposition cp;
    SPAunit_vector n_ref;
    logical has_n = FindClosestPointAndNormalOnGroup(*step2, group_i, p_ref, cp, n_ref);
    if (has_n == FALSE)
        has_n = FindClosestPointAndNormalOnGroup(*step2, group_j, p_ref, cp, n_ref);
    if (has_n == FALSE)
        return FALSE;

    const double half = ComputeMm2SweepHalfProfile(step3, pair_i, pair_j, options, path_len);
    if (half <= 0.0)
        return FALSE;

    const SPAposition a(
        p_ref.x() - n_ref.x() * half,
        p_ref.y() - n_ref.y() * half,
        p_ref.z() - n_ref.z() * half);
    const SPAposition b(
        p_ref.x() + n_ref.x() * half,
        p_ref.y() + n_ref.y() * half,
        p_ref.z() + n_ref.z() * half);

    EDGE* profile = nullptr;
    outcome rp = api_curve_line(a, b, profile);
    if (!rp.ok() || profile == nullptr)
        return FALSE;

    sweep_options swopt;
    swopt.set_solid(FALSE);
    swopt.set_two_sided(TRUE);
    BODY* swept = nullptr;
    outcome rs = api_sweep_with_options((ENTITY*)profile, path_entity, &swopt, swept);
    if (!rs.ok() || swept == nullptr)
        return FALSE;

    out_half_profile = half;
    out_body = swept;
    return TRUE;
}

void CopyEdgesByApiEdge(const std::vector<EDGE*>& in_edges, std::vector<EDGE*>& out_copied)
{
    out_copied.clear();
    int i = 0;
    for (i = 0; i < (int)in_edges.size(); ++i)
    {
        EDGE* cp = nullptr;
        outcome rc = api_edge(in_edges[i], cp);
        if (rc.ok() && cp != nullptr)
            out_copied.push_back(cp);
    }
}

logical BuildSweepWallsFromBucket(
    const Step3PairState& step3,
    const Step4RelationOptions& options,
    int pair_i,
    int pair_j,
    const Mm2HitBucket& bucket,
    std::vector<Mm2SweepWallBuild>& out_walls)
{
    out_walls.clear();
    std::vector<EDGE*> base_edges;
    std::vector<int> base_owner_masks;
    CollectUniqueBucketEdges(bucket, options.mm2_sweep_owner1_or3_only, base_edges, base_owner_masks);
    if (base_edges.empty())
        return FALSE;

    const double gap_tol = std::max(options.mm2_connect_gap_abs_min,
        options.mm2_connect_gap_ratio * std::max(1.0e-12, bucket.longest_len));
    std::vector<EDGE*> bridge_edges;
    if (options.mm2_enable_connect_strategy != FALSE)
        BuildBridgeEdgesForBucket(base_edges, base_owner_masks, gap_tol, options.mm2_endpoint_single_use, bridge_edges);

    std::vector<EDGE*> all_edges = base_edges;
    all_edges.insert(all_edges.end(), bridge_edges.begin(), bridge_edges.end());

    if (options.mm2_per_edge_sweep_only != FALSE)
    {
        int ei = 0;
        for (ei = 0; ei < (int)all_edges.size(); ++ei)
        {
            EDGE* ecopy = nullptr;
            outcome rc = api_edge(all_edges[ei], ecopy);
            if (!rc.ok() || ecopy == nullptr)
                continue;
            BODY* wall = nullptr;
            double half = 0.0;
            if (SweepWallFromPathEntity(step3, options, pair_i, pair_j, bucket.group_i, bucket.group_j, (ENTITY*)ecopy, wall, half) == FALSE)
                continue;
            Mm2SweepWallBuild built;
            built.body = wall;
            built.group_i = bucket.group_i;
            built.group_j = bucket.group_j;
            built.bucket_slot = bucket.slot;
            built.from_orphan_recorded = bucket.from_orphan_recorded;
            built.from_partner_collected = bucket.from_partner_collected;
            built.coedge_len = bucket.longest_len;
            built.half_profile = half;
            out_walls.push_back(built);
        }
        return out_walls.empty() ? FALSE : TRUE;
    }

    std::vector<EDGE*> copied_edges;
    CopyEdgesByApiEdge(all_edges, copied_edges);
    if (copied_edges.empty())
        return FALSE;

    int n_bodies = 0;
    BODY** bodies = nullptr;
    outcome rw = api_make_ewires((int)copied_edges.size(), &copied_edges[0], n_bodies, bodies);
    if (rw.ok() && n_bodies > 0 && bodies != nullptr)
    {
        int bi = 0;
        for (bi = 0; bi < n_bodies; ++bi)
        {
            BODY* path_body = bodies[bi];
            if (path_body == nullptr)
                continue;
            BODY* wall = nullptr;
            double half = 0.0;
            if (SweepWallFromPathEntity(step3, options, pair_i, pair_j, bucket.group_i, bucket.group_j, (ENTITY*)path_body, wall, half) == FALSE)
                continue;
            Mm2SweepWallBuild built;
            built.body = wall;
            built.group_i = bucket.group_i;
            built.group_j = bucket.group_j;
            built.bucket_slot = bucket.slot;
            built.from_orphan_recorded = bucket.from_orphan_recorded;
            built.from_partner_collected = bucket.from_partner_collected;
            built.coedge_len = bucket.longest_len;
            built.half_profile = half;
            out_walls.push_back(built);
        }
    }

    if (!out_walls.empty())
        return TRUE;
    if (options.mm2_fallback_edgewise_sweep == FALSE)
        return FALSE;

    int ei = 0;
    for (ei = 0; ei < (int)all_edges.size(); ++ei)
    {
        EDGE* e = all_edges[ei];
        if (e == nullptr)
            continue;
        BODY* wall = nullptr;
        double half = 0.0;
        if (SweepWallFromPathEntity(step3, options, pair_i, pair_j, bucket.group_i, bucket.group_j, (ENTITY*)e, wall, half) == FALSE)
            continue;
        Mm2SweepWallBuild built;
        built.body = wall;
        built.group_i = bucket.group_i;
        built.group_j = bucket.group_j;
        built.bucket_slot = bucket.slot;
        built.from_orphan_recorded = bucket.from_orphan_recorded;
        built.from_partner_collected = bucket.from_partner_collected;
        built.coedge_len = bucket.longest_len;
        built.half_profile = half;
        out_walls.push_back(built);
    }
    return out_walls.empty() ? FALSE : TRUE;
}

logical BuildVirtualWallBodiesFromMm2LinkSweep(
    const Step3PairState& step3,
    const std::map<FACE*, int>& face_to_group,
    int pair_i,
    int pair_j,
    const PairLinkAccum& acc,
    const PairSlotHitCoedgeMap* pair_slot_hit_coedges,
    const Step4RelationOptions& options,
    std::vector<Mm2SweepWallBuild>& out_walls)
{
    out_walls.clear();
    const Step2GroupState* step2 = step3.input_step2;
    if (step2 == nullptr)
        return FALSE;
    if (g_step4_sweep_init_done == FALSE)
    {
        outcome ri = api_initialize_sweeping();
        if (!ri.ok())
            return FALSE;
        g_step4_sweep_init_done = TRUE;
    }

    std::vector<Mm2HitBucket> buckets;
    CollectMm2HitBuckets(step3, face_to_group, pair_i, pair_j, acc, pair_slot_hit_coedges, buckets);
    if (buckets.empty())
        return FALSE;

    std::vector<int> selected;
    if (options.mm2_use_all_hit_buckets != FALSE)
    {
        int i = 0;
        for (i = 0; i < (int)buckets.size(); ++i)
            selected.push_back(i);
    }
    else
    {
        const int bi = SelectPrimaryMm2Bucket(buckets);
        if (bi >= 0)
            selected.push_back(bi);
    }

    int si = 0;
    for (si = 0; si < (int)selected.size(); ++si)
    {
        const Mm2HitBucket& bucket = buckets[selected[si]];
        std::vector<Mm2SweepWallBuild> one;
        if (BuildSweepWallsFromBucket(step3, options, pair_i, pair_j, bucket, one) == FALSE)
            continue;
        int wi = 0;
        for (wi = 0; wi < (int)one.size(); ++wi)
        {
            double hit_cos = -1.0;
            if (ComputeHitNormalCosForGroupPair(*step2, face_to_group, bucket.group_i, bucket.group_j, hit_cos) != FALSE)
                one[wi].hit_normal_cos_abs = hit_cos;
            out_walls.push_back(one[wi]);
        }
    }
    return out_walls.empty() ? FALSE : TRUE;
}

void EmitVirtualWallEvent(DiagnosticSink* diagnostics, const WallRecord& record, logical ok)
{
    StructuredEvent event = ok != FALSE ? Step4SingleSummaryEvent() : Step4DetailEvent();
    event.AddTag("virtual-wall");
    event.AddTag(ok != FALSE ? "ok" : "fail");
    event.SetProperty("wall_id", IntText(record.wall_id));
    event.SetProperty("pair_a", IntText(record.pair_a));
    event.SetProperty("pair_b", IntText(record.pair_b));
    event.SetProperty("group_a", IntText(record.group_a));
    event.SetProperty("group_b", IntText(record.group_b));
    event.SetProperty("bucket_slot", IntText(record.bucket_slot));
    event.SetProperty("source", record.source);
    event.SetProperty("coedge_len", DoubleText(record.coedge_len));
    event.SetProperty("half_profile", DoubleText(record.half_profile));
    event.SetProperty("hit_normal_cos_abs", DoubleText(record.hit_normal_cos_abs));
    EmitEvent(diagnostics, event);
}
} // namespace

Step4RelationOptions::Step4RelationOptions()
    : build_pair_links(TRUE),
      build_walls(TRUE),
      emit_relation_events(TRUE),
      enable_tiny_face_partner_patch(TRUE),
      enable_orphan_wall_bridge(TRUE),
      enable_mm2_angle_gate(TRUE),
      enable_mm2_sweep_wall(TRUE),
      mm2_use_all_hit_buckets(FALSE),
      mm2_per_edge_sweep_only(FALSE),
      mm2_enable_smooth_sampled_path(FALSE),
      mm2_smooth_keep_closed(TRUE),
      mm2_enable_connect_strategy(TRUE),
      mm2_endpoint_single_use(TRUE),
      mm2_sweep_owner1_or3_only(FALSE),
      mm2_extend_only_open_ends(FALSE),
      mm2_path_end_tbar_enable(TRUE),
      mm2_fallback_edgewise_sweep(TRUE),
      min_link_hits(1),
      pair_mm2_angle_deg(5.0),
      mm2_smooth_sample_step_ratio(0.1),
      mm2_smooth_sample_step_abs_min(1.0e-4),
      mm2_connect_gap_ratio(0.2),
      mm2_connect_gap_abs_min(5.0e-4),
      mm2_sweep_thickness_scale(4.0),
      mm2_sweep_thickness_min(1.0e-3),
      mm2_path_end_extend_ratio(0.1),
      mm2_path_end_extend_abs_min(1.0e-4),
      mm2_path_end_tbar_ratio(0.1)
{
}

RelationBuildStats::RelationBuildStats()
    : relation_count(0),
      wall_count(0),
      group_adjacency_count(0),
      coedge_total(0),
      coedge_with_partner(0),
      coedge_partner_ring_gt2(0),
      unique_nonmanifold_edges(0),
      partner_ring_max(0),
      tiny_faces_found(0),
      tiny_partner_coedges(0),
      tiny_patch_applied(0),
      pair_wall_relation_count(0),
      orphan_wall_group_count(0),
      orphan_bridge_injected_count(0),
      direct_pair_link_count(0),
      mm1_count(0),
      mm2_count(0),
      virtual_wall_try_count(0),
      virtual_wall_ok_count(0)
{
}

Step4RelationState::Step4RelationState()
    : input_step3(nullptr)
{
}

Step4RelationResult::Step4RelationResult()
    : ok(FALSE)
{
}

logical RunStep4RelationBuild(
    const Step3PairState& step3,
    const Step4RelationOptions& options,
    DiagnosticSink* diagnostics,
    Step4RelationResult& result)
{
    result = Step4RelationResult();
    result.state.input_step3 = &step3;
    result.state.options_snapshot = options;

    if (options.emit_relation_events != FALSE)
        EmitStartEvent(diagnostics, step3);

    const Step2GroupState* step2 = step3.input_step2;
    if (step2 == nullptr)
    {
        StructuredEvent event = Step4EventTemplate();
        event.AddTag("invalid_input");
        event.SetProperty("reason", "missing_step2_state");
        EmitEvent(diagnostics, event);
        return FALSE;
    }

    std::map<FACE*, int> face_to_group;
    BuildFaceToGroupMap(*step2, face_to_group);

    std::map<std::pair<int, int>, int> group_hits;
    GroupAdjBuildStatsLocal adj_stats;
    BuildGroupAdjacencyHitsByPartner(
        *step2,
        options,
        options.emit_relation_events != FALSE ? diagnostics : nullptr,
        face_to_group,
        group_hits,
        adj_stats);

    result.state.stats.group_adjacency_count = (int)group_hits.size();
    result.state.stats.coedge_total = adj_stats.coedge_total;
    result.state.stats.coedge_with_partner = adj_stats.coedge_with_partner;
    result.state.stats.coedge_partner_ring_gt2 = adj_stats.coedge_partner_ring_gt2;
    result.state.stats.unique_nonmanifold_edges = adj_stats.unique_nonmanifold_edges;
    result.state.stats.partner_ring_max = adj_stats.partner_ring_max;
    result.state.stats.tiny_faces_found = adj_stats.tiny_faces_found;
    result.state.stats.tiny_partner_coedges = adj_stats.coedges_with_tiny_partner;
    result.state.stats.tiny_patch_applied = adj_stats.tiny_partner_patch_applied;

    if (options.emit_relation_events != FALSE)
        EmitAdjacencySummary(diagnostics, group_hits, adj_stats);

    std::map<int, std::vector<int> > group_to_pairs;
    group_to_pairs = step3.group_to_pairs.group_to_pairs;
    const std::map<int, int>& group_to_walls = step3.group_to_walls.group_to_wall;

    std::set<int> mw1_wall_groups;
    int pair_wall_checked_count = 0;
    int pair_wall_rejected_count = 0;
    if (options.build_walls != FALSE)
    {
        int pi = 0;
        for (pi = 0; pi < (int)step3.pairs.pairs.size(); ++pi)
        {
            const PairRecord& pair = step3.pairs.pairs[pi];
            int wall_index = 0;
            for (wall_index = 0; wall_index < (int)step3.walls.walls.size(); ++wall_index)
            {
                const WallRecord& wall = step3.walls.walls[wall_index];
                const int gw = wall.group_a;
                if (gw == pair.group_a || gw == pair.group_b)
                    continue;

                const int hga = GetGroupAdjHit(group_hits, gw, pair.group_a);
                const int hgb = GetGroupAdjHit(group_hits, gw, pair.group_b);
                ++pair_wall_checked_count;
                if (hga <= 0 || hgb <= 0)
                {
                    ++pair_wall_rejected_count;
                    continue;
                }

                PairWallRelationRecord record;
                record.relation_id = (int)result.state.pair_wall_relations.relations.size();
                record.pair_id = pair.pair_id;
                record.wall_group_id = gw;
                record.hit_group_a = hga;
                record.hit_group_b = hgb;
                record.wall_mode = "MW1";
                result.state.pair_wall_relations.relations.push_back(record);
                mw1_wall_groups.insert(gw);
                if (options.emit_relation_events != FALSE)
                    EmitPairWallEvent(diagnostics, record);
            }
        }
    }
    if (options.emit_relation_events != FALSE)
    {
        EmitPairWallStageSummary(
            diagnostics,
            pair_wall_checked_count,
            (int)result.state.pair_wall_relations.relations.size(),
            pair_wall_rejected_count,
            (int)group_to_walls.size());
    }

    std::map<std::pair<int, int>, PairLinkAccum> pair_accum;
    std::map<std::pair<int, int>, PairDirOverride> pair_dir_override;
    PairSlotHitCoedgeMap pair_slot_hit_coedges;
    if (options.build_pair_links != FALSE)
    {
        std::map<std::pair<int, int>, int>::const_iterator git = group_hits.begin();
        for (; git != group_hits.end(); ++git)
        {
            const int ga = git->first.first;
            const int gb = git->first.second;
            const int gh = git->second;
            if (gh <= 0)
                continue;

            const std::map<int, std::vector<int> >::const_iterator owners_a_it = group_to_pairs.find(ga);
            const std::map<int, std::vector<int> >::const_iterator owners_b_it = group_to_pairs.find(gb);
            if (owners_a_it == group_to_pairs.end() || owners_b_it == group_to_pairs.end())
                continue;
            const std::vector<int>& owners_a = owners_a_it->second;
            const std::vector<int>& owners_b = owners_b_it->second;
            if (owners_a.empty() || owners_b.empty())
                continue;

            int ia = 0;
            for (ia = 0; ia < (int)owners_a.size(); ++ia)
            {
                const int pa = owners_a[ia];
                const PairRecord* pair_a = FindPairById(step3, pa);
                if (pair_a == nullptr)
                    continue;
                const int sa = PairSide(*pair_a, ga);
                if (sa < 0)
                    continue;

                int ib = 0;
                for (ib = 0; ib < (int)owners_b.size(); ++ib)
                {
                    const int pb = owners_b[ib];
                    if (pa == pb)
                        continue;
                    const PairRecord* pair_b = FindPairById(step3, pb);
                    if (pair_b == nullptr)
                        continue;
                    const int sb = PairSide(*pair_b, gb);
                    if (sb < 0)
                        continue;

                    int p0 = pa;
                    int p1 = pb;
                    int s0 = sa;
                    int s1 = sb;
                    if (p0 > p1)
                    {
                        std::swap(p0, p1);
                        std::swap(s0, s1);
                    }

                    PairLinkAccum& acc = pair_accum[std::make_pair(p0, p1)];
                    AddSlotHit(acc, s0, s1, gh);
                    acc.direct_hits += gh;
                    if (options.emit_relation_events != FALSE)
                        EmitPairLinkCandidateDetail(diagnostics, p0, p1, ga, gb, s0, s1, gh, acc);
                }
            }
        }

        OrphanBridgeStats bridge_stats;
        if (options.enable_orphan_wall_bridge != FALSE)
        {
            BuildSyntheticPairHitsFromOrphanWalls(
                step3,
                face_to_group,
                group_to_pairs,
                mw1_wall_groups,
                pair_accum,
                pair_dir_override,
                &pair_slot_hit_coedges,
                options.emit_relation_events != FALSE ? diagnostics : nullptr,
                bridge_stats);
            result.state.stats.orphan_wall_group_count = bridge_stats.orphan_wall_groups;
            result.state.stats.orphan_bridge_injected_count = bridge_stats.injected_links;
        }
        if (options.emit_relation_events != FALSE)
            EmitOrphanBridgeStageSummary(diagnostics, bridge_stats);

        const int min_link_hits = std::max(1, options.min_link_hits);
        int pair_link_rejected_count = 0;
        std::map<std::pair<int, int>, PairLinkAccum>::const_iterator pit = pair_accum.begin();
        for (; pit != pair_accum.end(); ++pit)
        {
            const int p0 = pit->first.first;
            const int p1 = pit->first.second;
            const PairLinkAccum& acc = pit->second;
            const int total_hits = acc.aa + acc.ab + acc.ba + acc.bb;
            if (total_hits < min_link_hits)
            {
                ++pair_link_rejected_count;
                continue;
            }

            double hit_cos_abs = -1.0;
            std::string pair_mode = "UNKNOWN";
            std::string classify_source = "pair_direction";
            int gi = -1;
            int gj = -1;
            if (SelectDominantHitGroupPair(step3, p0, p1, acc, gi, gj) != FALSE &&
                ComputeHitNormalCosForGroupPair(*step2, face_to_group, gi, gj, hit_cos_abs) != FALSE)
            {
                const double threshold = std::cos(std::max(0.0, options.pair_mm2_angle_deg) * kPi / 180.0);
                pair_mode = hit_cos_abs >= threshold ? "MM2" : "MM1";
                classify_source = "hit_normal";
            }
            else
            {
                const std::map<std::pair<int, int>, PairDirOverride>::const_iterator oit = pair_dir_override.find(pit->first);
                pair_mode = ClassifyPairModeByAngleFallback(
                    step3,
                    p0,
                    p1,
                    oit == pair_dir_override.end() ? nullptr : &oit->second,
                    options.pair_mm2_angle_deg);
            }
            if (pair_mode == "UNKNOWN")
            {
                ++pair_link_rejected_count;
                continue;
            }
            if (options.enable_mm2_angle_gate == FALSE && pair_mode == "MM2")
                pair_mode = "MM1";

            PairRelationRecord record;
            record.relation_id = (int)result.state.pair_relations.relations.size();
            record.pair_a = p0;
            record.pair_b = p1;
            record.relation_type = "pair-pair";
            record.pair_mode = pair_mode;
            record.classify_source = classify_source;
            if (acc.direct_hits > 0 && acc.orphan_hits > 0)
                record.source = "mixed";
            else if (acc.orphan_hits > 0)
                record.source = "orphan_bridge";
            else
                record.source = "group_adjacency";
            record.hits_aa = acc.aa;
            record.hits_ab = acc.ab;
            record.hits_ba = acc.ba;
            record.hits_bb = acc.bb;
            record.total_hits = total_hits;
            record.hit_normal_cos_abs = hit_cos_abs;
            record.hit_normal_angle_deg = (hit_cos_abs >= 0.0 && hit_cos_abs <= 1.0)
                ? std::acos(hit_cos_abs) * 180.0 / kPi
                : -1.0;
            result.state.pair_relations.relations.push_back(record);
            if (record.source == "group_adjacency")
                ++result.state.stats.direct_pair_link_count;
            if (record.pair_mode == "MM2")
                ++result.state.stats.mm2_count;
            else if (record.pair_mode == "MM1")
                ++result.state.stats.mm1_count;
            if (options.emit_relation_events != FALSE)
                EmitPairLinkEvent(diagnostics, record);
        }
        if (options.emit_relation_events != FALSE)
        {
            int direct_hit_count = 0;
            int orphan_hit_count = 0;
            std::map<std::pair<int, int>, PairLinkAccum>::const_iterator sit = pair_accum.begin();
            for (; sit != pair_accum.end(); ++sit)
            {
                direct_hit_count += sit->second.direct_hits;
                orphan_hit_count += sit->second.orphan_hits;
            }
            EmitPairLinkStageSummary(
                diagnostics,
                (int)pair_accum.size(),
                (int)result.state.pair_relations.relations.size(),
                pair_link_rejected_count,
                direct_hit_count,
                orphan_hit_count,
                result.state.stats.mm1_count,
                result.state.stats.mm2_count);
        }
    }

    if (options.build_walls != FALSE)
    {
        int virtual_wall_fail_count = 0;
        int next_virtual_wall_id = (int)step2->groups.groups.size();
        int ri = 0;
        for (ri = 0; ri < (int)result.state.pair_relations.relations.size(); ++ri)
        {
            const PairRelationRecord& relation = result.state.pair_relations.relations[ri];
            if (relation.pair_mode != "MM2")
                continue;

            PairLinkAccum acc;
            acc.aa = relation.hits_aa;
            acc.ab = relation.hits_ab;
            acc.ba = relation.hits_ba;
            acc.bb = relation.hits_bb;
            ++result.state.stats.virtual_wall_try_count;

            std::vector<Mm2SweepWallBuild> built_walls;
            logical ok_build = FALSE;
            if (options.enable_mm2_sweep_wall != FALSE)
            {
                ok_build = BuildVirtualWallBodiesFromMm2LinkSweep(
                    step3,
                    face_to_group,
                    relation.pair_a,
                    relation.pair_b,
                    acc,
                    &pair_slot_hit_coedges,
                    options,
                    built_walls);
            }

            if (ok_build == FALSE || built_walls.empty())
            {
                WallRecord fail_record;
                fail_record.wall_id = -1;
                fail_record.pair_a = relation.pair_a;
                fail_record.pair_b = relation.pair_b;
                fail_record.source = options.enable_mm2_sweep_wall != FALSE ? "mm2_sweep" : "disabled";
                if (options.emit_relation_events != FALSE)
                    EmitVirtualWallEvent(diagnostics, fail_record, FALSE);
                ++virtual_wall_fail_count;
                continue;
            }

            int wi = 0;
            for (wi = 0; wi < (int)built_walls.size(); ++wi)
            {
                const Mm2SweepWallBuild& built = built_walls[wi];
                if (built.body == nullptr)
                    continue;

                WallRecord record;
                record.wall_id = next_virtual_wall_id++;
                record.body = built.body;
                record.source =
                    (built.from_orphan_recorded != FALSE && built.from_partner_collected != FALSE) ? "mm2_sweep_mixed" :
                    (built.from_orphan_recorded != FALSE ? "mm2_sweep_orphan" : "mm2_sweep_partner");
                record.pair_a = relation.pair_a;
                record.pair_b = relation.pair_b;
                record.group_a = built.group_i;
                record.group_b = built.group_j;
                record.bucket_slot = built.bucket_slot;
                record.coedge_len = built.coedge_len;
                record.half_profile = built.half_profile;
                record.hit_normal_cos_abs = built.hit_normal_cos_abs;
                result.state.walls.walls.push_back(record);
                ++result.state.stats.virtual_wall_ok_count;
                if (options.emit_relation_events != FALSE)
                    EmitVirtualWallEvent(diagnostics, record, TRUE);

                PairWallRelationRecord rel_a;
                rel_a.relation_id = (int)result.state.pair_wall_relations.relations.size();
                rel_a.pair_id = relation.pair_a;
                rel_a.wall_group_id = record.wall_id;
                rel_a.wall_mode = "MW1";
                result.state.pair_wall_relations.relations.push_back(rel_a);

                PairWallRelationRecord rel_b;
                rel_b.relation_id = (int)result.state.pair_wall_relations.relations.size();
                rel_b.pair_id = relation.pair_b;
                rel_b.wall_group_id = record.wall_id;
                rel_b.wall_mode = "MW1";
                result.state.pair_wall_relations.relations.push_back(rel_b);
            }
        }
        if (options.emit_relation_events != FALSE)
        {
            EmitVirtualWallStageSummary(
                diagnostics,
                result.state.stats.virtual_wall_try_count,
                result.state.stats.virtual_wall_ok_count,
                virtual_wall_fail_count);
        }
    }

    result.state.stats.relation_count = (int)result.state.pair_relations.relations.size();
    result.state.stats.pair_wall_relation_count = (int)result.state.pair_wall_relations.relations.size();
    result.state.stats.wall_count = (int)mw1_wall_groups.size() + (int)result.state.walls.walls.size();
    result.ok = TRUE;

    if (options.emit_relation_events != FALSE)
        EmitFinishEvent(diagnostics, result.state.stats);

    return TRUE;
}
} // namespace midsurface_new
