#pragma once

#include <vector>

#ifdef _WIN32
#include <immintrin.h>
template <typename T, std::size_t Alignment>
struct AlignedAllocator {
    using value_type = T;

    T* allocate(std::size_t n) {
        void* ptr = _mm_malloc(n * sizeof(T), Alignment);
        if (!ptr) throw std::bad_alloc();
        return static_cast<T*>(ptr);
    }
    void deallocate(T* p, std::size_t) noexcept { _mm_free(p); }
};

#endif


struct Tensor {
#ifdef _WIN32
    std::vector<float, AlignedAllocator<float, 32>> data;
#else
    std::vector<float> data;
#endif
    std::vector<int> shape;
    float norm();
    float mean();
    float sum();
};
