#define _CRT_SECURE_NO_WARNINGS
#include "axiom/storage/spill_manager.h"
#include <chrono>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace axiom {

static uint32_t get_current_pid() {
#ifdef _WIN32
    return static_cast<uint32_t>(GetCurrentProcessId());
#else
    return static_cast<uint32_t>(getpid());
#endif
}

// SpillPartition implementation
SpillPartition::SpillPartition(std::string file_path, Schema schema, std::shared_ptr<SpillManager> manager)
    : file_path_(std::move(file_path)),
      schema_(std::move(schema)),
      manager_(std::move(manager)) {
    writer_ = std::make_unique<std::ofstream>(file_path_, std::ios::binary | std::ios::trunc);
    if (!writer_->is_open()) {
        std::string err = std::strerror(errno);
        throw AxiomException(ErrorCategory::Storage, "Failed to create spill file (" + err + "): " + file_path_);
    }
}

SpillPartition::~SpillPartition() {
    close_streams();
    remove_file();
}

SpillPartition::SpillPartition(SpillPartition&& other) noexcept
    : file_path_(std::move(other.file_path_)),
      schema_(std::move(other.schema_)),
      manager_(std::move(other.manager_)),
      writer_(std::move(other.writer_)),
      reader_(std::move(other.reader_)),
      total_rows_(other.total_rows_),
      total_bytes_(other.total_bytes_),
      is_reading_(other.is_reading_),
      deleted_(other.deleted_) {
    other.deleted_ = true;
}

SpillPartition& SpillPartition::operator=(SpillPartition&& other) noexcept {
    if (this != &other) {
        close_streams();
        remove_file();
        file_path_ = std::move(other.file_path_);
        schema_ = std::move(other.schema_);
        manager_ = std::move(other.manager_);
        writer_ = std::move(other.writer_);
        reader_ = std::move(other.reader_);
        total_rows_ = other.total_rows_;
        total_bytes_ = other.total_bytes_;
        is_reading_ = other.is_reading_;
        deleted_ = other.deleted_;
        other.deleted_ = true;
    }
    return *this;
}

void SpillPartition::close_streams() {
    if (writer_ && writer_->is_open()) {
        writer_->close();
    }
    writer_.reset();

    if (reader_ && reader_->is_open()) {
        reader_->close();
    }
    reader_.reset();
}

void SpillPartition::finish_write() {
    if (writer_ && writer_->is_open()) {
        writer_->flush();
        writer_->close();
        writer_.reset();
    }
}

void SpillPartition::write_batch(const RecordBatch& batch) {
    if (!writer_ || !writer_->is_open()) {
        writer_ = std::make_unique<std::ofstream>(file_path_, std::ios::binary | std::ios::app);
        if (!writer_->is_open()) {
            throw AxiomException(ErrorCategory::Storage, "Failed to open spill partition for writing: " + file_path_);
        }
    }

    auto start_pos = writer_->tellp();
    BinarySerializer::write_batch(*writer_, batch);
    if (!writer_->good()) {
        throw AxiomException(ErrorCategory::Storage, "Failed to write batch to spill partition: " + file_path_);
    }
    auto end_pos = writer_->tellp();

    size_t written = (end_pos >= start_pos) ? static_cast<size_t>(end_pos - start_pos) : 0;
    total_bytes_ += written;
    total_rows_ += batch.row_count();

    if (manager_) {
        manager_->record_bytes_written(written);
    }
}

void SpillPartition::prepare_read() {
    if (deleted_) return;

    if (writer_ && writer_->is_open()) {
        writer_->flush();
        writer_->close();
        writer_.reset();
    }

    if (total_rows_ == 0) {
        is_reading_ = true;
        return;
    }

    reader_ = std::make_unique<std::ifstream>(file_path_, std::ios::binary);
    if (!reader_->is_open()) {
        throw AxiomException(ErrorCategory::Storage, "Failed to open spill partition for reading: " + file_path_);
    }
    is_reading_ = true;
}

std::unique_ptr<RecordBatch> SpillPartition::read_next_batch() {
    if (total_rows_ == 0 || deleted_) {
        return nullptr;
    }

    if (!is_reading_ || !reader_) {
        prepare_read();
    }
    if (!reader_ || !reader_->is_open()) {
        return nullptr;
    }

    auto batch = BinarySerializer::read_batch(*reader_, schema_);
    if (!batch) {
        if (reader_ && reader_->is_open()) {
            reader_->close();
            reader_.reset();
        }
    }
    return batch;
}

void SpillPartition::rewind() {
    if (!reader_) {
        prepare_read();
        return;
    }
    reader_->clear();
    reader_->seekg(0, std::ios::beg);
}

void SpillPartition::remove_file() {
    if (!deleted_) {
        close_streams();
        std::error_code ec;
        std::filesystem::remove(file_path_, ec);
        if (manager_ && total_bytes_ > 0) {
            manager_->record_bytes_freed(total_bytes_);
        }
        deleted_ = true;
    }
}

