#pragma once

#include "core/MidSurfaceNewTypes.hpp"
#include "utils/CheckpointStore.hpp"
#include "utils/StructuredEventWriter.hpp"
#include "utils/TraceFilter.hpp"

#include "logical.h"

#include <set>
#include <string>
#include <vector>
#include <map>

namespace midsurface_new
{
struct RunContextOptions
{
    RunContextOptions();

    std::string run_id;
    std::string workspace_root;
    std::string start_method;
    std::string input_sat_path;
    std::string output_root;
    std::string output_sat_dir;
    std::string output_log_dir;
    std::string checkpoint_root;
    int stop_after_step;
    std::vector<std::string> debug_sat_outputs;
    std::vector<std::string> checkpoint_outputs;
    std::string resume_from_checkpoint;
    logical enable_diagnostics;
    logical append_diagnostics;
    logical enable_checkpoints;
    TraceFilterOptions trace;
};

class RunContext
{
public:
    RunContext();
    ~RunContext();

    logical Configure(const RunContextOptions& options);
    const RunContextOptions& options() const;
    const std::string& run_id() const;
    const std::string& output_root() const;
    const std::string& output_sat_dir() const;
    const std::string& output_log_dir() const;
    logical ShouldOutputDebugSat(const char* artifact_role) const;
    std::string DebugSatPath(const char* artifact_role, int body_index) const;
    TraceFilter& trace_filter();
    const TraceFilter& trace_filter() const;
    CheckpointStore& checkpoints();
    const CheckpointStore& checkpoints() const;

    logical Emit(const StructuredEvent& event);
    logical EmitForBody(int body_index, const StructuredEvent& event);
    logical EmitSimple(
        const StructuredEvent& base_event,
        const char* phase_tag,
        DiagnosticLevel level,
        const char* message);
    logical EmitSimpleForBody(
        int body_index,
        const StructuredEvent& base_event,
        const char* phase_tag,
        DiagnosticLevel level,
        const char* message);

private:
    std::string MakeRunId() const;
    std::string NowText() const;
    std::string SanitizeArtifactRole(const char* artifact_role) const;
    std::string BodyEventPath(int body_index) const;
    StructuredEventWriter* BodyEventWriter(int body_index);

private:
    RunContextOptions options_;
    std::string run_id_;
    std::string output_root_;
    std::string output_sat_dir_;
    std::string output_log_dir_;
    std::set<std::string> debug_sat_output_set_;
    TraceFilter trace_filter_;
    CheckpointStore checkpoint_store_;
    StructuredEventWriter event_writer_;
    std::map<int, StructuredEventWriter*> body_event_writers_;
};
} // namespace midsurface_new
