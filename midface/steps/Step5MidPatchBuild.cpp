#include "steps/Step5MidPatchBuild.hpp"

#include "utils/JsonUtils.hpp"
#include "utils/PathUtils.hpp"

#include "cstrapi.hxx"
#include "entity.hxx"
#include "faceqry.hxx"
#include "faceutil.hxx"
#include "geometry.hxx"
#include "getbox.hxx"
#include "intrapi.hxx"
#include "mk_face.hxx"
#include "ofstapi.hxx"
#include "param.hxx"
#include "queryapi.hxx"
#include "surdef.hxx"
#include "cone.hxx"
#include "sphere.hxx"
#include "loop.hxx"
#include "coedge.hxx"
#include "edge.hxx"
#include "curve.hxx"
#include "curveq.hxx"
#include "straight.hxx"
#include "ellipse.hxx"
#include "spline.hxx"
#include "intcurve.hxx"

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
const char* kDebugStep5MidPatchFaces = "step5.mid_patch_faces";
const double kPlaneExtentQuantile = 0.97;
const double kPlaneSharedLargeBlend = 0.75;
const double kPlaneExtentInflate = 1.80;
const double kCylinderHeightInflate = 1.15;

struct Step5RuntimeConfig
{
    Step5RuntimeConfig()
        : analytic_patch_inflate(1.10),
          analytic_min_half_extent(1e-3),
          spline_fit_only(FALSE),
          spline_fit_tol_scale(1.0),
          spline_fit_tol_min(1e-6),
          spline_fit_tol_max(5e-2),
          spline_grid_min(4),
          spline_grid_max(9),
          freeform_uv_init_u(6),
          freeform_uv_init_v(6),
          freeform_uv_refine_rounds(3),
          freeform_uv_refine_angle_deg(20.0),
          freeform_uv_max_u(28),
          freeform_uv_max_v(28),
          freeform_uv_fail_ratio_max(0.45),
          freeform_uv_min_valid_points(16)
    {
    }

    double analytic_patch_inflate;
    double analytic_min_half_extent;
    logical spline_fit_only;
    double spline_fit_tol_scale;
    double spline_fit_tol_min;
    double spline_fit_tol_max;
    int spline_grid_min;
    int spline_grid_max;
    int freeform_uv_init_u;
    int freeform_uv_init_v;
    int freeform_uv_refine_rounds;
    double freeform_uv_refine_angle_deg;
    int freeform_uv_max_u;
    int freeform_uv_max_v;
    double freeform_uv_fail_ratio_max;
    int freeform_uv_min_valid_points;
};

struct EdgeBoundaryInfo
{
    EdgeBoundaryInfo()
        : coedge(nullptr),
          edge(nullptr),
          p0(0.0),
          p1(0.0),
          len(0.0),
          curve_type_id(-1),
          type_weight(1.0),
          interior_min(1)
    {
    }

    COEDGE* coedge;
    EDGE* edge;
    double p0;
    double p1;
    double len;
    int curve_type_id;
    double type_weight;
    int interior_min;
};

struct Step5GroupView
{
    std::vector<FACE*> faces;
    SPAposition seed_point;
    SPAunit_vector seed_normal;
    std::string type;
};

struct Step5PairLinkView
{
    Step5PairLinkView()
        : pair_i(-1),
          pair_j(-1),
          link_type(MID_PATCH_LINK_UNKNOWN),
          conn_class(MID_PATCH_CONN_UNKNOWN),
          pair_mode(MID_PATCH_PAIR_UNKNOWN),
          hits_aa(0),
          hits_ab(0),
          hits_ba(0),
          hits_bb(0),
          total_hits(0)
    {
    }

    int pair_i;
    int pair_j;
    MidPatchLinkType link_type;
    MidPatchConnectClass conn_class;
    MidPatchPairMode pair_mode;
    int hits_aa;
    int hits_ab;
    int hits_ba;
    int hits_bb;
    int total_hits;
};

Step5RuntimeConfig g_runtime;

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

std::string IntListJsonText(const std::vector<int>& values)
{
    JsonValue arr = JsonValue::array();
    int i = 0;
    for (i = 0; i < (int)values.size(); ++i)
        arr.push_back(values[i]);
    return JsonDump(arr);
}

JsonValue PointJson(const SPAposition& p)
{
    JsonValue arr = JsonValue::array();
    arr.push_back(p.x());
    arr.push_back(p.y());
    arr.push_back(p.z());
    return arr;
}

JsonValue VectorJson(const SPAunit_vector& v)
{
    JsonValue arr = JsonValue::array();
    arr.push_back(v.x());
    arr.push_back(v.y());
    arr.push_back(v.z());
    return arr;
}

StructuredEvent Step5DetailEvent()
{
    StructuredEvent event;
    event.AddTag("step5");
    event.AddTag("mid-patch");
    event.AddTag("detail");
    return event;
}

StructuredEvent Step5SingleSummaryEvent()
{
    StructuredEvent event;
    event.AddTag("step5");
    event.AddTag("mid-patch");
    event.AddTag("summary");
    event.AddTag("single");
    return event;
}

StructuredEvent Step5StageSummaryEvent()
{
    StructuredEvent event;
    event.AddTag("step5");
    event.AddTag("mid-patch");
    event.AddTag("summary");
    event.AddTag("stage");
    return event;
}

StructuredEvent Step5AllSummaryEvent()
{
    StructuredEvent event;
    event.AddTag("step5");
    event.AddTag("mid-patch");
    event.AddTag("summary");
    event.AddTag("all");
    return event;
}

void EmitEvent(DiagnosticSink* diagnostics, const StructuredEvent& event)
{
    if (diagnostics != nullptr)
        (void)diagnostics->EmitEvent(event);
}

face_type FaceTypeFromString(const std::string& type)
{
    if (type == "plane")
        return face_plane;
    if (type == "cylinder")
        return face_cylinder;
    if (type == "cone")
        return face_cone;
    if (type == "sphere")
        return face_sphere;
    if (type == "torus")
        return face_torus;
    if (type == "spline")
        return face_spline;
    return face_unknown;
}

const char* FaceTypeName(face_type type)
{
    switch (type)
    {
    case face_plane: return "plane";
    case face_cylinder: return "cylinder";
    case face_cone: return "cone";
    case face_sphere: return "sphere";
    case face_torus: return "torus";
    case face_spline: return "spline";
    case face_unknown:
    default:
        return "unknown";
    }
}

const char* PatchKindName(MidPatchKind kind)
{
    switch (kind)
    {
    case MID_PATCH_PLANE: return "plane";
    case MID_PATCH_CYLINDER: return "cylinder";
    case MID_PATCH_SPHERE: return "sphere";
    case MID_PATCH_ANALYTIC: return "analytic";
    case MID_PATCH_SPLINE: return "spline";
    case MID_PATCH_MIXED: return "mixed";
    case MID_PATCH_UNKNOWN:
    default:
        return "unknown";
    }
}

const char* SideTypeName(MidPatchSideGroupType side)
{
    return side == MID_PATCH_SIDE_MIXED ? "mixed" : "single";
}

MidPatchKind DeducePatchKind(face_type ta, face_type tb)
{
    if (ta == face_plane && tb == face_plane)
        return MID_PATCH_PLANE;
    if (ta == face_cylinder && tb == face_cylinder)
        return MID_PATCH_CYLINDER;
    if (ta == face_sphere && tb == face_sphere)
        return MID_PATCH_SPHERE;
    if (ta == face_spline && tb == face_spline)
        return MID_PATCH_SPLINE;

    const logical a_analytic = (ta == face_plane || ta == face_cylinder || ta == face_cone || ta == face_sphere || ta == face_torus) ? TRUE : FALSE;
    const logical b_analytic = (tb == face_plane || tb == face_cylinder || tb == face_cone || tb == face_sphere || tb == face_torus) ? TRUE : FALSE;
    if (a_analytic != FALSE && b_analytic != FALSE)
        return MID_PATCH_ANALYTIC;

    return MID_PATCH_MIXED;
}

SPAvector UnitToVector(const SPAunit_vector& u)
{
    return SPAvector(u.x(), u.y(), u.z());
}

SPAunit_vector MakeUnitFromVector(const SPAvector& v)
{
    const double l = v.len();
    if (l <= 1e-12)
        return SPAunit_vector(1.0, 0.0, 0.0);
    return SPAunit_vector(v.x() / l, v.y() / l, v.z() / l);
}

SPAposition AddScaled(const SPAposition& p, const SPAvector& v, double s)
{
    return SPAposition(
        p.x() + v.x() * s,
        p.y() + v.y() * s,
        p.z() + v.z() * s);
}

void BuildTangentBasis(const SPAunit_vector& n, SPAvector& u, SPAvector& v)
{
    SPAvector ref(0.0, 0.0, 1.0);
    if (std::fabs(n.z()) > 0.9)
        ref = SPAvector(1.0, 0.0, 0.0);

    SPAvector nn(n.x(), n.y(), n.z());
    u = ref * nn;
    if (u.len() <= 1e-12)
        u = SPAvector(1.0, 0.0, 0.0);
    else
        u = SPAvector(u.x() / u.len(), u.y() / u.len(), u.z() / u.len());

    v = nn * u;
    if (v.len() <= 1e-12)
        v = SPAvector(0.0, 1.0, 0.0);
    else
        v = SPAvector(v.x() / v.len(), v.y() / v.len(), v.z() / v.len());
}

logical SafeFindClsPtToFace(const SPAposition& guess, FACE* face, SPAposition& out_p, logical& out_crashed)
{
    out_crashed = FALSE;
    outcome r = api_find_cls_ptto_face(guess, face, out_p);
    return r.ok() ? TRUE : FALSE;
}

logical SafeGetFaceNormal(FACE* face, const SPAposition& p, SPAunit_vector& out_n, logical& out_crashed)
{
    out_crashed = FALSE;
    out_n = sg_get_face_normal(face, p);
    return TRUE;
}

logical SafeEntityPointDistance(FACE* face, SPAposition& q, SPAposition& cp, double& d, logical& out_crashed)
{
    out_crashed = FALSE;
    outcome r = api_entity_point_distance(face, q, cp, d);
    return r.ok() ? TRUE : FALSE;
}

logical SafeSurfaceEvalPosition(const surface& surf, const SPApar_pos& uv, SPAposition& out_p, logical& out_crashed)
{
    out_crashed = FALSE;
    out_p = surf.eval_position(uv);
    return TRUE;
}

logical IsNearPos(const SPAposition& p, const std::vector<SPAposition>& pts, double tol2)
{
    int i = 0;
    for (i = 0; i < (int)pts.size(); ++i)
    {
        const SPAvector d = pts[i] - p;
        if ((d % d) <= tol2)
            return TRUE;
    }
    return FALSE;
}

double ComputePosTol2(const std::vector<SPAposition>& pts)
{
    if (pts.empty())
        return 1e-16;

    SPAposition lo = pts[0];
    SPAposition hi = pts[0];
    int i = 0;
    for (i = 1; i < (int)pts.size(); ++i)
    {
        const SPAposition& p = pts[i];
        lo = SPAposition(
            (std::min)(lo.x(), p.x()),
            (std::min)(lo.y(), p.y()),
            (std::min)(lo.z(), p.z()));
        hi = SPAposition(
            (std::max)(hi.x(), p.x()),
            (std::max)(hi.y(), p.y()),
            (std::max)(hi.z(), p.z()));
    }
    const SPAvector d = hi - lo;
    const double d2 = d % d;
    return (std::max)(1e-16, d2 * 1e-10);
}

logical IsFiniteCoord(double x)
{
    return (x == x && x > -1e300 && x < 1e300) ? TRUE : FALSE;
}

