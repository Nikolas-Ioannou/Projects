#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include "timer.h"
#include "files.h"

#define SOFTENING 1e-9f
#define BLOCK_SIZE 256

typedef struct { float x, y, z, vx, vy, vz; } Body;

__global__ void bodyForce(Body *p, float dt, int n) {
    int idx = threadIdx.x + blockIdx.x * blockDim.x;
    if (idx >= n) return;
    float Fx = 0.0f, Fy = 0.0f, Fz = 0.0f;
    __shared__ Body sharedBodies[BLOCK_SIZE];
    for (int tile = 0; tile < (n + BLOCK_SIZE - 1) / BLOCK_SIZE; tile++) {
        int index = tile * BLOCK_SIZE + threadIdx.x;
        if (index < n)
            sharedBodies[threadIdx.x] = p[index];
        __syncthreads();
        for (int j = 0; j < BLOCK_SIZE && (tile * BLOCK_SIZE + j) < n; j++) {
            float dx = sharedBodies[j].x - p[idx].x;
            float dy = sharedBodies[j].y - p[idx].y;
            float dz = sharedBodies[j].z - p[idx].z;
            float distSqr = dx * dx + dy * dy + dz * dz + SOFTENING;
            float invDist = rsqrtf(distSqr);
            float invDist3 = invDist * invDist * invDist;
            Fx += dx * invDist3;
            Fy += dy * invDist3;
            Fz += dz * invDist3;
        }
        __syncthreads();
    }
    p[idx].vx += dt * Fx;
    p[idx].vy += dt * Fy;
    p[idx].vz += dt * Fz;
}

__global__ void integratePosition(Body *p, float dt, int n) {
    int idx = threadIdx.x + blockIdx.x * blockDim.x;
    if (idx >= n) return;
    
    p[idx].x += p[idx].vx * dt;
    p[idx].y += p[idx].vy * dt;
    p[idx].z += p[idx].vz * dt;
}

int main(const int argc, const char** argv) {
    int nBodies = 2 << 11;
    if (argc > 1) nBodies = 2 << atoi(argv[1]);

    const char *initialized_values, *solution_values;
    if (nBodies == 2 << 11) {
        initialized_values = "09-nbody/files/initialized_4096";
        solution_values = "09-nbody/files/solution_4096";
    } else {
        initialized_values = "09-nbody/files/initialized_65536";
        solution_values = "09-nbody/files/solution_65536";
    }
    if (argc > 2) initialized_values = argv[2];
    if (argc > 3) solution_values = argv[3];

    const float dt = 0.01f;
    const int nIters = 10;
    int bytes = nBodies * sizeof(Body);
    float *buf = (float*)malloc(bytes);
    Body *p = (Body*)buf;
    read_values_from_file(initialized_values, buf, bytes);

    Body *d_p;
    cudaMalloc((void**)&d_p, bytes);
    cudaMemcpy(d_p, p, bytes, cudaMemcpyHostToDevice);

    int blockSize = BLOCK_SIZE;
    int gridSize = (nBodies + blockSize - 1) / blockSize;
    
    double totalTime = 0.0;
    for (int iter = 0; iter < nIters; iter++) {
        StartTimer();
        bodyForce<<<gridSize, blockSize>>>(d_p, dt, nBodies);
        cudaDeviceSynchronize();
        integratePosition<<<gridSize, blockSize>>>(d_p, dt, nBodies);
        cudaDeviceSynchronize();
        const double tElapsed = GetTimer() / 1000.0;
        totalTime += tElapsed;
    }
    
    cudaMemcpy(p, d_p, bytes, cudaMemcpyDeviceToHost);
    double avgTime = totalTime / (double)(nIters);
    float billionsOfOpsPerSecond = 1e-9 * nBodies * nBodies / avgTime;
    write_values_to_file(solution_values, buf, bytes);
    
    printf("%0.3f Billion Interactions / second\n", billionsOfOpsPerSecond);
    free(buf);
    cudaFree(d_p);
}
