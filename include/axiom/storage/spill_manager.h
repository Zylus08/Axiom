#pragma once

#include "axiom/storage/binary_serializer.h"
#include <string>
#include <memory>
#include <vector>
#include <fstream>
#include <atomic>
#include <filesystem>

namespace axiom {

class SpillManager;

class SpillPartition {
public:
    SpillPartition(std::string file_path, Schema schema, std::shared_ptr<SpillManager> manager);
    ~SpillPartition();

    SpillPartition(const SpillPartition&) = delete;
    SpillPartition& operator=(const SpillPartition&) = delete;
    SpillPartition(SpillPartition&&) noexcept;
    SpillPartition& operator=(SpillPartition&&) noexcept;

    // Writing batches to spill file
    void write_batch(const RecordBatch& batch);
    void finish_write();

    // Finishes writing and prepares partition for reading
    void prepare_read();

    // Reads the next batch from the partition. Returns nullptr on EOF.
    [[nodiscard]] std::unique_ptr<RecordBatch> read_next_batch();

    // Rewinds reader to beginning of partition
    void rewind();

    // Closes and explicitly deletes file
    void remove_file();

    [[nodiscard]] const std::string& file_path() const noexcept { return file_path_; }
    [[nodiscard]] size_t row_count() const noexcept { return total_rows_; }
    [[nodiscard]] size_t byte_size() const noexcept { return total_bytes_; }
    [[nodiscard]] const Schema& schema() const noexcept { return schema_; }

private:
    std::string file_path_;
    Schema schema_;
    std::shared_ptr<SpillManager> manager_;

    std::unique_ptr<std::ofstream> writer_;
    std::unique_ptr<std::ifstream> reader_;

    size_t total_rows_{0};
    size_t total_bytes_{0};
    bool is_reading_{false};
    bool deleted_{false};

    void close_streams();
};

class SpillManager : public std::enable_shared_from_this<SpillManager> {
public:
    explicit SpillManager(std::string temp_dir = "", size_t max_disk_bytes = 0);
    ~SpillManager();

    [[nodiscard]] std::unique_ptr<SpillPartition> create_partition(const Schema& schema, std::string_view prefix = "spill");

    void record_bytes_written(size_t bytes);
    void record_bytes_freed(size_t bytes) noexcept;

    [[nodiscard]] size_t current_disk_bytes() const noexcept { return current_disk_bytes_.load(std::memory_order_relaxed); }
    [[nodiscard]] size_t peak_disk_bytes() const noexcept { return peak_disk_bytes_.load(std::memory_order_relaxed); }
    [[nodiscard]] size_t cumulative_disk_bytes() const noexcept { return cumulative_disk_bytes_.load(std::memory_order_relaxed); }
    [[nodiscard]] size_t total_spill_count() const noexcept { return total_spill_count_.load(std::memory_order_relaxed); }
    [[nodiscard]] size_t max_disk_quota() const noexcept { return max_disk_bytes_; }
    [[nodiscard]] const std::string& temp_directory() const noexcept { return temp_dir_; }

    void set_temp_directory(std::string dir);
    void set_max_disk_quota(size_t bytes) noexcept { max_disk_bytes_ = bytes; }

    // Cleans up all managed temporary files created by this manager instance
    void cleanup_all();

private:
    std::string temp_dir_;
    size_t max_disk_bytes_{0};
    std::atomic<size_t> current_disk_bytes_{0};
    std::atomic<size_t> peak_disk_bytes_{0};
    std::atomic<size_t> cumulative_disk_bytes_{0};
    std::atomic<size_t> total_spill_count_{0};
    std::atomic<uint64_t> file_sequence_{0};

    std::vector<std::string> active_files_;
    bool cleaned_up_{false};
};

} // namespace axiom

