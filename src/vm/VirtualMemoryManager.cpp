#include "VirtualMemoryManager.h"
#include <cstring>
#include <numeric>

namespace uvm_sim
{

    VirtualMemoryManager &VirtualMemoryManager::instance()
    {
        static VirtualMemoryManager instance;
        return instance;
    }

    void VirtualMemoryManager::initialize(const VMConfig &config)
    {
        std::unique_lock<std::shared_mutex> lock(manager_mutex_);

        if (initialized_)
        {
            LOG_WARN("VirtualMemoryManager already initialized");
            return;
        }

        config_ = config;
        Logger::instance().set_level(config_.log_level);

        LOG_INFO("Initializing VirtualMemoryManager with config:");
        LOG_INFO("  Page size: %zu bytes", config_.page_size);
        LOG_INFO("  Virtual address space: %zu bytes", config_.virtual_address_space);
        LOG_INFO("  GPU memory: %zu bytes", config_.gpu_memory);
        LOG_INFO("  TLB size: %zu entries", config_.tlb_size);
        LOG_INFO("  Replacement policy: %s", config_.replacement_policy == PageReplacementPolicy::LRU ? "LRU" : "CLOCK");
        LOG_INFO("  GPU simulator mode: %s", config_.use_gpu_simulator ? "ON" : "OFF");

        
        page_table_ = std::make_unique<PageTable>(config_.page_size);
        page_table_->initialize(config_.virtual_address_space);

        PageAllocator::Config alloc_config;
        alloc_config.page_size = config_.page_size;
        alloc_config.cpu_page_pool_size = config_.gpu_memory; 
        alloc_config.gpu_page_pool_size = config_.gpu_memory;
        alloc_config.use_pinned_memory = config_.use_pinned_memory;
        alloc_config.use_gpu_simulator = config_.use_gpu_simulator;

        allocator_ = std::make_unique<PageAllocator>(alloc_config);
        allocator_->initialize();

        TLB::Config tlb_config;
        tlb_config.tlb_size = config_.tlb_size;
        tlb_config.associativity = config_.tlb_associativity;

        tlb_ = std::make_unique<TLB>(tlb_config);
        tlb_->initialize();

        MigrationManager::Config mig_config;
        mig_config.async_migration = true;
        mig_config.max_concurrent_migrations = 4;

        migration_manager_ = std::make_unique<MigrationManager>(page_table_.get(), mig_config);

        
        if (config_.replacement_policy == PageReplacementPolicy::LRU)
        {
            replacement_policy_ = std::make_unique<LRUPolicy>(65536);
        }
        else
        {
            replacement_policy_ = std::make_unique<CLOCKPolicy>(65536);
        }

        next_vpn_ = 0;
        initialized_ = true;

        LOG_INFO("VirtualMemoryManager initialized successfully");
    }

    void VirtualMemoryManager::shutdown()
    {
        std::unique_lock<std::shared_mutex> lock(manager_mutex_);

        if (!initialized_)
        {
            return;
        }

        LOG_INFO("Shutting down VirtualMemoryManager");

        migration_manager_.reset();
        replacement_policy_.reset();
        tlb_.reset();
        allocator_.reset();
        page_table_.reset();

        vaddr_to_vpn_map_.clear();
        gpu_resident_pages_.clear();

        initialized_ = false;
        LOG_INFO("VirtualMemoryManager shutdown complete");
    }

