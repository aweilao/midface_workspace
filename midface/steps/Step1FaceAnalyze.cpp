#include "steps/Step1FaceAnalyze.hpp"

#include "utils/ColorUtils.hpp"
#include "utils/JsonUtils.hpp"

#include "entity.hxx"
#include "cstrapi.hxx"
#include "faceq.hxx"
#include "faceqry.hxx"
#include "intrapi.hxx"
#include "kernapi.hxx"
#include "lists.hxx"
#include "queryapi.hxx"

#include <cstdio>
#include <algorithm>
#include <limits>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace midsurface_new
{
namespace
{
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

std::string PointText(const SPAposition& point)
{
    std::ostringstream ss;
    ss << point.x() << "," << point.y() << "," << point.z();
    return ss.str();
}

std::string VectorText(const SPAunit_vector& vector)
{
    std::ostringstream ss;
    ss << vector.x() << "," << vector.y() << "," << vector.z();
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

StructuredEvent Step1FaceEventTemplate()
{
    StructuredEvent event;
    event.AddTag("step1");
    event.AddTag("face-analyze");
    return event;
}

std::string FaceTypeText(face_type type)
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

void AppendInvalidReason(FaceRecord& record, const std::string& reason)
{
    if (reason.empty())
        return;

    if (!record.invalid_reason.empty())
        record.invalid_reason += ";";
    record.invalid_reason += reason;
}

void EmitStep1Event(
    DiagnosticSink* diagnostics,
    const char* event_name,
    DiagnosticLevel level,
    int face_id,
    const ColorRgb255* color,
    const char* note,
    const FaceRecord* record)
{
    if (diagnostics == nullptr)
        return;

    StructuredEvent event = Step1FaceEventTemplate().Clone();
    (void)level;
    if (event_name != nullptr)
        event.AddTag(event_name);
    if (event_name != nullptr && std::string(event_name) == "face_recorded")
    {
        event.AddTag("summary");
        event.AddTag("single");
    }

    if (face_id >= 0)
        event.SetProperty("face_id", IntText(face_id));
    if (color != nullptr)
        event.SetJsonProperty("color_rgb", JsonDump(ColorRgbJson(*color)));
    if (note != nullptr)
        event.SetProperty("note", note);
    if (record != nullptr)
    {
        event.SetProperty("face_type", record->face_type);
        event.SetProperty("valid", BoolText(record->valid));
        event.SetProperty("edge_count", IntText(record->edge_count));
        event.SetProperty("adjacency_degree", IntText(record->adjacency_degree));
        event.SetProperty("has_representative_point", BoolText(record->has_representative_point));
        event.SetProperty("has_representative_normal", BoolText(record->has_representative_normal));
        event.SetProperty("has_area_proxy", BoolText(record->has_area_proxy));
        event.SetProperty("has_edge_lengths", BoolText(record->has_edge_lengths));
        if (record->has_representative_point != FALSE)
            event.SetProperty("representative_point", PointText(record->representative_point));
        if (record->has_representative_normal != FALSE)
            event.SetProperty("representative_normal", VectorText(record->representative_normal));
        if (record->has_area_proxy != FALSE)
            event.SetProperty("area_proxy", DoubleText(record->area_proxy));
        if (record->has_edge_lengths != FALSE)
        {
            event.SetProperty("edge_min", DoubleText(record->edge_min));
            event.SetProperty("edge_max", DoubleText(record->edge_max));
        }
        if (record->invalid_reason.empty() == FALSE)
            event.SetProperty("invalid_reason", record->invalid_reason);
        if (record->sample_summary.empty() == FALSE)
            event.SetProperty("sample_summary", record->sample_summary);
    }

    (void)diagnostics->EmitEvent(event);
}

JsonValue ColorRgbJson(int r, int g, int b)
{
    JsonValue rgb = JsonValue::array();
    rgb.push_back(r);
    rgb.push_back(g);
    rgb.push_back(b);
    return rgb;
}

void EmitStep1ColorMapEvents(DiagnosticSink* diagnostics, const Step1FaceAnalyzeState& state)
{
    if (diagnostics == nullptr)
        return;

    std::vector<std::string> tags;
    tags.push_back("step1");
    tags.push_back("face-analyze");

    int i = 0;
    for (i = 0; i < (int)state.faces.records.size(); ++i)
    {
        const FaceRecord& record = state.faces.records[i];
        std::map<std::string, std::string> properties;
        std::map<std::string, std::string> json_properties;

        properties["face_id"] = IntText(record.face_id);
        properties["type"] = record.face_type;
        json_properties["rgb"] = JsonDump(ColorRgbJson(record.color_r, record.color_g, record.color_b));

        (void)diagnostics->EmitColorIdMapEntryIfEnabled(
            tags,
            "step1.colored_body",
            "face_id",
            properties,
            json_properties);
    }
}

logical CollectFaces(BODY* body, ENTITY_LIST& out_faces)
{
    if (body == nullptr)
        return FALSE;

    outcome result = api_get_faces((ENTITY*)body, out_faces);
    return result.ok() ? TRUE : FALSE;
}

FACE* EntityToFace(ENTITY* entity)
{
    if (entity == nullptr)
        return nullptr;
    if (!is_FACE(entity))
        return nullptr;
    return (FACE*)entity;
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

logical FillRepresentativePoint(FACE* face, FaceRecord& record)
{
    if (face == nullptr)
        return FALSE;

    SPAposition interior_point;
    if (find_interior_point(face, interior_point) != FALSE)
    {
        record.representative_point = interior_point;
        record.has_representative_point = TRUE;
        return TRUE;
    }

    SPAposition min_pt;
    SPAposition max_pt;
    outcome box_result = api_get_entity_box((ENTITY*)face, min_pt, max_pt);
    if (!box_result.ok())
        return FALSE;

    record.representative_point = SPAposition(
        0.5 * (min_pt.x() + max_pt.x()),
        0.5 * (min_pt.y() + max_pt.y()),
        0.5 * (min_pt.z() + max_pt.z()));
    record.has_representative_point = TRUE;
    return TRUE;
}

logical FillRepresentativeNormal(FACE* face, FaceRecord& record)
{
    if (face == nullptr)
        return FALSE;

    if (record.has_representative_point != FALSE)
    {
        SPAposition normal_position;
        SPAunit_vector normal;
        if (get_face_normal(face, record.representative_point, normal_position, normal) != FALSE)
        {
            record.representative_normal = normal;
            record.has_representative_normal = TRUE;
            return TRUE;
        }
    }

    SPAunit_vector planar_normal;
    if (get_face_normal((FACE const*)face, planar_normal) != FALSE)
    {
        record.representative_normal = planar_normal;
        record.has_representative_normal = TRUE;
        return TRUE;
    }

    return FALSE;
}

logical FillAreaProxy(FACE* face, FaceRecord& record)
{
    if (face == nullptr)
        return FALSE;

    double area = 0.0;
    double achieved_accuracy = 0.0;
    outcome area_result = api_ent_area((ENTITY*)face, 1.0e-6, area, achieved_accuracy);
    if (!area_result.ok())
        return FALSE;

    record.area_proxy = area;
    record.has_area_proxy = TRUE;
    return TRUE;
}

logical FillEdgeLengthRange(const std::vector<EDGE*>& face_edges, FaceRecord& record)
{
    if (face_edges.empty())
        return FALSE;

    double min_length = std::numeric_limits<double>::max();
    double max_length = 0.0;
    int measured_count = 0;

    int edge_index = 0;
    for (edge_index = 0; edge_index < (int)face_edges.size(); ++edge_index)
    {
        EDGE* edge = face_edges[edge_index];
        if (edge == nullptr)
            continue;

        const double length = edge->length(TRUE);
        if (length <= 1.0e-9)
            continue;

        if (length < min_length)
            min_length = length;
        if (length > max_length)
            max_length = length;
        ++measured_count;
    }

    if (measured_count == 0)
        return FALSE;

    record.edge_min = min_length;
    record.edge_max = max_length;
    record.has_edge_lengths = TRUE;
    return TRUE;
}

void CollectUniqueValidEdgeLengths(
    const std::vector<EDGE*>& face_edges,
    std::set<EDGE*>& seen_edges,
    std::vector<double>& edge_lengths)
{
    int edge_index = 0;
    for (edge_index = 0; edge_index < (int)face_edges.size(); ++edge_index)
    {
        EDGE* edge = face_edges[edge_index];
        if (edge == nullptr || !seen_edges.insert(edge).second)
            continue;

        const double length = edge->length(TRUE);
        if (length <= 1.0e-9)
            continue;
        edge_lengths.push_back(length);
    }
}

ModelScaleContext BuildModelScaleContext(const std::vector<double>& edge_lengths)
{
    ModelScaleContext scale;
    scale.valid_edge_count = (int)edge_lengths.size();
    if (edge_lengths.empty())
        return scale;

    std::vector<double> sorted = edge_lengths;
    std::sort(sorted.begin(), sorted.end(), std::greater<double>());

    scale.longest_edge_length = sorted.front();
    scale.top_edge_count_used = (std::min)(scale.top_edge_count_requested, (int)sorted.size());
    double sum = 0.0;
    int i = 0;
    for (i = 0; i < scale.top_edge_count_used; ++i)
        sum += sorted[i];

    if (scale.top_edge_count_used <= 0 || scale.normalized_reference <= 0.0)
        return scale;

    scale.reference_length = sum / (double)scale.top_edge_count_used;
    scale.distance_unit = scale.reference_length / scale.normalized_reference;
    scale.shortest_used_edge_length = sorted[scale.top_edge_count_used - 1];
    scale.valid = scale.reference_length > 0.0 && scale.distance_unit > 0.0 ? TRUE : FALSE;
    return scale;
}

void EmitModelScaleEvent(DiagnosticSink* diagnostics, const ModelScaleContext& scale)
{
    if (diagnostics == nullptr)
        return;

    StructuredEvent event = Step1FaceEventTemplate().Clone();
    event.AddTag("model-scale");
    event.AddTag("summary");
    event.AddTag("stage");
    event.SetProperty("valid", BoolText(scale.valid));
    event.SetProperty("reference_length", DoubleText(scale.reference_length));
    event.SetProperty("distance_unit", DoubleText(scale.distance_unit));
    event.SetProperty("normalized_reference", DoubleText(scale.normalized_reference));
    event.SetProperty("top_edge_count_requested", IntText(scale.top_edge_count_requested));
    event.SetProperty("top_edge_count_used", IntText(scale.top_edge_count_used));
    event.SetProperty("valid_edge_count", IntText(scale.valid_edge_count));
    event.SetProperty("min_valid_edge_length", DoubleText(scale.min_valid_edge_length));
    event.SetProperty("longest_edge_length", DoubleText(scale.longest_edge_length));
    event.SetProperty("shortest_used_edge_length", DoubleText(scale.shortest_used_edge_length));
    (void)diagnostics->EmitEvent(event);
}

void AddAdjacencyEdge(
    int face_a,
    int face_b,
    int shared_edge_count,
    Step1FaceAnalyzeState& state)
{
    if (face_a == face_b)
        return;
    if (face_a > face_b)
    {
        const int tmp = face_a;
        face_a = face_b;
        face_b = tmp;
    }

    FaceAdjacency adjacency;
    adjacency.face_a = face_a;
    adjacency.face_b = face_b;
    adjacency.shared_edge_count = shared_edge_count;
    adjacency.relation = "shared_edge";
    state.adjacency.edges.push_back(adjacency);

    if (face_a >= 0 && face_a < (int)state.faces.records.size())
        ++state.faces.records[face_a].adjacency_degree;
    if (face_b >= 0 && face_b < (int)state.faces.records.size())
        ++state.faces.records[face_b].adjacency_degree;
}

void BuildAdjacencyFromEdges(
    const std::map<EDGE*, std::vector<int> >& edge_faces,
    Step1FaceAnalyzeState& state)
{
    std::map<std::pair<int, int>, int> pair_counts;
    std::map<EDGE*, std::vector<int> >::const_iterator edge_it = edge_faces.begin();
    for (; edge_it != edge_faces.end(); ++edge_it)
    {
        const std::vector<int>& faces = edge_it->second;
        int i = 0;
        for (i = 0; i < (int)faces.size(); ++i)
        {
            int j = i + 1;
            for (; j < (int)faces.size(); ++j)
            {
                int face_a = faces[i];
                int face_b = faces[j];
                if (face_a == face_b)
                    continue;
                if (face_a > face_b)
                {
                    const int tmp = face_a;
                    face_a = face_b;
                    face_b = tmp;
                }
                ++pair_counts[std::make_pair(face_a, face_b)];
            }
        }
    }

    std::map<std::pair<int, int>, int>::const_iterator pair_it = pair_counts.begin();
    for (; pair_it != pair_counts.end(); ++pair_it)
        AddAdjacencyEdge(pair_it->first.first, pair_it->first.second, pair_it->second, state);

    state.stats.adjacency_count = (int)state.adjacency.edges.size();
}

std::string BuildSampleSummary(const FaceRecord& record, logical edges_ok)
{
    std::ostringstream ss;
    ss << "type=" << record.face_type
       << ";edge_count=" << record.edge_count
       << ";bbox_point=" << BoolText(record.has_representative_point)
       << ";normal=" << (record.has_representative_normal != FALSE ? VectorText(record.representative_normal) : "unavailable")
       << ";area=" << (record.has_area_proxy != FALSE ? DoubleText(record.area_proxy) : "unavailable")
       << ";edge_lengths=";
    if (record.has_edge_lengths != FALSE)
        ss << record.edge_min << "," << record.edge_max;
    else
        ss << "unavailable";
    if (edges_ok == FALSE)
        ss << ";edge_collect=failed";
    if (record.invalid_reason.empty() == FALSE)
        ss << ";invalid_reason=" << record.invalid_reason;
    return ss.str();
}
} // namespace

Step1FaceAnalyzeOptions::Step1FaceAnalyzeOptions()
    : build_adjacency(TRUE),
      build_sample_summary(TRUE),
      emit_face_events(TRUE)
{
}

FaceAnalyzeStats::FaceAnalyzeStats()
    : face_count(0),
      valid_face_count(0),
      invalid_face_count(0),
      adjacency_count(0)
{
}

Step1FaceAnalyzeState::Step1FaceAnalyzeState()
    : input_body(nullptr)
{
}

Step1FaceAnalyzeResult::Step1FaceAnalyzeResult()
    : ok(FALSE)
{
}

logical RunStep1FaceAnalyze(
    BODY* body,
    const Step1FaceAnalyzeOptions& options,
    DiagnosticSink* diagnostics,
    Step1FaceAnalyzeResult& result)
{
    result = Step1FaceAnalyzeResult();
    result.state.input_body = body;
    result.state.options_snapshot = options;

    if (body == nullptr)
    {
        EmitStep1Event(diagnostics, "invalid_input", DIAG_ERROR, -1, nullptr, "input body is null", nullptr);
        return FALSE;
    }

    EmitStep1Event(diagnostics, "start", DIAG_INFO, -1, nullptr, nullptr, nullptr);

    ENTITY_LIST face_entities;
    if (CollectFaces(body, face_entities) == FALSE)
    {
        EmitStep1Event(diagnostics, "collect_faces_failed", DIAG_ERROR, -1, nullptr, "api_get_faces failed", nullptr);
        return FALSE;
    }

    std::map<EDGE*, std::vector<int> > edge_faces;
    std::set<EDGE*> scale_seen_edges;
    std::vector<double> scale_edge_lengths;
    const int face_count = face_entities.count();

    face_entities.init();
    ENTITY* entity = nullptr;
    int face_id = 0;
    while ((entity = face_entities.next()) != nullptr)
    {
        FACE* face = EntityToFace(entity);
        if (face == nullptr)
            continue;

        const ColorRgb255 color = DistinctColorByIndex(face_id, face_count);
        (void)ApplyFaceColor(face, color);

        FaceRecord record;
        record.face_id = face_id;
        record.face = face;
        record.surface_geometry = face->geometry();
        record.face_type = record.surface_geometry != nullptr ? FaceTypeText(get_face_type((FACE const*)face)) : "missing_surface";
        record.color_r = color.r;
        record.color_g = color.g;
        record.color_b = color.b;
        record.valid = record.surface_geometry != nullptr ? TRUE : FALSE;
        record.invalid_reason = record.valid != FALSE ? "" : "missing_surface";

        if (FillRepresentativePoint(face, record) == FALSE)
            AppendInvalidReason(record, "representative_point_unavailable");
        if (FillRepresentativeNormal(face, record) == FALSE)
            AppendInvalidReason(record, "representative_normal_unavailable");
        if (FillAreaProxy(face, record) == FALSE)
            AppendInvalidReason(record, "area_unavailable");

        std::vector<EDGE*> face_edges;
        const logical edges_ok = CollectFaceEdges(face, face_edges);
        record.edge_count = (int)face_edges.size();
        int edge_index = 0;
        for (edge_index = 0; edge_index < (int)face_edges.size(); ++edge_index)
            edge_faces[face_edges[edge_index]].push_back(face_id);

        if (edges_ok == FALSE)
            AppendInvalidReason(record, "edge_collect_failed");
        if (FillEdgeLengthRange(face_edges, record) == FALSE && !face_edges.empty())
            AppendInvalidReason(record, "edge_lengths_unavailable");
        CollectUniqueValidEdgeLengths(face_edges, scale_seen_edges, scale_edge_lengths);
        record.sample_summary = BuildSampleSummary(record, edges_ok);

        result.state.faces.records.push_back(record);
        ++result.state.stats.face_count;
        if (record.valid != FALSE)
            ++result.state.stats.valid_face_count;
        else
            ++result.state.stats.invalid_face_count;

        if (options.emit_face_events != FALSE)
            EmitStep1Event(diagnostics, "face_recorded", DIAG_DEBUG, face_id, &color, nullptr, &record);

        ++face_id;
    }

    if (options.build_adjacency != FALSE)
    {
        BuildAdjacencyFromEdges(edge_faces, result.state);
        StructuredEvent adjacency_event = Step1FaceEventTemplate().Clone();
        adjacency_event.AddTag("adjacency");
        adjacency_event.AddTag("adjacency_built");
        adjacency_event.AddTag("summary");
        adjacency_event.AddTag("stage");
        adjacency_event.SetProperty("adjacency_count", IntText(result.state.stats.adjacency_count));
        if (diagnostics != nullptr)
            (void)diagnostics->EmitEvent(adjacency_event);
    }
    else
    {
        result.state.stats.adjacency_count = 0;
    }
    result.state.model_scale = BuildModelScaleContext(scale_edge_lengths);
    EmitModelScaleEvent(diagnostics, result.state.model_scale);

    result.ok = result.state.stats.face_count > 0 ? TRUE : FALSE;

    StructuredEvent finish_event = Step1FaceEventTemplate().Clone();
    finish_event.AddTag(result.ok != FALSE ? "finish" : "finish_without_faces");
    finish_event.AddTag("summary");
    finish_event.AddTag("all");
    finish_event.SetProperty("face_count", IntText(result.state.stats.face_count));
    finish_event.SetProperty("valid_face_count", IntText(result.state.stats.valid_face_count));
    finish_event.SetProperty("invalid_face_count", IntText(result.state.stats.invalid_face_count));
    finish_event.SetProperty("adjacency_count", IntText(result.state.stats.adjacency_count));
    if (diagnostics != nullptr)
    {
        (void)diagnostics->EmitEvent(finish_event);
        EmitStep1ColorMapEvents(diagnostics, result.state);
        (void)diagnostics->EmitBodySatIfEnabled("step1.colored_body", body);
    }

    return result.ok;
}
} // namespace midsurface_new
