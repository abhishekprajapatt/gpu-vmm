#pragma once

#include "Common.h"
#include "PageTable.h"

namespace uvm_sim
{

    
    
    

    struct TLBEntry
    {
        VirtualPageNumber vpn;
        void *cpu_address;    
        uint64_t gpu_address; 
        uint64_t timestamp;   
        bool valid;

        TLBEntry() : vpn(0), cpu_address(nullptr), gpu_address(0), timestamp(0), valid(false) {}
    };

    class TLB
    {
    public:
        struct Config
        {
            size_t tlb_size = DEFAULT_TLB_SIZE;
            size_t associativity = DEFAULT_TLB_ASSOCIATIVITY;
        };

        explicit TLB(const Config &config = Config());

        
        void initialize();

        
        bool lookup(VirtualPageNumber vpn, TLBEntry *out_entry);

        
        void insert(VirtualPageNumber vpn, const TLBEntry &entry);

        
        void invalidate(VirtualPageNumber vpn);

        
        void flush();

        
        uint64_t get_hits() const { return hits_; }
        uint64_t get_misses() const { return misses_; }
        double get_hit_rate() const
        {
            uint64_t total = hits_ + misses_;
            return total > 0 ? (double)hits_ / (double)total : 0.0;
        }

        
        void reset_stats()
        {
            hits_ = 0;
            misses_ = 0;
        }

        size_t get_tlb_size() const { return config_.tlb_size; }
        size_t get_associativity() const { return config_.associativity; }

    private:
        Config config_;
        std::vector<std::vector<TLBEntry>> sets_; 
        uint64_t hits_;
        uint64_t misses_;
        mutable std::mutex mutex_;

        
        size_t get_set_index(VirtualPageNumber vpn) const;

        
        void evict_lru(size_t set_idx);
    };

} 
