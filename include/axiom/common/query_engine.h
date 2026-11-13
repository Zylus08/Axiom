#pragma once

#include "axiom/catalog/catalog.h"
#include "axiom/common/record_batch.h"
#include "axiom/common/query_context.h"
#include <string>
#include <vector>
#include <memory>

namespace axiom {

struct QueryResult {
    Schema schema;
    std::vector<std::unique_ptr<RecordBatch>> batches;
    bool is_explain{false};
    std::string explain_text;
    size_t total_rows{0};
    QueryTelemetry telemetry;

    [[nodiscard]] std::string format_table() const;
    [[nodiscard]] std::string format_csv() const;
    [[nodiscard]] std::string format_json() const;
};

class QueryEngine {
public:
    explicit QueryEngine(size_t batch_size = 1024);

    Catalog& catalog() noexcept { return catalog_; }
    const Catalog& catalog() const noexcept { return catalog_; }

    [[nodiscard]] QueryResult execute(const std::string& sql, QueryOptions options = {});

    void set_batch_size(size_t batch_size) noexcept { default_options_.batch_size = batch_size; }
    [[nodiscard]] size_t batch_size() const noexcept { return default_options_.batch_size; }

    void set_default_options(QueryOptions options) { default_options_ = std::move(options); }
    [[nodiscard]] const QueryOptions& default_options() const noexcept { return default_options_; }

private:
    Catalog catalog_;
    QueryOptions default_options_;
};

} // namespace axiom

