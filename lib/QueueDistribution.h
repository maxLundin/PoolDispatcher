//
// Created by maxlundin on 4/3/21.
//

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <random>

template<size_t queue_size>
class QueueDistribution {

    constexpr static std::array<uint32_t, queue_size> fill_distribution() {
        std::array<uint32_t, queue_size> array{};
        uint32_t val = 1;
        for (uint32_t i = 0; i < queue_size; ++i) {
            array[i] = val;
            val += (i + 2);
        }
        return array;
    }
public:

    constexpr static std::array<uint32_t, queue_size> distribution = fill_distribution();
    constexpr static size_t max_random_val = (queue_size * (queue_size + 1)) / 2;
    static_assert(distribution.back() == max_random_val);

    static size_t generate() {
        std::random_device r;

        std::default_random_engine e1(r());
        std::uniform_int_distribution<size_t> uniform_dist(0, max_random_val);
        return queue_size - 1 - (std::lower_bound(distribution.begin(), distribution.end(), uniform_dist(e1)) - distribution.begin());
    }
};
