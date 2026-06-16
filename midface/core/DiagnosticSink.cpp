#include "core/DiagnosticSink.hpp"

#include "utils/AcisSatWriter.hpp"
#include "utils/DebugSatBuilder.hpp"

namespace midsurface_new
{
namespace
{
std::string StepTag(int step)
{
    char buf[64];
    sprintf(buf, "step%d", step);
    return std::string(buf);
}

std::string NormalizeRole(const char* artifact_role)
{
    if (artifact_role == nullptr)
        return std::string("entity_list");

    std::string role(artifact_role);
    const std::string step1("step1.");
    const std::string step2("step2.");
    const std::string step3("step3.");
    if (role.compare(0, step1.size(), step1) == 0)
        return role.substr(step1.size());
    if (role.compare(0, step2.size(), step2) == 0)
        return role.substr(step2.size());
    if (role.compare(0, step3.size(), step3) == 0)
        return role.substr(step3.size());
    return role;
}
} // namespace

DiagnosticSink::DiagnosticSink()
    : context_(nullptr),
      current_body_index_(0)
{
}

void DiagnosticSink::Configure(RunContext* context)
{
    context_ = context;
}

void DiagnosticSink::SetCurrentBodyIndex(int body_index)
{
    current_body_index_ = body_index;
}

RunContext* DiagnosticSink::context()
{
    return context_;
}

const RunContext* DiagnosticSink::context() const
{
    return context_;
}

int DiagnosticSink::current_body_index() const
{
    return current_body_index_;
}

logical DiagnosticSink::EmitEvent(const StructuredEvent& event)
{
    if (context_ == nullptr)
        return TRUE;
    return context_->EmitForBody(current_body_index_, event);
}

logical DiagnosticSink::EmitPointSet(
    int step,
    const char* schema,
    const char* event_name,
    const std::vector<PointSample>& points)
{
    StructuredEvent event;
    event.AddTag("points");
    event.AddTag(StepTag(step));
    event.AddTag(event_name == nullptr ? "point-set" : event_name);
    event.SetProperty("count", std::to_string((int)points.size()));
    (void)schema;
    return EmitEvent(event);
}

logical DiagnosticSink::EmitCrossMarkerSet(
    int step,
    const char* schema,
    const char* event_name,
    const std::vector<PointSample>& points)
{
    StructuredEvent event;
    event.AddTag("cross-markers");
    event.AddTag(StepTag(step));
    event.AddTag(event_name == nullptr ? "cross-marker-set" : event_name);
    event.SetProperty("count", std::to_string((int)points.size()));
    (void)schema;
    return EmitEvent(event);
}

logical DiagnosticSink::EmitBodySatIfEnabled(const char* artifact_role, BODY* body)
{
    if (context_ == nullptr || body == nullptr)
        return TRUE;
    if (context_->ShouldOutputDebugSat(artifact_role) == FALSE)
        return TRUE;

    ENTITY_LIST entities;
    entities.add((ENTITY*)body);
    const std::string file_path = context_->DebugSatPath(artifact_role, current_body_index_);
    return EmitEntityListSat(artifact_role, file_path.c_str(), entities);
}

logical DiagnosticSink::EmitEntityListSat(const char* artifact_role, const char* file_path, ENTITY_LIST& entities)
{
    AcisSatWriter writer;
    const logical ok = writer.Save(file_path, entities);

    StructuredEvent event;
    event.AddTag("output");
    event.AddTag("sat");
    event.AddTag(ok != FALSE ? "ok" : "fail");
    event.SetProperty("role", NormalizeRole(artifact_role));
    event.SetProperty("path", file_path == nullptr ? "" : file_path);
    (void)EmitEvent(event);
    return ok;
}

logical DiagnosticSink::EmitFaceSetSat(const char* artifact_role, const char* file_path, const std::vector<FACE*>& faces)
{
    DebugSatBuilder builder;
    int i = 0;
    for (i = 0; i < (int)faces.size(); ++i)
        builder.Add(faces[i]);
    return EmitEntityListSat(artifact_role, file_path, builder.entities());
}

logical DiagnosticSink::EmitColorIdMapEntryIfEnabled(
    const std::vector<std::string>& tags,
    const char* artifact_role,
    const char* id_type,
    const std::map<std::string, std::string>& properties,
    const std::map<std::string, std::string>& json_properties)
{
    if (context_ == nullptr || artifact_role == nullptr)
        return TRUE;
    if (context_->ShouldOutputDebugSat(artifact_role) == FALSE)
        return TRUE;

    StructuredEvent event;
    int i = 0;
    for (i = 0; i < (int)tags.size(); ++i)
        event.AddTag(tags[i]);
    event.AddTag("color-map");

    (void)id_type;
    std::map<std::string, std::string>::const_iterator prop_it = properties.begin();
    for (; prop_it != properties.end(); ++prop_it)
        event.SetProperty(prop_it->first, prop_it->second);

    std::map<std::string, std::string>::const_iterator json_it = json_properties.begin();
    for (; json_it != json_properties.end(); ++json_it)
        event.SetJsonProperty(json_it->first, json_it->second);
    return EmitEvent(event);
}

logical DiagnosticSink::ShouldOutputDebugSat(const char* artifact_role) const
{
    if (context_ == nullptr)
        return FALSE;
    return context_->ShouldOutputDebugSat(artifact_role);
}

logical DiagnosticSink::ShouldTraceFacePair(int face_a, int face_b) const
{
    if (context_ == nullptr)
        return FALSE;
    return context_->trace_filter().ShouldTraceFacePair(face_a, face_b);
}
} // namespace midsurface_new
