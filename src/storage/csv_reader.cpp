#include "axiom/storage/csv_reader.h"
#include <charconv>
#include <system_error>

namespace axiom {

CsvReader::CsvReader(std::string file_path, Schema schema, CsvOptions options)
    : file_path_(std::move(file_path)),
      schema_(std::move(schema)),
      options_(std::move(options)),
      buffer_(BUFFER_CAPACITY) {
    file_.open(file_path_, std::ios::binary);
    if (!file_.is_open()) {
        throw AxiomException(ErrorCategory::Storage, "Failed to open CSV file: " + file_path_);
    }

    if (options_.has_header) {
        std::vector<std::string> dummy_fields;
        std::vector<bool> dummy_nulls;
        if (!read_next_row(dummy_fields, dummy_nulls)) {
            eof_reached_ = true;
        }
    }
}

CsvReader::~CsvReader() {
    if (file_.is_open()) {
        file_.close();
    }
}

CsvReader::CsvReader(CsvReader&& other) noexcept
    : file_path_(std::move(other.file_path_)),
      schema_(std::move(other.schema_)),
      options_(std::move(other.options_)),
      file_(std::move(other.file_)),
      line_num_(other.line_num_),
      eof_reached_(other.eof_reached_),
      buffer_(std::move(other.buffer_)),
      buffer_pos_(other.buffer_pos_),
      buffer_len_(other.buffer_len_) {}

CsvReader& CsvReader::operator=(CsvReader&& other) noexcept {
    if (this != &other) {
        file_path_ = std::move(other.file_path_);
        schema_ = std::move(other.schema_);
        options_ = std::move(other.options_);
        file_ = std::move(other.file_);
        line_num_ = other.line_num_;
        eof_reached_ = other.eof_reached_;
        buffer_ = std::move(other.buffer_);
        buffer_pos_ = other.buffer_pos_;
        buffer_len_ = other.buffer_len_;
    }
    return *this;
}

void CsvReader::rewind() {
    file_.clear();
    file_.seekg(0, std::ios::beg);
    buffer_pos_ = 0;
    buffer_len_ = 0;
    line_num_ = 1;
    eof_reached_ = false;

    if (options_.has_header) {
        std::vector<std::string> dummy_fields;
        std::vector<bool> dummy_nulls;
        if (!read_next_row(dummy_fields, dummy_nulls)) {
            eof_reached_ = true;
        }
    }
}

bool CsvReader::fill_buffer() {
    if (file_.eof()) return false;
    file_.read(buffer_.data(), static_cast<std::streamsize>(buffer_.size()));
    buffer_len_ = static_cast<size_t>(file_.gcount());
    buffer_pos_ = 0;
    return buffer_len_ > 0;
}

std::optional<char> CsvReader::next_char() {
    if (buffer_pos_ >= buffer_len_) {
        if (!fill_buffer()) {
            return std::nullopt;
        }
    }
    return buffer_[buffer_pos_++];
}

void CsvReader::put_back() {
    if (buffer_pos_ > 0) {
        buffer_pos_--;
    }
}

bool CsvReader::read_next_row(std::vector<std::string>& row_fields, std::vector<bool>& is_null) {
    row_fields.clear();
    is_null.clear();

    enum class State {
        StartOfField,
        UnquotedField,
        QuotedField,
        AfterQuote,
        Escape
    };

    State state = State::StartOfField;
    std::string current_field;
    bool field_was_quoted = false;
    bool seen_any_char = false;

    while (true) {
        auto ch_opt = next_char();
        if (!ch_opt.has_value()) {
            // EOF
            if (!seen_any_char && current_field.empty() && row_fields.empty()) {
                return false;
            }
            // End of last field
            bool is_null_val = false;
            if (!field_was_quoted) {
                if (options_.treat_empty_as_null && current_field.empty()) {
                    is_null_val = true;
                } else if (!options_.null_string.empty() && current_field == options_.null_string) {
                    is_null_val = true;
                }
            } else if (current_field == options_.null_string && !options_.null_string.empty()) {
                is_null_val = true;
            }
            row_fields.push_back(std::move(current_field));
            is_null.push_back(is_null_val);
            return true;
        }

        char c = *ch_opt;
        seen_any_char = true;

        if (c == '\n') {
            line_num_++;
        }

        switch (state) {
            case State::StartOfField: {
                if (c == options_.quote) {
                    state = State::QuotedField;
                    field_was_quoted = true;
                } else if (c == options_.delimiter) {
                    // Empty field
                    bool is_null_val = options_.treat_empty_as_null || options_.null_string.empty();
                    row_fields.push_back("");
                    is_null.push_back(is_null_val);
                    field_was_quoted = false;
                } else if (c == '\r') {
                    // Check if next is \n
                    auto next_c = next_char();
                    if (next_c.has_value() && *next_c == '\n') {
                        line_num_++;
                    } else if (next_c.has_value()) {
                        put_back();
                    }
                    if (!row_fields.empty() || !current_field.empty()) {
                        bool is_null_val = options_.treat_empty_as_null || options_.null_string.empty();
                        row_fields.push_back("");
                        is_null.push_back(is_null_val);
                        return true;
                    }
                    // Empty blank line, continue
                } else if (c == '\n') {
                    if (!row_fields.empty() || !current_field.empty()) {
                        bool is_null_val = options_.treat_empty_as_null || options_.null_string.empty();
                        row_fields.push_back("");
                        is_null.push_back(is_null_val);
                        return true;
                    }
                    // Empty blank line, continue
                } else {
                    current_field += c;
                    state = State::UnquotedField;
                    field_was_quoted = false;
                }
                break;
            }
            case State::UnquotedField: {
                if (c == options_.delimiter) {
                    bool is_null_val = false;
                    if (options_.treat_empty_as_null && current_field.empty()) {
                        is_null_val = true;
                    } else if (!options_.null_string.empty() && current_field == options_.null_string) {
                        is_null_val = true;
                    }
                    row_fields.push_back(std::move(current_field));
                    is_null.push_back(is_null_val);
                    current_field.clear();
                    field_was_quoted = false;
                    state = State::StartOfField;
                } else if (c == '\r') {
                    auto next_c = next_char();
                    if (next_c.has_value() && *next_c == '\n') {
                        line_num_++;
                    } else if (next_c.has_value()) {
                        put_back();
                    }
                    bool is_null_val = (!options_.null_string.empty() && current_field == options_.null_string);
                    row_fields.push_back(std::move(current_field));
                    is_null.push_back(is_null_val);
                    return true;
                } else if (c == '\n') {
                    bool is_null_val = (!options_.null_string.empty() && current_field == options_.null_string);
                    row_fields.push_back(std::move(current_field));
                    is_null.push_back(is_null_val);
                    return true;
                } else {
                    current_field += c;
                }
                break;
            }
            case State::QuotedField: {
                if (c == options_.escape && options_.escape != options_.quote) {
                    state = State::Escape;
                } else if (c == options_.quote) {
                    state = State::AfterQuote;
                } else {
                    current_field += c;
                }
                break;
            }
            case State::AfterQuote: {
                if (c == options_.quote) {
                    // Escaped quote ""
                    current_field += options_.quote;
                    state = State::QuotedField;
                } else if (c == options_.delimiter) {
                    bool is_null_val = (!options_.null_string.empty() && current_field == options_.null_string);
                    row_fields.push_back(std::move(current_field));
                    is_null.push_back(is_null_val);
                    current_field.clear();
                    field_was_quoted = false;
                    state = State::StartOfField;
                } else if (c == '\r') {
                    auto next_c = next_char();
                    if (next_c.has_value() && *next_c == '\n') {
                        line_num_++;
                    } else if (next_c.has_value()) {
                        put_back();
                    }
                    bool is_null_val = (!options_.null_string.empty() && current_field == options_.null_string);
                    row_fields.push_back(std::move(current_field));
                    is_null.push_back(is_null_val);
                    return true;
                } else if (c == '\n') {
                    bool is_null_val = (!options_.null_string.empty() && current_field == options_.null_string);
                    row_fields.push_back(std::move(current_field));
                    is_null.push_back(is_null_val);
                    return true;
                } else if (c == ' ' || c == '\t') {
                    // Ignore whitespace after closing quote until delimiter/newline
                } else {
                    // Stray character after quote
                    current_field += c;
                    state = State::UnquotedField;
                }
                break;
            }
            case State::Escape: {
                current_field += c;
                state = State::QuotedField;
                break;
            }
        }
    }
}

Value CsvReader::parse_value(std::string_view raw, bool is_null, TypeKind target_type) {
    if (is_null) {
        return Value::make_null(target_type);
    }

    switch (target_type) {
        case TypeKind::Null:
            return Value::make_null(TypeKind::Null);
        case TypeKind::Boolean: {
            std::string s;
            for (char c : raw) s += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            if (s == "true" || s == "t" || s == "1") return Value::make_bool(true);
            if (s == "false" || s == "f" || s == "0") return Value::make_bool(false);
            throw AxiomException(ErrorCategory::Storage, "Invalid boolean literal in CSV: '" + std::string(raw) + "'");
        }
        case TypeKind::BigInt: {
            int64_t val = 0;
            // Trim leading/trailing whitespace
            size_t start = 0;
            while (start < raw.size() && std::isspace(static_cast<unsigned char>(raw[start]))) start++;
            size_t end = raw.size();
            while (end > start && std::isspace(static_cast<unsigned char>(raw[end - 1]))) end--;
            if (start == end) {
                return Value::make_null(TypeKind::BigInt);
            }
            std::string_view trimmed = raw.substr(start, end - start);
            auto [ptr, ec] = std::from_chars(trimmed.data(), trimmed.data() + trimmed.size(), val);
            if (ec != std::errc() || ptr != trimmed.data() + trimmed.size()) {
                throw AxiomException(ErrorCategory::Storage, "Invalid integer literal in CSV at line " + std::to_string(line_num_) + ": '" + std::string(raw) + "'");
            }
            return Value::make_bigint(val);
        }
        case TypeKind::Double: {
            size_t start = 0;
            while (start < raw.size() && std::isspace(static_cast<unsigned char>(raw[start]))) start++;
            size_t end = raw.size();
            while (end > start && std::isspace(static_cast<unsigned char>(raw[end - 1]))) end--;
            if (start == end) {
                return Value::make_null(TypeKind::Double);
            }
            double val = 0.0;
            const char* first = raw.data() + start;
            const char* last = raw.data() + end;
            auto [ptr, ec] = std::from_chars(first, last, val);
            if (ec != std::errc() || ptr != last) {
                throw AxiomException(ErrorCategory::Storage, "Invalid double literal in CSV at line " + std::to_string(line_num_) + ": '" + std::string(raw) + "'");
            }
            return Value::make_double(val);
        }
        case TypeKind::VarChar:
            return Value::make_varchar(std::string(raw));
    }
    return Value::make_null(target_type);
}

std::unique_ptr<RecordBatch> CsvReader::read_batch(size_t max_rows) {
    if (eof_reached_) {
        return nullptr;
    }

    std::vector<std::vector<Value>> columns(schema_.column_count());
    for (auto& col : columns) {
        col.reserve(max_rows);
    }

    size_t rows_read = 0;
    row_fields_buf_.reserve(schema_.column_count());
    is_null_buf_.reserve(schema_.column_count());

    while (rows_read < max_rows) {
        if (!read_next_row(row_fields_buf_, is_null_buf_)) {
            eof_reached_ = true;
            break;
        }

        if (row_fields_buf_.size() != schema_.column_count()) {
            throw AxiomException(
                ErrorCategory::Storage,
                "CSV column count mismatch at line " + std::to_string(line_num_) +
                ": expected " + std::to_string(schema_.column_count()) +
                ", got " + std::to_string(row_fields_buf_.size()) + " in " + file_path_
            );
        }

        for (size_t c = 0; c < schema_.column_count(); ++c) {
            columns[c].push_back(parse_value(row_fields_buf_[c], is_null_buf_[c], schema_.column(c).type));
        }
        rows_read++;
    }

    if (rows_read == 0) {
        return nullptr;
    }

    return std::make_unique<RecordBatch>(schema_, std::move(columns), rows_read);
}

} // namespace axiom
