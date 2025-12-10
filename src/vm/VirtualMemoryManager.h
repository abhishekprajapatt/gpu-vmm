#pragma once

#include "Common.h"
#include "PageTable.h"
#include "PageAllocator.h"
#include "TLB.h"
#include "MigrationManager.h"
#include "Policies.h"
#include <memory>
#include <thread>

namespace uvm_sim
{

    
    
    

    struct VMConfig
    {
        size_t page_size = DEFAULT_PAGE_SIZE;
        size_t virtual_address_space = DEFAULT_VIRTUAL_ADDRESS_SPACE;
        size_t gpu_memory = DEFAULT_GPU_MEMORY;
        size_t tlb_size = DEFAULT_TLB_SIZE;
        size_t tlb_associativity = DEFAULT_TLB_ASSOCIATIVITY;
        PageReplacementPolicy replacement_policy = PageReplacementPolicy::LRU;
        bool use_pinned_memory = true;
        bool use_gpu_simulator = false;
        bool enable_prefetch = true;
        LogLevel log_level = LogLevel::INFO;
    };

    
    
    

    class VirtualMemoryManager
    {
    public:
        
        static VirtualMemoryManager &instance();

        
        void initialize(const VMConfig &config = VMConfig());

        
        void shutdown();

        
        
        

        
        void *allocate(size_t bytes, bool prefetch_to_gpu = false);

        
        void free(void *vaddr);

        
        
        

        
        void map_to_cpu(void *vaddr, bool prefetch = false);
        void map_to_gpu(void *vaddr);

        
        void prefetch_to_gpu(void *vaddr);

        
        void touch_page(void *vaddr, bool is_write = false);

        
        void read_from_vaddr(void *vaddr, void *buffer, size_t bytes);

        
        void write_to_vaddr(void *vaddr, const void *buffer, size_t bytes);

        
        
        

        
        void sync_all_migrations();

        
        
        

        PerfCounters &get_perf_counters() { return perf_counters_; }

        void print_stats() const;
        void reset_counters() { perf_counters_.reset(); }

        
        size_t get_gpu_pages_used() const;
        size_t get_gpu_pages_available() const;
        size_t get_cpu_pages_used() const;

        
        
        

        PageTable *get_page_table() { return page_table_.get(); }
        PageAllocator *get_allocator() { return allocator_.get(); }
        TLB *get_tlb() { return tlb_.get(); }

    private:
        VirtualMemoryManager() : initialized_(false) {}
        ~VirtualMemoryManager();

        
        VirtualMemoryManager(const VirtualMemoryManager &) = delete;
        VirtualMemoryManager &operator=(const VirtualMemoryManager &) = delete;

        
        
        

        
        void resolve_page_fault(VirtualPageNumber vpn, bool access_gpu);

        
        void evict_page_from_gpu();

        
        VirtualPageNumber get_next_vpn();

        
        void handle_cpu_access(VirtualPageNumber vpn);
        void handle_gpu_access(VirtualPageNumber vpn);

        
        
        

        bool initialized_;
        VMConfig config_;
        PerfCounters perf_counters_;

        std::unique_ptr<PageTable> page_table_;
        std::unique_ptr<PageAllocator> allocator_;
        std::unique_ptr<TLB> tlb_;
        std::unique_ptr<MigrationManager> migration_manager_;
        std::unique_ptr<ReplacementPolicy> replacement_policy_;

        
        VirtualPageNumber next_vpn_;
        std::unordered_map<Address, VirtualPageNumber> vaddr_to_vpn_map_;
        std::unordered_set<VirtualPageNumber> gpu_resident_pages_;

        
        mutable std::shared_mutex manager_mutex_;
        std::mutex alloc_mutex_;
    };

    
    
    

    template <typename T>
    class DeviceMapped
    {
    public:
        DeviceMapped(size_t count, bool gpu_resident = false)
            : ptr_(nullptr), count_(count)
        {
            size_t bytes = count * sizeof(T);
            ptr_ = static_cast<T *>(VirtualMemoryManager::instance().allocate(bytes, gpu_resident));
            if (!ptr_)
            {
                throw std::runtime_error("Failed to allocate virtual memory");
            }
        }

        ~DeviceMapped()
        {
            if (ptr_)
            {
                VirtualMemoryManager::instance().free(ptr_);
            }
        }

        
        DeviceMapped(const DeviceMapped &) = delete;
        DeviceMapped &operator=(const DeviceMapped &) = delete;

        
        DeviceMapped(DeviceMapped &&other) noexcept : ptr_(other.ptr_), count_(other.count_)
        {
            other.ptr_ = nullptr;
            other.count_ = 0;
        }

        T *get() { return ptr_; }
        const T *get() const { return ptr_; }
        T *data() { return ptr_; }
        const T *data() const { return ptr_; }
        size_t size() const { return count_; }

        T &operator[](size_t idx)
        {
            assert(idx < count_);
            return ptr_[idx];
        }

        const T &operator[](size_t idx) const
        {
            assert(idx < count_);
            return ptr_[idx];
        }

    private:
        T *ptr_;
        size_t count_;
    };

} 