logical IsFinitePos(const SPAposition& p)
{
    return (IsFiniteCoord(p.x()) && IsFiniteCoord(p.y()) && IsFiniteCoord(p.z())) ? TRUE : FALSE;
}

logical IsSplineGridSane(const std::vector<SPAposition>& grid, int nu, int nv)
{
    if (nu < 2 || nv < 2)
        return FALSE;
    if ((int)grid.size() != nu * nv)
        return FALSE;

    int i = 0;
    for (i = 0; i < (int)grid.size(); ++i)
    {
        if (IsFinitePos(grid[i]) == FALSE)
            return FALSE;
    }

    const double tol2 = ComputePosTol2(grid);
    std::vector<SPAposition> uniq;
    uniq.reserve(grid.size());
    for (i = 0; i < (int)grid.size(); ++i)
    {
        if (IsNearPos(grid[i], uniq, tol2) == FALSE)
            uniq.push_back(grid[i]);
    }
    if ((int)uniq.size() < (int)(0.60 * (double)grid.size()))
        return FALSE;

    double row_span_max = 0.0;
    int j = 0;
    for (j = 0; j < nv; ++j)
    {
        const double l = (grid[j * nu + (nu - 1)] - grid[j * nu]).len();
        if (l > row_span_max)
            row_span_max = l;
    }

    double col_span_max = 0.0;
    for (i = 0; i < nu; ++i)
    {
        const double l = (grid[(nv - 1) * nu + i] - grid[i]).len();
        if (l > col_span_max)
            col_span_max = l;
    }

    if (row_span_max <= 1e-9 || col_span_max <= 1e-9)
        return FALSE;
    return TRUE;
}

double EstimateSplineFitTol(const std::vector<SPAposition>& pts)
{
    if (pts.empty())
        return 1e-5;
    const double tol2 = ComputePosTol2(pts);
    const double tol = std::sqrt((std::max)(1e-16, tol2)) * 10.0;
    return (std::max)(1e-6, tol);
}

logical BuildSplineFaceFitSafe(
    int nu,
    int nv,
    const SPAposition* pts,
    double fit_tol,
    FACE*& out_face,
    logical& out_crashed)
{
    out_face = nullptr;
    out_crashed = FALSE;
    if (pts == nullptr || nu < 2 || nv < 2)
        return FALSE;

    std::vector<SPAunit_vector> du_s(nv, SPAunit_vector(1.0, 0.0, 0.0));
    std::vector<SPAunit_vector> du_e(nv, SPAunit_vector(1.0, 0.0, 0.0));
    int j = 0;
    for (j = 0; j < nv; ++j)
    {
        const int r0 = j * nu;
        SPAvector t0 = pts[r0 + 1] - pts[r0 + 0];
        SPAvector t1 = pts[r0 + (nu - 1)] - pts[r0 + (nu - 2)];
        if (t0.len() <= 1e-12) t0 = SPAvector(1.0, 0.0, 0.0);
        if (t1.len() <= 1e-12) t1 = SPAvector(1.0, 0.0, 0.0);
        du_s[j] = MakeUnitFromVector(t0);
        du_e[j] = MakeUnitFromVector(t1);
    }

    FACE* f = nullptr;
    outcome r = api_mk_fa_spl_fit(
        fit_tol,
        nu,
        nv,
        pts,
        &du_s[0],
        &du_e[0],
        f);
    if (!r.ok() || f == nullptr)
        return FALSE;
    out_face = f;
    return TRUE;
}

logical BuildSplineFaceInterpSafe(
    int nu,
    int nv,
    const SPAposition* pts,
    FACE*& out_face,
    logical& out_crashed)
{
    out_face = nullptr;
    out_crashed = FALSE;
    if (pts == nullptr || nu < 2 || nv < 2)
        return FALSE;

    const SPAunit_vector* du_s = nullptr;
    const SPAunit_vector* du_e = nullptr;
    const SPAunit_vector* dv_s = nullptr;
    const SPAunit_vector* dv_e = nullptr;

    FACE* f = nullptr;
    outcome r = api_mk_fa_spl_intp(
        nu,
        nv,
        pts,
        du_s,
        du_e,
        dv_s,
        dv_e,
        f);
    if (!r.ok() || f == nullptr)
        return FALSE;
    out_face = f;
    return TRUE;
}

int EdgeCurveTypeId(EDGE* e)
{
    if (e == nullptr)
        return -1;
    CURVE* c = e->geometry();
    if (c == nullptr)
        return -1;
    return c->identity();
}

double EdgeTypeWeightFromCurveId(int curve_id)
{
    if (curve_id == STRAIGHT_TYPE)
        return 1.00;
    if (curve_id == ELLIPSE_TYPE)
        return 1.80;
    if (curve_id == SPLINE_TYPE || curve_id == INTCURVE_TYPE)
        return 2.80;
    return 1.35;
}

int EdgeTypeInteriorMinFromCurveId(int curve_id)
{
    if (curve_id == STRAIGHT_TYPE)
        return 1;
    if (curve_id == ELLIPSE_TYPE)
        return 4;
    if (curve_id == SPLINE_TYPE || curve_id == INTCURVE_TYPE)
        return 8;
    return 3;
}

double Quantile1D(std::vector<double> vals, double q)
{
    if (vals.empty())
        return 0.0;
    if (q < 0.0) q = 0.0;
    if (q > 1.0) q = 1.0;
    std::sort(vals.begin(), vals.end());
    const double pos = q * (double)(vals.size() - 1);
    const int i0 = (int)std::floor(pos);
    const int i1 = (int)std::ceil(pos);
    if (i0 == i1)
        return vals[i0];
    const double t = pos - (double)i0;
    return vals[i0] * (1.0 - t) + vals[i1] * t;
}

void CollectFaceBoundarySamples(FACE* f, std::vector<SPAposition>& out_pts)
{
    if (f == nullptr)
        return;

    std::vector<EdgeBoundaryInfo> infos;
    double total_len = 0.0;
    int edge_count = 0;

    LOOP* lp = f->loop();
    while (lp != nullptr)
    {
        COEDGE* start = lp->start();
        COEDGE* c = start;
        if (c != nullptr)
        {
            do
            {
                EDGE* e = c->edge();
                if (e != nullptr)
                {
                    EdgeBoundaryInfo bi;
                    bi.coedge = c;
                    bi.edge = e;
                    bi.p0 = coedge_start_param(c);
                    bi.p1 = coedge_end_param(c);
                    bi.curve_type_id = EdgeCurveTypeId(e);
                    bi.type_weight = EdgeTypeWeightFromCurveId(bi.curve_type_id);
                    bi.interior_min = EdgeTypeInteriorMinFromCurveId(bi.curve_type_id);

                    const double dp = std::fabs(bi.p1 - bi.p0);
                    if (dp > 1e-12)
                    {
                        const int nseg = 12;
                        SPAposition prev = coedge_param_pos(c, bi.p0);
                        int s = 1;
                        for (s = 1; s <= nseg; ++s)
                        {
                            const double t = (double)s / (double)nseg;
                            const double u = bi.p0 + (bi.p1 - bi.p0) * t;
                            SPAposition cur = coedge_param_pos(c, u);
                            bi.len += (cur - prev).len();
                            prev = cur;
                        }
                    }
                    else
                    {
                        const SPAposition sp = c->start_pos();
                        const SPAposition ep = c->end_pos();
                        bi.len = (ep - sp).len();
                    }
                    if (bi.len <= 1e-12)
                        bi.len = 1e-6;

                    infos.push_back(bi);
                    total_len += bi.len;
                    ++edge_count;
                }
                c = c->next();
            } while (c != nullptr && c != start);
        }
        lp = lp->next();
    }

    if (infos.empty())
        return;

    const double mean_len = edge_count > 0 ? total_len / (double)edge_count : 1.0;
    int i = 0;
    for (i = 0; i < (int)infos.size(); ++i)
    {
        const EdgeBoundaryInfo& bi = infos[i];
        out_pts.push_back(coedge_param_pos(bi.coedge, bi.p0));
        out_pts.push_back(coedge_param_pos(bi.coedge, bi.p1));

        int n_interior = bi.interior_min;
        if (mean_len > 1e-12)
        {
            double len_ratio = bi.len / mean_len;
            if (len_ratio < 0.25) len_ratio = 0.25;
            if (len_ratio > 6.00) len_ratio = 6.00;
            n_interior += (int)std::floor(4.0 * bi.type_weight * len_ratio + 0.5);
        }
        if (n_interior < 1) n_interior = 1;
        if (n_interior > 72) n_interior = 72;

        int s = 1;
        for (s = 1; s <= n_interior; ++s)
        {
            const double t = (double)s / (double)(n_interior + 1);
            const double u = bi.p0 + (bi.p1 - bi.p0) * t;
            out_pts.push_back(coedge_param_pos(bi.coedge, u));
        }
    }
}

void CollectGroupBoundarySamples(const Step5GroupView& group, std::vector<SPAposition>& out_pts)
{
    int i = 0;
    for (i = 0; i < (int)group.faces.size(); ++i)
        CollectFaceBoundarySamples(group.faces[i], out_pts);
}

void UpdateExtentsByFaceBoundary(
    FACE* f,
    const SPAposition& mid,
    const SPAvector& u,
    const SPAvector& v,
    std::vector<double>& out_abs_u,
    std::vector<double>& out_abs_v)
{
    std::vector<SPAposition> pts;
    CollectFaceBoundarySamples(f, pts);
    int i = 0;
    for (i = 0; i < (int)pts.size(); ++i)
    {
        const SPAvector r = pts[i] - mid;
        out_abs_u.push_back(std::fabs(r % u));
        out_abs_v.push_back(std::fabs(r % v));
    }
}

void UpdateSignedByFaceBoundary(
    FACE* f,
    const SPAposition& mid,
    const SPAvector& u,
    const SPAvector& v,
    std::vector<double>& out_u,
    std::vector<double>& out_v)
{
    std::vector<SPAposition> pts;
    CollectFaceBoundarySamples(f, pts);
    int i = 0;
    for (i = 0; i < (int)pts.size(); ++i)
    {
        const SPAvector r = pts[i] - mid;
        out_u.push_back(r % u);
        out_v.push_back(r % v);
    }
}

double SelectSharedExtent(double a, double b, double floor_v)
{
    const double eps = 1e-12;
    double x = 0.0;
    if (a > eps && b > eps)
    {
        const double s = (std::min)(a, b);
        const double l = (std::max)(a, b);
        x = s + kPlaneSharedLargeBlend * (l - s);
    }
    else if (a > eps)
        x = a;
    else
        x = b;

    if (x < floor_v)
        x = floor_v;
    return x;
}

FACE* FindFirstFaceOfType(const Step5GroupView& group, face_type type)
{
    int i = 0;
    for (i = 0; i < (int)group.faces.size(); ++i)
    {
        FACE* face = group.faces[i];
        if (face != nullptr && get_face_type(face) == type)
            return face;
    }
    for (i = 0; i < (int)group.faces.size(); ++i)
    {
        if (group.faces[i] != nullptr)
            return group.faces[i];
    }
    return nullptr;
}

CONE* FaceAsCone(FACE* face)
{
    if (face == nullptr)
        return nullptr;
    SURFACE* surface = face->geometry();
    if (surface == nullptr || surface->identity() != CONE_TYPE)
        return nullptr;
    return (CONE*)surface;
}

SPHERE* FaceAsSphere(FACE* face)
{
    if (face == nullptr)
        return nullptr;
    SURFACE* surface = face->geometry();
    if (surface == nullptr || surface->identity() != SPHERE_TYPE)
        return nullptr;
    return (SPHERE*)surface;
}

