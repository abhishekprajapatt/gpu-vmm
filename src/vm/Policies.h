#pragma once

#include "Common.h"
#include "PageTable.h"

namespace uvm_sim
{

    
    
    

    class ReplacementPolicy
    {
    public:
        virtual ~ReplacementPolicy() = default;

        virtual void on_page_access(VirtualPageNumber vpn) = 0;
        virtual void on_page_allocated(VirtualPageNumber vpn) = 0;
        virtual void on_page_freed(VirtualPageNumber vpn) = 0;
        virtual VirtualPageNumber select_victim() = 0;
        virtual void reset() = 0;
    };

    
    
    

    class LRUPolicy : public ReplacementPolicy
    {
    public:
        explicit LRUPolicy(size_t max_pages = 10000);

        void on_page_access(VirtualPageNumber vpn) override;
        void on_page_allocated(VirtualPageNumber vpn) override;
        void on_page_freed(VirtualPageNumber vpn) override;
        VirtualPageNumber select_victim() override;
        void reset() override;

    private:
        std::queue<VirtualPageNumber> lru_queue_;
        std::unordered_set<VirtualPageNumber> active_pages_;
        size_t max_pages_;
        mutable std::mutex mutex_;
    };

    
    
    

    class CLOCKPolicy : public ReplacementPolicy
    {
    public:
        explicit CLOCKPolicy(size_t max_pages = 10000);

        void on_page_access(VirtualPageNumber vpn) override;
        void on_page_allocated(VirtualPageNumber vpn) override;
        void on_page_freed(VirtualPageNumber vpn) override;
        VirtualPageNumber select_victim() override;
        void reset() override;

    private:
        struct ClockEntry
        {
            VirtualPageNumber vpn;
            bool reference_bit;

            ClockEntry(VirtualPageNumber v) : vpn(v), reference_bit(true) {}
        };

        std::vector<ClockEntry> clock_hand_vec_;
        size_t hand_pos_;
        size_t max_pages_;
        mutable std::mutex mutex_;
    };

} 
