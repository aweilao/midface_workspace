#include "core/RunContext.hpp"

#include "utils/PathUtils.hpp"

#include <sstream>
#include <time.h>

namespace midsurface_new
{
namespace
{
std::string NormalizeEventTag(const char* phase_tag)
{
    if (phase_tag == nullptr)
        return std::string();
    const std::string tag(phase_tag);
    const std::string prefix("event:");
    if (tag.compare(0, prefix.size(), prefix) == 0)
        return tag.substr(prefix.size());
    return tag;
}
} // namespace

RunContextOptions::RunContextOptions()
    : workspace_root("."),
      start_method("body_pipeline"),
      output_root("test/out/midface_new"),
      output_sat_dir(),
      output_log_dir(),
      checkpoint_root("test/out/midface_new/checkpoints"),
      stop_after_step(PIPELINE_STEP7_FINAL_EXPORT),
      resume_from_checkpoint(),
      enable_diagnostics(TRUE),
      append_diagnostics(FALSE),
      enable_checkpoints(FALSE)
{
}

RunContext::RunContext()
{
}

RunContext::~RunContext()
{
    std::map<int, StructuredEventWriter*>::iterator it = body_event_writers_.begin();
    for (; it != body_event_writers_.end(); ++it)
        delete it->second;
    body_event_writers_.clear();
}

logical RunContext::Configure(const RunContextOptions& options)
{
    std::map<int, StructuredEventWriter*>::iterator bit = body_event_writers_.begin();
    for (; bit != body_event_writers_.end(); ++bit)
        delete bit->second;
    body_event_writers_.clear();

    options_ = options;
    run_id_ = options_.run_id.empty() ? MakeRunId() : options_.run_id;
    output_root_ = NormalizePath(options_.output_root.empty() ? "test/out/midface_new" : options_.output_root);
    output_sat_dir_ = NormalizePath(options_.output_sat_dir.empty() ? JoinPath(output_root_, "sat") : options_.output_sat_dir);
    output_log_dir_ = NormalizePath(options_.output_log_dir.empty() ? JoinPath(output_root_, "logs") : options_.output_log_dir);
    debug_sat_output_set_.clear();
    int i = 0;
    for (i = 0; i < (int)options_.debug_sat_outputs.size(); ++i)
        debug_sat_output_set_.insert(options_.debug_sat_outputs[i]);

    trace_filter_.Configure(options_.trace);
    checkpoint_store_.Configure(options_.checkpoint_root.c_str());

    if (EnsureDirectoryRecursive(output_root_) == FALSE)
        return FALSE;
    if (EnsureDirectoryRecursive(output_sat_dir_) == FALSE)
        return FALSE;

    if (options_.enable_checkpoints != FALSE)
    {
        if (EnsureDirectoryRecursive(options_.checkpoint_root) == FALSE)
            return FALSE;
    }

    if (options_.enable_diagnostics != FALSE)
    {
        if (EnsureDirectoryRecursive(output_log_dir_) == FALSE)
            return FALSE;
        const std::string event_path = JoinPath(output_log_dir_, "events.jsonl");
        if (event_writer_.Open(event_path.c_str(), options_.append_diagnostics) == FALSE)
            return FALSE;
    }

    return TRUE;
}

const RunContextOptions& RunContext::options() const
{
    return options_;
}

const std::string& RunContext::run_id() const
{
    return run_id_;
}

const std::string& RunContext::output_root() const
{
    return output_root_;
}

const std::string& RunContext::output_sat_dir() const
{
    return output_sat_dir_;
}

const std::string& RunContext::output_log_dir() const
{
    return output_log_dir_;
}

logical RunContext::ShouldOutputDebugSat(const char* artifact_role) const
{
    if (artifact_role == nullptr)
        return FALSE;
    return debug_sat_output_set_.find(artifact_role) != debug_sat_output_set_.end() ? TRUE : FALSE;
}

std::string RunContext::DebugSatPath(const char* artifact_role, int body_index) const
{
    std::ostringstream ss;
    ss << SanitizeArtifactRole(artifact_role);
    if (body_index >= 0)
        ss << "_body_" << body_index;
    ss << ".sat";
    return JoinPath(output_sat_dir_, ss.str());
}

TraceFilter& RunContext::trace_filter()
{
    return trace_filter_;
}

const TraceFilter& RunContext::trace_filter() const
{
    return trace_filter_;
}

CheckpointStore& RunContext::checkpoints()
{
    return checkpoint_store_;
}

const CheckpointStore& RunContext::checkpoints() const
{
    return checkpoint_store_;
}

logical RunContext::Emit(const StructuredEvent& src_event)
{
    if (options_.enable_diagnostics == FALSE)
        return TRUE;

    return event_writer_.Write(src_event);
}

logical RunContext::EmitForBody(int body_index, const StructuredEvent& event)
{
    if (options_.enable_diagnostics == FALSE)
        return TRUE;
    if (body_index < 0)
        return Emit(event);

    StructuredEventWriter* writer = BodyEventWriter(body_index);
    if (writer == nullptr)
        return FALSE;
    return writer->Write(event);
}

logical RunContext::EmitSimple(
    const StructuredEvent& base_event,
    const char* phase_tag,
    DiagnosticLevel level,
    const char* message)
{
    StructuredEvent event = base_event.Clone();
    (void)level;
    const std::string tag = NormalizeEventTag(phase_tag);
    if (!tag.empty())
        event.AddTag(tag);
    if (message != nullptr)
        event.SetProperty("message", message);
    return Emit(event);
}

logical RunContext::EmitSimpleForBody(
    int body_index,
    const StructuredEvent& base_event,
    const char* phase_tag,
    DiagnosticLevel level,
    const char* message)
{
    StructuredEvent event = base_event.Clone();
    (void)level;
    const std::string tag = NormalizeEventTag(phase_tag);
    if (!tag.empty())
        event.AddTag(tag);
    if (message != nullptr)
        event.SetProperty("message", message);
    return EmitForBody(body_index, event);
}

std::string RunContext::MakeRunId() const
{
    std::ostringstream ss;
    ss << "midface-new-" << (long)time(nullptr);
    return ss.str();
}

std::string RunContext::NowText() const
{
    const time_t now = time(nullptr);
    struct tm* tmv = localtime(&now);
    char buf[64];
    if (tmv == nullptr)
        return std::string();

    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", tmv);
    return std::string(buf);
}

std::string RunContext::SanitizeArtifactRole(const char* artifact_role) const
{
    std::string out = artifact_role == nullptr ? "debug" : artifact_role;
    int i = 0;
    for (i = 0; i < (int)out.size(); ++i)
    {
        const char ch = out[i];
        const logical keep =
            ((ch >= 'a' && ch <= 'z') ||
             (ch >= 'A' && ch <= 'Z') ||
             (ch >= '0' && ch <= '9') ||
             ch == '-' ||
             ch == '_') ? TRUE : FALSE;
        if (keep == FALSE)
            out[i] = '_';
    }
    return out.empty() ? "debug" : out;
}

std::string RunContext::BodyEventPath(int body_index) const
{
    std::ostringstream ss;
    ss << "body_" << body_index << "_events.jsonl";
    return JoinPath(output_log_dir_, ss.str());
}

StructuredEventWriter* RunContext::BodyEventWriter(int body_index)
{
    std::map<int, StructuredEventWriter*>::iterator it = body_event_writers_.find(body_index);
    if (it != body_event_writers_.end())
        return it->second;

    StructuredEventWriter* writer = new StructuredEventWriter();
    if (writer == nullptr)
        return nullptr;
    if (writer->Open(BodyEventPath(body_index).c_str(), options_.append_diagnostics) == FALSE)
    {
        delete writer;
        return nullptr;
    }
    body_event_writers_[body_index] = writer;
    return writer;
}
} // namespace midsurface_new