logical BuildFaceCenterOnFace(FACE* face, SPAposition& out_p)
{
    out_p = SPAposition(0.0, 0.0, 0.0);
    if (face == nullptr)
        return FALSE;
    const SPAbox box = get_face_box(face);
    const SPAposition center(
        0.5 * (box.low().x() + box.high().x()),
        0.5 * (box.low().y() + box.high().y()),
        0.5 * (box.low().z() + box.high().z()));
    logical crashed = FALSE;
    return SafeFindClsPtToFace(center, face, out_p, crashed);
}

logical EstimateGroupRepPointNormal(
    const Step5GroupView& group,
    logical prefer_plane_normal,
    SPAposition& out_point,
    SPAunit_vector& out_normal,
    std::vector<SPAposition>& out_boundary_samples)
{
    out_point = SPAposition(0.0, 0.0, 0.0);
    out_normal = SPAunit_vector(1.0, 0.0, 0.0);
    out_boundary_samples.clear();
    CollectGroupBoundarySamples(group, out_boundary_samples);
    if (out_boundary_samples.empty())
        return FALSE;

    double sx = 0.0, sy = 0.0, sz = 0.0;
    int i = 0;
    for (i = 0; i < (int)out_boundary_samples.size(); ++i)
    {
        sx += out_boundary_samples[i].x();
        sy += out_boundary_samples[i].y();
        sz += out_boundary_samples[i].z();
    }
    const double inv = 1.0 / (double)out_boundary_samples.size();
    out_point = SPAposition(sx * inv, sy * inv, sz * inv);

    SPAvector nsum(0.0, 0.0, 0.0);
    logical has_n = FALSE;
    for (i = 0; i < (int)group.faces.size(); ++i)
    {
        FACE* face = group.faces[i];
        if (face == nullptr)
            continue;
        if (prefer_plane_normal != FALSE && get_face_type(face) != face_plane)
            continue;
        SPAposition p;
        if (BuildFaceCenterOnFace(face, p) == FALSE)
            continue;
        SPAunit_vector fn;
        logical crashed = FALSE;
        if (SafeGetFaceNormal(face, p, fn, crashed) == FALSE)
            continue;
        SPAvector v(fn.x(), fn.y(), fn.z());
        if (has_n == FALSE)
        {
            nsum = v;
            has_n = TRUE;
        }
        else
        {
            if ((nsum % v) < 0.0)
                v = SPAvector(-v.x(), -v.y(), -v.z());
            nsum = SPAvector(nsum.x() + v.x(), nsum.y() + v.y(), nsum.z() + v.z());
        }
    }
    if (has_n == FALSE)
    {
        nsum = SPAvector(group.seed_normal.x(), group.seed_normal.y(), group.seed_normal.z());
        if (nsum.len() <= 1e-12)
            nsum = SPAvector(1.0, 0.0, 0.0);
    }
    out_normal = MakeUnitFromVector(nsum);
    return TRUE;
}

logical BuildPlanePatchByProjectedBounds(
    const SPAposition& mid_point,
    const SPAunit_vector& normal,
    const std::vector<SPAposition>& all_points,
    FACE*& out_face)
{
    out_face = nullptr;
    if (all_points.empty())
        return FALSE;

    SPAvector u, v;
    BuildTangentBasis(normal, u, v);
    double umin = 0.0, umax = 0.0, vmin = 0.0, vmax = 0.0;
    logical has = FALSE;
    int i = 0;
    for (i = 0; i < (int)all_points.size(); ++i)
    {
        const SPAvector r = all_points[i] - mid_point;
        const double pu = r % u;
        const double pv = r % v;
        if (has == FALSE)
        {
            umin = umax = pu;
            vmin = vmax = pv;
            has = TRUE;
        }
        else
        {
            if (pu < umin) umin = pu;
            if (pu > umax) umax = pu;
            if (pv < vmin) vmin = pv;
            if (pv > vmax) vmax = pv;
        }
    }
    if (has == FALSE)
        return FALSE;

    double hu = 0.5 * (umax - umin);
    double hv = 0.5 * (vmax - vmin);
    if (hu < g_runtime.analytic_min_half_extent) hu = g_runtime.analytic_min_half_extent;
    if (hv < g_runtime.analytic_min_half_extent) hv = g_runtime.analytic_min_half_extent;
    hu *= g_runtime.analytic_patch_inflate;
    hv *= g_runtime.analytic_patch_inflate;

    const double cu = 0.5 * (umin + umax);
    const double cv = 0.5 * (vmin + vmax);
    const SPAposition center_uv = AddScaled(AddScaled(mid_point, u, cu), v, cv);
    const SPAposition origin = AddScaled(AddScaled(center_uv, u, -hu), v, -hv);
    const SPAposition left = AddScaled(AddScaled(center_uv, u, -hu), v, hv);
    const SPAposition right = AddScaled(AddScaled(center_uv, u, hu), v, -hv);

    outcome rmk = api_make_plface(origin, left, right, out_face);
    return rmk.ok() && out_face != nullptr ? TRUE : FALSE;
}

logical BuildMidPlaneFacePure(
    const std::vector<Step5GroupView>& groups,
    const MidPatchRecord& patch,
    FACE*& out_face)
{
    out_face = nullptr;
    if (patch.group_a < 0 || patch.group_b < 0 ||
        patch.group_a >= (int)groups.size() ||
        patch.group_b >= (int)groups.size())
        return FALSE;

    const Step5GroupView& ga = groups[patch.group_a];
    const Step5GroupView& gb = groups[patch.group_b];
    SPAposition pa, pb;
    SPAunit_vector na, nb;
    std::vector<SPAposition> pts_a, pts_b;
    if (EstimateGroupRepPointNormal(ga, TRUE, pa, na, pts_a) == FALSE)
        return FALSE;
    if (EstimateGroupRepPointNormal(gb, TRUE, pb, nb, pts_b) == FALSE)
        return FALSE;

    const SPAposition pm(
        0.5 * (pa.x() + pb.x()),
        0.5 * (pa.y() + pb.y()),
        0.5 * (pa.z() + pb.z()));
    std::vector<SPAposition> all_pts = pts_a;
    int i = 0;
    for (i = 0; i < (int)pts_b.size(); ++i)
        all_pts.push_back(pts_b[i]);
    return BuildPlanePatchByProjectedBounds(pm, na, all_pts, out_face);
}

logical BuildMidFaceByDominantOffset(
    const std::vector<Step5GroupView>& groups,
    const MidPatchRecord& patch,
    int dominant_side,
    FACE*& out_face)
{
    out_face = nullptr;
    if (patch.group_a < 0 || patch.group_b < 0 ||
        patch.group_a >= (int)groups.size() ||
        patch.group_b >= (int)groups.size())
        return FALSE;

    const Step5GroupView& g_dom = dominant_side == 0 ? groups[patch.group_a] : groups[patch.group_b];
    const Step5GroupView& g_oth = dominant_side == 0 ? groups[patch.group_b] : groups[patch.group_a];
    const face_type t_dom = dominant_side == 0 ? FaceTypeFromString(patch.type_a) : FaceTypeFromString(patch.type_b);
    FACE* fd = FindFirstFaceOfType(g_dom, t_dom);
    if (fd == nullptr)
        return FALSE;

    SPAposition pd;
    if (BuildFaceCenterOnFace(fd, pd) == FALSE)
        return FALSE;
    SPAunit_vector nd;
    logical crashed = FALSE;
    if (SafeGetFaceNormal(fd, pd, nd, crashed) == FALSE)
        return FALSE;

    std::vector<SPAposition> pts_other;
    CollectGroupBoundarySamples(g_oth, pts_other);
    if (pts_other.empty())
        return FALSE;

    double sx = 0.0, sy = 0.0, sz = 0.0;
    int i = 0;
    for (i = 0; i < (int)pts_other.size(); ++i)
    {
        sx += pts_other[i].x();
        sy += pts_other[i].y();
        sz += pts_other[i].z();
    }
    const double inv = 1.0 / (double)pts_other.size();
    const SPAposition po(sx * inv, sy * inv, sz * inv);

    const SPAvector d = po - pd;
    const double sign = ((d % UnitToVector(nd)) >= 0.0) ? 1.0 : -1.0;
    const double half_t = 0.5 * (std::max)(1e-6, std::fabs(patch.thickness));
    FACE* fo = nullptr;
    outcome ro = api_offset_face(fd, sign * half_t, fo, nullptr, nullptr);
    if (!ro.ok() || fo == nullptr)
    {
        ro = api_offset_face(fd, -sign * half_t, fo, nullptr, nullptr);
        if (!ro.ok() || fo == nullptr)
            return FALSE;
    }
    out_face = fo;
    return TRUE;
}

logical TryBuildCylinderAnalyticFace(
    const std::vector<Step5GroupView>& groups,
    const MidPatchRecord& patch,
    FACE*& out_face)
{
    out_face = nullptr;
    if (patch.kind != MID_PATCH_CYLINDER)
        return FALSE;

    const Step5GroupView& ga = groups[patch.group_a];
    const Step5GroupView& gb = groups[patch.group_b];
    FACE* fa = FindFirstFaceOfType(ga, face_cylinder);
    FACE* fb = FindFirstFaceOfType(gb, face_cylinder);
    CONE* ca = FaceAsCone(fa);
    CONE* cb = FaceAsCone(fb);
    if (ca == nullptr || cb == nullptr)
        return FALSE;

    SPAvector va = UnitToVector(ca->direction());
    SPAvector vb = UnitToVector(cb->direction());
    if ((va % vb) < 0.0)
        vb = SPAvector(-vb.x(), -vb.y(), -vb.z());
    SPAunit_vector axis = MakeUnitFromVector(va + vb);

    const SPAposition ra = ca->root_point();
    const SPAposition rb = cb->root_point();
    SPAposition center0(
        0.5 * (ra.x() + rb.x()),
        0.5 * (ra.y() + rb.y()),
        0.5 * (ra.z() + rb.z()));

    std::vector<SPAposition> pts;
    CollectGroupBoundarySamples(ga, pts);
    CollectGroupBoundarySamples(gb, pts);
    if (pts.empty())
    {
        pts.push_back(patch.point_a);
        pts.push_back(patch.point_b);
        pts.push_back(patch.point_mid);
    }

    const SPAvector axis_v = UnitToVector(axis);
    double tmin = 0.0, tmax = 0.0;
    logical has_t = FALSE;
    int i = 0;
    for (i = 0; i < (int)pts.size(); ++i)
    {
        const double t = (pts[i] - center0) % axis_v;
        if (has_t == FALSE)
        {
            tmin = tmax = t;
            has_t = TRUE;
        }
        else
        {
            if (t < tmin) tmin = t;
            if (t > tmax) tmax = t;
        }
    }
    if (has_t == FALSE)
        return FALSE;

    double h = tmax - tmin;
    if (h <= 1e-6)
    {
        h = (std::max)(1e-3, 6.0 * (std::max)(1e-6, patch.thickness));
        tmin = -0.5 * h;
        tmax = 0.5 * h;
    }
    else
    {
        const double tc = 0.5 * (tmin + tmax);
        const double hh = 0.5 * h * kCylinderHeightInflate;
        tmin = tc - hh;
        tmax = tc + hh;
        h = tmax - tmin;
    }

    const SPAposition base = AddScaled(center0, axis_v, tmin);
    const SPAvector axis_h(axis_v.x() * h, axis_v.y() * h, axis_v.z() * h);
    const double ra_major = ca->major_axis().len();
    const double rb_major = cb->major_axis().len();
    double rmid = 0.5 * (ra_major + rb_major);
    if (rmid <= 1e-8)
        rmid = (std::max)(1e-4, 0.5 * patch.thickness);

    double minor_over_major = 0.5 * (ca->radius_ratio() + cb->radius_ratio());
    if (minor_over_major <= 1e-8)
        minor_over_major = 1.0;
    double major_over_minor = 1.0 / minor_over_major;
    if (major_over_minor < 1.0)
        major_over_minor = 1.0;

    SPAvector ma = ca->major_axis();
    SPAvector mb = cb->major_axis();
    if ((ma % mb) < 0.0)
        mb = SPAvector(-mb.x(), -mb.y(), -mb.z());
    SPAvector m = ma + mb;
    if (m.len() <= 1e-12)
        m = ma;
    const double proj = m % axis_v;
    m = SPAvector(
        m.x() - axis_v.x() * proj,
        m.y() - axis_v.y() * proj,
        m.z() - axis_v.z() * proj);
    if (m.len() <= 1e-12)
    {
        SPAvector u, v;
        BuildTangentBasis(axis, u, v);
        m = u;
    }
    const SPAunit_vector mdir = MakeUnitFromVector(m);
    const SPAposition pt_major = AddScaled(base, UnitToVector(mdir), rmid);
    FACE* f = nullptr;
    outcome r = api_face_cylinder_cone(base, axis_h, rmid, rmid, 0.0, 360.0, major_over_minor, &pt_major, f);
    if (!r.ok() || f == nullptr)
        return FALSE;
    out_face = f;
    return TRUE;
}

