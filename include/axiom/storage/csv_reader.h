#pragma once

#include "axiom/common/record_batch.h"
#include <string>
#include <vector>
#include <memory>
#include <fstream>
#include <optional>

namespace axiom {

struct CsvOptions {
    char delimiter{','};
    char quote{'"'};
    char escape{'\\'};
    bool has_header{true};
    std::string null_string{""};
    bool treat_empty_as_null{true};
};

class CsvReader {
public:
    CsvReader(std::string file_path, Schema schema, CsvOptions options = {});
    ~CsvReader();

    // Disable copy, enable move
    CsvReader(const CsvReader&) = delete;
    CsvReader& operator=(const CsvReader&) = delete;
    CsvReader(CsvReader&&) noexcept;
    CsvReader& operator=(CsvReader&&) noexcept;

    // Reads up to max_rows and returns a RecordBatch. Returns nullptr on EOF.
    [[nodiscard]] std::unique_ptr<RecordBatch> read_batch(size_t max_rows = 1024);

    // Rewinds reader to beginning of data (after header if present)
    void rewind();

    [[nodiscard]] const Schema& schema() const noexcept { return schema_; }
    [[nodiscard]] const std::string& file_path() const noexcept { return file_path_; }
    [[nodiscard]] size_t current_line() const noexcept { return line_num_; }

private:
    std::string file_path_;
    Schema schema_;
    CsvOptions options_;
    std::ifstream file_;
    size_t line_num_{1};
    bool eof_reached_{false};

    // Buffer for chunked reading
    std::vector<char> buffer_;
    size_t buffer_pos_{0};
    size_t buffer_len_{0};
    static constexpr size_t BUFFER_CAPACITY = 64 * 1024;

    bool fill_buffer();
    std::optional<char> next_char();
    void put_back();

    // Reads a single row of tokens from the stream
    bool read_next_row(std::vector<std::string>& row_fields, std::vector<bool>& is_null);
    Value parse_value(std::string_view raw, bool is_null, TypeKind target_type);
};

} // namespace axiom
