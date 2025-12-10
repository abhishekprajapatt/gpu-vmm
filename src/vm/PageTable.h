#pragma once

#include "Common.h"
#include <cstring>

namespace uvm_sim
{

    
    
    

    struct PageTableEntry
    {
        
        bool resident_on_cpu : 1;
        bool resident_on_gpu : 1;
        bool is_dirty : 1;
        bool is_pinned : 1; 
        bool is_valid : 1;

        
        void *cpu_address;    
        uint64_t gpu_address; 

        
        uint64_t access_timestamp_us; 
        uint32_t access_count;        
        uint8_t clock_hand;           

        PageTableEntry()
            : resident_on_cpu(false), resident_on_gpu(false), is_dirty(false),
              is_pinned(false), is_valid(false), cpu_address(nullptr), gpu_address(0),
              access_timestamp_us(0), access_count(0), clock_hand(0) {}
    };

    
    
    

    class PageTable
    {
    public:
        explicit PageTable(size_t page_size = DEFAULT_PAGE_SIZE)
            : page_size_(page_size), num_pages_(0) {}

        
        void initialize(size_t virtual_space_size);

        
        bool allocate_vpn_range(VirtualPageNumber vpn_start, uint32_t num_pages);

        
        bool deallocate_vpn_range(VirtualPageNumber vpn_start, uint32_t num_pages);

        
        PageTableEntry *get_entry(VirtualPageNumber vpn);

        
        const PageTableEntry *get_entry(VirtualPageNumber vpn) const;

        
        PageTableEntry *lookup_entry(VirtualPageNumber vpn);

        
        void set_cpu_resident(VirtualPageNumber vpn, void *cpu_addr);

        
        void set_gpu_resident(VirtualPageNumber vpn, uint64_t gpu_addr);

        
        void mark_dirty(VirtualPageNumber vpn);

        
        void clear_dirty(VirtualPageNumber vpn);

        
        void update_access_time(VirtualPageNumber vpn);

        
        size_t get_num_allocated_pages() const { return num_pages_; }

        
        size_t get_page_size() const { return page_size_; }

        
        std::vector<std::pair<VirtualPageNumber, PageTableEntry *>> get_all_entries();

        
        void clear();

    private:
        size_t page_size_;
        size_t num_pages_;
        std::unordered_map<VirtualPageNumber, PageTableEntry> entries_;
        mutable std::shared_mutex mutex_;
    };

} 