logical TryBuildSphereAnalyticFace(
    const std::vector<Step5GroupView>& groups,
    const MidPatchRecord& patch,
    FACE*& out_face)
{
    out_face = nullptr;
    if (patch.kind != MID_PATCH_SPHERE)
        return FALSE;

    const Step5GroupView& ga = groups[patch.group_a];
    const Step5GroupView& gb = groups[patch.group_b];
    FACE* fa = FindFirstFaceOfType(ga, face_sphere);
    FACE* fb = FindFirstFaceOfType(gb, face_sphere);
    SPHERE* sa = FaceAsSphere(fa);
    SPHERE* sb = FaceAsSphere(fb);
    if (sa == nullptr || sb == nullptr)
        return FALSE;

    const SPAposition ca = sa->centre();
    const SPAposition cb = sb->centre();
    SPAposition center(
        0.5 * (ca.x() + cb.x()),
        0.5 * (ca.y() + cb.y()),
        0.5 * (ca.z() + cb.z()));
    double rmid = 0.5 * (std::fabs(sa->radius()) + std::fabs(sb->radius()));
    if (rmid <= 1e-8)
        return FALSE;

    SPAvector nvec(patch.normal_mid.x(), patch.normal_mid.y(), patch.normal_mid.z());
    if (nvec.len() <= 1e-12)
        nvec = SPAvector(0.0, 0.0, 1.0);

    FACE* f = nullptr;
    outcome ro = api_face_sphere(center, rmid, -90.0, 90.0, -180.0, 180.0, &nvec, f);
    if (!ro.ok() || f == nullptr)
        return FALSE;
    out_face = f;
    return TRUE;
}

double FaceAreaProxySimple(FACE* face)
{
    if (face == nullptr)
        return 0.0;
    const SPAbox box = get_face_box(face);
    const double dx = std::fabs(box.high().x() - box.low().x());
    const double dy = std::fabs(box.high().y() - box.low().y());
    const double dz = std::fabs(box.high().z() - box.low().z());
    double d0 = dx;
    double d1 = dy;
    double d2 = dz;
    if (d0 > d1) { double t = d0; d0 = d1; d1 = t; }
    if (d1 > d2) { double t = d1; d1 = d2; d2 = t; }
    if (d0 > d1) { double t = d0; d0 = d1; d1 = t; }
    return (std::max)(1e-12, d1 * d2);
}

double GroupAreaProxySimple(const Step5GroupView& group, face_type only_type, logical type_filter)
{
    double s = 0.0;
    int i = 0;
    for (i = 0; i < (int)group.faces.size(); ++i)
    {
        FACE* face = group.faces[i];
        if (face == nullptr)
            continue;
        if (type_filter != FALSE && get_face_type(face) != only_type)
            continue;
        s += FaceAreaProxySimple(face);
    }
    return s;
}

logical IsFreeformTypeLocal(face_type t)
{
    return (t == face_spline || t == face_unknown) ? TRUE : FALSE;
}

int MidTypePriority(face_type t)
{
    if (t == face_plane) return 6;
    if (t == face_cylinder) return 5;
    if (t == face_sphere) return 4;
    if (t == face_cone) return 3;
    if (t == face_spline) return 2;
    if (t == face_torus) return 1;
    return 0;
}

int ChooseDominantSideByType(face_type ta, face_type tb)
{
    if (ta == face_torus && tb != face_torus)
        return 1;
    if (tb == face_torus && ta != face_torus)
        return 0;

    const int pa = MidTypePriority(ta);
    const int pb = MidTypePriority(tb);
    if (pa > pb) return 0;
    if (pb > pa) return 1;
    return 0;
}

int ChooseAnchorSideForMergedUnit(const MidPatchRecord& patch)
{
    if (patch.merged_unit == FALSE || patch.anchor_group < 0)
        return -1;
    if (patch.anchor_group == patch.group_a)
        return 0;
    if (patch.anchor_group == patch.group_b)
        return 1;
    return -1;
}

int ChooseDominantSideForPatch(const MidPatchRecord& patch)
{
    const int merged = ChooseAnchorSideForMergedUnit(patch);
    if (merged >= 0)
        return merged;
    if (patch.side_type_a != patch.side_type_b)
        return patch.side_type_a == MID_PATCH_SIDE_SINGLE ? 0 : 1;
    return ChooseDominantSideByType(FaceTypeFromString(patch.type_a), FaceTypeFromString(patch.type_b));
}

logical BuildAnalyticMidFacePure(
    const std::vector<Step5GroupView>& groups,
    const MidPatchRecord& patch,
    FACE*& out_face,
    std::string& out_reason,
    std::string& out_method)
{
    out_face = nullptr;
    out_reason.clear();
    out_method.clear();

    const int merged_side = ChooseAnchorSideForMergedUnit(patch);
    if (merged_side >= 0)
    {
        if (BuildMidFaceByDominantOffset(groups, patch, merged_side, out_face) != FALSE)
        {
            out_method = "dominant_offset_merged_anchor";
            return TRUE;
        }
    }

    if (patch.side_type_a != patch.side_type_b)
    {
        const int dominant_side_mixed = ChooseDominantSideForPatch(patch);
        if (BuildMidFaceByDominantOffset(groups, patch, dominant_side_mixed, out_face) != FALSE)
        {
            out_method = "dominant_offset_single_side";
            return TRUE;
        }
    }

    const face_type ta = FaceTypeFromString(patch.type_a);
    const face_type tb = FaceTypeFromString(patch.type_b);
    if (ta == face_plane && tb == face_plane)
    {
        if (BuildMidPlaneFacePure(groups, patch, out_face) != FALSE)
        {
            out_method = "plane_projected_bounds";
            return TRUE;
        }
        out_reason = "PLANE_BUILD_FAIL";
        return FALSE;
    }
    if (ta == face_cylinder && tb == face_cylinder)
    {
        if (TryBuildCylinderAnalyticFace(groups, patch, out_face) != FALSE)
        {
            out_method = "cylinder_analytic";
            return TRUE;
        }
        out_reason = "CYLINDER_BUILD_FAIL";
        return FALSE;
    }
    if (ta == face_sphere && tb == face_sphere)
    {
        if (TryBuildSphereAnalyticFace(groups, patch, out_face) != FALSE)
        {
            out_method = "sphere_analytic";
            return TRUE;
        }
        out_reason = "SPHERE_BUILD_FAIL";
        return FALSE;
    }

    const int dominant_side = ChooseDominantSideForPatch(patch);
    if (BuildMidFaceByDominantOffset(groups, patch, dominant_side, out_face) != FALSE)
    {
        out_method = "dominant_offset";
        return TRUE;
    }
    out_reason = "DOMINANT_OFFSET_FAIL";
    return FALSE;
}

