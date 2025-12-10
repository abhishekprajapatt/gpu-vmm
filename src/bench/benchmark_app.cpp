

#include "../vm/VirtualMemoryManager.h"
#include <iostream>
#include <iomanip>
#include <vector>
#include <cmath>
#include <cstring>
#include <fstream>
#include <chrono>
#include <random>

using namespace uvm_sim;

struct BenchmarkResult
{
    std::string name;
    size_t working_set_size;
    size_t gpu_memory;
    uint64_t page_faults;
    uint64_t migrations;
    uint64_t migrated_bytes;
    uint64_t total_time_us;
    double throughput_pages_per_sec;
    double fault_rate_per_second;
};

BenchmarkResult bench_random_page_access(size_t working_set_size, size_t num_accesses,
                                         size_t gpu_memory_limit)
{
    BenchmarkResult result;
    result.name = "Random Page Access";
    result.working_set_size = working_set_size;
    result.gpu_memory = gpu_memory_limit;

    
    VMConfig config;
    config.page_size = 64 * 1024; 
    config.gpu_memory = gpu_memory_limit;
    config.replacement_policy = PageReplacementPolicy::LRU;
    config.use_gpu_simulator = true; 
    config.log_level = LogLevel::INFO;

    VirtualMemoryManager &vm = VirtualMemoryManager::instance();
    vm.initialize(config);

    
    void *vaddr = vm.allocate(working_set_size);
    if (!vaddr)
    {
        std::cerr << "Failed to allocate virtual memory" << std::endl;
        return result;
    }

    
    size_t num_pages = working_set_size / config.page_size;
    std::vector<uint32_t> page_indices(num_pages);
    for (size_t i = 0; i < num_pages; i++)
    {
        page_indices[i] = i;
    }

    
    for (size_t i = 0; i < std::min(num_accesses / 10, size_t(1000)); i++)
    {
        vm.touch_page((uint8_t *)vaddr + (rand() % working_set_size));
    }

    
    vm.reset_counters();

    
    auto bench_start = std::chrono::high_resolution_clock::now();

    std::mt19937 rng(42);
    std::uniform_int_distribution<> page_dist(0, num_pages - 1);

    for (size_t i = 0; i < num_accesses; i++)
    {
        uint32_t page_idx = page_dist(rng);
        void *page_addr = (uint8_t *)vaddr + (page_idx * config.page_size);
        vm.touch_page(page_addr, i % 2 == 0); 
    }

    auto bench_end = std::chrono::high_resolution_clock::now();
    uint64_t elapsed_us = std::chrono::duration_cast<std::chrono::microseconds>(
                              bench_end - bench_start)
                              .count();

    
    const auto &perf = vm.get_perf_counters();
    result.page_faults = perf.total_page_faults;
    result.migrations = perf.cpu_to_gpu_migrations + perf.gpu_to_cpu_migrations;
    result.migrated_bytes = perf.total_bytes_migrated;
    result.total_time_us = elapsed_us;
    result.throughput_pages_per_sec = (num_accesses * 1e6) / elapsed_us;
    result.fault_rate_per_second = (result.page_faults * 1e6) / elapsed_us;

    vm.free(vaddr);
    vm.shutdown();

    return result;
}

BenchmarkResult bench_sequential_access(size_t working_set_size, size_t num_passes)
{
    BenchmarkResult result;
    result.name = "Sequential Page Access";
    result.working_set_size = working_set_size;
    result.gpu_memory = 4UL * 1024 * 1024 * 1024;

    VMConfig config;
    config.page_size = 64 * 1024;
    config.gpu_memory = 4UL * 1024 * 1024 * 1024;
    config.replacement_policy = PageReplacementPolicy::LRU;
    config.use_gpu_simulator = true;
    config.log_level = LogLevel::INFO;

    VirtualMemoryManager &vm = VirtualMemoryManager::instance();
    vm.initialize(config);

    void *vaddr = vm.allocate(working_set_size);
    if (!vaddr)
    {
        std::cerr << "Failed to allocate memory" << std::endl;
        return result;
    }

    size_t num_pages = working_set_size / config.page_size;

    vm.reset_counters();

    auto bench_start = std::chrono::high_resolution_clock::now();

    
    for (size_t pass = 0; pass < num_passes; pass++)
    {
        for (size_t page = 0; page < num_pages; page++)
        {
            void *page_addr = (uint8_t *)vaddr + (page * config.page_size);
            vm.touch_page(page_addr);
        }
    }

    auto bench_end = std::chrono::high_resolution_clock::now();
    uint64_t elapsed_us = std::chrono::duration_cast<std::chrono::microseconds>(
                              bench_end - bench_start)
                              .count();

    const auto &perf = vm.get_perf_counters();
    result.page_faults = perf.total_page_faults;
    result.migrations = perf.cpu_to_gpu_migrations + perf.gpu_to_cpu_migrations;
    result.migrated_bytes = perf.total_bytes_migrated;
    result.total_time_us = elapsed_us;
    result.throughput_pages_per_sec = (num_pages * num_passes * 1e6) / elapsed_us;
    result.fault_rate_per_second = (result.page_faults * 1e6) / elapsed_us;

    vm.free(vaddr);
    vm.shutdown();

    return result;
}

