#include "PageAllocator.h"
#include <cstdlib>

namespace uvm_sim
{

    PageAllocator::PageAllocator(const Config &config)
        : config_(config), cpu_pages_allocated_(0), gpu_pages_allocated_(0), cpu_pool_(nullptr)
    {
    }

    PageAllocator::~PageAllocator()
    {
        if (cpu_pool_)
        {
            if (config_.use_pinned_memory)
            {
                
                free(cpu_pool_);
            }
            else
            {
                free(cpu_pool_);
            }
        }
    }

    void PageAllocator::initialize()
    {
        std::lock_guard<std::mutex> lock(mutex_);

        size_t num_cpu_pages = config_.cpu_page_pool_size / config_.page_size;
        size_t num_gpu_pages = config_.gpu_page_pool_size / config_.page_size;

        
        if (config_.use_pinned_memory)
        {
            
            cpu_pool_ = malloc(config_.cpu_page_pool_size);
            if (!cpu_pool_)
            {
                LOG_ERROR("Failed to allocate CPU page pool");
                throw std::runtime_error("CPU pool allocation failed");
            }
        }
        else
        {
            cpu_pool_ = malloc(config_.cpu_page_pool_size);
            if (!cpu_pool_)
            {
                LOG_ERROR("Failed to allocate CPU page pool");
                throw std::runtime_error("CPU pool allocation failed");
            }
        }

        cpu_page_bitmap_.resize(num_cpu_pages, false);

        
        if (config_.use_gpu_simulator)
        {
            gpu_pool_.resize(config_.gpu_page_pool_size, 0);
        }
        gpu_page_bitmap_.resize(num_gpu_pages, false);

        LOG_INFO("PageAllocator initialized: CPU=%zu pages, GPU=%zu pages", num_cpu_pages, num_gpu_pages);
    }

    void *PageAllocator::allocate_cpu_page()
    {
        std::lock_guard<std::mutex> lock(mutex_);

        
        for (size_t i = 0; i < cpu_page_bitmap_.size(); i++)
        {
            if (!cpu_page_bitmap_[i])
            {
                cpu_page_bitmap_[i] = true;
                cpu_pages_allocated_++;
                void *page_addr = static_cast<uint8_t *>(cpu_pool_) + (i * config_.page_size);
                LOG_TRACE("Allocated CPU page %zu at %p", i, page_addr);
                return page_addr;
            }
        }

        LOG_WARN("No free CPU pages available");
        return nullptr;
    }

    void PageAllocator::deallocate_cpu_page(void *ptr)
    {
        if (!ptr)
            return;

        std::lock_guard<std::mutex> lock(mutex_);

        
        uint8_t *ptr_uint = static_cast<uint8_t *>(ptr);
        uint8_t *pool_base = static_cast<uint8_t *>(cpu_pool_);
        ptrdiff_t offset = ptr_uint - pool_base;

        if (offset < 0 || offset >= (ptrdiff_t)config_.cpu_page_pool_size)
        {
            LOG_WARN("Attempted to deallocate invalid CPU page pointer");
            return;
        }

        size_t page_idx = offset / config_.page_size;
        if (page_idx < cpu_page_bitmap_.size() && cpu_page_bitmap_[page_idx])
        {
            cpu_page_bitmap_[page_idx] = false;
            cpu_pages_allocated_--;
            LOG_TRACE("Deallocated CPU page %zu", page_idx);
        }
    }

    uint64_t PageAllocator::allocate_gpu_page()
    {
        std::lock_guard<std::mutex> lock(mutex_);

        for (size_t i = 0; i < gpu_page_bitmap_.size(); i++)
        {
            if (!gpu_page_bitmap_[i])
            {
                gpu_page_bitmap_[i] = true;
                gpu_pages_allocated_++;

                
                uint64_t gpu_addr = 0x100000000UL + (i * config_.page_size);
                LOG_TRACE("Allocated GPU page %zu at 0x%lx", i, gpu_addr);
                return gpu_addr;
            }
        }

        LOG_WARN("No free GPU pages available");
        return 0;
    }

    void PageAllocator::deallocate_gpu_page(uint64_t gpu_addr)
    {
        std::lock_guard<std::mutex> lock(mutex_);

        uint64_t base = 0x100000000UL;
        if (gpu_addr < base)
        {
            LOG_WARN("Invalid GPU address: 0x%lx", gpu_addr);
            return;
        }

        size_t page_idx = (gpu_addr - base) / config_.page_size;
        if (page_idx < gpu_page_bitmap_.size() && gpu_page_bitmap_[page_idx])
        {
            gpu_page_bitmap_[page_idx] = false;
            gpu_pages_allocated_--;
            LOG_TRACE("Deallocated GPU page %zu", page_idx);
        }
    }

    size_t PageAllocator::get_available_cpu_pages() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return cpu_page_bitmap_.size() - cpu_pages_allocated_;
    }

    size_t PageAllocator::get_available_gpu_pages() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return gpu_page_bitmap_.size() - gpu_pages_allocated_;
    }

    size_t PageAllocator::get_total_cpu_pages() const
    {
        return cpu_page_bitmap_.size();
    }

    size_t PageAllocator::get_total_gpu_pages() const
    {
        return gpu_page_bitmap_.size();
    }

} 
