#pragma once

#include "axiom/common/record_batch.h"
#include <iostream>
#include <memory>
#include <vector>

namespace axiom {

class BinarySerializer {
public:
    static constexpr uint32_t MAGIC = 0x424D5841; // 'AXMB' in little endian
    static constexpr uint16_t VERSION = 1;

    // Serializes a RecordBatch to an output stream
    static void write_batch(std::ostream& os, const RecordBatch& batch);

    // Reads a serialized RecordBatch from an input stream.
    // Returns nullptr if stream is at EOF (clean end of partition).
    [[nodiscard]] static std::unique_ptr<RecordBatch> read_batch(std::istream& is, const Schema& expected_schema);

    // Calculates approximate in-memory byte size of a RecordBatch
    [[nodiscard]] static size_t estimate_batch_size(const RecordBatch& batch);

    // Calculates approximate in-memory byte size of a single row of values
    [[nodiscard]] static size_t estimate_row_size(const std::vector<Value>& row);
};

} // namespace axiom