    void *VirtualMemoryManager::allocate(size_t bytes, bool prefetch_to_gpu)
    {
        std::unique_lock<std::shared_mutex> lock(manager_mutex_);

        if (!initialized_)
        {
            LOG_ERROR("VirtualMemoryManager not initialized");
            return nullptr;
        }

        size_t aligned_size = align_to_page(bytes, config_.page_size);
        uint32_t num_pages = aligned_size / config_.page_size;
        VirtualPageNumber vpn_start = next_vpn_;

        
        if (!page_table_->allocate_vpn_range(vpn_start, num_pages))
        {
            LOG_ERROR("Failed to allocate VPN range");
            return nullptr;
        }

        
        std::vector<void *> cpu_pages;
        for (uint32_t i = 0; i < num_pages; i++)
        {
            void *cpu_page = allocator_->allocate_cpu_page();
            if (!cpu_page)
            {
                LOG_ERROR("Failed to allocate CPU page");
                for (auto p : cpu_pages)
                {
                    allocator_->deallocate_cpu_page(p);
                }
                page_table_->deallocate_vpn_range(vpn_start, num_pages);
                return nullptr;
            }
            cpu_pages.push_back(cpu_page);

            VirtualPageNumber vpn = vpn_start + i;
            page_table_->set_cpu_resident(vpn, cpu_page);
            page_table_->update_access_time(vpn);

            replacement_policy_->on_page_allocated(vpn);
        }

        
        if (prefetch_to_gpu)
        {
            for (uint32_t i = 0; i < num_pages; i++)
            {
                uint64_t gpu_addr = allocator_->allocate_gpu_page();
                if (gpu_addr == 0)
                {
                    LOG_WARN("Failed to allocate GPU page %u", i);
                    continue;
                }

                VirtualPageNumber vpn = vpn_start + i;
                page_table_->set_gpu_resident(vpn, gpu_addr);
                gpu_resident_pages_.insert(vpn);

                
                uint64_t mig_time =
                    migration_manager_->migrate_cpu_to_gpu(vpn, cpu_pages[i], gpu_addr, config_.page_size);
                perf_counters_.cpu_to_gpu_migrations++;
                perf_counters_.total_bytes_migrated += config_.page_size;
                perf_counters_.total_migration_time_us += mig_time;

                perf_counters_.page_prefetches++;
            }
        }

        
        Address vaddr = vpn_to_vaddr(vpn_start, config_.page_size);
        vaddr_to_vpn_map_[vaddr] = vpn_start;

        next_vpn_ += num_pages;

        LOG_DEBUG("Allocated virtual memory: vaddr=%p, size=%zu bytes, num_pages=%u", (void *)vaddr, bytes, num_pages);

        return (void *)vaddr;
    }

    void VirtualMemoryManager::free(void *vaddr)
    {
        std::unique_lock<std::shared_mutex> lock(manager_mutex_);

        if (!initialized_)
        {
            LOG_ERROR("VirtualMemoryManager not initialized");
            return;
        }

        Address addr = (Address)vaddr;
        auto it = vaddr_to_vpn_map_.find(addr);
        if (it == vaddr_to_vpn_map_.end())
        {
            LOG_WARN("Freeing unmapped virtual address %p", vaddr);
            return;
        }

        VirtualPageNumber vpn_start = it->second;

        
        auto entries = page_table_->get_all_entries();
        uint32_t num_pages = 0;
        for (auto &entry : entries)
        {
            if (entry.first >= vpn_start && entry.second && entry.second->is_valid)
            {
                num_pages++;
            }
        }

        if (num_pages == 0)
        {
            num_pages = 1; 
        }

        
        for (uint32_t i = 0; i < num_pages; i++)
        {
            VirtualPageNumber vpn = vpn_start + i;
            auto entry = page_table_->lookup_entry(vpn);
            if (entry && entry->cpu_address)
            {
                allocator_->deallocate_cpu_page(entry->cpu_address);
            }
            if (entry && entry->gpu_address != 0)
            {
                allocator_->deallocate_gpu_page(entry->gpu_address);
            }

            gpu_resident_pages_.erase(vpn);
            replacement_policy_->on_page_freed(vpn);
            tlb_->invalidate(vpn);
        }

        page_table_->deallocate_vpn_range(vpn_start, num_pages);
        vaddr_to_vpn_map_.erase(it);

        LOG_DEBUG("Freed virtual memory: vaddr=%p, num_pages=%u", vaddr, num_pages);
    }

    void VirtualMemoryManager::map_to_cpu(void *vaddr, bool prefetch)
    {
        std::unique_lock<std::shared_mutex> lock(manager_mutex_);

        if (!initialized_)
            return;

        Address addr = (Address)vaddr;
        VirtualPageNumber vpn = vaddr_to_vpn(addr, config_.page_size);

        auto entry = page_table_->lookup_entry(vpn);
        if (!entry)
            return;

        
        if (!entry->resident_on_cpu)
        {
            resolve_page_fault(vpn, false); 
        }
    }