// SpillManager implementation
SpillManager::SpillManager(std::string temp_dir, size_t max_disk_bytes)
    : max_disk_bytes_(max_disk_bytes) {
#ifdef _WIN32
    _setmaxstdio(2048);
#endif
    if (temp_dir.empty()) {
        const char* env_temp = std::getenv("AXIOM_TEMP_DIR");
        if (env_temp && *env_temp) {
            temp_dir_ = (std::filesystem::path(env_temp) / ("axiom_spill_" + std::to_string(get_current_pid()))).string();
        } else {
            std::error_code ec;
            auto sys_temp = std::filesystem::temp_directory_path(ec);
            auto cur_tmp = std::filesystem::current_path(ec) / "tmp";

            uintmax_t sys_space = 0;
            uintmax_t cur_space = 0;

            if (!ec) {
                auto s = std::filesystem::space(sys_temp, ec);
                if (!ec) sys_space = s.available;
            }
            ec.clear();
            std::filesystem::create_directories(cur_tmp, ec);
            if (!ec) {
                auto s = std::filesystem::space(cur_tmp, ec);
                if (!ec) cur_space = s.available;
            }

            // Prefer location with substantially more free disk space
            if (cur_space > sys_space && cur_space > 1024 * 1024) {
                temp_dir_ = (cur_tmp / ("axiom_spill_" + std::to_string(get_current_pid()))).string();
            } else if (sys_space > 0) {
                temp_dir_ = (sys_temp / ("axiom_spill_" + std::to_string(get_current_pid()))).string();
            } else {
                temp_dir_ = "./tmp/axiom_spill_" + std::to_string(get_current_pid());
            }
        }
    } else {
        temp_dir_ = std::move(temp_dir);
    }

    std::error_code ec;
    std::filesystem::create_directories(temp_dir_, ec);
    if (ec) {
        throw AxiomException(ErrorCategory::Storage, "Failed to create temporary spill directory: " + temp_dir_);
    }
}

SpillManager::~SpillManager() {
    cleanup_all();
}

void SpillManager::set_temp_directory(std::string dir) {
    temp_dir_ = std::move(dir);
    std::error_code ec;
    std::filesystem::create_directories(temp_dir_, ec);
    if (ec) {
        throw AxiomException(ErrorCategory::Storage, "Failed to create temporary directory: " + temp_dir_);
    }
}

std::unique_ptr<SpillPartition> SpillManager::create_partition(const Schema& schema, std::string_view prefix) {
    uint64_t seq = ++file_sequence_;
    std::string filename = std::string(prefix) + "_" + std::to_string(get_current_pid()) + "_" + std::to_string(seq) + ".axmb";
    std::filesystem::path p = std::filesystem::path(temp_dir_) / filename;
    std::string full_path = p.string();

    total_spill_count_++;
    active_files_.push_back(full_path);

    return std::make_unique<SpillPartition>(std::move(full_path), schema, shared_from_this());
}

void SpillManager::record_bytes_written(size_t bytes) {
    if (bytes == 0) return;
    size_t prev = current_disk_bytes_.fetch_add(bytes, std::memory_order_relaxed);
    size_t new_val = prev + bytes;

    if (max_disk_bytes_ > 0 && new_val > max_disk_bytes_) {
        current_disk_bytes_.fetch_sub(bytes, std::memory_order_relaxed);
        throw AxiomException(
            ErrorCategory::Storage,
            "Temporary disk quota exceeded: requested " + std::to_string(bytes) +
            " bytes, limit " + std::to_string(max_disk_bytes_) + " bytes"
        );
    }

    size_t cur_peak = peak_disk_bytes_.load(std::memory_order_relaxed);
    while (new_val > cur_peak && !peak_disk_bytes_.compare_exchange_weak(cur_peak, new_val, std::memory_order_relaxed)) {}
}

void SpillManager::record_bytes_freed(size_t bytes) noexcept {
    if (bytes == 0) return;
    size_t current = current_disk_bytes_.load(std::memory_order_relaxed);
    while (true) {
        size_t new_val = (bytes >= current) ? 0 : (current - bytes);
        if (current_disk_bytes_.compare_exchange_weak(current, new_val, std::memory_order_relaxed)) {
            break;
        }
    }
}

void SpillManager::cleanup_all() {
    if (!cleaned_up_) {
        for (const auto& file : active_files_) {
            std::error_code ec;
            std::filesystem::remove(file, ec);
        }
        active_files_.clear();

        // If directory is empty, remove it
        std::error_code ec;
        if (std::filesystem::is_empty(temp_dir_, ec)) {
            std::filesystem::remove(temp_dir_, ec);
        }
        cleaned_up_ = true;
    }
}

} // namespace axiom