logical BuildFreeformMidFacePure(
    const std::vector<Step5GroupView>& groups,
    const MidPatchRecord& patch,
    FACE*& out_face,
    std::string& out_reason,
    std::string& out_method)
{
    struct Local
    {
        static void BuildUniformUvAxis(double minv, double maxv, int n, std::vector<double>& out_vals)
        {
            out_vals.clear();
            if (n <= 0)
                return;
            out_vals.resize(n);
            if (n == 1)
            {
                out_vals[0] = 0.5 * (minv + maxv);
                return;
            }
            int i = 0;
            for (i = 0; i < n; ++i)
            {
                const double t = (double)i / (double)(n - 1);
                out_vals[i] = minv + (maxv - minv) * t;
            }
        }
    };

    out_face = nullptr;
    out_reason.clear();
    out_method.clear();

    struct UvMidSample
    {
        UvMidSample()
            : uv(0.0, 0.0),
              mid(0.0, 0.0, 0.0),
              src(0.0, 0.0, 0.0),
              dst(0.0, 0.0, 0.0),
              dist(0.0),
              dir(1.0, 0.0, 0.0)
        {
        }

        UvMidSample(
            const SPApar_pos& p_uv,
            const SPAposition& p_mid,
            const SPAposition& p_src,
            const SPAposition& p_dst,
            double p_dist,
            const SPAunit_vector& p_dir)
            : uv(p_uv),
              mid(p_mid),
              src(p_src),
              dst(p_dst),
              dist(p_dist),
              dir(p_dir)
        {
        }

        SPApar_pos uv;
        SPAposition mid;
        SPAposition src;
        SPAposition dst;
        double dist;
        SPAunit_vector dir;
    };

    struct UvEvalCacheEntry
    {
        UvEvalCacheEntry() : ready(FALSE), valid(FALSE), sample() {}
        logical ready;
        logical valid;
        UvMidSample sample;
    };

    struct FreeformOps
    {
        static std::string MakeUvCacheKey(double u, double v)
        {
            char buf[96];
            sprintf(buf, "%.12e|%.12e", u, v);
            return std::string(buf);
        }

        static logical FindNearestOnGroup(
            const Step5GroupView& group,
            const SPAposition& probe,
            FACE*& out_face,
            SPAposition& out_cp,
            double& out_d)
        {
            out_face = nullptr;
            out_cp = SPAposition(0.0, 0.0, 0.0);
            out_d = 0.0;
            logical has = FALSE;
            int i = 0;
            for (i = 0; i < (int)group.faces.size(); ++i)
            {
                FACE* face = group.faces[i];
                if (face == nullptr)
                    continue;
                SPAposition q = probe;
                SPAposition cp;
                double d = 0.0;
                logical crashed = FALSE;
                if (SafeEntityPointDistance(face, q, cp, d, crashed) == FALSE)
                    continue;
                if (d < -1e-12)
                    continue;
                if (has == FALSE || d < out_d)
                {
                    has = TRUE;
                    out_face = face;
                    out_cp = cp;
                    out_d = d;
                }
            }
            return has;
        }

        static logical EvaluateUvMidOne(
            FACE* src_face,
            const surface& src_surf,
            const Step5GroupView& dst,
            const SPAunit_vector& ref_dir,
            double uu,
            double vv,
            UvMidSample& out_sample,
            int& io_total,
            int& io_hits,
            int& io_probe,
            int& io_hit)
        {
            const SPApar_pos uv(uu, vv);
            SPAposition pe;
            logical cev = FALSE;
            if (SafeSurfaceEvalPosition(src_surf, uv, pe, cev) == FALSE)
                return FALSE;

            SPAposition p;
            logical cfp = FALSE;
            if (SafeFindClsPtToFace(pe, src_face, p, cfp) == FALSE)
                return FALSE;

            ++io_total;
            ++io_probe;

            FACE* fd = nullptr;
            SPAposition cp;
            double d = 0.0;
            if (FindNearestOnGroup(dst, p, fd, cp, d) == FALSE)
                return FALSE;
            if (d <= 1e-9)
                return FALSE;

            SPAunit_vector na, nb;
            logical cna = FALSE, cnb = FALSE;
            if (SafeGetFaceNormal(src_face, p, na, cna) == FALSE)
                return FALSE;
            if (SafeGetFaceNormal(fd, cp, nb, cnb) == FALSE)
                return FALSE;
            const double n_dot = na % nb;
            if (n_dot > 0.3)
                return FALSE;

            SPAvector dirv = cp - p;
            if (dirv.len() <= 1e-12)
                return FALSE;
            SPAunit_vector dunit = MakeUnitFromVector(dirv);
            if ((dunit % ref_dir) < 0.0)
                dunit = SPAunit_vector(-dunit.x(), -dunit.y(), -dunit.z());

            ++io_hits;
            ++io_hit;
            out_sample = UvMidSample(
                uv,
                SPAposition(
                    0.5 * (p.x() + cp.x()),
                    0.5 * (p.y() + cp.y()),
                    0.5 * (p.z() + cp.z())),
                p,
                cp,
                d,
                dunit);
            return TRUE;
        }

        static logical GetOrEvalUvMidOne(
            FACE* src_face,
            const surface& src_surf,
            const Step5GroupView& dst,
            const SPAunit_vector& ref_dir,
            double uu,
            double vv,
            std::map<std::string, UvEvalCacheEntry>& cache,
            UvMidSample& out_sample,
            logical& out_valid,
            int& io_total,
            int& io_hits,
            int& io_probe,
            int& io_hit)
        {
            const std::string key = MakeUvCacheKey(uu, vv);
            std::map<std::string, UvEvalCacheEntry>::iterator it = cache.find(key);
            if (it != cache.end() && it->second.ready != FALSE)
            {
                out_valid = it->second.valid;
                out_sample = it->second.sample;
                return TRUE;
            }

            UvEvalCacheEntry e;
            e.ready = TRUE;
            UvMidSample s;
            e.valid = EvaluateUvMidOne(
                src_face, src_surf, dst, ref_dir, uu, vv, s,
                io_total, io_hits, io_probe, io_hit) ? TRUE : FALSE;
            if (e.valid != FALSE)
                e.sample = s;
            cache[key] = e;
            out_valid = e.valid;
            out_sample = e.sample;
            return TRUE;
        }

        static int InsertSortedUniqueAxis(std::vector<double>& axis, double v, double tol, int max_size)
        {
            if ((int)axis.size() >= max_size)
                return 0;
            int i = 0;
            for (i = 0; i < (int)axis.size(); ++i)
            {
                if (std::fabs(axis[i] - v) <= tol)
                    return 0;
            }
            axis.push_back(v);
            std::sort(axis.begin(), axis.end());
            if ((int)axis.size() > max_size)
                axis.resize(max_size);
            return 1;
        }

        static logical BuildSplineGridDirectFromUvSamples(
            const std::vector<UvMidSample>& samples,
            int& out_nu,
            int& out_nv,
            std::vector<SPAposition>& out_grid)
        {
            out_nu = 0;
            out_nv = 0;
            out_grid.clear();
            if ((int)samples.size() < 12)
                return FALSE;

            std::vector<double> us;
            std::vector<double> vs;
            us.reserve(samples.size());
            vs.reserve(samples.size());
            int i = 0;
            for (i = 0; i < (int)samples.size(); ++i)
            {
                us.push_back((double)samples[i].uv.u);
                vs.push_back((double)samples[i].uv.v);
            }
            std::sort(us.begin(), us.end());
            std::sort(vs.begin(), vs.end());
            if (us.empty() || vs.empty())
                return FALSE;

            const double ur = us.back() - us.front();
            const double vr = vs.back() - vs.front();
            const double utol = (std::max)(1e-12, ur * 1e-9);
            const double vtol = (std::max)(1e-12, vr * 1e-9);

            std::vector<double> uu;
            std::vector<double> vv;
            for (i = 0; i < (int)us.size(); ++i)
            {
                if (uu.empty() || std::fabs(us[i] - uu.back()) > utol)
                    uu.push_back(us[i]);
            }
            for (i = 0; i < (int)vs.size(); ++i)
            {
                if (vv.empty() || std::fabs(vs[i] - vv.back()) > vtol)
                    vv.push_back(vs[i]);
            }

            if ((int)uu.size() < 2 || (int)vv.size() < 2)
                return FALSE;

            out_nu = (int)uu.size();
            out_nv = (int)vv.size();
            const int total = out_nu * out_nv;
            if (total != (int)samples.size())
                return FALSE;

            out_grid.resize(total);
            std::vector<int> filled(total, 0);
            int k = 0;
            for (k = 0; k < (int)samples.size(); ++k)
            {
                const double u = (double)samples[k].uv.u;
                const double v = (double)samples[k].uv.v;
                int iu = -1;
                int iv = -1;
                int a = 0;
                for (a = 0; a < out_nu; ++a)
                {
                    if (std::fabs(u - uu[a]) <= utol)
                    {
                        iu = a;
                        break;
                    }
                }
                for (a = 0; a < out_nv; ++a)
                {
                    if (std::fabs(v - vv[a]) <= vtol)
                    {
                        iv = a;
                        break;
                    }
                }
                if (iu < 0 || iv < 0)
                    return FALSE;
                const int idx = iv * out_nu + iu;
                if (idx < 0 || idx >= total || filled[idx])
                    return FALSE;
                out_grid[idx] = samples[k].mid;
                filled[idx] = 1;
            }
            for (k = 0; k < total; ++k)
            {
                if (!filled[k])
                    return FALSE;
            }
            return TRUE;
        }

        static logical FitSplineFaceFromUvMidSamples(
            const std::vector<UvMidSample>& samples,
            FACE*& out_face,
            int& out_nu,
            int& out_nv,
            std::string* out_fail_reason,
            std::string* out_api_used)
        {
            out_face = nullptr;
            out_nu = 0;
            out_nv = 0;
            if (out_fail_reason != nullptr)
                *out_fail_reason = "";
            if (out_api_used != nullptr)
                *out_api_used = "";
            if ((int)samples.size() < 12)
            {
                if (out_fail_reason != nullptr)
                    *out_fail_reason = "UV_MID_TOO_FEW";
                return FALSE;
            }

            int nu = 0;
            int nv = 0;
            std::vector<SPAposition> grid;
            if (BuildSplineGridDirectFromUvSamples(samples, nu, nv, grid) == FALSE)
            {
                if (out_fail_reason != nullptr)
                    *out_fail_reason = "UV_DIRECT_GRID_BUILD_FAIL";
                return FALSE;
            }
            if (IsSplineGridSane(grid, nu, nv) == FALSE)
            {
                if (out_fail_reason != nullptr)
                    *out_fail_reason = "UV_DIRECT_GRID_DEGENERATE";
                return FALSE;
            }

            FACE* f = nullptr;
            logical crashed = FALSE;
            if (g_runtime.spline_fit_only == FALSE)
            {
                if (BuildSplineFaceInterpSafe(nu, nv, &grid[0], f, crashed) != FALSE)
                {
                    out_face = f;
                    out_nu = nu;
                    out_nv = nv;
                    if (out_api_used != nullptr)
                        *out_api_used = "api_mk_fa_spl_intp(no_tangent,uv_direct)";
                    return TRUE;
                }
                if (crashed != FALSE)
                {
                    if (out_fail_reason != nullptr)
                        *out_fail_reason = "UV_SPLINE_INTP_CRASH";
                    return FALSE;
                }
            }

            double fit_tol = EstimateSplineFitTol(grid);
            fit_tol *= g_runtime.spline_fit_tol_scale;
            if (fit_tol < g_runtime.spline_fit_tol_min)
                fit_tol = g_runtime.spline_fit_tol_min;
            if (fit_tol > g_runtime.spline_fit_tol_max)
                fit_tol = g_runtime.spline_fit_tol_max;

            if (BuildSplineFaceFitSafe(nu, nv, &grid[0], fit_tol, f, crashed) != FALSE)
            {
                out_face = f;
                out_nu = nu;
                out_nv = nv;
                if (out_api_used != nullptr)
                {
                    char buf[128];
                    sprintf(buf, "api_mk_fa_spl_fit(tol=%.6g,uv_direct)", fit_tol);
                    *out_api_used = buf;
                }
                return TRUE;
            }
            if (crashed != FALSE)
            {
                if (out_fail_reason != nullptr)
                    *out_fail_reason = "UV_SPLINE_FIT_CRASH";
                return FALSE;
            }
            if (out_fail_reason != nullptr)
                *out_fail_reason = (g_runtime.spline_fit_only == FALSE) ? "UV_SPLINE_INTP_AND_FIT_FAIL" : "UV_SPLINE_FIT_FAIL";
            return FALSE;
        }
    };

    FACE* ref_face = nullptr;
    const Step5GroupView* dst_group = nullptr;
    SPAunit_vector ref_dir = patch.normal_mid;
    if (patch.group_a < 0 || patch.group_b < 0 ||
        patch.group_a >= (int)groups.size() ||
        patch.group_b >= (int)groups.size())
    {
        out_reason = "FREEFORM_REF_FAIL";
        return FALSE;
    }

    const Step5GroupView& ga = groups[patch.group_a];
    const Step5GroupView& gb = groups[patch.group_b];
    FACE* fa = FindFirstFaceOfType(ga, face_spline);
    FACE* fb = FindFirstFaceOfType(gb, face_spline);
    const logical has_a = (fa != nullptr) ? TRUE : FALSE;
    const logical has_b = (fb != nullptr) ? TRUE : FALSE;
    if (has_a == FALSE && has_b == FALSE)
    {
        out_reason = "FREEFORM_REF_FAIL";
        return FALSE;
    }

    int ref_side = 0;
    const int preferred_merged_side = ChooseAnchorSideForMergedUnit(patch);
    const int preferred_single_side =
        (patch.side_type_a != patch.side_type_b)
            ? ((patch.side_type_a == MID_PATCH_SIDE_SINGLE) ? 0 : 1)
            : -1;
    if (preferred_merged_side == 0 && has_a != FALSE)
        ref_side = 0;
    else if (preferred_merged_side == 1 && has_b != FALSE)
        ref_side = 1;
    else if (preferred_single_side == 0 && has_a != FALSE)
        ref_side = 0;
    else if (preferred_single_side == 1 && has_b != FALSE)
        ref_side = 1;
    else if (has_a != FALSE && has_b == FALSE)
        ref_side = 0;
    else if (has_a == FALSE && has_b != FALSE)
        ref_side = 1;
    else
    {
        const double aa = GroupAreaProxySimple(ga, face_spline, TRUE);
        const double ab = GroupAreaProxySimple(gb, face_spline, TRUE);
        ref_side = (aa <= ab) ? 0 : 1;
    }

    if (ref_side == 0)
    {
        ref_face = fa;
        dst_group = &gb;
        ref_dir = patch.normal_mid;
    }
    else
    {
        ref_face = fb;
        dst_group = &ga;
        ref_dir = SPAunit_vector(-patch.normal_mid.x(), -patch.normal_mid.y(), -patch.normal_mid.z());
    }
    if (ref_face == nullptr || dst_group == nullptr)
    {
        out_reason = "FREEFORM_REF_FAIL";
        return FALSE;
    }
    const SPAvector rv(ref_dir.x(), ref_dir.y(), ref_dir.z());
    if (rv.len() <= 1e-12)
        ref_dir = SPAunit_vector(1.0, 0.0, 0.0);

    SURFACE* sg = ref_face->geometry();
    if (sg == nullptr)
    {
        out_reason = "FREEFORM_REF_SURF_NULL";
        return FALSE;
    }
    const surface& src_surf = sg->equation();
    SPApar_box pb;
    if (sg_get_face_par_box(ref_face, pb) == FALSE)
        pb = src_surf.param_range();

    const SPAinterval ur = pb.u_range();
    const SPAinterval vr = pb.v_range();
    const double umin = ur.start_pt();
    const double umax = ur.end_pt();
    const double vmin = vr.start_pt();
    const double vmax = vr.end_pt();
    if (std::fabs(umax - umin) <= 1e-12 || std::fabs(vmax - vmin) <= 1e-12)
    {
        out_reason = "FREEFORM_UV_RANGE_BAD";
        return FALSE;
    }

    std::vector<double> uu;
    std::vector<double> vv;
    Local::BuildUniformUvAxis(umin, umax, g_runtime.freeform_uv_init_u, uu);
    Local::BuildUniformUvAxis(vmin, vmax, g_runtime.freeform_uv_init_v, vv);

    const double cos_turn = std::cos(g_runtime.freeform_uv_refine_angle_deg * 3.14159265358979323846 / 180.0);
    std::map<std::string, UvEvalCacheEntry> cache;
    int io_total = 0;
    int io_hits = 0;
    int io_probe = 0;
    int io_hit = 0;

    int round = 0;
    for (round = 0; round <= g_runtime.freeform_uv_refine_rounds; ++round)
    {
        const int nu = (int)uu.size();
        const int nv = (int)vv.size();
        std::vector<UvMidSample> samples(nu * nv);
        std::vector<char> valid(nu * nv, 0);

        int i = 0;
        int j = 0;
        for (j = 0; j < nv; ++j)
        {
            for (i = 0; i < nu; ++i)
            {
                UvMidSample s;
                logical ok = FALSE;
                (void)FreeformOps::GetOrEvalUvMidOne(
                    ref_face, src_surf, *dst_group, ref_dir, uu[i], vv[j],
                    cache, s, ok, io_total, io_hits, io_probe, io_hit);
                if (ok != FALSE)
                {
                    samples[j * nu + i] = s;
                    valid[j * nu + i] = 1;
                }
            }
        }

        if (round >= g_runtime.freeform_uv_refine_rounds)
            break;

        std::vector<double> add_rows;
        std::vector<double> add_cols;
        for (j = 0; j + 1 < nv; ++j)
        {
            logical need = FALSE;
            for (i = 0; i < nu; ++i)
            {
                const int i0 = j * nu + i;
                const int i1 = (j + 1) * nu + i;
                if (!valid[i0] || !valid[i1])
                    continue;
                if ((samples[i0].dir % samples[i1].dir) < cos_turn)
                {
                    need = TRUE;
                    break;
                }
            }
            if (need != FALSE)
                add_rows.push_back(0.5 * (vv[j] + vv[j + 1]));
        }
        for (i = 0; i + 1 < nu; ++i)
        {
            logical need = FALSE;
            for (j = 0; j < nv; ++j)
            {
                const int i0 = j * nu + i;
                const int i1 = j * nu + (i + 1);
                if (!valid[i0] || !valid[i1])
                    continue;
                if ((samples[i0].dir % samples[i1].dir) < cos_turn)
                {
                    need = TRUE;
                    break;
                }
            }
            if (need != FALSE)
                add_cols.push_back(0.5 * (uu[i] + uu[i + 1]));
        }

        int added = 0;
        const double utol = (std::max)(1e-12, (umax - umin) * 1e-9);
        const double vtol = (std::max)(1e-12, (vmax - vmin) * 1e-9);
        int k = 0;
        for (k = 0; k < (int)add_rows.size(); ++k)
            added += FreeformOps::InsertSortedUniqueAxis(vv, add_rows[k], vtol, g_runtime.freeform_uv_max_v);
        for (k = 0; k < (int)add_cols.size(); ++k)
            added += FreeformOps::InsertSortedUniqueAxis(uu, add_cols[k], utol, g_runtime.freeform_uv_max_u);
        if (added <= 0)
            break;
    }

    const int nu = (int)uu.size();
    const int nv = (int)vv.size();
    std::vector<UvMidSample> lattice(nu * nv);
    std::vector<char> valid(nu * nv, 0);

    int out_valid = 0;
    int i = 0;
    int j = 0;
    for (j = 0; j < nv; ++j)
    {
        for (i = 0; i < nu; ++i)
        {
            UvMidSample s;
            logical ok = FALSE;
            (void)FreeformOps::GetOrEvalUvMidOne(
                ref_face, src_surf, *dst_group, ref_dir, uu[i], vv[j],
                cache, s, ok, io_total, io_hits, io_probe, io_hit);
            if (ok != FALSE)
            {
                lattice[j * nu + i] = s;
                valid[j * nu + i] = 1;
                ++out_valid;
            }
        }
    }

    const int total = nu * nv;
    const int invalid = total - out_valid;
    const double invalid_ratio = (total > 0) ? ((double)invalid / (double)total) : 1.0;
    if (out_valid < g_runtime.freeform_uv_min_valid_points || invalid_ratio > g_runtime.freeform_uv_fail_ratio_max)
    {
        out_reason = "FREEFORM_TOO_MANY_INVALID";
        return FALSE;
    }

    for (j = 0; j < nv; ++j)
    {
        for (i = 0; i < nu; ++i)
        {
            const int idx = j * nu + i;
            if (valid[idx])
                continue;
            int best = -1;
            double best_d2 = 0.0;
            int jj = 0;
            int ii = 0;
            for (jj = 0; jj < nv; ++jj)
            {
                for (ii = 0; ii < nu; ++ii)
                {
                    const int id2 = jj * nu + ii;
                    if (!valid[id2])
                        continue;
                    const double du = uu[ii] - uu[i];
                    const double dv = vv[jj] - vv[j];
                    const double d2 = du * du + dv * dv;
                    if (best < 0 || d2 < best_d2)
                    {
                        best = id2;
                        best_d2 = d2;
                    }
                }
            }
            if (best >= 0)
            {
                UvMidSample s = lattice[best];
                s.uv = SPApar_pos(uu[i], vv[j]);
                lattice[idx] = s;
                valid[idx] = 1;
            }
        }
    }

    int out_nu = 0;
    int out_nv = 0;
    std::string fit_fail;
    std::string api_used;
    if (FreeformOps::FitSplineFaceFromUvMidSamples(lattice, out_face, out_nu, out_nv, &fit_fail, &api_used) == FALSE)
    {
        out_reason = fit_fail;
        return FALSE;
    }

    out_method = api_used;
    return TRUE;
}