    void VirtualMemoryManager::map_to_gpu(void *vaddr)
    {
        std::unique_lock<std::shared_mutex> lock(manager_mutex_);

        if (!initialized_)
            return;

        Address addr = (Address)vaddr;
        VirtualPageNumber vpn = vaddr_to_vpn(addr, config_.page_size);

        auto entry = page_table_->lookup_entry(vpn);
        if (!entry)
            return;

        
        if (!entry->resident_on_gpu)
        {
            
            if (entry->gpu_address == 0)
            {
                entry->gpu_address = allocator_->allocate_gpu_page();
                if (entry->gpu_address == 0)
                {
                    evict_page_from_gpu();
                    entry->gpu_address = allocator_->allocate_gpu_page();
                }
            }

            
            if (entry->resident_on_cpu)
            {
                uint64_t mig_time =
                    migration_manager_->migrate_cpu_to_gpu(vpn, entry->cpu_address, entry->gpu_address, config_.page_size);
                perf_counters_.cpu_to_gpu_migrations++;
                perf_counters_.total_bytes_migrated += config_.page_size;
                perf_counters_.total_migration_time_us += mig_time;
            }

            entry->resident_on_gpu = true;
            gpu_resident_pages_.insert(vpn);
        }
    }

    void VirtualMemoryManager::prefetch_to_gpu(void *vaddr)
    {
        map_to_gpu(vaddr);
    }

    void VirtualMemoryManager::touch_page(void *vaddr, bool is_write)
    {
        std::unique_lock<std::shared_mutex> lock(manager_mutex_);

        if (!initialized_)
            return;

        Address addr = (Address)vaddr;
        VirtualPageNumber vpn = vaddr_to_vpn(addr, config_.page_size);

        auto entry = page_table_->lookup_entry(vpn);
        if (!entry)
        {
            perf_counters_.total_page_faults++;
            resolve_page_fault(vpn, false); 
            entry = page_table_->lookup_entry(vpn);
        }

        if (entry)
        {
            entry->access_timestamp_us = get_timestamp_us();
            entry->access_count++;
            if (is_write)
            {
                entry->is_dirty = true;
            }
            replacement_policy_->on_page_access(vpn);
        }
    }

    void VirtualMemoryManager::read_from_vaddr(void *vaddr, void *buffer, size_t bytes)
    {
        std::unique_lock<std::shared_mutex> lock(manager_mutex_);

        if (!initialized_ || !vaddr || !buffer)
            return;

        
        Address addr = (Address)vaddr;
        VirtualPageNumber vpn = vaddr_to_vpn(addr, config_.page_size);

        auto entry = page_table_->lookup_entry(vpn);
        if (!entry)
        {
            LOG_ERROR("Invalid virtual address");
            return;
        }

        if (!entry->resident_on_cpu)
        {
            resolve_page_fault(vpn, false);
            entry = page_table_->lookup_entry(vpn);
        }

        if (entry && entry->cpu_address)
        {
            std::memcpy(buffer, entry->cpu_address, bytes);
            entry->access_timestamp_us = get_timestamp_us();
        }
    }

    void VirtualMemoryManager::write_to_vaddr(void *vaddr, const void *buffer, size_t bytes)
    {
        std::unique_lock<std::shared_mutex> lock(manager_mutex_);

        if (!initialized_ || !vaddr || !buffer)
            return;

        Address addr = (Address)vaddr;
        VirtualPageNumber vpn = vaddr_to_vpn(addr, config_.page_size);

        auto entry = page_table_->lookup_entry(vpn);
        if (!entry)
        {
            LOG_ERROR("Invalid virtual address");
            return;
        }

        if (!entry->resident_on_cpu)
        {
            resolve_page_fault(vpn, false);
            entry = page_table_->lookup_entry(vpn);
        }

        if (entry && entry->cpu_address)
        {
            std::memcpy(entry->cpu_address, buffer, bytes);
            entry->is_dirty = true;
            entry->access_timestamp_us = get_timestamp_us();
        }
    }

    void VirtualMemoryManager::sync_all_migrations()
    {
        if (!initialized_)
            return;

        migration_manager_->wait_for_migrations();
        LOG_DEBUG("All migrations completed");
    }

