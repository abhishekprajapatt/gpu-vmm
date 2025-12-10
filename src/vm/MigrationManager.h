#pragma once

#include "Common.h"
#include "PageTable.h"

namespace uvm_sim
{
    class MigrationManager
    {
    public:
        struct Config
        {
            bool async_migration = true;
            size_t max_concurrent_migrations = 4;
            
        };

        explicit MigrationManager(PageTable *page_table, const Config &config = Config());
        ~MigrationManager();

        
        uint64_t migrate_cpu_to_gpu(VirtualPageNumber vpn, void *cpu_addr, uint64_t gpu_addr, size_t page_size);

        
        uint64_t migrate_gpu_to_cpu(VirtualPageNumber vpn, uint64_t gpu_addr, void *cpu_addr, size_t page_size);

        
        void async_migrate_cpu_to_gpu(VirtualPageNumber vpn, void *cpu_addr, uint64_t gpu_addr, size_t page_size);
        void async_migrate_gpu_to_cpu(VirtualPageNumber vpn, uint64_t gpu_addr, void *cpu_addr, size_t page_size);

        
        void wait_for_migrations();

        
        size_t get_pending_migrations() const;

    private:
        PageTable *page_table_;
        Config config_;
        std::vector<std::thread> migration_workers_;
        std::queue<std::pair<VirtualPageNumber, std::function<void()>>> migration_queue_;
        std::mutex queue_mutex_;
        std::condition_variable queue_cv_;
        std::atomic<bool> shutdown_{false};

        void migration_worker_thread();
    };

} 
