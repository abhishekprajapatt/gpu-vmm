#include "TLB.h"

namespace uvm_sim
{

    TLB::TLB(const Config &config) : config_(config), hits_(0), misses_(0) {}

    void TLB::initialize()
    {
        size_t num_sets = config_.tlb_size / config_.associativity;
        sets_.resize(num_sets);
        for (auto &set : sets_)
        {
            set.reserve(config_.associativity);
        }
        LOG_INFO("TLB initialized: %zu sets, %zu-way associative", num_sets, config_.associativity);
    }

    size_t TLB::get_set_index(VirtualPageNumber vpn) const
    {
        uint32_t hash = hash_vpn(vpn);
        size_t num_sets = config_.tlb_size / config_.associativity;
        return hash % num_sets;
    }

    bool TLB::lookup(VirtualPageNumber vpn, TLBEntry *out_entry)
    {
        std::lock_guard<std::mutex> lock(mutex_);

        size_t set_idx = get_set_index(vpn);
        auto &set = sets_[set_idx];

        for (auto &entry : set)
        {
            if (entry.valid && entry.vpn == vpn)
            {
                entry.timestamp = get_timestamp_us(); 
                *out_entry = entry;
                hits_++;
                return true;
            }
        }

        misses_++;
        return false;
    }

    void TLB::insert(VirtualPageNumber vpn, const TLBEntry &entry)
    {
        std::lock_guard<std::mutex> lock(mutex_);

        size_t set_idx = get_set_index(vpn);
        auto &set = sets_[set_idx];

        
        for (auto &e : set)
        {
            if (e.vpn == vpn)
            {
                e = entry;
                e.timestamp = get_timestamp_us();
                return;
            }
        }

        
        if (set.size() < config_.associativity)
        {
            TLBEntry new_entry = entry;
            new_entry.timestamp = get_timestamp_us();
            new_entry.valid = true;
            set.push_back(new_entry);
        }
        else
        {
            
            evict_lru(set_idx);
            TLBEntry new_entry = entry;
            new_entry.timestamp = get_timestamp_us();
            new_entry.valid = true;
            set.push_back(new_entry);
        }
    }

    void TLB::evict_lru(size_t set_idx)
    {
        auto &set = sets_[set_idx];
        if (set.empty())
            return;

        
        size_t min_idx = 0;
        for (size_t i = 1; i < set.size(); i++)
        {
            if (set[i].timestamp < set[min_idx].timestamp)
            {
                min_idx = i;
            }
        }

        set.erase(set.begin() + min_idx);
    }

    void TLB::invalidate(VirtualPageNumber vpn)
    {
        std::lock_guard<std::mutex> lock(mutex_);

        size_t set_idx = get_set_index(vpn);
        auto &set = sets_[set_idx];

        for (auto it = set.begin(); it != set.end(); ++it)
        {
            if (it->vpn == vpn)
            {
                set.erase(it);
                return;
            }
        }
    }

    void TLB::flush()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto &set : sets_)
        {
            set.clear();
        }
    }

} 
