#include "axiom/common/query_context.h"
#include <sstream>
#include <iomanip>

namespace axiom {

std::string QueryTelemetry::format_profile() const {
    std::ostringstream oss;
    oss << "=== Query Profile & Resource Accounting ===\n";
    oss << "Total Execution Time: " << std::fixed << std::setprecision(2) << total_execution_time_ms << " ms\n";
    oss << "Peak Accounted Memory: " << format_memory_size(peak_accounted_memory) << " (" << peak_accounted_memory << " bytes)\n";
    oss << "Peak Process Working Set: " << format_memory_size(peak_process_rss) << " (" << peak_process_rss << " bytes)\n";
    oss << "Total Disk Spilled: " << format_memory_size(total_spilled_bytes) << " (" << total_spilled_bytes << " bytes)\n";
    oss << "Total Spill Partitions/Runs Created: " << total_spill_count << "\n\n";

    if (!operator_profiles.empty()) {
        oss << "--- Operator Metrics ---\n";
        for (const auto& op : operator_profiles) {
            oss << "Operator: " << op.operator_name << "\n";
            oss << "  Rows Produced:   " << op.rows_produced << "\n";
            oss << "  Batches:         " << op.batches_produced << "\n";
            oss << "  Peak Memory:     " << format_memory_size(op.peak_memory_bytes) << "\n";
            oss << "  Spilled:         " << format_memory_size(op.spilled_bytes) << " in " << op.spill_count << " runs/parts\n";
            oss << "  Execution Time:  " << std::fixed << std::setprecision(2) << op.execution_time_ms << " ms\n";
        }
    }
    return oss.str();
}

QueryExecutionContext::QueryExecutionContext(QueryOptions options)
    : options_(std::move(options)),
      start_time_(std::chrono::steady_clock::now()) {
    memory_manager_ = std::make_shared<QueryMemoryManager>(options_.memory_limit);
    spill_manager_ = std::make_shared<SpillManager>(options_.temp_dir, options_.max_temp_disk);
}

QueryExecutionContext::~QueryExecutionContext() {
    if (spill_manager_) {
        spill_manager_->cleanup_all();
    }
}

void QueryExecutionContext::record_operator_metric(OperatorMetrics metric) {
    telemetry_.operator_profiles.push_back(std::move(metric));
}

void QueryExecutionContext::finalize_telemetry() {
    auto end_time = std::chrono::steady_clock::now();
    telemetry_.total_execution_time_ms = std::chrono::duration<double, std::milli>(end_time - start_time_).count();

    if (memory_manager_) {
        telemetry_.peak_accounted_memory = memory_manager_->peak_bytes();
    }

    auto proc_mem = get_process_memory_info();
    telemetry_.peak_process_rss = proc_mem.peak_rss;

    if (spill_manager_) {
        telemetry_.total_spilled_bytes = spill_manager_->peak_disk_bytes();
        telemetry_.total_spill_count = spill_manager_->total_spill_count();
    }
}

} // namespace axiom

