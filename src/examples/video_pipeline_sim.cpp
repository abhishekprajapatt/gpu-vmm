

#include "../vm/VirtualMemoryManager.h"
#include <iostream>
#include <vector>
#include <iomanip>
#include <cstring>

using namespace uvm_sim;

struct Frame
{
    uint32_t width;
    uint32_t height;
    uint8_t *data; 
};

struct VideoPipelineConfig
{
    uint32_t frame_width = 1920;
    uint32_t frame_height = 1080;
    uint32_t num_frames = 100;
    uint32_t batch_size = 4;        
    uint32_t processing_passes = 3; 
};

size_t frame_size(uint32_t width, uint32_t height)
{
    return width * height * 3; 
}

void decode_frame(Frame *frame)
{
    
    uint8_t checksum = 0;
    for (size_t i = 0; i < frame_size(frame->width, frame->height); i++)
    {
        checksum ^= frame->data[i];
    }
    
    if (checksum == 255)
    {
        frame->data[0] = checksum;
    }
}

void color_space_convert(Frame *frame)
{
    uint8_t *data = frame->data;
    size_t size = frame_size(frame->width, frame->height);

    
    for (size_t i = 0; i < size; i += 3)
    {
        uint8_t r = data[i];
        uint8_t g = data[i + 1];
        uint8_t b = data[i + 2];

        
        uint8_t y = (r + g + b) / 3;
        data[i] = data[i + 1] = data[i + 2] = y;
    }
}

void apply_filter(Frame *frame)
{
    uint8_t *data = frame->data;
    size_t width = frame->width;
    size_t height = frame->height;

    
    for (size_t y = 1; y < height - 1; y++)
    {
        for (size_t x = 1; x < width - 1; x++)
        {
            size_t idx = (y * width + x) * 3;
            size_t idx_left = (y * width + (x - 1)) * 3;
            size_t idx_right = (y * width + (x + 1)) * 3;
            size_t idx_up = ((y - 1) * width + x) * 3;
            size_t idx_down = ((y + 1) * width + x) * 3;

            for (int c = 0; c < 3; c++)
            {
                uint16_t sum = (uint16_t)data[idx + c] + data[idx_left + c] + data[idx_right + c] +
                               data[idx_up + c] + data[idx_down + c];
                data[idx + c] = sum / 5;
            }
        }
    }
}

int main(int argc, char **argv)
{
    VideoPipelineConfig config;

    if (argc > 1)
        config.num_frames = atoi(argv[1]);
    if (argc > 2)
        config.batch_size = atoi(argv[2]);

    std::cout << "GPU Virtual Memory - Video Processing Pipeline\n";
    std::cout << "==============================================\n";
    std::cout << "Frame Resolution: " << config.frame_width << "x" << config.frame_height << "\n";
    std::cout << "Frame Count:      " << config.num_frames << "\n";
    std::cout << "Batch Size:       " << config.batch_size << "\n";
    std::cout << "Processing Passes:" << config.processing_passes << "\n";

    size_t frame_data_size = frame_size(config.frame_width, config.frame_height);
    size_t total_frame_memory = frame_data_size * config.num_frames;

    std::cout << "Per-Frame Size:   " << (frame_data_size / (1024.0 * 1024.0)) << " MB\n";
    std::cout << "Total Memory:     " << (total_frame_memory / (1024.0 * 1024.0)) << " MB\n\n";

    
    VMConfig vm_config;
    vm_config.page_size = 64 * 1024;
    vm_config.gpu_memory = 512UL * 1024 * 1024; 
    vm_config.replacement_policy = PageReplacementPolicy::LRU;
    vm_config.use_gpu_simulator = true;
    vm_config.log_level = LogLevel::INFO;

    VirtualMemoryManager &vm = VirtualMemoryManager::instance();
    vm.initialize(vm_config);

    
    uint8_t *frame_buffer = (uint8_t *)vm.allocate(total_frame_memory);
    if (!frame_buffer)
    {
        std::cerr << "Failed to allocate frame buffer\n";
        return 1;
    }

    std::cout << "Allocated frame buffer at " << (void *)frame_buffer << "\n\n";

    
    for (uint32_t i = 0; i < config.num_frames; i++)
    {
        uint8_t *frame_data = frame_buffer + (i * frame_data_size);
        
        for (size_t j = 0; j < frame_data_size; j++)
        {
            frame_data[j] = (uint8_t)((i + j) % 256);
        }
    }

    std::cout << "Initialized " << config.num_frames << " frames\n";
    std::cout << "Starting pipeline processing...\n\n";

    
    auto pipeline_start = std::chrono::high_resolution_clock::now();
    uint64_t frames_processed = 0;

    for (uint32_t pass = 0; pass < config.processing_passes; pass++)
    {
        std::cout << "Processing Pass " << (pass + 1) << " of " << config.processing_passes << "\n";

        
        for (uint32_t batch_start = 0; batch_start < config.num_frames; batch_start += config.batch_size)
        {
            uint32_t batch_end = std::min(batch_start + config.batch_size, config.num_frames);

            
            for (uint32_t i = batch_start; i < batch_end; i++)
            {
                uint8_t *frame_data = frame_buffer + (i * frame_data_size);
                vm.prefetch_to_gpu(frame_data);
            }

            
            for (uint32_t i = batch_start; i < batch_end; i++)
            {
                Frame frame;
                frame.width = config.frame_width;
                frame.height = config.frame_height;
                frame.data = frame_buffer + (i * frame_data_size);

                
                decode_frame(&frame);
                color_space_convert(&frame);
                apply_filter(&frame);

                frames_processed++;

                
                vm.touch_page(frame.data, true);
            }

            if ((batch_start / config.batch_size + 1) % 5 == 0)
            {
                std::cout << "  Processed batch " << (batch_start / config.batch_size) + 1 << "\n";
            }
        }
    }

    auto pipeline_end = std::chrono::high_resolution_clock::now();
    uint64_t pipeline_time_us = std::chrono::duration_cast<std::chrono::microseconds>(
                                    pipeline_end - pipeline_start)
                                    .count();

    std::cout << "\n"
              << std::string(50, '=') << "\n";
    std::cout << "Pipeline Results\n";
    std::cout << std::string(50, '=') << "\n";
    std::cout << "Total Frames Processed:  " << frames_processed << "\n";
    std::cout << "Total Time:              " << (pipeline_time_us / 1e6) << " seconds\n";
    std::cout << "Throughput:              " << std::fixed << std::setprecision(2)
              << (frames_processed / (pipeline_time_us / 1e6)) << " frames/sec\n";
    std::cout << "Data Throughput:         "
              << ((frames_processed * frame_data_size / 1e6) / (pipeline_time_us / 1e6))
              << " MB/sec\n";

    
    std::cout << "\nVM Statistics:\n";
    vm.print_stats();

    
    vm.free(frame_buffer);
    vm.shutdown();

    return 0;
}
