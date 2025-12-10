

#include <gtest/gtest.h>
#include "../src/vm/VirtualMemoryManager.h"
#include "../src/vm/PageTable.h"
#include "../src/vm/PageAllocator.h"
#include "../src/vm/TLB.h"
#include "../src/vm/Policies.h"
#include <cstring>
#include <vector>

using namespace uvm_sim;

class PageTableTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        pt = std::make_unique<PageTable>(DEFAULT_PAGE_SIZE);
        pt->initialize(256UL * 1024 * 1024);
    }

    void TearDown() override
    {
        pt.reset();
    }

    std::unique_ptr<PageTable> pt;
};

TEST_F(PageTableTest, AllocateAndLookup)
{
    VirtualPageNumber vpn = 100;
    ASSERT_TRUE(pt->allocate_vpn_range(vpn, 10));

    auto entry = pt->lookup_entry(vpn);
    ASSERT_NE(entry, nullptr);
    EXPECT_TRUE(entry->is_valid);
}

TEST_F(PageTableTest, SetCPUResident)
{
    VirtualPageNumber vpn = 200;
    pt->allocate_vpn_range(vpn, 1);

    void *cpu_addr = (void *)0x1000;
    pt->set_cpu_resident(vpn, cpu_addr);

    auto entry = pt->lookup_entry(vpn);
    ASSERT_NE(entry, nullptr);
    EXPECT_TRUE(entry->resident_on_cpu);
    EXPECT_EQ(entry->cpu_address, cpu_addr);
}

TEST_F(PageTableTest, DirtyBit)
{
    VirtualPageNumber vpn = 300;
    pt->allocate_vpn_range(vpn, 1);

    auto entry = pt->lookup_entry(vpn);
    EXPECT_FALSE(entry->is_dirty);

    pt->mark_dirty(vpn);
    EXPECT_TRUE(entry->is_dirty);

    pt->clear_dirty(vpn);
    EXPECT_FALSE(entry->is_dirty);
}

TEST_F(PageTableTest, MultiplePages)
{
    VirtualPageNumber vpn_start = 400;
    uint32_t num_pages = 100;

    ASSERT_TRUE(pt->allocate_vpn_range(vpn_start, num_pages));

    for (uint32_t i = 0; i < num_pages; i++)
    {
        auto entry = pt->lookup_entry(vpn_start + i);
        EXPECT_NE(entry, nullptr);
        EXPECT_TRUE(entry->is_valid);
    }
}

class PageAllocatorTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        PageAllocator::Config config;
        config.page_size = DEFAULT_PAGE_SIZE;
        config.cpu_page_pool_size = 64UL * 1024 * 1024; 
        config.gpu_page_pool_size = 64UL * 1024 * 1024;
        config.use_gpu_simulator = true;
        config.use_pinned_memory = false;

        allocator = std::make_unique<PageAllocator>(config);
        allocator->initialize();
    }

    void TearDown() override
    {
        allocator.reset();
    }

    std::unique_ptr<PageAllocator> allocator;
};

TEST_F(PageAllocatorTest, AllocateCPUPage)
{
    void *page = allocator->allocate_cpu_page();
    ASSERT_NE(page, nullptr);

    size_t available_before = allocator->get_available_cpu_pages();
    allocator->deallocate_cpu_page(page);
    size_t available_after = allocator->get_available_cpu_pages();

    EXPECT_GT(available_after, available_before);
}

TEST_F(PageAllocatorTest, AllocateGPUPage)
{
    uint64_t page = allocator->allocate_gpu_page();
    ASSERT_NE(page, 0UL);

    size_t available_before = allocator->get_available_gpu_pages();
    allocator->deallocate_gpu_page(page);
    size_t available_after = allocator->get_available_gpu_pages();

    EXPECT_GT(available_after, available_before);
}

TEST_F(PageAllocatorTest, MultipleAllocations)
{
    std::vector<void *> pages;
    size_t num_allocs = 100;

    for (size_t i = 0; i < num_allocs; i++)
    {
        void *page = allocator->allocate_cpu_page();
        ASSERT_NE(page, nullptr);
        pages.push_back(page);
    }

    for (auto page : pages)
    {
        allocator->deallocate_cpu_page(page);
    }
}

class TLBTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        TLB::Config config;
        config.tlb_size = 1024;
        config.associativity = 8;

        tlb = std::make_unique<TLB>(config);
        tlb->initialize();
    }

    void TearDown() override
    {
        tlb.reset();
    }

    std::unique_ptr<TLB> tlb;
};

TEST_F(TLBTest, InsertAndLookup)
{
    TLBEntry entry;
    entry.vpn = 100;
    entry.cpu_address = (void *)0x1000;
    entry.gpu_address = 0x1000;

    tlb->insert(100, entry);

    TLBEntry retrieved;
    ASSERT_TRUE(tlb->lookup(100, &retrieved));
    EXPECT_EQ(retrieved.vpn, 100);
    EXPECT_EQ(retrieved.cpu_address, (void *)0x1000);
}

TEST_F(TLBTest, HitRate)
{
    TLBEntry entry;
    entry.cpu_address = (void *)0x1000;
    entry.gpu_address = 0x1000;

    
    for (int i = 0; i < 10; i++)
    {
        entry.vpn = i;
        tlb->insert(i, entry);
    }

    
    TLBEntry dummy;
    for (int i = 0; i < 10; i++)
    {
        tlb->lookup(i, &dummy);
    }

    
    tlb->lookup(999, &dummy);

    uint64_t hits = tlb->get_hits();
    uint64_t misses = tlb->get_misses();

    EXPECT_EQ(hits, 10);
    EXPECT_EQ(misses, 1);
}

