// SPDX-FileCopyrightText: Copyright 2020 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#ifdef _WIN32
// clang-format off
#include <windows.h>
#include <sysinfoapi.h>
// clang-format on
#else
#include <sys/types.h>
#if defined(__APPLE__) || defined(__FreeBSD__)
#include <sys/sysctl.h>
#if defined(__APPLE__)
#include <mach/mach.h>
#include <unistd.h>
#endif
#elif defined(__linux__)
#include <cstdio>
#include <sys/sysinfo.h>
#else
#include <unistd.h>
#endif
#endif

#include "common/memory_detect.h"

namespace Common {

// Detects the RAM and Swapfile sizes
static MemoryInfo Detect() {
    MemoryInfo mem_info{};

#ifdef _WIN32
    MEMORYSTATUSEX memorystatus;
    memorystatus.dwLength = sizeof(memorystatus);
    GlobalMemoryStatusEx(&memorystatus);
    mem_info.TotalPhysicalMemory = memorystatus.ullTotalPhys;
    mem_info.TotalSwapMemory = memorystatus.ullTotalPageFile - mem_info.TotalPhysicalMemory;
#elif defined(__APPLE__)
    u64 ramsize;
    struct xsw_usage vmusage;
    std::size_t sizeof_ramsize = sizeof(ramsize);
    std::size_t sizeof_vmusage = sizeof(vmusage);
    // hw and vm are defined in sysctl.h
    // https://github.com/apple/darwin-xnu/blob/master/bsd/sys/sysctl.h#L471
    // sysctlbyname(const char *, void *, size_t *, void *, size_t);
    sysctlbyname("hw.memsize", &ramsize, &sizeof_ramsize, nullptr, 0);
    sysctlbyname("vm.swapusage", &vmusage, &sizeof_vmusage, nullptr, 0);
    mem_info.TotalPhysicalMemory = ramsize;
    mem_info.TotalSwapMemory = vmusage.xsu_total;
#elif defined(__FreeBSD__)
    u_long physmem, swap_total;
    std::size_t sizeof_u_long = sizeof(u_long);
    // sysctlbyname(const char *, void *, size_t *, const void *, size_t);
    sysctlbyname("hw.physmem", &physmem, &sizeof_u_long, nullptr, 0);
    sysctlbyname("vm.swap_total", &swap_total, &sizeof_u_long, nullptr, 0);
    mem_info.TotalPhysicalMemory = physmem;
    mem_info.TotalSwapMemory = swap_total;
#elif defined(__linux__)
    struct sysinfo meminfo;
    sysinfo(&meminfo);
    mem_info.TotalPhysicalMemory = meminfo.totalram;
    mem_info.TotalSwapMemory = meminfo.totalswap;
#else
    mem_info.TotalPhysicalMemory = sysconf(_SC_PHYS_PAGES) * sysconf(_SC_PAGE_SIZE);
    mem_info.TotalSwapMemory = 0;
#endif

    return mem_info;
}

const MemoryInfo& GetMemInfo() {
    static MemoryInfo mem_info = Detect();
    return mem_info;
}

u64 GetAvailablePhysicalMemory() {
#ifdef _WIN32
    MEMORYSTATUSEX memorystatus;
    memorystatus.dwLength = sizeof(memorystatus);
    return GlobalMemoryStatusEx(&memorystatus) ? memorystatus.ullAvailPhys : 0;
#elif defined(__APPLE__)
    vm_statistics64_data_t stats;
    mach_msg_type_number_t count = HOST_VM_INFO64_COUNT;
    if (host_statistics64(mach_host_self(), HOST_VM_INFO64,
                          reinterpret_cast<host_info64_t>(&stats), &count) != KERN_SUCCESS) {
        return 0;
    }
    const u64 pages = u64{stats.free_count} + stats.inactive_count + stats.purgeable_count;
    return pages * static_cast<u64>(sysconf(_SC_PAGESIZE));
#elif defined(__linux__) && !defined(__ANDROID__)
    std::FILE* const file = std::fopen("/proc/meminfo", "r");
    if (file == nullptr) {
        return 0;
    }
    char line[128];
    unsigned long long kib = 0;
    while (std::fgets(line, sizeof(line), file) != nullptr) {
        if (std::sscanf(line, "MemAvailable: %llu kB", &kib) == 1) {
            break;
        }
    }
    std::fclose(file);
    return u64{kib} * 1024;
#else
    return 0;
#endif
}

} // namespace Common