BenchmarkResult bench_working_set_overflow(size_t working_set_size)
{
    BenchmarkResult result;
    result.name = "Working Set Overflow";
    result.working_set_size = working_set_size;
    result.gpu_memory = 512 * 1024 * 1024; 

    VMConfig config;
    config.page_size = 64 * 1024;
    config.gpu_memory = 512 * 1024 * 1024; 
    config.replacement_policy = PageReplacementPolicy::CLOCK;
    config.use_gpu_simulator = true;
    config.log_level = LogLevel::INFO;

    VirtualMemoryManager &vm = VirtualMemoryManager::instance();
    vm.initialize(config);

    void *vaddr = vm.allocate(working_set_size);
    if (!vaddr)
        return result;

    
    size_t num_pages = working_set_size / config.page_size;
    vm.reset_counters();

    auto bench_start = std::chrono::high_resolution_clock::now();

    
    for (size_t i = 0; i < num_pages && i < 1000; i++)
    { 
        void *page_addr = (uint8_t *)vaddr + (i * config.page_size);
        vm.prefetch_to_gpu(page_addr);
    }

    auto bench_end = std::chrono::high_resolution_clock::now();
    uint64_t elapsed_us = std::chrono::duration_cast<std::chrono::microseconds>(
                              bench_end - bench_start)
                              .count();

    const auto &perf = vm.get_perf_counters();
    result.page_faults = perf.total_page_faults;
    result.migrations = perf.cpu_to_gpu_migrations + perf.gpu_to_cpu_migrations;
    result.migrated_bytes = perf.total_bytes_migrated;
    result.total_time_us = elapsed_us;
    result.throughput_pages_per_sec = (1000 * 1e6) / elapsed_us;
    result.fault_rate_per_second = (perf.evictions * 1e6) / elapsed_us;

    vm.free(vaddr);
    vm.shutdown();

    return result;
}

void print_benchmark_header()
{
    std::cout << "\n"
              << std::string(100, '=') << std::endl;
    std::cout << "GPU Virtual Memory Subsystem Benchmark Results" << std::endl;
    std::cout << std::string(100, '=') << std::endl;
}

void print_benchmark_result(const BenchmarkResult &result)
{
    std::cout << "\nBenchmark: " << result.name << std::endl;
    std::cout << std::string(80, '-') << std::endl;
    std::cout << "Working Set Size:        " << std::fixed << (result.working_set_size / (1024.0 * 1024.0))
              << " MB" << std::endl;
    std::cout << "GPU Memory:              " << (result.gpu_memory / (1024.0 * 1024.0)) << " MB" << std::endl;
    std::cout << "Total Time:              " << result.total_time_us / 1000.0 << " ms" << std::endl;
    std::cout << "Page Faults:             " << result.page_faults << std::endl;
    std::cout << "Migrations:              " << result.migrations << std::endl;
    std::cout << "Total Bytes Migrated:    " << std::fixed << std::setprecision(2)
              << (result.migrated_bytes / (1024.0 * 1024.0)) << " MB" << std::endl;

    if (result.migrated_bytes > 0)
    {
        double bw = (result.migrated_bytes / 1e6) / (result.total_time_us / 1e6);
        std::cout << "Migration Bandwidth:     " << bw << " GB/s" << std::endl;
    }

    std::cout << "Throughput:              " << std::setprecision(0)
              << result.throughput_pages_per_sec << " pages/sec" << std::endl;
    std::cout << "Fault Rate:              " << std::fixed << std::setprecision(1)
              << result.fault_rate_per_second << " faults/sec" << std::endl;
}

void save_results_to_csv(const std::vector<BenchmarkResult> &results, const std::string &filename)
{
    std::ofstream csv(filename);
    if (!csv.is_open())
    {
        std::cerr << "Failed to open CSV file: " << filename << std::endl;
        return;
    }

    
    csv << "Benchmark,Working_Set_MB,GPU_Memory_MB,Page_Faults,Migrations,"
        << "Migrated_MB,Total_Time_us,Throughput_pages_sec,Fault_Rate_per_sec\n";

    
    for (const auto &result : results)
    {
        csv << result.name << ","
            << (result.working_set_size / (1024.0 * 1024.0)) << ","
            << (result.gpu_memory / (1024.0 * 1024.0)) << ","
            << result.page_faults << ","
            << result.migrations << ","
            << (result.migrated_bytes / (1024.0 * 1024.0)) << ","
            << result.total_time_us << ","
            << result.throughput_pages_per_sec << ","
            << result.fault_rate_per_second << "\n";
    }

    csv.close();
    std::cout << "\nResults saved to: " << filename << std::endl;
}

int main(int argc, char **argv)
{
    print_benchmark_header();

    std::vector<BenchmarkResult> results;

    
    std::cout << "\nRunning Random Access Benchmark (512 MB working set, 10K accesses)..." << std::endl;
    results.push_back(bench_random_page_access(512UL * 1024 * 1024, 10000, 4UL * 1024 * 1024 * 1024));

    std::cout << "\nRunning Sequential Access Benchmark (256 MB, 4 passes)..." << std::endl;
    results.push_back(bench_sequential_access(256UL * 1024 * 1024, 4));

    std::cout << "\nRunning Working Set Overflow Benchmark (1 GB > 512 MB GPU)..." << std::endl;
    results.push_back(bench_working_set_overflow(1UL * 1024 * 1024 * 1024));

    
    for (const auto &result : results)
    {
        print_benchmark_result(result);
    }

    
    save_results_to_csv(results, "benchmark_results.csv");

    std::cout << "\n"
              << std::string(100, '=') << std::endl;
    std::cout << "Benchmark completed successfully!" << std::endl;

    return 0;
}
