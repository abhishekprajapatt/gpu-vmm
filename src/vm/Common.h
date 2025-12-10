#pragma once

#include <cstdint>
#include <cstddef>
#include <chrono>
#include <memory>
#include <vector>
#include <unordered_map>
#include <queue>
#include <deque>
#include <mutex>
#include <shared_mutex>
#include <thread>
#include <condition_variable>
#include <atomic>
#include <cassert>
#include <iostream>
#include <iomanip>
#include <algorithm>

namespace uvm_sim
{

    using PageIndex = uint32_t;
    using VirtualPageNumber = uint64_t;
    using PhysicalPageNumber = uint32_t;
    using Address = uint64_t;

    constexpr size_t DEFAULT_PAGE_SIZE = 64 * 1024;
    constexpr size_t DEFAULT_VIRTUAL_ADDRESS_SPACE = 256UL * 1024 * 1024 * 1024;
    constexpr size_t DEFAULT_GPU_MEMORY = 4UL * 1024 * 1024 * 1024;
    constexpr size_t DEFAULT_TLB_SIZE = 1024;
    constexpr size_t DEFAULT_TLB_ASSOCIATIVITY = 8;
    constexpr uint32_t DEFAULT_GPU_POOL_SIZE = 65536;

    enum class PageResidency : uint8_t
    {
        CPU_ONLY = 0,
        GPU_ONLY = 1,
        BOTH = 2,
        UNALLOCATED = 3
    };

    enum class PageReplacementPolicy : uint8_t
    {
        LRU = 0,
        CLOCK = 1
    };

    enum class LogLevel : uint8_t
    {
        TRACE = 0,
        DEBUG = 1,
        INFO = 2,
        WARN = 3,
        ERROR = 4
    };

    struct PerfCounters
    {
        std::atomic<uint64_t> total_page_faults{0};
        std::atomic<uint64_t> cpu_to_gpu_migrations{0};
        std::atomic<uint64_t> gpu_to_cpu_migrations{0};
        std::atomic<uint64_t> total_bytes_migrated{0};
        std::atomic<uint64_t> total_migration_time_us{0};
        std::atomic<uint64_t> tlb_hits{0};
        std::atomic<uint64_t> tlb_misses{0};
        std::atomic<uint64_t> evictions{0};
        std::atomic<uint64_t> kernel_launches{0};
        std::atomic<uint64_t> page_prefetches{0};

        void reset()
        {
            total_page_faults = 0;
            cpu_to_gpu_migrations = 0;
            gpu_to_cpu_migrations = 0;
            total_bytes_migrated = 0;
            total_migration_time_us = 0;
            tlb_hits = 0;
            tlb_misses = 0;
            evictions = 0;
            kernel_launches = 0;
            page_prefetches = 0;
        }

        void print() const
        {
            std::cout << "=== Performance Counters ===" << std::endl;
            std::cout << "Page Faults:                  " << total_page_faults << std::endl;
            std::cout << "CPU->GPU Migrations:         " << cpu_to_gpu_migrations << std::endl;
            std::cout << "GPU->CPU Migrations:         " << gpu_to_cpu_migrations << std::endl;
            std::cout << "Total Bytes Migrated:        " << total_bytes_migrated << std::endl;
            std::cout << "Total Migration Time (us):   " << total_migration_time_us << std::endl;
            if (total_bytes_migrated > 0)
            {
                double avg_bw = (double)total_bytes_migrated / (double)total_migration_time_us;
                std::cout << "Migration Bandwidth (GB/s):  " << std::fixed << std::setprecision(2)
                          << (avg_bw * 1e6 / 1e9) << std::endl;
            }
            std::cout << "TLB Hits:                    " << tlb_hits << std::endl;
            std::cout << "TLB Misses:                  " << tlb_misses << std::endl;
            std::cout << "Total TLB Lookups:           " << (tlb_hits + tlb_misses) << std::endl;
            if ((tlb_hits + tlb_misses) > 0)
            {
                double hit_rate = (double)tlb_hits / (double)(tlb_hits + tlb_misses) * 100.0;
                std::cout << "TLB Hit Rate (%):            " << std::fixed << std::setprecision(2)
                          << hit_rate << std::endl;
            }
            std::cout << "Page Evictions:              " << evictions << std::endl;
            std::cout << "Kernel Launches:             " << kernel_launches << std::endl;
            std::cout << "Page Prefetches:             " << page_prefetches << std::endl;
        }
    };

    class Logger
    {
    public:
        static Logger &instance()
        {
            static Logger logger;
            return logger;
        }

        void set_level(LogLevel level) { level_ = level; }

        template <typename... Args>
        void log(LogLevel level, const char *format, Args... args)
        {
            if (level < level_)
                return;

            std::lock_guard<std::mutex> lock(mutex_);
            std::string level_str;
            switch (level)
            {
            case LogLevel::TRACE:
                level_str = "[TRACE]";
                break;
            case LogLevel::DEBUG:
                level_str = "[DEBUG]";
                break;
            case LogLevel::INFO:
                level_str = "[INFO]";
                break;
            case LogLevel::WARN:
                level_str = "[WARN]";
                break;
            case LogLevel::ERROR:
                level_str = "[ERROR]";
                break;
            }

            std::cout << level_str << " ";
            printf(format, args...);
            std::cout << std::endl;
        }

    private:
        Logger() : level_(LogLevel::INFO) {}
        LogLevel level_;
        std::mutex mutex_;
    };

#define LOG_TRACE(fmt, ...) uvm_sim::Logger::instance().log(uvm_sim::LogLevel::TRACE, fmt, ##__VA_ARGS__)
#define LOG_DEBUG(fmt, ...) uvm_sim::Logger::instance().log(uvm_sim::LogLevel::DEBUG, fmt, ##__VA_ARGS__)
#define LOG_INFO(fmt, ...) uvm_sim::Logger::instance().log(uvm_sim::LogLevel::INFO, fmt, ##__VA_ARGS__)
#define LOG_WARN(fmt, ...) uvm_sim::Logger::instance().log(uvm_sim::LogLevel::WARN, fmt, ##__VA_ARGS__)
#define LOG_ERROR(fmt, ...) uvm_sim::Logger::instance().log(uvm_sim::LogLevel::ERROR, fmt, ##__VA_ARGS__)

    inline VirtualPageNumber vaddr_to_vpn(Address vaddr, size_t page_size)
    {
        return vaddr / page_size;
    }

    inline Address vpn_to_vaddr(VirtualPageNumber vpn, size_t page_size)
    {
        return vpn * page_size;
    }

    inline size_t align_to_page(size_t size, size_t page_size)
    {
        return ((size + page_size - 1) / page_size) * page_size;
    }

    inline uint64_t get_timestamp_us()
    {
        return std::chrono::duration_cast<std::chrono::microseconds>(
                   std::chrono::high_resolution_clock::now().time_since_epoch())
            .count();
    }

    inline uint32_t hash_vpn(VirtualPageNumber vpn)
    {
        uint32_t hash = 2166136261u;
        for (int i = 0; i < 8; i++)
        {
            hash ^= (vpn >> (i * 8)) & 0xFF;
            hash *= 16777619u;
        }
        return hash;
    }

}
