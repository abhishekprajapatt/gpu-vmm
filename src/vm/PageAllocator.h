#pragma once

#include "Common.h"

namespace uvm_sim
{

    
    
    

    class PageAllocator
    {
    public:
        struct Config
        {
            size_t page_size = DEFAULT_PAGE_SIZE;
            size_t cpu_page_pool_size = 1024 * 1024 * 1024; 
            size_t gpu_page_pool_size = DEFAULT_GPU_MEMORY;
            bool use_pinned_memory = true;  
            bool use_gpu_simulator = false; 
        };

        explicit PageAllocator(const Config &config = Config());
        ~PageAllocator();

        
        void initialize();

        
        void *allocate_cpu_page();

        
        void deallocate_cpu_page(void *ptr);

        
        uint64_t allocate_gpu_page();

        
        void deallocate_gpu_page(uint64_t gpu_addr);

        
        size_t get_available_cpu_pages() const;

        
        size_t get_available_gpu_pages() const;

        
        size_t get_total_cpu_pages() const;

        
        size_t get_total_gpu_pages() const;

        
        size_t get_page_size() const { return config_.page_size; }

        
        bool is_simulator_mode() const { return config_.use_gpu_simulator; }

    private:
        Config config_;
        size_t cpu_pages_allocated_;
        size_t gpu_pages_allocated_;

        
        void *cpu_pool_;
        std::vector<bool> cpu_page_bitmap_; 

        
        std::vector<uint8_t> gpu_pool_;
        std::vector<bool> gpu_page_bitmap_; 

        mutable std::mutex mutex_;
    };

} 
