#include "utils/Step1Checkpoint.hpp"

#include "utils/AcisSatWriter.hpp"
#include "utils/JsonUtils.hpp"

#include "entity.hxx"
#include "faceqry.hxx"
#include "kernapi.hxx"
#include "lists.hxx"

#include <cstdio>
#include <fstream>
#include <sstream>

namespace midsurface_new
{
namespace
{
std::string BodyFileName(int body_index, const char* suffix)
{
    char buf[128];
    sprintf(buf, "body_%d_%s", body_index, suffix == nullptr ? "" : suffix);
    return std::string(buf);
}

std::string IntText(int value)
{
    char buf[64];
    sprintf(buf, "%d", value);
    return std::string(buf);
}

std::string Step1ResultFileName(int body_index)
{
    return BodyFileName(body_index, "step1_result.json");
}

std::string Step1SatFileName(int body_index)
{
    return BodyFileName(body_index, "colored.sat");
}

JsonValue PointJson(const SPAposition& p)
{
    return JsonPoint(p);
}

JsonValue VectorJson(const SPAunit_vector& v)
{
    JsonValue out = JsonValue::array();
    out.push_back(v.x());
    out.push_back(v.y());
    out.push_back(v.z());
    return out;
}

SPAposition PointFromJson(const JsonValue& value)
{
    if (!value.is_array() || value.size() != 3)
        return SPAposition();
    return SPAposition(value[0].get<double>(), value[1].get<double>(), value[2].get<double>());
}

SPAunit_vector UnitVectorFromJson(const JsonValue& value)
{
    if (!value.is_array() || value.size() != 3)
        return SPAunit_vector(1.0, 0.0, 0.0);
    return SPAunit_vector(value[0].get<double>(), value[1].get<double>(), value[2].get<double>());
}

JsonValue FaceRecordJson(const FaceRecord& record)
{
    JsonValue j = JsonValue::object();
    j["face_id"] = record.face_id;
    j["face_type"] = record.face_type;
    j["color"] = JsonValue::array({record.color_r, record.color_g, record.color_b});
    j["edge_count"] = record.edge_count;
    j["adjacency_degree"] = record.adjacency_degree;
    j["has_representative_point"] = record.has_representative_point != FALSE;
    j["has_representative_normal"] = record.has_representative_normal != FALSE;
    j["has_area_proxy"] = record.has_area_proxy != FALSE;
    j["has_edge_lengths"] = record.has_edge_lengths != FALSE;
    j["representative_point"] = PointJson(record.representative_point);
    j["representative_normal"] = VectorJson(record.representative_normal);
    j["area_proxy"] = record.area_proxy;
    j["edge_min"] = record.edge_min;
    j["edge_max"] = record.edge_max;
    j["valid"] = record.valid != FALSE;
    j["invalid_reason"] = record.invalid_reason;
    j["sample_summary"] = record.sample_summary;
    return j;
}

JsonValue ModelScaleContextJson(const ModelScaleContext& scale)
{
    JsonValue j = JsonValue::object();
    j["valid"] = scale.valid != FALSE;
    j["reference_length"] = scale.reference_length;
    j["distance_unit"] = scale.distance_unit;
    j["normalized_reference"] = scale.normalized_reference;
    j["top_edge_count_requested"] = scale.top_edge_count_requested;
    j["top_edge_count_used"] = scale.top_edge_count_used;
    j["valid_edge_count"] = scale.valid_edge_count;
    j["min_valid_edge_length"] = scale.min_valid_edge_length;
    j["longest_edge_length"] = scale.longest_edge_length;
    j["shortest_used_edge_length"] = scale.shortest_used_edge_length;
    return j;
}

void LoadModelScaleContextJson(const JsonValue& j, ModelScaleContext& scale)
{
    if (!j.is_object())
        return;

    scale.valid = j.value("valid", false) ? TRUE : FALSE;
    scale.reference_length = j.value("reference_length", 0.0);
    scale.distance_unit = j.value("distance_unit", 0.0);
    scale.normalized_reference = j.value("normalized_reference", 1000.0);
    scale.top_edge_count_requested = j.value("top_edge_count_requested", 10);
    scale.top_edge_count_used = j.value("top_edge_count_used", 0);
    scale.valid_edge_count = j.value("valid_edge_count", 0);
    scale.min_valid_edge_length = j.value("min_valid_edge_length", 1.0e-9);
    scale.longest_edge_length = j.value("longest_edge_length", 0.0);
    scale.shortest_used_edge_length = j.value("shortest_used_edge_length", 0.0);
}

void LoadFaceRecordJson(const JsonValue& j, FaceRecord& record)
{
    record.face_id = j.value("face_id", -1);
    record.face_type = j.value("face_type", std::string());
    if (j.contains("color") && j["color"].is_array() && j["color"].size() == 3)
    {
        record.color_r = j["color"][0].get<int>();
        record.color_g = j["color"][1].get<int>();
        record.color_b = j["color"][2].get<int>();
    }
    record.edge_count = j.value("edge_count", 0);
    record.adjacency_degree = j.value("adjacency_degree", 0);
    record.has_representative_point = j.value("has_representative_point", false) ? TRUE : FALSE;
    record.has_representative_normal = j.value("has_representative_normal", false) ? TRUE : FALSE;
    record.has_area_proxy = j.value("has_area_proxy", false) ? TRUE : FALSE;
    record.has_edge_lengths = j.value("has_edge_lengths", false) ? TRUE : FALSE;
    if (j.contains("representative_point"))
        record.representative_point = PointFromJson(j["representative_point"]);
    if (j.contains("representative_normal"))
        record.representative_normal = UnitVectorFromJson(j["representative_normal"]);
    record.area_proxy = j.value("area_proxy", 0.0);
    record.edge_min = j.value("edge_min", 0.0);
    record.edge_max = j.value("edge_max", 0.0);
    record.valid = j.value("valid", false) ? TRUE : FALSE;
    record.invalid_reason = j.value("invalid_reason", std::string());
    record.sample_summary = j.value("sample_summary", std::string());
}

JsonValue Step1ResultJson(int body_index, const Step1FaceAnalyzeResult& result)
{
    JsonValue j = JsonValue::object();
    j["schema"] = "midface_new.step1_checkpoint.v1";
    j["body_index"] = body_index;
    j["ok"] = result.ok != FALSE;
    j["stats"] = {
        {"face_count", result.state.stats.face_count},
        {"valid_face_count", result.state.stats.valid_face_count},
        {"invalid_face_count", result.state.stats.invalid_face_count},
        {"adjacency_count", result.state.stats.adjacency_count}
    };

    JsonValue faces = JsonValue::array();
    int i = 0;
    for (i = 0; i < (int)result.state.faces.records.size(); ++i)
        faces.push_back(FaceRecordJson(result.state.faces.records[i]));
    j["faces"] = faces;

    JsonValue adjacency = JsonValue::array();
    for (i = 0; i < (int)result.state.adjacency.edges.size(); ++i)
    {
        const FaceAdjacency& edge = result.state.adjacency.edges[i];
        JsonValue a = JsonValue::object();
        a["face_a"] = edge.face_a;
        a["face_b"] = edge.face_b;
        a["shared_edge_count"] = edge.shared_edge_count;
        a["relation"] = edge.relation;
        adjacency.push_back(a);
    }
    j["adjacency"] = adjacency;
    j["model_scale"] = ModelScaleContextJson(result.state.model_scale);
    return j;
}

logical RestoreSatFile(const char* file_path, ENTITY_LIST& out_entities)
{
    if (file_path == nullptr)
        return FALSE;

    FILE* fp = fopen(file_path, "r");
    if (fp == nullptr)
        return FALSE;

    outcome rc = api_restore_entity_list(fp, TRUE, out_entities);
    fclose(fp);
    return rc.ok() ? TRUE : FALSE;
}

void CollectBodies(ENTITY_LIST& entities, std::vector<BODY*>& out_bodies)
{
    out_bodies.clear();
    int i = 0;
    for (i = 0; i < entities.count(); ++i)
    {
        ENTITY* ent = entities[i];
        if (ent != nullptr && is_BODY(ent))
            out_bodies.push_back((BODY*)ent);
    }
}

logical CollectFaces(BODY* body, std::vector<FACE*>& out_faces)
{
    out_faces.clear();
    ENTITY_LIST face_entities;
    outcome result = api_get_faces((ENTITY*)body, face_entities);
    if (!result.ok())
        return FALSE;

    face_entities.init();
    ENTITY* entity = nullptr;
    while ((entity = face_entities.next()) != nullptr)
    {
        if (entity != nullptr && is_FACE(entity))
            out_faces.push_back((FACE*)entity);
    }
    return TRUE;
}

void EmitCheckpointEvent(
    DiagnosticSink* diagnostics,
    const char* phase,
    const char* result_text,
    int body_index,
    const std::string& path)
{
    if (diagnostics == nullptr)
        return;

    StructuredEvent event;
    event.AddTag("checkpoint");
    event.AddTag("step1");
    event.AddTag(phase == nullptr ? "checkpoint" : phase);
    if (result_text != nullptr)
        event.AddTag(result_text);
    event.SetProperty("body_index", IntText(body_index));
    event.SetProperty("path", path);
    (void)diagnostics->EmitEvent(event);
}
} // namespace

logical SaveStep1Checkpoint(
    const CheckpointStore& store,
    int body_index,
    const Step1FaceAnalyzeResult& result,
    DiagnosticSink* diagnostics)
{
    if (result.state.input_body == nullptr)
        return FALSE;
    if (store.EnsureStepDir(PIPELINE_STEP1_FACE_ANALYZE) == FALSE)
        return FALSE;

    const std::string result_file = Step1ResultFileName(body_index);
    const std::string sat_file = Step1SatFileName(body_index);
    const std::string result_path = store.StepFile(PIPELINE_STEP1_FACE_ANALYZE, result_file.c_str());
    const std::string sat_path = store.StepFile(PIPELINE_STEP1_FACE_ANALYZE, sat_file.c_str());

    ENTITY_LIST entities;
    entities.add((ENTITY*)result.state.input_body);
    AcisSatWriter writer;
    if (writer.Save(sat_path.c_str(), entities) == FALSE)
    {
        EmitCheckpointEvent(diagnostics, "save_step1", "fail", body_index, sat_path);
        return FALSE;
    }

    const std::string json = JsonDump(Step1ResultJson(body_index, result));
    if (store.WriteTextFile(PIPELINE_STEP1_FACE_ANALYZE, result_file.c_str(), json) == FALSE)
    {
        EmitCheckpointEvent(diagnostics, "save_step1", "fail", body_index, result_path);
        return FALSE;
    }

    EmitCheckpointEvent(diagnostics, "save_step1", "ok", body_index, result_path);
    return TRUE;
}

logical RestoreStep1Checkpoint(
    const CheckpointStore& store,
    int body_index,
    BODY*& out_body,
    Step1FaceAnalyzeResult& out_result,
    DiagnosticSink* diagnostics)
{
    out_body = nullptr;
    out_result = Step1FaceAnalyzeResult();

    std::string json_text;
    const std::string result_file = Step1ResultFileName(body_index);
    const std::string sat_file = Step1SatFileName(body_index);
    const std::string result_path = store.StepFile(PIPELINE_STEP1_FACE_ANALYZE, result_file.c_str());
    const std::string sat_path = store.StepFile(PIPELINE_STEP1_FACE_ANALYZE, sat_file.c_str());

    if (store.ReadTextFile(PIPELINE_STEP1_FACE_ANALYZE, result_file.c_str(), json_text) == FALSE)
    {
        EmitCheckpointEvent(diagnostics, "restore_step1", "fail", body_index, result_path);
        return FALSE;
    }

    ENTITY_LIST entities;
    if (RestoreSatFile(sat_path.c_str(), entities) == FALSE)
    {
        EmitCheckpointEvent(diagnostics, "restore_step1", "fail", body_index, sat_path);
        return FALSE;
    }

    std::vector<BODY*> bodies;
    CollectBodies(entities, bodies);
    if (bodies.empty())
    {
        EmitCheckpointEvent(diagnostics, "restore_step1", "fail", body_index, sat_path);
        return FALSE;
    }
    out_body = bodies[0];

    JsonValue root = JsonValue::parse(json_text, nullptr, false);
    if (root.is_discarded())
    {
        EmitCheckpointEvent(diagnostics, "restore_step1", "fail", body_index, result_path);
        return FALSE;
    }

    std::vector<FACE*> faces;
    if (CollectFaces(out_body, faces) == FALSE)
    {
        EmitCheckpointEvent(diagnostics, "restore_step1", "fail", body_index, sat_path);
        return FALSE;
    }

    out_result.ok = root.value("ok", false) ? TRUE : FALSE;
    out_result.state.input_body = out_body;
    if (root.contains("stats"))
    {
        const JsonValue& stats = root["stats"];
        out_result.state.stats.face_count = stats.value("face_count", 0);
        out_result.state.stats.valid_face_count = stats.value("valid_face_count", 0);
        out_result.state.stats.invalid_face_count = stats.value("invalid_face_count", 0);
        out_result.state.stats.adjacency_count = stats.value("adjacency_count", 0);
    }

    if (root.contains("faces") && root["faces"].is_array())
    {
        int i = 0;
        for (i = 0; i < (int)root["faces"].size(); ++i)
        {
            FaceRecord record;
            LoadFaceRecordJson(root["faces"][i], record);
            if (record.face_id >= 0 && record.face_id < (int)faces.size())
            {
                record.face = faces[record.face_id];
                record.surface_geometry = record.face != nullptr ? record.face->geometry() : nullptr;
            }
            out_result.state.faces.records.push_back(record);
        }
    }

    if (root.contains("model_scale"))
        LoadModelScaleContextJson(root["model_scale"], out_result.state.model_scale);

    if (root.contains("adjacency" ) && root["adjacency"].is_array())
    {
        int i = 0;
        for (i = 0; i < (int)root["adjacency"].size(); ++i)
        {
            const JsonValue& a = root["adjacency"][i];
            FaceAdjacency edge;
            edge.face_a = a.value("face_a", -1);
            edge.face_b = a.value("face_b", -1);
            edge.shared_edge_count = a.value("shared_edge_count", 0);
            edge.relation = a.value("relation", std::string());
            out_result.state.adjacency.edges.push_back(edge);
        }
    }

    EmitCheckpointEvent(diagnostics, "restore_step1", "ok", body_index, result_path);
    return out_result.ok;
}
} // namespace midsurface_new
