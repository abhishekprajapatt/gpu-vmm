#include "Policies.h"

namespace uvm_sim
{

    
    
    

    LRUPolicy::LRUPolicy(size_t max_pages) : max_pages_(max_pages) {}

    void LRUPolicy::on_page_access(VirtualPageNumber vpn)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        
        auto it = std::find(lru_queue_.begin(), lru_queue_.end(), vpn);
        if (it != lru_queue_.end())
        {
            
            
        }
    }

    void LRUPolicy::on_page_allocated(VirtualPageNumber vpn)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        lru_queue_.push(vpn);
        active_pages_.insert(vpn);

        
        while (lru_queue_.size() > max_pages_)
        {
            auto oldest = lru_queue_.front();
            lru_queue_.pop();
            active_pages_.erase(oldest);
        }
    }

    void LRUPolicy::on_page_freed(VirtualPageNumber vpn)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        active_pages_.erase(vpn);
    }

    VirtualPageNumber LRUPolicy::select_victim()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (lru_queue_.empty())
        {
            return 0;
        }
        VirtualPageNumber victim = lru_queue_.front();
        lru_queue_.pop();
        active_pages_.erase(victim);
        return victim;
    }

    void LRUPolicy::reset()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        while (!lru_queue_.empty())
        {
            lru_queue_.pop();
        }
        active_pages_.clear();
    }

    
    
    

    CLOCKPolicy::CLOCKPolicy(size_t max_pages) : hand_pos_(0), max_pages_(max_pages) {}

    void CLOCKPolicy::on_page_access(VirtualPageNumber vpn)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        
        for (auto &entry : clock_hand_vec_)
        {
            if (entry.vpn == vpn)
            {
                entry.reference_bit = true;
                return;
            }
        }
    }

    void CLOCKPolicy::on_page_allocated(VirtualPageNumber vpn)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        clock_hand_vec_.emplace_back(vpn);

        
        while (clock_hand_vec_.size() > max_pages_)
        {
            if (hand_pos_ >= clock_hand_vec_.size())
            {
                hand_pos_ = 0;
            }
            clock_hand_vec_.erase(clock_hand_vec_.begin() + hand_pos_);
            if (hand_pos_ >= clock_hand_vec_.size() && !clock_hand_vec_.empty())
            {
                hand_pos_ = 0;
            }
        }
    }

    void CLOCKPolicy::on_page_freed(VirtualPageNumber vpn)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (size_t i = 0; i < clock_hand_vec_.size(); i++)
        {
            if (clock_hand_vec_[i].vpn == vpn)
            {
                clock_hand_vec_.erase(clock_hand_vec_.begin() + i);
                if (hand_pos_ >= clock_hand_vec_.size() && !clock_hand_vec_.empty())
                {
                    hand_pos_ = 0;
                }
                return;
            }
        }
    }

    VirtualPageNumber CLOCKPolicy::select_victim()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (clock_hand_vec_.empty())
        {
            return 0;
        }

        
        while (hand_pos_ < clock_hand_vec_.size())
        {
            if (!clock_hand_vec_[hand_pos_].reference_bit)
            {
                VirtualPageNumber victim = clock_hand_vec_[hand_pos_].vpn;
                hand_pos_ = (hand_pos_ + 1) % clock_hand_vec_.size();
                clock_hand_vec_.erase(clock_hand_vec_.begin() + hand_pos_);
                return victim;
            }

            
            clock_hand_vec_[hand_pos_].reference_bit = false;
            hand_pos_ = (hand_pos_ + 1) % clock_hand_vec_.size();
        }

        
        if (!clock_hand_vec_.empty())
        {
            VirtualPageNumber victim = clock_hand_vec_[hand_pos_].vpn;
            hand_pos_ = (hand_pos_ + 1) % clock_hand_vec_.size();
            clock_hand_vec_.erase(clock_hand_vec_.begin() + hand_pos_);
            return victim;
        }

        return 0;
    }

    void CLOCKPolicy::reset()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        clock_hand_vec_.clear();
        hand_pos_ = 0;
    }

} 
