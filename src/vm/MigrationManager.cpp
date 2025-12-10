#include "MigrationManager.h"
#include <thread>
#include <chrono>
#include <cstring>

namespace uvm_sim
{

    MigrationManager::MigrationManager(PageTable *page_table, const Config &config)
        : page_table_(page_table), config_(config)
    {
        if (config_.async_migration)
        {
            for (size_t i = 0; i < config_.max_concurrent_migrations; i++)
            {
                migration_workers_.emplace_back(&MigrationManager::migration_worker_thread, this);
            }
        }
    }

    MigrationManager::~MigrationManager()
    {
        shutdown_ = true;
        queue_cv_.notify_all();
        for (auto &worker : migration_workers_)
        {
            if (worker.joinable())
            {
                worker.join();
            }
        }
    }

    uint64_t MigrationManager::migrate_cpu_to_gpu(VirtualPageNumber vpn, void *cpu_addr,
                                                  uint64_t gpu_addr, size_t page_size)
    {
        if (!cpu_addr)
            return 0;

        
        uint64_t start_us = get_timestamp_us();

        
        
        
        if (!page_table_->lookup_entry(vpn))
        {
            
            return 0;
        }

        
        uint64_t estimated_us = page_size / 1000; 

        std::this_thread::sleep_for(std::chrono::microseconds(1)); 

        uint64_t end_us = get_timestamp_us();
        uint64_t actual_time_us = end_us - start_us;

        if (page_table_)
        {
            auto entry = page_table_->lookup_entry(vpn);
            if (entry)
            {
                entry->resident_on_gpu = true;
                entry->gpu_address = gpu_addr;
                entry->is_dirty = false;
            }
        }

        LOG_DEBUG("Migrated page VPN=%lu CPU->GPU (%zu bytes) in %lu us", vpn, page_size, actual_time_us);
        return actual_time_us;
    }

    uint64_t MigrationManager::migrate_gpu_to_cpu(VirtualPageNumber vpn, uint64_t gpu_addr,
                                                  void *cpu_addr, size_t page_size)
    {
        if (!cpu_addr || !gpu_addr)
            return 0;

        uint64_t start_us = get_timestamp_us();

        
        

        if (page_table_)
        {
            auto entry = page_table_->lookup_entry(vpn);
            if (entry)
            {
                entry->resident_on_cpu = true;
                entry->cpu_address = cpu_addr;
            }
        }

        std::this_thread::sleep_for(std::chrono::microseconds(1));

        uint64_t end_us = get_timestamp_us();
        uint64_t actual_time_us = end_us - start_us;

        LOG_DEBUG("Migrated page VPN=%lu GPU->CPU (%zu bytes) in %lu us", vpn, page_size, actual_time_us);
        return actual_time_us;
    }

    void MigrationManager::async_migrate_cpu_to_gpu(VirtualPageNumber vpn, void *cpu_addr,
                                                    uint64_t gpu_addr, size_t page_size)
    {
        auto migration_fn = [this, vpn, cpu_addr, gpu_addr, page_size]()
        {
            migrate_cpu_to_gpu(vpn, cpu_addr, gpu_addr, page_size);
        };

        {
            std::lock_guard<std::mutex> lock(queue_mutex_);
            migration_queue_.push({vpn, migration_fn});
        }
        queue_cv_.notify_one();
    }

    void MigrationManager::async_migrate_gpu_to_cpu(VirtualPageNumber vpn, uint64_t gpu_addr,
                                                    void *cpu_addr, size_t page_size)
    {
        auto migration_fn = [this, vpn, gpu_addr, cpu_addr, page_size]()
        {
            migrate_gpu_to_cpu(vpn, gpu_addr, cpu_addr, page_size);
        };

        {
            std::lock_guard<std::mutex> lock(queue_mutex_);
            migration_queue_.push({vpn, migration_fn});
        }
        queue_cv_.notify_one();
    }

    void MigrationManager::wait_for_migrations()
    {
        
        while (true)
        {
            {
                std::lock_guard<std::mutex> lock(queue_mutex_);
                if (migration_queue_.empty())
                {
                    break;
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }

    size_t MigrationManager::get_pending_migrations() const
    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        return migration_queue_.size();
    }

    void MigrationManager::migration_worker_thread()
    {
        while (!shutdown_)
        {
            std::pair<VirtualPageNumber, std::function<void()>> migration_job;

            {
                std::unique_lock<std::mutex> lock(queue_mutex_);
                queue_cv_.wait(lock, [this]()
                               { return !migration_queue_.empty() || shutdown_; });

                if (shutdown_ && migration_queue_.empty())
                {
                    break;
                }

                if (!migration_queue_.empty())
                {
                    migration_job = migration_queue_.front();
                    migration_queue_.pop();
                }
                else
                {
                    continue;
                }
            }

            
            if (migration_job.second)
            {
                migration_job.second();
            }
        }
    }

} 
