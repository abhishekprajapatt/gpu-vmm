

#include "../vm/VirtualMemoryManager.h"
#include <iostream>
#include <cmath>
#include <vector>
#include <random>
#include <iomanip>

using namespace uvm_sim;

struct Particle
{
    float x, y, z;    
    float vx, vy, vz; 
    float ax, ay, az; 
    float mass;       
};

struct NBodyConfig
{
    uint32_t num_particles = 1024;
    uint32_t num_steps = 100;
    float softening = 0.001f;
    float dt = 0.01f;
};

void compute_acceleration(Particle &p, const Particle *particles, uint32_t num_particles)
{
    p.ax = p.ay = p.az = 0.0f;

    for (uint32_t i = 0; i < num_particles; i++)
    {
        const Particle &q = particles[i];
        if (&p == &q)
            continue;

        float dx = q.x - p.x;
        float dy = q.y - p.y;
        float dz = q.z - p.z;
        float dist_sq = dx * dx + dy * dy + dz * dz + p.mass * p.mass; 
        float dist = std::sqrt(dist_sq);
        float inv_dist_cubed = 1.0f / (dist_sq * dist);

        p.ax += q.mass * dx * inv_dist_cubed;
        p.ay += q.mass * dy * inv_dist_cubed;
        p.az += q.mass * dz * inv_dist_cubed;
    }
}

void integrate_particles(Particle *particles, uint32_t num_particles, float dt)
{
    for (uint32_t i = 0; i < num_particles; i++)
    {
        Particle &p = particles[i];

        
        p.vx += p.ax * dt;
        p.vy += p.ay * dt;
        p.vz += p.az * dt;

        
        p.x += p.vx * dt;
        p.y += p.vy * dt;
        p.z += p.vz * dt;
    }
}

float compute_kinetic_energy(const Particle *particles, uint32_t num_particles)
{
    float ke = 0.0f;
    for (uint32_t i = 0; i < num_particles; i++)
    {
        const Particle &p = particles[i];
        float v_sq = p.vx * p.vx + p.vy * p.vy + p.vz * p.vz;
        ke += 0.5f * p.mass * v_sq;
    }
    return ke;
}

void initialize_particles(Particle *particles, uint32_t num_particles, uint32_t seed = 42)
{
    std::mt19937 rng(seed);
    std::uniform_real_distribution<float> vel_dist(-1.0f, 1.0f);
    std::uniform_real_distribution<float> pos_dist(-10.0f, 10.0f);
    std::uniform_real_distribution<float> mass_dist(0.1f, 1.0f);

    for (uint32_t i = 0; i < num_particles; i++)
    {
        particles[i].x = pos_dist(rng);
        particles[i].y = pos_dist(rng);
        particles[i].z = pos_dist(rng);
        particles[i].vx = vel_dist(rng) * 0.1f;
        particles[i].vy = vel_dist(rng) * 0.1f;
        particles[i].vz = vel_dist(rng) * 0.1f;
        particles[i].mass = mass_dist(rng);
        particles[i].ax = particles[i].ay = particles[i].az = 0.0f;
    }
}

int main(int argc, char **argv)
{
    NBodyConfig config;

    
    if (argc > 1)
        config.num_particles = atoi(argv[1]);
    if (argc > 2)
        config.num_steps = atoi(argv[2]);

    std::cout << "N-Body Simulation with GPU Virtual Memory\n";
    std::cout << "==========================================\n";
    std::cout << "Particles:    " << config.num_particles << "\n";
    std::cout << "Steps:        " << config.num_steps << "\n";
    std::cout << "Particle Size:" << sizeof(Particle) << " bytes\n";

    size_t total_memory = config.num_particles * sizeof(Particle);
    std::cout << "Total Memory: " << (total_memory / (1024.0 * 1024.0)) << " MB\n\n";

    
    VMConfig vm_config;
    vm_config.page_size = 64 * 1024;
    vm_config.gpu_memory = 2UL * 1024 * 1024 * 1024; 
    vm_config.replacement_policy = PageReplacementPolicy::LRU;
    vm_config.use_gpu_simulator = true;
    vm_config.log_level = LogLevel::INFO;

    VirtualMemoryManager &vm = VirtualMemoryManager::instance();
    vm.initialize(vm_config);

    
    Particle *particles = (Particle *)vm.allocate(total_memory);
    if (!particles)
    {
        std::cerr << "Failed to allocate particle memory\n";
        return 1;
    }

    std::cout << "Initialized particle memory at " << (void *)particles << "\n";

    
    initialize_particles(particles, config.num_particles);

    std::cout << "Initialized " << config.num_particles << " particles\n";
    std::cout << "\nRunning simulation...\n\n";

    
    auto sim_start = std::chrono::high_resolution_clock::now();
    float initial_ke = compute_kinetic_energy(particles, config.num_particles);

    for (uint32_t step = 0; step < config.num_steps; step++)
    {
        
        for (uint32_t i = 0; i < config.num_particles; i++)
        {
            
            if (i % 128 == 0)
            {
                vm.touch_page(&particles[i], true);
            }
            compute_acceleration(particles[i], particles, config.num_particles);
        }

        
        integrate_particles(particles, config.num_particles, config.dt);

        if ((step + 1) % 10 == 0)
        {
            float ke = compute_kinetic_energy(particles, config.num_particles);
            std::cout << "Step " << std::setw(4) << (step + 1) << " / " << config.num_steps
                      << " - KE: " << std::scientific << std::setprecision(6) << ke
                      << " (Δ: " << ((ke - initial_ke) / initial_ke * 100.0f) << "%)\n";
        }
    }

    auto sim_end = std::chrono::high_resolution_clock::now();
    uint64_t sim_time_us = std::chrono::duration_cast<std::chrono::microseconds>(
                               sim_end - sim_start)
                               .count();

    float final_ke = compute_kinetic_energy(particles, config.num_particles);

    std::cout << "\n"
              << std::string(50, '=') << "\n";
    std::cout << "Simulation Results\n";
    std::cout << std::string(50, '=') << "\n";
    std::cout << "Simulation Time:     " << (sim_time_us / 1e6) << " seconds\n";
    std::cout << "Performance:         " << std::fixed << std::setprecision(2)
              << ((uint64_t)config.num_particles * config.num_particles * config.num_steps / (sim_time_us / 1e6) / 1e9)
              << " billion interactions/sec\n";
    std::cout << "Initial KE:          " << std::scientific << initial_ke << "\n";
    std::cout << "Final KE:            " << final_ke << "\n";
    std::cout << "Energy Conservation: " << std::fixed << std::setprecision(2)
              << ((final_ke - initial_ke) / initial_ke * 100.0f) << "%\n";

    
    std::cout << "\nVM Statistics:\n";
    vm.print_stats();

    
    vm.free(particles);
    vm.shutdown();

    return 0;
}
