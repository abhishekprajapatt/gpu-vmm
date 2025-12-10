

#include <cstdint>
#include <cstring>

__global__ void kernel_read_pages(uint32_t* pages, size_t num_elements) {
    size_t idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx < num_elements) {
        
        uint32_t val = pages[idx];
        
        if (val > 1000000) {
            pages[idx] = val + 1;
        }
    }
}

__global__ void kernel_write_pages(uint32_t* pages, uint32_t value, size_t num_elements) {
    size_t idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx < num_elements) {
        pages[idx] = value + idx;
    }
}

__global__ void kernel_sequential_access(uint32_t* pages, size_t num_elements, int num_passes) {
    size_t idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx < num_elements) {
        for (int pass = 0; pass < num_passes; pass++) {
            pages[idx] += 1;
        }
    }
}

__global__ void kernel_random_access(uint32_t* pages, size_t num_elements, 
                                     uint32_t* indices, size_t num_indices) {
    size_t idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx < num_indices) {
        size_t page_idx = indices[idx] % num_elements;
        pages[page_idx] += 1;
    }
}

__global__ void kernel_checksum(const uint32_t* pages, size_t num_elements, 
                                uint64_t* out_checksum) {
    __shared__ uint64_t sdata[256];
    
    size_t idx = blockIdx.x * blockDim.x + threadIdx.x;
    uint64_t val = 0;
    
    if (idx < num_elements) {
        val = pages[idx];
    }
    
    
    for (int offset = warpSize / 2; offset > 0; offset /= 2) {
        val += __shfl_down_sync(0xffffffff, val, offset);
    }
    
    if (threadIdx.x % warpSize == 0) {
        sdata[threadIdx.x / warpSize] = val;
    }
    
    __syncthreads();
    
    
    if (threadIdx.x < blockDim.x / warpSize) {
        val = sdata[threadIdx.x];
        for (int offset = 1; offset < blockDim.x / warpSize; offset *= 2) {
            val += __shfl_down_sync(0xffffffff, val, offset);
        }
        
        if (threadIdx.x == 0) {
            atomicAdd(out_checksum, val);
        }
    }
}

__global__ void kernel_stencil_1d(const float* input, float* output, size_t num_elements) {
    size_t idx = blockIdx.x * blockDim.x + threadIdx.x;
    
    if (idx > 0 && idx < num_elements - 1) {
        output[idx] = (input[idx - 1] + 2.0f * input[idx] + input[idx + 1]) / 4.0f;
    } else if (idx == 0 || idx == num_elements - 1) {
        output[idx] = input[idx];
    }
}

__host__ __device__
uint64_t compute_checksum(const uint8_t* data, size_t size) {
    uint64_t checksum = 0;
    for (size_t i = 0; i < size; i++) {
        checksum += data[i];
        checksum = (checksum << 1) | (checksum >> 63);  
    }
    return checksum;
}

extern "C" {

cudaError_t cuda_kernel_read_pages(uint32_t* d_pages, size_t num_elements, cudaStream_t stream) {
    int block_size = 256;
    int grid_size = (num_elements + block_size - 1) / block_size;
    kernel_read_pages<<<grid_size, block_size, 0, stream>>>(d_pages, num_elements);
    return cudaGetLastError();
}

cudaError_t cuda_kernel_write_pages(uint32_t* d_pages, uint32_t value, size_t num_elements, cudaStream_t stream) {
    int block_size = 256;
    int grid_size = (num_elements + block_size - 1) / block_size;
    kernel_write_pages<<<grid_size, block_size, 0, stream>>>(d_pages, value, num_elements);
    return cudaGetLastError();
}

cudaError_t cuda_kernel_sequential_access(uint32_t* d_pages, size_t num_elements, 
                                           int num_passes, cudaStream_t stream) {
    int block_size = 256;
    int grid_size = (num_elements + block_size - 1) / block_size;
    kernel_sequential_access<<<grid_size, block_size, 0, stream>>>(d_pages, num_elements, num_passes);
    return cudaGetLastError();
}

cudaError_t cuda_kernel_random_access(uint32_t* d_pages, size_t num_elements,
                                       uint32_t* d_indices, size_t num_indices, cudaStream_t stream) {
    int block_size = 256;
    int grid_size = (num_indices + block_size - 1) / block_size;
    kernel_random_access<<<grid_size, block_size, 0, stream>>>(d_pages, num_elements, d_indices, num_indices);
    return cudaGetLastError();
}

cudaError_t cuda_kernel_checksum(const uint32_t* d_pages, size_t num_elements,
                                  uint64_t* d_out_checksum, cudaStream_t stream) {
    int block_size = 256;
    int grid_size = (num_elements + block_size - 1) / block_size;
    kernel_checksum<<<grid_size, block_size, 0, stream>>>(d_pages, num_elements, d_out_checksum);
    return cudaGetLastError();
}

cudaError_t cuda_kernel_stencil_1d(const float* d_input, float* d_output, size_t num_elements, cudaStream_t stream) {
    int block_size = 256;
    int grid_size = (num_elements + block_size - 1) / block_size;
    kernel_stencil_1d<<<grid_size, block_size, 0, stream>>>(d_input, d_output, num_elements);
    return cudaGetLastError();
}

}  
