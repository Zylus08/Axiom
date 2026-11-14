#pragma once

#include "axiom/common/memory_manager.h"
#include "axiom/storage/spill_manager.h"
#include <string>
#include <memory>
#include <chrono>
#include <vector>

namespace axiom {

struct OperatorMetrics {
    std::string operator_name;
    size_t rows_produced{0};
    size_t batches_produced{0};
    size_t peak_memory_bytes{0};
    size_t spilled_bytes{0};
    size_t spill_count{0};
    double execution_time_ms{0.0};
};

struct QueryOptions {
    size_t memory_limit{0};           // In bytes (0 = unlimited / engine default)
    std::string temp_dir{""};         // Directory for temporary spill files
    size_t max_temp_disk{0};         // In bytes (0 = unlimited)
    size_t batch_size{1024};
    bool profile{false};
    bool explain_analyze{false};
    bool enable_optimizer{true};
};

struct QueryTelemetry {
    size_t peak_accounted_memory{0};
    size_t peak_process_rss{0};
    size_t peak_process_commit{0};
    size_t total_spilled_bytes{0};
    size_t total_spill_count{0};
    double total_execution_time_ms{0.0};
    std::vector<OperatorMetrics> operator_profiles;

    [[nodiscard]] std::string format_profile() const;
};

class QueryExecutionContext {
public:
    explicit QueryExecutionContext(QueryOptions options = {});
    ~QueryExecutionContext();

    QueryExecutionContext(const QueryExecutionContext&) = delete;
    QueryExecutionContext& operator=(const QueryExecutionContext&) = delete;
    QueryExecutionContext(QueryExecutionContext&&) noexcept = default;
    QueryExecutionContext& operator=(QueryExecutionContext&&) noexcept = default;

    [[nodiscard]] const QueryOptions& options() const noexcept { return options_; }
    QueryOptions& options() noexcept { return options_; }

    [[nodiscard]] const std::shared_ptr<QueryMemoryManager>& memory_manager() const noexcept { return memory_manager_; }
    [[nodiscard]] const std::shared_ptr<SpillManager>& spill_manager() const noexcept { return spill_manager_; }
    [[nodiscard]] QueryTelemetry& telemetry() noexcept { return telemetry_; }
    [[nodiscard]] const QueryTelemetry& telemetry() const noexcept { return telemetry_; }

    void record_operator_metric(OperatorMetrics metric);
    void finalize_telemetry();

private:
    QueryOptions options_;
    std::shared_ptr<QueryMemoryManager> memory_manager_;
    std::shared_ptr<SpillManager> spill_manager_;
    QueryTelemetry telemetry_;
    std::chrono::steady_clock::time_point start_time_;
};

} // namespace axiom

