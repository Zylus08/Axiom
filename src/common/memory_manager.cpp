#include "axiom/common/memory_manager.h"
#include <iomanip>
#include <sstream>
#include <charconv>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <psapi.h>
#endif

namespace axiom {

ProcessMemoryInfo get_process_memory_info() {
    ProcessMemoryInfo info{};
#ifdef _WIN32
    PROCESS_MEMORY_COUNTERS pmc{};
    if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc))) {
        info.current_rss = pmc.WorkingSetSize;
        info.peak_rss = pmc.PeakWorkingSetSize;
    }
#endif
    return info;
}

size_t parse_memory_size(std::string_view str) {
    size_t start = str.find_first_not_of(" \t\r\n");
    if (start == std::string_view::npos) {
        throw AxiomException(ErrorCategory::Internal, "Empty memory size string");
    }
    size_t end = str.find_last_not_of(" \t\r\n");
    std::string_view trimmed = str.substr(start, end - start + 1);

    size_t num_end = 0;
    while (num_end < trimmed.size() && (std::isdigit(static_cast<unsigned char>(trimmed[num_end])) || trimmed[num_end] == '.')) {
        num_end++;
    }

    if (num_end == 0) {
        throw AxiomException(ErrorCategory::Internal, "Invalid memory size format: " + std::string(str));
    }

    double val = std::stod(std::string(trimmed.substr(0, num_end)));
    std::string unit;
    for (size_t i = num_end; i < trimmed.size(); ++i) {
        if (!std::isspace(static_cast<unsigned char>(trimmed[i]))) {
            unit += static_cast<char>(std::toupper(static_cast<unsigned char>(trimmed[i])));
        }
    }

    size_t multiplier = 1;
    if (unit == "B" || unit.empty()) {
        multiplier = 1;
    } else if (unit == "K" || unit == "KB") {
        multiplier = 1024ULL;
    } else if (unit == "M" || unit == "MB") {
        multiplier = 1024ULL * 1024ULL;
    } else if (unit == "G" || unit == "GB") {
        multiplier = 1024ULL * 1024ULL * 1024ULL;
    } else if (unit == "T" || unit == "TB") {
        multiplier = 1024ULL * 1024ULL * 1024ULL * 1024ULL;
    } else {
        throw AxiomException(ErrorCategory::Internal, "Unknown memory unit: " + unit + " in " + std::string(str));
    }

    return static_cast<size_t>(val * static_cast<double>(multiplier));
}

std::string format_memory_size(size_t bytes) {
    constexpr double KB = 1024.0;
    constexpr double MB = KB * 1024.0;
    constexpr double GB = MB * 1024.0;

    std::ostringstream oss;
    oss << std::fixed << std::setprecision(2);
    if (bytes >= static_cast<size_t>(GB)) {
        oss << (static_cast<double>(bytes) / GB) << " GB";
    } else if (bytes >= static_cast<size_t>(MB)) {
        oss << (static_cast<double>(bytes) / MB) << " MB";
    } else if (bytes >= static_cast<size_t>(KB)) {
        oss << (static_cast<double>(bytes) / KB) << " KB";
    } else {
        oss << bytes << " B";
    }
    return oss.str();
}

// MemoryReservation implementation
MemoryReservation::MemoryReservation(std::shared_ptr<QueryMemoryManager> manager, size_t initial_bytes)
    : manager_(std::move(manager)), reserved_bytes_(0) {
    if (initial_bytes > 0 && manager_) {
        reserve_or_throw(initial_bytes, "MemoryReservation::init");
    }
}

MemoryReservation::~MemoryReservation() {
    release_all();
}

MemoryReservation::MemoryReservation(MemoryReservation&& other) noexcept
    : manager_(std::move(other.manager_)),
      reserved_bytes_(other.reserved_bytes_) {
    other.reserved_bytes_ = 0;
}