TEST_F(TLBTest, Invalidate)
{
    TLBEntry entry;
    entry.vpn = 200;
    entry.cpu_address = (void *)0x2000;
    entry.gpu_address = 0x2000;

    tlb->insert(200, entry);

    TLBEntry retrieved;
    ASSERT_TRUE(tlb->lookup(200, &retrieved));

    tlb->invalidate(200);
    tlb->reset_stats();

    ASSERT_FALSE(tlb->lookup(200, &retrieved));
}

class LRUPolicyTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        policy = std::make_unique<LRUPolicy>(100);
    }

    void TearDown() override
    {
        policy.reset();
    }

    std::unique_ptr<LRUPolicy> policy;
};

TEST_F(LRUPolicyTest, PageAllocationAndEviction)
{
    
    for (int i = 0; i < 50; i++)
    {
        policy->on_page_allocated(i);
    }

    
    VirtualPageNumber victim = policy->select_victim();
    EXPECT_EQ(victim, 0);
}

TEST_F(LRUPolicyTest, AccessUpdatesRecency)
{
    policy->on_page_allocated(0);
    policy->on_page_allocated(1);

    
    policy->on_page_access(0);

    
    VirtualPageNumber victim = policy->select_victim();
    EXPECT_EQ(victim, 1);
}

class CLOCKPolicyTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        policy = std::make_unique<CLOCKPolicy>(100);
    }

    void TearDown() override
    {
        policy.reset();
    }

    std::unique_ptr<CLOCKPolicy> policy;
};

TEST_F(CLOCKPolicyTest, BasicEviction)
{
    for (int i = 0; i < 10; i++)
    {
        policy->on_page_allocated(i);
    }

    VirtualPageNumber victim = policy->select_victim();
    EXPECT_GE(victim, 0);
}

class VirtualMemoryManagerTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        VMConfig config;
        config.page_size = 64 * 1024;
        config.gpu_memory = 512UL * 1024 * 1024;
        config.replacement_policy = PageReplacementPolicy::LRU;
        config.use_gpu_simulator = true;
        config.log_level = LogLevel::ERROR; 

        VirtualMemoryManager::instance().initialize(config);
    }

    void TearDown() override
    {
        VirtualMemoryManager::instance().shutdown();
    }
};

TEST_F(VirtualMemoryManagerTest, AllocateAndFree)
{
    size_t size = 4UL * 1024 * 1024; 

    void *ptr = VirtualMemoryManager::instance().allocate(size);
    ASSERT_NE(ptr, nullptr);

    VirtualMemoryManager::instance().free(ptr);
}

TEST_F(VirtualMemoryManagerTest, WriteAndRead)
{
    size_t size = 1024 * 1024; 
    void *ptr = VirtualMemoryManager::instance().allocate(size);
    ASSERT_NE(ptr, nullptr);

    
    uint32_t test_value = 0xDEADBEEF;
    VirtualMemoryManager::instance().write_to_vaddr(ptr, &test_value, sizeof(test_value));

    
    uint32_t read_value = 0;
    VirtualMemoryManager::instance().read_from_vaddr(ptr, &read_value, sizeof(read_value));

    EXPECT_EQ(read_value, test_value);

    VirtualMemoryManager::instance().free(ptr);
}

TEST_F(VirtualMemoryManagerTest, TouchPage)
{
    size_t size = 1024 * 1024;
    void *ptr = VirtualMemoryManager::instance().allocate(size);
    ASSERT_NE(ptr, nullptr);

    auto &perf_before = VirtualMemoryManager::instance().get_perf_counters();
    uint64_t faults_before = perf_before.total_page_faults;

    VirtualMemoryManager::instance().touch_page(ptr);

    VirtualMemoryManager::instance().free(ptr);
}

TEST_F(VirtualMemoryManagerTest, LargeAllocation)
{
    size_t size = 256UL * 1024 * 1024; 
    void *ptr = VirtualMemoryManager::instance().allocate(size);

    if (ptr)
    {
        
        size_t page_size = 64 * 1024;
        for (size_t offset = 0; offset < size; offset += page_size * 10)
        {
            VirtualMemoryManager::instance().touch_page((uint8_t *)ptr + offset);
        }

        VirtualMemoryManager::instance().free(ptr);
    }
}

TEST_F(VirtualMemoryManagerTest, DeviceMappedHelper)
{
    {
        DeviceMapped<uint32_t> arr(1024);
        EXPECT_EQ(arr.size(), 1024);
        ASSERT_NE(arr.get(), nullptr);

        
        arr[0] = 42;
        arr[1] = 99;

        EXPECT_EQ(arr[0], 42);
        EXPECT_EQ(arr[1], 99);
    } 
}

TEST_F(VirtualMemoryManagerTest, DataIntegrity)
{
    size_t size = 8 * 1024 * 1024; 
    void *ptr = VirtualMemoryManager::instance().allocate(size);
    ASSERT_NE(ptr, nullptr);

    
    std::vector<uint32_t> pattern(size / sizeof(uint32_t));
    for (size_t i = 0; i < pattern.size(); i++)
    {
        pattern[i] = i ^ 0xDEADBEEF;
    }

    VirtualMemoryManager::instance().write_to_vaddr(ptr, pattern.data(), size);

    
    std::vector<uint32_t> read_back(size / sizeof(uint32_t));
    VirtualMemoryManager::instance().read_from_vaddr(ptr, read_back.data(), size);

    for (size_t i = 0; i < pattern.size(); i++)
    {
        EXPECT_EQ(read_back[i], pattern[i]) << "Data mismatch at index " << i;
    }

    VirtualMemoryManager::instance().free(ptr);
}

int main(int argc, char **argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