void BuildConnectedComponents(
    const std::vector< std::vector<int> >& pair_neighbors,
    std::vector< std::vector<int> >& out_components)
{
    out_components.clear();
    const int n = (int)pair_neighbors.size();
    std::vector<int> vis(n, 0);
    int i = 0;
    for (i = 0; i < n; ++i)
    {
        if (vis[i])
            continue;
        vis[i] = 1;
        std::vector<int> comp;
        std::vector<int> q;
        q.push_back(i);
        int head = 0;
        while (head < (int)q.size())
        {
            const int u = q[head++];
            comp.push_back(u);
            const std::vector<int>& nei = pair_neighbors[u];
            int k = 0;
            for (k = 0; k < (int)nei.size(); ++k)
            {
                const int v = nei[k];
                if (v < 0 || v >= n || vis[v])
                    continue;
                vis[v] = 1;
                q.push_back(v);
            }
        }
        std::sort(comp.begin(), comp.end());
        out_components.push_back(comp);
    }
}

logical BuildOnePatch(
    int pair_idx,
    const PairRecord& pair,
    const std::vector<Step5GroupView>& groups,
    MidPatchRecord& out)
{
    out = MidPatchRecord();
    out.patch_id = -1;
    out.source_pair_id = pair_idx;
    out.group_a = pair.group_a;
    out.group_b = pair.group_b;
    out.anchor_group = -1;
    out.component_id = -1;
    out.merged_unit = FALSE;
    out.member_pairs.clear();
    out.member_pairs.push_back(pair_idx);
    out.valid = FALSE;
    out.is_rib = pair.rib_candidate;
    out.is_uncertain = pair.uncertain;
    out.thickness = pair.thickness;

    if (pair.group_a < 0 || pair.group_b < 0)
        return FALSE;
    if (pair.group_a >= (int)groups.size() || pair.group_b >= (int)groups.size())
        return FALSE;

    const Step5GroupView& ga = groups[pair.group_a];
    const Step5GroupView& gb = groups[pair.group_b];
    const face_type ta = FaceTypeFromString(ga.type);
    const face_type tb = FaceTypeFromString(gb.type);
    out.type_a = FaceTypeName(ta);
    out.type_b = FaceTypeName(tb);
    out.kind = DeducePatchKind(ta, tb);

    SPAposition pa = pair.point_a;
    SPAposition pb = pair.point_b;
    SPAvector vab = pb - pa;
    double d = vab.len();
    if (d <= 1e-12)
    {
        pa = ga.seed_point;
        pb = gb.seed_point;
        vab = pb - pa;
        d = vab.len();
    }
    out.point_a = pa;
    out.point_b = pb;
    out.point_mid = SPAposition(
        0.5 * (pa.x() + pb.x()),
        0.5 * (pa.y() + pb.y()),
        0.5 * (pa.z() + pb.z()));

    if (d > 1e-12)
        out.normal_mid = SPAunit_vector(vab.x() / d, vab.y() / d, vab.z() / d);
    else
        out.normal_mid = ga.seed_normal;

    out.valid = TRUE;
    return TRUE;
}

void BuildGroupViews(const Step2GroupState& step2, std::vector<Step5GroupView>& out_groups)
{
    out_groups.clear();
    int i = 0;
    for (i = 0; i < (int)step2.groups.groups.size(); ++i)
    {
        const GroupRecord& src = step2.groups.groups[i];
        Step5GroupView view;
        view.faces = src.faces;
        view.seed_point = src.seed_point;
        view.seed_normal = src.seed_normal;
        view.type = src.type;
        out_groups.push_back(view);
    }
}

MidPatchPairMode PairModeFromText(const std::string& text)
{
    if (text == "MM1")
        return MID_PATCH_PAIR_MM1;
    if (text == "MM2")
        return MID_PATCH_PAIR_MM2;
    return MID_PATCH_PAIR_UNKNOWN;
}

MidPatchConnectClass ConnectClassFromPairMode(MidPatchPairMode mode)
{
    if (mode == MID_PATCH_PAIR_MM1)
        return MID_PATCH_CONN_MM1;
    if (mode == MID_PATCH_PAIR_MM2)
        return MID_PATCH_CONN_MM2;
    return MID_PATCH_CONN_UNKNOWN;
}