MemoryReservation& MemoryReservation::operator=(MemoryReservation&& other) noexcept {
    if (this != &other) {
        release_all();
        manager_ = std::move(other.manager_);
        reserved_bytes_ = other.reserved_bytes_;
        other.reserved_bytes_ = 0;
    }
    return *this;
}

bool MemoryReservation::try_reserve(size_t additional_bytes) {
    if (additional_bytes == 0) return true;
    if (!manager_) {
        reserved_bytes_ += additional_bytes;
        return true;
    }
    if (manager_->try_allocate(additional_bytes)) {
        reserved_bytes_ += additional_bytes;
        return true;
    }
    return false;
}

void MemoryReservation::reserve_or_throw(size_t additional_bytes, std::string_view op_name) {
    if (additional_bytes == 0) return;
    if (!manager_) {
        reserved_bytes_ += additional_bytes;
        return;
    }
    manager_->allocate_or_throw(additional_bytes, op_name);
    reserved_bytes_ += additional_bytes;
}

void MemoryReservation::release(size_t bytes) {
    if (bytes == 0) return;
    size_t to_release = std::min(bytes, reserved_bytes_);
    reserved_bytes_ -= to_release;
    if (manager_) {
        manager_->deallocate(to_release);
    }
}

void MemoryReservation::release_all() {
    if (reserved_bytes_ > 0) {
        if (manager_) {
            manager_->deallocate(reserved_bytes_);
        }
        reserved_bytes_ = 0;
    }
}

// QueryMemoryManager implementation
QueryMemoryManager::QueryMemoryManager(size_t max_budget_bytes)
    : max_budget_(max_budget_bytes) {}

size_t QueryMemoryManager::remaining_budget() const noexcept {
    if (max_budget_ == 0) return std::numeric_limits<size_t>::max();
    size_t cur = current_bytes_.load(std::memory_order_relaxed);
    return (cur >= max_budget_) ? 0 : (max_budget_ - cur);
}

bool QueryMemoryManager::try_allocate(size_t bytes) {
    if (bytes == 0) return true;
    if (max_budget_ == 0) {
        size_t prev = current_bytes_.fetch_add(bytes, std::memory_order_relaxed);
        size_t new_val = prev + bytes;
        size_t cur_peak = peak_bytes_.load(std::memory_order_relaxed);
        while (new_val > cur_peak && !peak_bytes_.compare_exchange_weak(cur_peak, new_val, std::memory_order_relaxed)) {}
        return true;
    }

    size_t current = current_bytes_.load(std::memory_order_relaxed);
    while (true) {
        if (current + bytes > max_budget_) {
            return false;
        }
        if (current_bytes_.compare_exchange_weak(current, current + bytes, std::memory_order_relaxed)) {
            size_t new_val = current + bytes;
            size_t cur_peak = peak_bytes_.load(std::memory_order_relaxed);
            while (new_val > cur_peak && !peak_bytes_.compare_exchange_weak(cur_peak, new_val, std::memory_order_relaxed)) {}
            return true;
        }
    }
}

void QueryMemoryManager::allocate_or_throw(size_t bytes, std::string_view op_name) {
    if (!try_allocate(bytes)) {
        throw AxiomException(
            ErrorCategory::Execution,
            "Query memory limit exceeded in operator '" + std::string(op_name) +
            "': requested " + format_memory_size(bytes) +
            ", current " + format_memory_size(current_bytes_.load(std::memory_order_relaxed)) +
            ", limit " + format_memory_size(max_budget_)
        );
    }
}

void QueryMemoryManager::deallocate(size_t bytes) noexcept {
    if (bytes == 0) return;
    size_t current = current_bytes_.load(std::memory_order_relaxed);
    while (true) {
        size_t new_val = (bytes >= current) ? 0 : (current - bytes);
        if (current_bytes_.compare_exchange_weak(current, new_val, std::memory_order_relaxed)) {
            break;
        }
    }
}

std::unique_ptr<MemoryReservation> QueryMemoryManager::create_reservation(size_t initial_bytes) {
    return std::make_unique<MemoryReservation>(shared_from_this(), initial_bytes);
}

} // namespace axiom

