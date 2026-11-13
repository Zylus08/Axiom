#pragma once

#include "axiom/catalog/catalog.h"
#include "axiom/common/record_batch.h"
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

    [[nodiscard]] std::string format_table() const;
    [[nodiscard]] std::string format_csv() const;
    [[nodiscard]] std::string format_json() const;
};

class QueryEngine {
public:
    explicit QueryEngine(size_t batch_size = 1024);

    Catalog& catalog() noexcept { return catalog_; }
    const Catalog& catalog() const noexcept { return catalog_; }

    [[nodiscard]] QueryResult execute(const std::string& sql);

    void set_batch_size(size_t batch_size) noexcept { batch_size_ = batch_size; }
    [[nodiscard]] size_t batch_size() const noexcept { return batch_size_; }

private:
    Catalog catalog_;
    size_t batch_size_;
};

} // namespace axiom

