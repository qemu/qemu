/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This C++ plugin ensures we don't have regression when compiling C++.
 */

#include <qemu-plugin.h>

/*
 * We include all C++ standard headers (without deprecated ones),
 * taken from: https://en.cppreference.com/w/cpp/headers.html
 *
 * To update, copy page text, and then:
 * grep '^<' |
 * sort -u |
 * grep -v strstream |
 * grep -v codecvt |
 * sed -e 's/\(.*\)/#if __has_include(\1)\n#include \1\n#endif/'
 */

#if __has_include(<algorithm>)
#include <algorithm>
#endif
#if __has_include(<any>)
#include <any>
#endif
#if __has_include(<array>)
#include <array>
#endif
#if __has_include(<atomic>)
#include <atomic>
#endif
#if __has_include(<barrier>)
#include <barrier>
#endif
#if __has_include(<bit>)
#include <bit>
#endif
#if __has_include(<bitset>)
#include <bitset>
#endif
#if __has_include(<cassert>)
#include <cassert>
#endif
#if __has_include(<cctype>)
#include <cctype>
#endif
#if __has_include(<cerrno>)
#include <cerrno>
#endif
#if __has_include(<cfenv>)
#include <cfenv>
#endif
#if __has_include(<cfloat>)
#include <cfloat>
#endif
#if __has_include(<charconv>)
#include <charconv>
#endif
#if __has_include(<chrono>)
#include <chrono>
#endif
#if __has_include(<cinttypes>)
#include <cinttypes>
#endif
#if __has_include(<climits>)
#include <climits>
#endif
#if __has_include(<clocale>)
#include <clocale>
#endif
#if __has_include(<cmath>)
#include <cmath>
#endif
#if __has_include(<compare>)
#include <compare>
#endif
#if __has_include(<complex>)
#include <complex>
#endif
#if __has_include(<concepts>)
#include <concepts>
#endif
#if __has_include(<condition_variable>)
#include <condition_variable>
#endif
#if __has_include(<contracts>)
#include <contracts>
#endif
#if __has_include(<coroutine>)
#include <coroutine>
#endif
#if __has_include(<csetjmp>)
#include <csetjmp>
#endif
#if __has_include(<csignal>)
#include <csignal>
#endif
#if __has_include(<cstdarg>)
#include <cstdarg>
#endif
#if __has_include(<cstddef>)
#include <cstddef>
#endif
#if __has_include(<cstdint>)
#include <cstdint>
#endif
#if __has_include(<cstdio>)
#include <cstdio>
#endif
#if __has_include(<cstdlib>)
#include <cstdlib>
#endif
#if __has_include(<cstring>)
#include <cstring>
#endif
#if __has_include(<ctime>)
#include <ctime>
#endif
#if __has_include(<cuchar>)
#include <cuchar>
#endif
#if __has_include(<cwchar>)
#include <cwchar>
#endif
#if __has_include(<cwctype>)
#include <cwctype>
#endif
#if __has_include(<debugging>)
#include <debugging>
#endif
#if __has_include(<deque>)
#include <deque>
#endif
#if __has_include(<exception>)
#include <exception>
#endif
#if __has_include(<execution>)
#include <execution>
#endif
#if __has_include(<expected>)
#include <expected>
#endif
#if __has_include(<filesystem>)
#include <filesystem>
#endif
#if __has_include(<flat_map>)
#include <flat_map>
#endif
#if __has_include(<flat_set>)
#include <flat_set>
#endif
#if __has_include(<format>)
#include <format>
#endif
#if __has_include(<forward_list>)
#include <forward_list>
#endif
#if __has_include(<fstream>)
#include <fstream>
#endif
#if __has_include(<functional>)
#include <functional>
#endif
#if __has_include(<future>)
#include <future>
#endif
#if __has_include(<generator>)
#include <generator>
#endif
#if __has_include(<hazard_pointer>)
#include <hazard_pointer>
#endif
#if __has_include(<hive>)
#include <hive>
#endif
#if __has_include(<initializer_list>)
#include <initializer_list>
#endif
#if __has_include(<inplace_vector>)
#include <inplace_vector>
#endif
#if __has_include(<iomanip>)
#include <iomanip>
#endif
#if __has_include(<ios>)
#include <ios>
#endif
#if __has_include(<iosfwd>)
#include <iosfwd>
#endif
#if __has_include(<iostream>)
#include <iostream>
#endif
#if __has_include(<istream>)
#include <istream>
#endif
#if __has_include(<iterator>)
#include <iterator>
#endif
#if __has_include(<latch>)
#include <latch>
#endif
#if __has_include(<limits>)
#include <limits>
#endif
#if __has_include(<linalg>)
#include <linalg>
#endif
#if __has_include(<list>)
#include <list>
#endif
#if __has_include(<locale>)
#include <locale>
#endif
#if __has_include(<map>)
#include <map>
#endif
#if __has_include(<mdspan>)
#include <mdspan>
#endif
#if __has_include(<memory>)
#include <memory>
#endif
#if __has_include(<memory_resource>)
#include <memory_resource>
#endif
#if __has_include(<mutex>)
#include <mutex>
#endif
#if __has_include(<new>)
#include <new>
#endif
#if __has_include(<numbers>)
#include <numbers>
#endif
#if __has_include(<numeric>)
#include <numeric>
#endif
#if __has_include(<optional>)
#include <optional>
#endif
#if __has_include(<ostream>)
#include <ostream>
#endif
#if __has_include(<print>)
#include <print>
#endif
#if __has_include(<queue>)
#include <queue>
#endif
#if __has_include(<random>)
#include <random>
#endif
#if __has_include(<ranges>)
#include <ranges>
#endif
#if __has_include(<ratio>)
#include <ratio>
#endif
#if __has_include(<rcu>)
#include <rcu>
#endif
#if __has_include(<regex>)
#include <regex>
#endif
#if __has_include(<scoped_allocator>)
#include <scoped_allocator>
#endif
#if __has_include(<semaphore>)
#include <semaphore>
#endif
#if __has_include(<set>)
#include <set>
#endif
#if __has_include(<shared_mutex>)
#include <shared_mutex>
#endif
#if __has_include(<simd>)
#include <simd>
#endif
#if __has_include(<source_location>)
#include <source_location>
#endif
#if __has_include(<span>)
#include <span>
#endif
#if __has_include(<spanstream>)
#include <spanstream>
#endif
#if __has_include(<sstream>)
#include <sstream>
#endif
#if __has_include(<stack>)
#include <stack>
#endif
#if __has_include(<stacktrace>)
#include <stacktrace>
#endif
#if __has_include(<stdexcept>)
#include <stdexcept>
#endif
#if __has_include(<stdfloat>)
#include <stdfloat>
#endif
#if __has_include(<stop_token>)
#include <stop_token>
#endif
#if __has_include(<streambuf>)
#include <streambuf>
#endif
#if __has_include(<string>)
#include <string>
#endif
#if __has_include(<string_view>)
#include <string_view>
#endif
#if __has_include(<syncstream>)
#include <syncstream>
#endif
#if __has_include(<system_error>)
#include <system_error>
#endif
#if __has_include(<text_encoding>)
#include <text_encoding>
#endif
#if __has_include(<thread>)
#include <thread>
#endif
#if __has_include(<tuple>)
#include <tuple>
#endif
#if __has_include(<typeindex>)
#include <typeindex>
#endif
#if __has_include(<typeinfo>)
#include <typeinfo>
#endif
#if __has_include(<type_traits>)
#include <type_traits>
#endif
#if __has_include(<unordered_map>)
#include <unordered_map>
#endif
#if __has_include(<unordered_set>)
#include <unordered_set>
#endif
#if __has_include(<utility>)
#include <utility>
#endif
#if __has_include(<valarray>)
#include <valarray>
#endif
#if __has_include(<variant>)
#include <variant>
#endif
#if __has_include(<vector>)
#include <vector>
#endif
#if __has_include(<version>)
#include <version>
#endif