    void VirtualMemoryManager::resolve_page_fault(VirtualPageNumber vpn, bool access_gpu)
    {
        auto entry = page_table_->lookup_entry(vpn);
        if (!entry)
        {
            LOG_ERROR("Page fault on invalid VPN %lu", vpn);
            return;
        }

        if (access_gpu)
        {
            
            if (!entry->resident_on_gpu)
            {
                if (entry->gpu_address == 0)
                {
                    entry->gpu_address = allocator_->allocate_gpu_page();
                    if (entry->gpu_address == 0)
                    {
                        evict_page_from_gpu();
                        entry->gpu_address = allocator_->allocate_gpu_page();
                    }
                }

                if (entry->resident_on_cpu)
                {
                    uint64_t mig_time = migration_manager_->migrate_cpu_to_gpu(
                        vpn, entry->cpu_address, entry->gpu_address, config_.page_size);
                    perf_counters_.cpu_to_gpu_migrations++;
                    perf_counters_.total_bytes_migrated += config_.page_size;
                    perf_counters_.total_migration_time_us += mig_time;
                }

                entry->resident_on_gpu = true;
                gpu_resident_pages_.insert(vpn);
            }
        }
        else
        {
            
            if (!entry->resident_on_cpu)
            {
                if (entry->cpu_address == nullptr)
                {
                    entry->cpu_address = allocator_->allocate_cpu_page();
                }

                if (entry->resident_on_gpu)
                {
                    uint64_t mig_time = migration_manager_->migrate_gpu_to_cpu(
                        vpn, entry->gpu_address, entry->cpu_address, config_.page_size);
                    perf_counters_.gpu_to_cpu_migrations++;
                    perf_counters_.total_bytes_migrated += config_.page_size;
                    perf_counters_.total_migration_time_us += mig_time;
                }

                entry->resident_on_cpu = true;
            }
        }
    }

    void VirtualMemoryManager::evict_page_from_gpu()
    {
        if (gpu_resident_pages_.empty())
            return;

        VirtualPageNumber victim = replacement_policy_->select_victim();
        if (victim == 0 && gpu_resident_pages_.size() > 0)
        {
            victim = *gpu_resident_pages_.begin();
        }

        if (victim == 0)
            return;

        auto entry = page_table_->lookup_entry(victim);
        if (entry)
        {
            if (entry->is_dirty && entry->resident_on_cpu)
            {
                
                uint64_t mig_time = migration_manager_->migrate_gpu_to_cpu(
                    victim, entry->gpu_address, entry->cpu_address, config_.page_size);
                perf_counters_.gpu_to_cpu_migrations++;
                perf_counters_.total_bytes_migrated += config_.page_size;
                perf_counters_.total_migration_time_us += mig_time;
            }

            allocator_->deallocate_gpu_page(entry->gpu_address);
            entry->gpu_address = 0;
            entry->resident_on_gpu = false;
            gpu_resident_pages_.erase(victim);
            perf_counters_.evictions++;
            tlb_->invalidate(victim);
        }
    }

    VirtualPageNumber VirtualMemoryManager::get_next_vpn()
    {
        return next_vpn_++;
    }

    size_t VirtualMemoryManager::get_gpu_pages_used() const
    {
        std::shared_lock<std::shared_mutex> lock(manager_mutex_);
        return gpu_resident_pages_.size();
    }

    size_t VirtualMemoryManager::get_gpu_pages_available() const
    {
        std::shared_lock<std::shared_mutex> lock(manager_mutex_);
        if (!allocator_)
            return 0;
        return allocator_->get_available_gpu_pages();
    }

    size_t VirtualMemoryManager::get_cpu_pages_used() const
    {
        std::shared_lock<std::shared_mutex> lock(manager_mutex_);
        if (!page_table_)
            return 0;
        return page_table_->get_num_allocated_pages();
    }

    void VirtualMemoryManager::print_stats() const
    {
        std::shared_lock<std::shared_mutex> lock(manager_mutex_);

        perf_counters_.print();

        if (tlb_)
        {
            std::cout << "\n=== TLB Statistics ===" << std::endl;
            std::cout << "TLB Hits:        " << tlb_->get_hits() << std::endl;
            std::cout << "TLB Misses:      " << tlb_->get_misses() << std::endl;
            std::cout << "TLB Hit Rate (%): " << std::fixed << std::setprecision(2)
                      << (tlb_->get_hit_rate() * 100.0) << std::endl;
        }

        if (allocator_)
        {
            std::cout << "\n=== Memory Usage ===" << std::endl;
            std::cout << "GPU Pages Used:    " << gpu_resident_pages_.size() << std::endl;
            std::cout << "GPU Pages Available: " << allocator_->get_available_gpu_pages() << std::endl;
        }
    }

    VirtualMemoryManager::~VirtualMemoryManager()
    {
        if (initialized_)
        {
            shutdown();
        }
    }

} 
