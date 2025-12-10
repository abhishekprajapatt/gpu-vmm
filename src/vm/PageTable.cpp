#include "PageTable.h"

namespace uvm_sim
{

    void PageTable::initialize(size_t virtual_space_size)
    {
        std::unique_lock<std::shared_mutex> lock(mutex_);
        entries_.clear();
        num_pages_ = virtual_space_size / page_size_;
        LOG_DEBUG("PageTable initialized: %zu pages (page_size=%zu)", num_pages_, page_size_);
    }

    bool PageTable::allocate_vpn_range(VirtualPageNumber vpn_start, uint32_t num_pages)
    {
        std::unique_lock<std::shared_mutex> lock(mutex_);
        for (uint32_t i = 0; i < num_pages; i++)
        {
            VirtualPageNumber vpn = vpn_start + i;
            if (entries_.find(vpn) != entries_.end())
            {
                LOG_WARN("VPN %lu already allocated", vpn);
                return false;
            }
            entries_[vpn] = PageTableEntry();
            entries_[vpn].is_valid = true;
        }
        LOG_DEBUG("Allocated VPN range [%lu, %lu)", vpn_start, vpn_start + num_pages);
        return true;
    }

    bool PageTable::deallocate_vpn_range(VirtualPageNumber vpn_start, uint32_t num_pages)
    {
        std::unique_lock<std::shared_mutex> lock(mutex_);
        for (uint32_t i = 0; i < num_pages; i++)
        {
            VirtualPageNumber vpn = vpn_start + i;
            entries_.erase(vpn);
        }
        LOG_DEBUG("Deallocated VPN range [%lu, %lu)", vpn_start, vpn_start + num_pages);
        return true;
    }

    PageTableEntry *PageTable::get_entry(VirtualPageNumber vpn)
    {
        std::unique_lock<std::shared_mutex> lock(mutex_);
        if (entries_.find(vpn) == entries_.end())
        {
            entries_[vpn] = PageTableEntry();
        }
        return &entries_[vpn];
    }

    const PageTableEntry *PageTable::get_entry(VirtualPageNumber vpn) const
    {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        auto it = entries_.find(vpn);
        if (it != entries_.end())
        {
            return &it->second;
        }
        return nullptr;
    }

    PageTableEntry *PageTable::lookup_entry(VirtualPageNumber vpn)
    {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        auto it = entries_.find(vpn);
        if (it != entries_.end())
        {
            return &it->second;
        }
        return nullptr;
    }

    void PageTable::set_cpu_resident(VirtualPageNumber vpn, void *cpu_addr)
    {
        std::unique_lock<std::shared_mutex> lock(mutex_);
        auto it = entries_.find(vpn);
        if (it != entries_.end())
        {
            it->second.resident_on_cpu = true;
            it->second.cpu_address = cpu_addr;
            it->second.access_timestamp_us = get_timestamp_us();
        }
    }

    void PageTable::set_gpu_resident(VirtualPageNumber vpn, uint64_t gpu_addr)
    {
        std::unique_lock<std::shared_mutex> lock(mutex_);
        auto it = entries_.find(vpn);
        if (it != entries_.end())
        {
            it->second.resident_on_gpu = true;
            it->second.gpu_address = gpu_addr;
            it->second.access_timestamp_us = get_timestamp_us();
        }
    }

    void PageTable::mark_dirty(VirtualPageNumber vpn)
    {
        std::unique_lock<std::shared_mutex> lock(mutex_);
        auto it = entries_.find(vpn);
        if (it != entries_.end())
        {
            it->second.is_dirty = true;
        }
    }

    void PageTable::clear_dirty(VirtualPageNumber vpn)
    {
        std::unique_lock<std::shared_mutex> lock(mutex_);
        auto it = entries_.find(vpn);
        if (it != entries_.end())
        {
            it->second.is_dirty = false;
        }
    }

    void PageTable::update_access_time(VirtualPageNumber vpn)
    {
        std::unique_lock<std::shared_mutex> lock(mutex_);
        auto it = entries_.find(vpn);
        if (it != entries_.end())
        {
            it->second.access_timestamp_us = get_timestamp_us();
            it->second.access_count++;
        }
    }

    std::vector<std::pair<VirtualPageNumber, PageTableEntry *>> PageTable::get_all_entries()
    {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        std::vector<std::pair<VirtualPageNumber, PageTableEntry *>> result;
        for (auto &entry : entries_)
        {
            if (entry.second.is_valid)
            {
                result.push_back({entry.first, &entry.second});
            }
        }
        return result;
    }

    void PageTable::clear()
    {
        std::unique_lock<std::shared_mutex> lock(mutex_);
        entries_.clear();
    }

} 
