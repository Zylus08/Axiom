#pragma once

#include "axiom/execution/physical_operator.h"
#include "axiom/storage/csv_reader.h"
#include "axiom/catalog/catalog.h"

namespace axiom {

class PhysicalScan : public PhysicalOperator {
public:
    PhysicalScan(TableDef table_def, std::string alias, Schema schema, size_t batch_size = 1024)
        : PhysicalOperator(PhysicalOperatorType::Scan, std::move(schema)),
          table_def_(std::move(table_def)),
          alias_(std::move(alias)),
          batch_size_(batch_size) {}

    void init() override {
        reader_ = std::make_unique<CsvReader>(table_def_.file_path, output_schema_, table_def_.options);
    }

    std::unique_ptr<RecordBatch> next() override {
        if (!reader_) return nullptr;
        return reader_->read_batch(batch_size_);
    }

    void close() override {
        reader_.reset();
    }

    [[nodiscard]] std::string to_string(size_t indent = 0) const override {
        return make_indent(indent) + "PhysicalScan: " + table_def_.name + " (" + table_def_.file_path + ")\n";
    }

private:
    TableDef table_def_;
    std::string alias_;
    size_t batch_size_;
    std::unique_ptr<CsvReader> reader_;
};

} // namespace axiom