MidPatchLinkType LinkTypeFromRelation(const PairRelationRecord& relation, const Step3PairState& step3)
{
    const int same = relation.hits_aa + relation.hits_bb;
    const int cross = relation.hits_ab + relation.hits_ba;
    if (same > 0 && cross > 0)
        return MID_PATCH_LINK_MIXED;
    if (same > 0)
    {
        logical rib = FALSE;
        if (relation.pair_a >= 0 && relation.pair_a < (int)step3.pairs.pairs.size())
            rib = step3.pairs.pairs[relation.pair_a].rib_candidate;
        if (relation.pair_b >= 0 && relation.pair_b < (int)step3.pairs.pairs.size())
            rib = rib || step3.pairs.pairs[relation.pair_b].rib_candidate;
        return rib != FALSE ? MID_PATCH_LINK_RIB_T : MID_PATCH_LINK_SAME_SIDE;
    }
    if (cross > 0)
        return MID_PATCH_LINK_CROSS_SIDE;
    return MID_PATCH_LINK_UNKNOWN;
}

void BuildPairLinks(
    const Step4RelationState& step4,
    std::vector<Step5PairLinkView>& out_links,
    std::vector< std::vector<int> >& out_neighbors)
{
    out_links.clear();
    const Step3PairState* step3 = step4.input_step3;
    const int pair_count = step3 == nullptr ? 0 : (int)step3->pairs.pairs.size();
    out_neighbors.assign(pair_count, std::vector<int>());

    int i = 0;
    for (i = 0; i < (int)step4.pair_relations.relations.size(); ++i)
    {
        const PairRelationRecord& rel = step4.pair_relations.relations[i];
        if (rel.pair_a < 0 || rel.pair_a >= pair_count || rel.pair_b < 0 || rel.pair_b >= pair_count)
            continue;
        Step5PairLinkView link;
        link.pair_i = rel.pair_a;
        link.pair_j = rel.pair_b;
        link.pair_mode = PairModeFromText(rel.pair_mode);
        link.conn_class = ConnectClassFromPairMode(link.pair_mode);
        link.link_type = LinkTypeFromRelation(rel, *step3);
        link.hits_aa = rel.hits_aa;
        link.hits_ab = rel.hits_ab;
        link.hits_ba = rel.hits_ba;
        link.hits_bb = rel.hits_bb;
        link.total_hits = rel.total_hits;
        out_links.push_back(link);
        out_neighbors[rel.pair_a].push_back(rel.pair_b);
        out_neighbors[rel.pair_b].push_back(rel.pair_a);
    }

    for (i = 0; i < (int)out_neighbors.size(); ++i)
    {
        std::vector<int>& nei = out_neighbors[i];
        std::sort(nei.begin(), nei.end());
        nei.erase(std::unique(nei.begin(), nei.end()), nei.end());
    }
}

void EmitPatchEvent(DiagnosticSink* diagnostics, const MidPatchRecord& patch)
{
    StructuredEvent event = Step5SingleSummaryEvent();
    event.AddTag("patch");
    event.AddTag(patch.face != nullptr ? "ok" : "fail");
    event.SetProperty("patch_id", IntText(patch.patch_id));
    event.SetProperty("source_pair_id", IntText(patch.source_pair_id));
    event.SetProperty("group_a", IntText(patch.group_a));
    event.SetProperty("group_b", IntText(patch.group_b));
    event.SetProperty("anchor_group", IntText(patch.anchor_group));
    event.SetProperty("component_id", IntText(patch.component_id));
    event.SetProperty("merged_unit", BoolText(patch.merged_unit));
    event.SetJsonProperty("member_pairs", IntListJsonText(patch.member_pairs));
    event.SetProperty("kind", PatchKindName(patch.kind));
    event.SetProperty("type_a", patch.type_a);
    event.SetProperty("type_b", patch.type_b);
    event.SetProperty("side_type_a", SideTypeName(patch.side_type_a));
    event.SetProperty("side_type_b", SideTypeName(patch.side_type_b));
    event.SetProperty("valid", BoolText(patch.valid));
    event.SetProperty("has_face", BoolText(patch.face != nullptr ? TRUE : FALSE));
    event.SetProperty("build_method", patch.build_method);
    event.SetProperty("fail_reason", patch.fail_reason);
    event.SetProperty("thickness", DoubleText(patch.thickness));
    event.SetJsonProperty("point_mid", JsonDump(PointJson(patch.point_mid)));
    event.SetJsonProperty("normal_mid", JsonDump(VectorJson(patch.normal_mid)));
    EmitEvent(diagnostics, event);
}

void EmitStageSummary(
    DiagnosticSink* diagnostics,
    const char* stage,
    const std::map<std::string, std::string>& properties)
{
    StructuredEvent event = Step5StageSummaryEvent();
    event.AddTag(stage == nullptr ? "stage" : stage);
    event.AddTag("finish");
    std::map<std::string, std::string>::const_iterator it = properties.begin();
    for (; it != properties.end(); ++it)
        event.SetProperty(it->first, it->second);
    EmitEvent(diagnostics, event);
}

void EmitFinishEvent(DiagnosticSink* diagnostics, const MidPatchBuildStats& stats)
{
    StructuredEvent event = Step5AllSummaryEvent();
    event.AddTag("finish");
    event.SetProperty("input_pair_count", IntText(stats.input_pair_count));
    event.SetProperty("component_count", IntText(stats.component_count));
    event.SetProperty("requested_patch_count", IntText(stats.requested_patch_count));
    event.SetProperty("built_patch_count", IntText(stats.built_patch_count));
    event.SetProperty("failed_patch_count", IntText(stats.failed_patch_count));
    event.SetProperty("junction_count", IntText(stats.junction_count));
    event.SetProperty("merged_unit_count", IntText(stats.merged_unit_count));
    event.SetProperty("merged_pair_count", IntText(stats.merged_pair_count));
    EmitEvent(diagnostics, event);
}
} // namespace

Step5MidPatchOptions::Step5MidPatchOptions()
    : build_sheet_body(TRUE),
      emit_patch_events(TRUE),
      min_component_pairs(1),
      keep_isolated_pairs(TRUE),
      enable_one_to_n_merge_units(FALSE),
      spline_fit_only(FALSE),
      spline_fit_tol_scale(1.0),
      spline_fit_tol_min(1e-6),
      spline_fit_tol_max(5e-2),
      spline_grid_min(4),
      spline_grid_max(9),
      freeform_uv_init_u(6),
      freeform_uv_init_v(6),
      freeform_uv_refine_rounds(3),
      freeform_uv_refine_angle_deg(20.0),
      freeform_uv_max_u(28),
      freeform_uv_max_v(28),
      freeform_uv_fail_ratio_max(0.45),
      freeform_uv_min_valid_points(16),
      analytic_patch_inflate(1.10),
      analytic_min_half_extent(1e-3)
{
}

MidPatchBuildStats::MidPatchBuildStats()
    : input_pair_count(0),
      component_count(0),
      requested_patch_count(0),
      built_patch_count(0),
      failed_patch_count(0),
      junction_count(0),
      merged_unit_count(0),
      merged_pair_count(0)
{
}

Step5MidPatchState::Step5MidPatchState()
    : input_step4(nullptr)
{
}

Step5MidPatchResult::Step5MidPatchResult()
    : ok(FALSE)
{
}

