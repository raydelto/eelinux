/* Native GNU C++ runtime validation. SPDX-License-Identifier: GPL-2.0-only */
#include <iostream>
#include <atomic>
#include <numeric>
#include <stdexcept>
#include <string>
#include <vector>

int main()
{
    std::vector<int> values{2, 3, 7};
    if (std::accumulate(values.begin(), values.end(), 0) != 12)
        return 1;
    std::atomic_flag flag = ATOMIC_FLAG_INIT;
    if (flag.test_and_set(std::memory_order_acquire)
        || !flag.test_and_set(std::memory_order_relaxed))
        return 3;
    flag.clear(std::memory_order_release);
    if (flag.test_and_set(std::memory_order_acquire))
        return 4;
    try {
        throw std::runtime_error("r5900");
    } catch (const std::runtime_error &error) {
        if (std::string(error.what()) != "r5900")
            return 2;
    }
    std::cout << "R5900 GNU C++ runtime passed\n";
    return 0;
}