QEMU_PLUGIN_EXPORT int qemu_plugin_version = QEMU_PLUGIN_VERSION;

class Plugin
{
    public:
    void tb_exec(std::atomic<uint64_t> &count)
    {
        count++;
    }

    void tb_trans(struct qemu_plugin_tb *tb)
    {
        auto vaddr = qemu_plugin_tb_vaddr(tb);

        TbData *data = [&]() {
            std::lock_guard l(tb_trans_lock);
            auto [it, is_new] = tb_data.try_emplace(vaddr);
            TbData &in = it->second;
            if (is_new) {
                in.first = 0;
                in.second = this;
            }
            return &in;
        } ();

        qemu_plugin_register_vcpu_tb_exec_cb(
            tb,
            [](unsigned int cpu_index, void *udata) {
                auto &[counter, p] = *static_cast<TbData*>(udata);
                p->tb_exec(counter);
            },
            QEMU_PLUGIN_CB_NO_REGS,
            data);
    }

    void at_exit()
    {
        std::vector<std::pair<Vaddr, uint64_t>> v;
        for (auto &[vaddr, data] : tb_data) {
            uint64_t count = data.first;
            v.push_back({vaddr, count});
        }
        std::sort(v.begin(), v.end(),
                  [](const auto &a, const auto &b) {
                    return a.second > b.second;
                  });
        std::stringstream ss;
        ss << "Top 10 executed TB are:\n";
        size_t idx = 0;
        for (auto &tb : v) {
            if (idx >= 10) {
                break;
            }
            ss << std::hex << "0x" << tb.first << std::dec << ": "
               << tb.second << '\n';
            ++idx;
        }
        qemu_plugin_outs(ss.str().c_str());
    }

    private:
    using Vaddr = uint64_t;
    using Counter = std::atomic<uint64_t>;
    using TbData = std::pair<Counter, Plugin*>;
    std::unordered_map<Vaddr, TbData> tb_data;
    std::mutex tb_trans_lock;
};

QEMU_PLUGIN_EXPORT int qemu_plugin_install(qemu_plugin_id_t id,
                                           const qemu_info_t *info,
                                           int argc, char **argv)
{
    Plugin *plugin = new Plugin;

    /*
     * We can register a cb with any function, which may be a free
     * function, a static class function, or a captureless lambda.
     */
    qemu_plugin_register_vcpu_tb_trans_cb(
        id,
        [](struct qemu_plugin_tb *tb, void *userdata) {
            Plugin *p = static_cast<Plugin*>(userdata);
            p->tb_trans(tb);
        },
        plugin);

    qemu_plugin_register_atexit_cb(
        id,
        [](void *userdata) {
            Plugin *p = static_cast<Plugin*>(userdata);
            p->at_exit();
            delete p;
        },
        plugin);

    return 0;
}