logical RunStep5MidPatchBuild(
    const Step4RelationState& step4,
    const Step5MidPatchOptions& options,
    DiagnosticSink* diagnostics,
    Step5MidPatchResult& result)
{
    result = Step5MidPatchResult();
    result.state.input_step4 = &step4;
    result.state.options_snapshot = options;

    const Step3PairState* step3 = step4.input_step3;
    const Step2GroupState* step2 = (step3 == nullptr) ? nullptr : step3->input_step2;
    if (step3 == nullptr || step2 == nullptr)
    {
        if (diagnostics != nullptr)
        {
            StructuredEvent event = Step5AllSummaryEvent();
            event.AddTag("finish");
            event.AddTag("fail");
            event.SetProperty("reason", "missing_step3_or_step2_input");
            (void)diagnostics->EmitEvent(event);
        }
        return FALSE;
    }

    g_runtime.analytic_patch_inflate = options.analytic_patch_inflate;
    g_runtime.analytic_min_half_extent = options.analytic_min_half_extent;
    g_runtime.spline_fit_only = options.spline_fit_only;
    g_runtime.spline_fit_tol_scale = options.spline_fit_tol_scale;
    g_runtime.spline_fit_tol_min = options.spline_fit_tol_min;
    g_runtime.spline_fit_tol_max = options.spline_fit_tol_max;
    g_runtime.spline_grid_min = options.spline_grid_min;
    g_runtime.spline_grid_max = options.spline_grid_max;
    g_runtime.freeform_uv_init_u = options.freeform_uv_init_u;
    g_runtime.freeform_uv_init_v = options.freeform_uv_init_v;
    g_runtime.freeform_uv_refine_rounds = options.freeform_uv_refine_rounds;
    g_runtime.freeform_uv_refine_angle_deg = options.freeform_uv_refine_angle_deg;
    g_runtime.freeform_uv_max_u = options.freeform_uv_max_u;
    g_runtime.freeform_uv_max_v = options.freeform_uv_max_v;
    g_runtime.freeform_uv_fail_ratio_max = options.freeform_uv_fail_ratio_max;
    g_runtime.freeform_uv_min_valid_points = options.freeform_uv_min_valid_points;
    if (g_runtime.analytic_patch_inflate < 1.0) g_runtime.analytic_patch_inflate = 1.0;
    if (g_runtime.analytic_patch_inflate > 3.0) g_runtime.analytic_patch_inflate = 3.0;
    if (g_runtime.analytic_min_half_extent < 1e-6) g_runtime.analytic_min_half_extent = 1e-6;
    if (g_runtime.spline_fit_tol_scale <= 0.0) g_runtime.spline_fit_tol_scale = 1.0;
    if (g_runtime.spline_fit_tol_min <= 0.0) g_runtime.spline_fit_tol_min = 1e-6;
    if (g_runtime.spline_fit_tol_max < g_runtime.spline_fit_tol_min) g_runtime.spline_fit_tol_max = g_runtime.spline_fit_tol_min;
    if (g_runtime.spline_grid_min < 2) g_runtime.spline_grid_min = 2;
    if (g_runtime.spline_grid_max < g_runtime.spline_grid_min) g_runtime.spline_grid_max = g_runtime.spline_grid_min;
    if (g_runtime.freeform_uv_init_u < 3) g_runtime.freeform_uv_init_u = 3;
    if (g_runtime.freeform_uv_init_v < 3) g_runtime.freeform_uv_init_v = 3;
    if (g_runtime.freeform_uv_refine_rounds < 0) g_runtime.freeform_uv_refine_rounds = 0;
    if (g_runtime.freeform_uv_refine_rounds > 5) g_runtime.freeform_uv_refine_rounds = 5;
    if (g_runtime.freeform_uv_max_u < g_runtime.freeform_uv_init_u) g_runtime.freeform_uv_max_u = g_runtime.freeform_uv_init_u;
    if (g_runtime.freeform_uv_max_v < g_runtime.freeform_uv_init_v) g_runtime.freeform_uv_max_v = g_runtime.freeform_uv_init_v;
    if (g_runtime.freeform_uv_fail_ratio_max < 0.0) g_runtime.freeform_uv_fail_ratio_max = 0.0;
    if (g_runtime.freeform_uv_fail_ratio_max > 0.95) g_runtime.freeform_uv_fail_ratio_max = 0.95;
    if (g_runtime.freeform_uv_min_valid_points < 4) g_runtime.freeform_uv_min_valid_points = 4;

    std::vector<Step5GroupView> groups;
    BuildGroupViews(*step2, groups);

    const int pair_count = (int)step3->pairs.pairs.size();
    result.state.stats.input_pair_count = pair_count;
    if (pair_count <= 0)
    {
        result.ok = TRUE;
        EmitFinishEvent(diagnostics, result.state.stats);
        return TRUE;
    }

    std::vector<Step5PairLinkView> links;
    std::vector< std::vector<int> > pair_neighbors;
    BuildPairLinks(step4, links, pair_neighbors);

    std::vector< std::vector<int> > all_components;
    BuildConnectedComponents(pair_neighbors, all_components);
    std::vector<int> pair_to_component(pair_count, -1);
    int ci = 0;
    for (ci = 0; ci < (int)all_components.size(); ++ci)
    {
        const std::vector<int>& comp = all_components[ci];
        const int sz = (int)comp.size();
        if (sz <= 0)
            continue;
        if (options.keep_isolated_pairs == FALSE && sz == 1)
            continue;
        if (sz < (std::max)(1, options.min_component_pairs))
            continue;

        const int new_cid = (int)result.state.components.components.size();
        result.state.components.components.push_back(comp);
        int k = 0;
        for (k = 0; k < sz; ++k)
        {
            const int pi = comp[k];
            if (pi >= 0 && pi < pair_count)
                pair_to_component[pi] = new_cid;
        }
    }
    result.state.stats.component_count = (int)result.state.components.components.size();
    if (options.emit_patch_events != FALSE)
    {
        std::map<std::string, std::string> props;
        props["input_pair_count"] = IntText(pair_count);
        props["component_count"] = IntText(result.state.stats.component_count);
        props["raw_component_count"] = IntText((int)all_components.size());
        EmitStageSummary(diagnostics, "component", props);
    }

    std::vector< std::vector<int> > group_owner_pairs(groups.size(), std::vector<int>());
    int pi = 0;
    for (pi = 0; pi < pair_count; ++pi)
    {
        const PairRecord& pair = step3->pairs.pairs[pi];
        if (pair.group_a >= 0 && pair.group_a < (int)groups.size())
            group_owner_pairs[pair.group_a].push_back(pi);
        if (pair.group_b >= 0 && pair.group_b < (int)groups.size() && pair.group_b != pair.group_a)
            group_owner_pairs[pair.group_b].push_back(pi);
    }
    int gi = 0;
    for (gi = 0; gi < (int)group_owner_pairs.size(); ++gi)
    {
        std::vector<int>& owners = group_owner_pairs[gi];
        std::sort(owners.begin(), owners.end());
        owners.erase(std::unique(owners.begin(), owners.end()), owners.end());
    }

    std::vector<int> pair_to_patch(pair_count, -1);
    std::vector<int> merged_member(pair_count, 0);
    if (options.enable_one_to_n_merge_units != FALSE)
    {
        std::map<int, std::vector<int> > anchor_to_pairs;
        for (pi = 0; pi < pair_count; ++pi)
        {
            const PairRecord& pair = step3->pairs.pairs[pi];
            int best_gid = -1;
            int best_owner_count = -1;
            if (pair.group_a >= 0 && pair.group_a < (int)groups.size())
            {
                const int cnt = (int)group_owner_pairs[pair.group_a].size();
                if (cnt > 1 && (cnt > best_owner_count || (cnt == best_owner_count && (best_gid < 0 || pair.group_a < best_gid))))
                {
                    best_gid = pair.group_a;
                    best_owner_count = cnt;
                }
            }
            if (pair.group_b >= 0 && pair.group_b < (int)groups.size() && pair.group_b != pair.group_a)
            {
                const int cnt = (int)group_owner_pairs[pair.group_b].size();
                if (cnt > 1 && (cnt > best_owner_count || (cnt == best_owner_count && (best_gid < 0 || pair.group_b < best_gid))))
                {
                    best_gid = pair.group_b;
                    best_owner_count = cnt;
                }
            }
            if (best_gid >= 0)
                anchor_to_pairs[best_gid].push_back(pi);
        }

        std::map<int, std::vector<int> >::iterator it = anchor_to_pairs.begin();
        for (; it != anchor_to_pairs.end(); ++it)
        {
            std::vector<int>& members = it->second;
            std::sort(members.begin(), members.end());
            members.erase(std::unique(members.begin(), members.end()), members.end());
            if ((int)members.size() <= 1)
                continue;

            const int rep_pair = members[0];
            MidPatchRecord patch;
            if (BuildOnePatch(rep_pair, step3->pairs.pairs[rep_pair], groups, patch) == FALSE)
                continue;
            patch.merged_unit = TRUE;
            patch.anchor_group = it->first;
            patch.member_pairs = members;
            if (patch.anchor_group == patch.group_a)
            {
                patch.side_type_a = MID_PATCH_SIDE_SINGLE;
                patch.side_type_b = MID_PATCH_SIDE_MIXED;
            }
            else if (patch.anchor_group == patch.group_b)
            {
                patch.side_type_a = MID_PATCH_SIDE_MIXED;
                patch.side_type_b = MID_PATCH_SIDE_SINGLE;
            }

            int merged_cid = -1;
            int mk = 0;
            for (mk = 0; mk < (int)members.size(); ++mk)
            {
                const int m = members[mk];
                if (m >= 0 && m < pair_count && pair_to_component[m] >= 0)
                {
                    merged_cid = pair_to_component[m];
                    break;
                }
            }
            patch.component_id = merged_cid;
            patch.patch_id = (int)result.state.patches.patches.size();
            result.state.patches.patches.push_back(patch);
            ++result.state.stats.merged_unit_count;
            result.state.stats.merged_pair_count += (int)members.size();
            for (mk = 0; mk < (int)members.size(); ++mk)
            {
                const int m = members[mk];
                if (m < 0 || m >= pair_count)
                    continue;
                merged_member[m] = 1;
                pair_to_patch[m] = patch.patch_id;
            }
        }
    }

    for (pi = 0; pi < pair_count; ++pi)
    {
        if (merged_member[pi])
            continue;
        MidPatchRecord patch;
        if (BuildOnePatch(pi, step3->pairs.pairs[pi], groups, patch) == FALSE)
            continue;
        patch.side_type_a = MID_PATCH_SIDE_SINGLE;
        patch.side_type_b = MID_PATCH_SIDE_SINGLE;
        patch.anchor_group = -1;
        patch.merged_unit = FALSE;
        patch.member_pairs.clear();
        patch.member_pairs.push_back(pi);
        patch.component_id = pair_to_component[pi];
        patch.patch_id = (int)result.state.patches.patches.size();
        result.state.patches.patches.push_back(patch);
        pair_to_patch[pi] = patch.patch_id;
    }

    std::map<std::string, int> junction_index_by_key;
    int li = 0;
    for (li = 0; li < (int)links.size(); ++li)
    {
        const Step5PairLinkView& link = links[li];
        if (link.pair_i < 0 || link.pair_i >= pair_count || link.pair_j < 0 || link.pair_j >= pair_count)
            continue;
        if (pair_to_component[link.pair_i] < 0 || pair_to_component[link.pair_j] < 0)
            continue;
        const int ui0 = pair_to_patch[link.pair_i];
        const int uj0 = pair_to_patch[link.pair_j];
        if (ui0 < 0 || uj0 < 0 || ui0 == uj0)
            continue;

        int ui = ui0;
        int uj = uj0;
        int rep_i = result.state.patches.patches[ui0].source_pair_id;
        int rep_j = result.state.patches.patches[uj0].source_pair_id;
        if (ui > uj)
        {
            const int tmp_u = ui; ui = uj; uj = tmp_u;
            const int tmp_p = rep_i; rep_i = rep_j; rep_j = tmp_p;
        }

        char key_buf[128];
        std::sprintf(
            key_buf,
            "%d|%d|%d|%d|%d",
            ui,
            uj,
            (int)link.pair_mode,
            (int)link.conn_class,
            (int)link.link_type);
        const std::string key(key_buf);
        std::map<std::string, int>::iterator kj = junction_index_by_key.find(key);
        if (kj == junction_index_by_key.end())
        {
            MidPatchJunctionRecord junction;
            junction.junction_id = (int)result.state.junctions.junctions.size();
            junction.pair_i = rep_i;
            junction.pair_j = rep_j;
            junction.link_type = link.link_type;
            junction.conn_class = link.conn_class;
            junction.pair_mode = link.pair_mode;
            junction.total_hits = link.total_hits;
            result.state.junctions.junctions.push_back(junction);
            junction_index_by_key[key] = junction.junction_id;
        }
        else
        {
            result.state.junctions.junctions[kj->second].total_hits += link.total_hits;
        }
    }

    result.state.stats.requested_patch_count = (int)result.state.patches.patches.size();
    result.state.stats.junction_count = (int)result.state.junctions.junctions.size();
    if (options.emit_patch_events != FALSE)
    {
        std::map<std::string, std::string> patch_props;
        patch_props["requested_patch_count"] = IntText(result.state.stats.requested_patch_count);
        patch_props["merged_unit_count"] = IntText(result.state.stats.merged_unit_count);
        patch_props["merged_pair_count"] = IntText(result.state.stats.merged_pair_count);
        EmitStageSummary(diagnostics, "patch-unit", patch_props);

        std::map<std::string, std::string> junction_props;
        junction_props["input_link_count"] = IntText((int)links.size());
        junction_props["junction_count"] = IntText(result.state.stats.junction_count);
        EmitStageSummary(diagnostics, "junction", junction_props);
    }

    std::vector<FACE*> built_faces;
    int patch_i = 0;
    for (patch_i = 0; patch_i < (int)result.state.patches.patches.size(); ++patch_i)
    {
        MidPatchRecord& patch = result.state.patches.patches[patch_i];
        if (patch.valid == FALSE)
        {
            ++result.state.stats.failed_patch_count;
            if (options.emit_patch_events != FALSE)
                EmitPatchEvent(diagnostics, patch);
            continue;
        }

        FACE* face = nullptr;
        std::string reason;
        std::string method;
        const face_type ta = FaceTypeFromString(patch.type_a);
        const face_type tb = FaceTypeFromString(patch.type_b);
        logical ok = FALSE;
        if (IsFreeformTypeLocal(ta) != FALSE || IsFreeformTypeLocal(tb) != FALSE)
            ok = BuildFreeformMidFacePure(groups, patch, face, reason, method);
        else
            ok = BuildAnalyticMidFacePure(groups, patch, face, reason, method);

        if (ok != FALSE && face != nullptr)
        {
            patch.face = face;
            patch.build_method = method;
            ++result.state.stats.built_patch_count;
            built_faces.push_back(face);
        }
        else
        {
            patch.fail_reason = reason.empty() ? "BUILD_FAIL" : reason;
            ++result.state.stats.failed_patch_count;
        }
        if (options.emit_patch_events != FALSE)
            EmitPatchEvent(diagnostics, patch);
    }
    if (options.emit_patch_events != FALSE)
    {
        std::map<std::string, std::string> geometry_props;
        geometry_props["requested_patch_count"] = IntText(result.state.stats.requested_patch_count);
        geometry_props["built_patch_count"] = IntText(result.state.stats.built_patch_count);
        geometry_props["failed_patch_count"] = IntText(result.state.stats.failed_patch_count);
        geometry_props["built_face_count"] = IntText((int)built_faces.size());
        EmitStageSummary(diagnostics, "geometry", geometry_props);
    }

    if (diagnostics != nullptr && diagnostics->ShouldOutputDebugSat(kDebugStep5MidPatchFaces) != FALSE)
    {
        RunContext* context = diagnostics->context();
        if (context != nullptr)
        {
            std::string path = context->DebugSatPath(kDebugStep5MidPatchFaces, diagnostics->current_body_index());
            (void)diagnostics->EmitFaceSetSat(kDebugStep5MidPatchFaces, path.c_str(), built_faces);
        }
    }

    result.ok = TRUE;
    EmitFinishEvent(diagnostics, result.state.stats);
    return TRUE;
}
} // namespace midsurface_new
