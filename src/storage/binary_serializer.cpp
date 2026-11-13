#include "axiom/storage/binary_serializer.h"
#include <cstring>

namespace axiom {

static uint32_t compute_checksum(const char* data, size_t len) {
    uint32_t hash = 2166136261u;
    for (size_t i = 0; i < len; ++i) {
        hash ^= static_cast<uint8_t>(data[i]);
        hash *= 16777619u;
    }
    return hash;
}

size_t BinarySerializer::estimate_row_size(const std::vector<Value>& row) {
    size_t bytes = sizeof(std::vector<Value>) + row.capacity() * sizeof(Value);
    for (const auto& v : row) {
        if (!v.is_null() && v.kind() == TypeKind::VarChar) {
            bytes += v.get_varchar().capacity();
        }
    }
    return bytes;
}

size_t BinarySerializer::estimate_batch_size(const RecordBatch& batch) {
    size_t bytes = sizeof(RecordBatch);
    size_t num_cols = batch.column_count();
    size_t num_rows = batch.row_count();

    bytes += num_cols * sizeof(std::vector<Value>);
    for (size_t c = 0; c < num_cols; ++c) {
        const auto& col = batch.get_column(c);
        bytes += col.capacity() * sizeof(Value);
        if (batch.schema().column(c).type == TypeKind::VarChar) {
            for (size_t r = 0; r < num_rows; ++r) {
                if (!col[r].is_null()) {
                    bytes += col[r].get_varchar().capacity();
                }
            }
        }
    }
    return bytes;
}

void BinarySerializer::write_batch(std::ostream& os, const RecordBatch& batch) {
    uint32_t magic = MAGIC;
    uint16_t version = VERSION;
    uint32_t num_cols = static_cast<uint32_t>(batch.column_count());
    uint32_t num_rows = static_cast<uint32_t>(batch.row_count());

    os.write(reinterpret_cast<const char*>(&magic), sizeof(magic));
    os.write(reinterpret_cast<const char*>(&version), sizeof(version));
    os.write(reinterpret_cast<const char*>(&num_cols), sizeof(num_cols));
    os.write(reinterpret_cast<const char*>(&num_rows), sizeof(num_rows));

    if (num_rows == 0) {
        uint32_t dummy_chk = 0;
        os.write(reinterpret_cast<const char*>(&dummy_chk), sizeof(dummy_chk));
        return;
    }

    std::vector<char> payload;

    for (size_t c = 0; c < num_cols; ++c) {
        const auto& col = batch.get_column(c);
        TypeKind type = batch.schema().column(c).type;
        uint8_t type_byte = static_cast<uint8_t>(type);
        payload.push_back(static_cast<char>(type_byte));

        // 1. Write null bitmap: 1 byte per 8 rows
        size_t bitmap_bytes = (num_rows + 7) / 8;
        size_t bmp_start = payload.size();
        payload.resize(bmp_start + bitmap_bytes, 0);

        for (size_t r = 0; r < num_rows; ++r) {
            if (col[r].is_null()) {
                payload[bmp_start + (r / 8)] |= static_cast<char>(1 << (r % 8));
            }
        }

        // 2. Write column data
        switch (type) {
            case TypeKind::Null:
                break;
            case TypeKind::Boolean: {
                for (size_t r = 0; r < num_rows; ++r) {
                    uint8_t b = (!col[r].is_null() && col[r].get_bool()) ? 1 : 0;
                    payload.push_back(static_cast<char>(b));
                }
                break;
            }
            case TypeKind::BigInt: {
                size_t old_sz = payload.size();
                payload.resize(old_sz + num_rows * sizeof(int64_t));
                auto* ptr = reinterpret_cast<int64_t*>(payload.data() + old_sz);
                for (size_t r = 0; r < num_rows; ++r) {
                    ptr[r] = col[r].is_null() ? 0 : col[r].get_bigint();
                }
                break;
            }
            case TypeKind::Double: {
                size_t old_sz = payload.size();
                payload.resize(old_sz + num_rows * sizeof(double));
                auto* ptr = reinterpret_cast<double*>(payload.data() + old_sz);
                for (size_t r = 0; r < num_rows; ++r) {
                    ptr[r] = col[r].is_null() ? 0.0 : col[r].get_double();
                }
                break;
            }
            case TypeKind::VarChar: {
                // String lengths
                for (size_t r = 0; r < num_rows; ++r) {
                    uint32_t len = col[r].is_null() ? 0 : static_cast<uint32_t>(col[r].get_varchar().size());
                    size_t cur = payload.size();
                    payload.resize(cur + sizeof(uint32_t));
                    std::memcpy(payload.data() + cur, &len, sizeof(uint32_t));
                }
                // String bytes
                for (size_t r = 0; r < num_rows; ++r) {
                    if (!col[r].is_null()) {
                        const auto& str = col[r].get_varchar();
                        size_t cur = payload.size();
                        payload.resize(cur + str.size());
                        std::memcpy(payload.data() + cur, str.data(), str.size());
                    }
                }
                break;
            }
        }
    }

    uint32_t checksum = compute_checksum(payload.data(), payload.size());
    uint64_t payload_len = payload.size();

    os.write(reinterpret_cast<const char*>(&payload_len), sizeof(payload_len));
    os.write(reinterpret_cast<const char*>(&checksum), sizeof(checksum));
    if (!payload.empty()) {
        os.write(payload.data(), static_cast<std::streamsize>(payload.size()));
    }
}

std::unique_ptr<RecordBatch> BinarySerializer::read_batch(std::istream& is, const Schema& expected_schema) {
    uint32_t magic = 0;
    if (!is.read(reinterpret_cast<char*>(&magic), sizeof(magic))) {
        return nullptr; // Clean EOF
    }

    if (magic != MAGIC) {
        throw AxiomException(ErrorCategory::Storage, "Invalid magic number in spill file format");
    }

    uint16_t version = 0;
    uint32_t num_cols = 0;
    uint32_t num_rows = 0;

    is.read(reinterpret_cast<char*>(&version), sizeof(version));
    is.read(reinterpret_cast<char*>(&num_cols), sizeof(num_cols));
    is.read(reinterpret_cast<char*>(&num_rows), sizeof(num_rows));

    if (!is) {
        throw AxiomException(ErrorCategory::Storage, "Truncated spill file header");
    }

    if (version != VERSION) {
        throw AxiomException(ErrorCategory::Storage, "Unsupported spill file format version: " + std::to_string(version));
    }

    if (num_cols != expected_schema.column_count()) {
        throw AxiomException(ErrorCategory::Storage, "Spill file column count mismatch: expected " +
                             std::to_string(expected_schema.column_count()) + ", got " + std::to_string(num_cols));
    }

    if (num_rows == 0) {
        uint32_t dummy_chk = 0;
        is.read(reinterpret_cast<char*>(&dummy_chk), sizeof(dummy_chk));
        return std::make_unique<RecordBatch>(expected_schema);
    }

    uint64_t payload_len = 0;
    uint32_t stored_chk = 0;
    is.read(reinterpret_cast<char*>(&payload_len), sizeof(payload_len));
    is.read(reinterpret_cast<char*>(&stored_chk), sizeof(stored_chk));

    if (!is) {
        throw AxiomException(ErrorCategory::Storage, "Truncated spill file payload header");
    }

    std::vector<char> payload(static_cast<size_t>(payload_len));
    if (payload_len > 0) {
        is.read(payload.data(), static_cast<std::streamsize>(payload_len));
        if (!is) {
            throw AxiomException(ErrorCategory::Storage, "Truncated spill file payload data");
        }
    }

    uint32_t actual_chk = compute_checksum(payload.data(), payload.size());
    if (actual_chk != stored_chk) {
        throw AxiomException(ErrorCategory::Storage, "Spill file checksum verification failure (data corruption)");
    }

    // Unpack columns
    size_t offset = 0;
    std::vector<std::vector<Value>> columns(num_cols);

    for (size_t c = 0; c < num_cols; ++c) {
        if (offset >= payload.size()) {
            throw AxiomException(ErrorCategory::Storage, "Spill file payload buffer underrun");
        }
        TypeKind type = static_cast<TypeKind>(static_cast<uint8_t>(payload[offset++]));
        if (type != expected_schema.column(c).type) {
            throw AxiomException(ErrorCategory::Storage, "Spill file column type mismatch for column " + std::to_string(c));
        }

        size_t bitmap_bytes = (num_rows + 7) / 8;
        if (offset + bitmap_bytes > payload.size()) {
            throw AxiomException(ErrorCategory::Storage, "Spill file null bitmap underrun");
        }
        const char* bmp = payload.data() + offset;
        offset += bitmap_bytes;

        columns[c].reserve(num_rows);

        switch (type) {
            case TypeKind::Null: {
                for (size_t r = 0; r < num_rows; ++r) {
                    columns[c].push_back(Value::make_null());
                }
                break;
            }
            case TypeKind::Boolean: {
                if (offset + num_rows > payload.size()) {
                    throw AxiomException(ErrorCategory::Storage, "Spill file boolean data underrun");
                }
                for (size_t r = 0; r < num_rows; ++r) {
                    bool is_null = (bmp[r / 8] & (1 << (r % 8))) != 0;
                    if (is_null) {
                        columns[c].push_back(Value::make_null(TypeKind::Boolean));
                    } else {
                        bool b = payload[offset + r] != 0;
                        columns[c].push_back(Value::make_bool(b));
                    }
                }
                offset += num_rows;
                break;
            }
            case TypeKind::BigInt: {
                size_t needed = num_rows * sizeof(int64_t);
                if (offset + needed > payload.size()) {
                    throw AxiomException(ErrorCategory::Storage, "Spill file bigint data underrun");
                }
                const auto* ptr = reinterpret_cast<const int64_t*>(payload.data() + offset);
                for (size_t r = 0; r < num_rows; ++r) {
                    bool is_null = (bmp[r / 8] & (1 << (r % 8))) != 0;
                    if (is_null) {
                        columns[c].push_back(Value::make_null(TypeKind::BigInt));
                    } else {
                        columns[c].push_back(Value::make_bigint(ptr[r]));
                    }
                }
                offset += needed;
                break;
            }
            case TypeKind::Double: {
                size_t needed = num_rows * sizeof(double);
                if (offset + needed > payload.size()) {
                    throw AxiomException(ErrorCategory::Storage, "Spill file double data underrun");
                }
                const auto* ptr = reinterpret_cast<const double*>(payload.data() + offset);
                for (size_t r = 0; r < num_rows; ++r) {
                    bool is_null = (bmp[r / 8] & (1 << (r % 8))) != 0;
                    if (is_null) {
                        columns[c].push_back(Value::make_null(TypeKind::Double));
                    } else {
                        columns[c].push_back(Value::make_double(ptr[r]));
                    }
                }
                offset += needed;
                break;
            }
            case TypeKind::VarChar: {
                size_t lengths_needed = num_rows * sizeof(uint32_t);
                if (offset + lengths_needed > payload.size()) {
                    throw AxiomException(ErrorCategory::Storage, "Spill file varchar lengths underrun");
                }
                std::vector<uint32_t> lengths(num_rows);
                std::memcpy(lengths.data(), payload.data() + offset, lengths_needed);
                offset += lengths_needed;

                for (size_t r = 0; r < num_rows; ++r) {
                    bool is_null = (bmp[r / 8] & (1 << (r % 8))) != 0;
                    uint32_t len = lengths[r];
                    if (is_null) {
                        columns[c].push_back(Value::make_null(TypeKind::VarChar));
                    } else {
                        if (offset + len > payload.size()) {
                            throw AxiomException(ErrorCategory::Storage, "Spill file varchar bytes underrun");
                        }
                        std::string s(payload.data() + offset, len);
                        columns[c].push_back(Value::make_varchar(std::move(s)));
                        offset += len;
                    }
                }
                break;
            }
        }
    }

    return std::make_unique<RecordBatch>(expected_schema, std::move(columns), num_rows);
}

} // namespace axiom

