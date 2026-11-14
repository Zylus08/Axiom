#pragma once

#include "axiom/common/error.h"
#include <string>
#include <string_view>
#include <memory>
#include <atomic>
#include <cstdint>
#include <optional>

namespace axiom {

struct ProcessMemoryInfo {
    size_t current_rss{0};
    size_t peak_rss{0};
    size_t current_commit{0};
    size_t peak_commit{0};
};

[[nodiscard]] ProcessMemoryInfo get_process_memory_info();
[[nodiscard]] size_t parse_memory_size(std::string_view str);
[[nodiscard]] std::string format_memory_size(size_t bytes);

class QueryMemoryManager;

class MemoryReservation {
public:
    MemoryReservation() = default;
    explicit MemoryReservation(std::shared_ptr<QueryMemoryManager> manager, size_t initial_bytes = 0);
    ~MemoryReservation();

    MemoryReservation(const MemoryReservation&) = delete;
    MemoryReservation& operator=(const MemoryReservation&) = delete;
    MemoryReservation(MemoryReservation&& other) noexcept;
    MemoryReservation& operator=(MemoryReservation&& other) noexcept;

    [[nodiscard]] bool try_reserve(size_t additional_bytes);
    void reserve_or_throw(size_t additional_bytes, std::string_view op_name);
    void release(size_t bytes);
    void release_all();

    [[nodiscard]] size_t reserved_bytes() const noexcept { return reserved_bytes_; }
    [[nodiscard]] bool has_manager() const noexcept { return manager_ != nullptr; }

private:
    std::shared_ptr<QueryMemoryManager> manager_;
    size_t reserved_bytes_{0};
};

class QueryMemoryManager : public std::enable_shared_from_this<QueryMemoryManager> {
public:
    explicit QueryMemoryManager(size_t max_budget_bytes = 0); // 0 = unlimited

    [[nodiscard]] bool try_allocate(size_t bytes);
    void allocate_or_throw(size_t bytes, std::string_view op_name);
    void deallocate(size_t bytes) noexcept;

    [[nodiscard]] size_t current_bytes() const noexcept { return current_bytes_.load(std::memory_order_relaxed); }
    [[nodiscard]] size_t peak_bytes() const noexcept { return peak_bytes_.load(std::memory_order_relaxed); }
    [[nodiscard]] size_t max_budget() const noexcept { return max_budget_; }
    [[nodiscard]] bool has_limit() const noexcept { return max_budget_ > 0; }
    [[nodiscard]] size_t remaining_budget() const noexcept;

    void set_max_budget(size_t bytes) noexcept { max_budget_ = bytes; }

    [[nodiscard]] std::unique_ptr<MemoryReservation> create_reservation(size_t initial_bytes = 0);

private:
    size_t max_budget_{0};
    std::atomic<size_t> current_bytes_{0};
    std::atomic<size_t> peak_bytes_{0};
};

} // namespace axiom

