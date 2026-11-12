// SPDX-FileCopyrightText: 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <algorithm>
#include <limits>
#include <utility>
#include <vector>

#include <boost/icl/interval.hpp>
#include <boost/icl/interval_base_set.hpp>
#include <boost/icl/interval_map.hpp>
#include <boost/icl/interval_set.hpp>
#include <boost/icl/split_interval_map.hpp>
#include <boost/pool/pool.hpp>
#include <boost/pool/pool_alloc.hpp>
#include <boost/pool/poolfwd.hpp>

#include "common/range_sets.h"

namespace Common {

namespace {
template <class T>
using RangeSetsAllocator =
    boost::fast_pool_allocator<T, boost::default_user_allocator_new_delete,
                               boost::details::pool::default_mutex, 1024, 2048>;
}

// Sorted, disjoint, non-touching ranges, in address order across blocks of at most MaxBlock. The
// buffer cache looks these up tens of thousands of times a frame and most lookups miss, so they
// stay in arrays rather than tree nodes. One flat array moved thousands of ranges on each insert
// once a game kept thousands of GPU-written ranges (Donkey Kong Bananza: up to 6,000 ranges and
// 30,000 inserts a second); an insert into a block moves at most MaxBlock.
template <typename AddressType>
struct RangeSet<AddressType>::RangeSetImpl {
    struct Range {
        AddressType lower;
        AddressType upper;
    };
    using Block = std::vector<Range>;
    static constexpr size_t MaxBlock = 128;

    /// A range's place: its block and its index there. The end is {m_blocks.size(), 0}.
    struct Pos {
        size_t block;
        size_t index;
        bool operator==(const Pos&) const = default;
    };

    Pos End() const {
        return {m_blocks.size(), 0};
    }
    Range& At(Pos pos) {
        return m_blocks[pos.block][pos.index];
    }
    const Range& At(Pos pos) const {
        return m_blocks[pos.block][pos.index];
    }
    Pos Next(Pos pos) const {
        return pos.index + 1 < m_blocks[pos.block].size() ? Pos{pos.block, pos.index + 1}
                                                          : Pos{pos.block + 1, 0};
    }
    Pos Prev(Pos pos) const {
        return pos.index > 0 ? Pos{pos.block, pos.index - 1}
                             : Pos{pos.block - 1, m_blocks[pos.block - 1].size() - 1};
    }

    /// The first range whose end passes `address` (`reaching`: or reaches it).
    template <bool reaching>
    Pos FirstEnding(AddressType address) const {
        const auto ends_before = [address](AddressType upper) {
            return reaching ? upper < address : upper <= address;
        };
        const size_t block =
            std::partition_point(m_last_upper.begin(), m_last_upper.end(), ends_before) -
            m_last_upper.begin();
        if (block == m_blocks.size()) {
            return End();
        }
        const Block& ranges = m_blocks[block];
        const auto it = std::partition_point(ranges.begin(), ranges.end(), [&](const Range& range) {
            return ends_before(range.upper);
        });
        return {block, static_cast<size_t>(it - ranges.begin())};
    }

    /// From `from` on, the first range whose start passes `address` (`reaching`: or reaches it).
    /// The blocks it walks past hold ranges the caller joins or removes.
    template <bool reaching>
    Pos FirstStarting(Pos from, AddressType address) const {
        const auto starts_before = [address](const Range& range) {
            return reaching ? range.lower < address : range.lower <= address;
        };
        for (size_t block = from.block; block < m_blocks.size(); ++block) {
            const Block& ranges = m_blocks[block];
            const auto begin = ranges.begin() + (block == from.block ? from.index : 0);
            const auto it = std::partition_point(begin, ranges.end(), starts_before);
            if (it != ranges.end()) {
                return {block, static_cast<size_t>(it - ranges.begin())};
            }
        }
        return End();
    }

    void Refresh(size_t block) {
        m_last_upper[block] = m_blocks[block].back().upper;
    }

    void Insert(Pos at, Range range) {
        if (m_blocks.empty()) {
            m_blocks.emplace_back().push_back(range);
            m_last_upper.push_back(range.upper);
            return;
        }
        if (at.block == m_blocks.size()) {
            at = {at.block - 1, m_blocks.back().size()};
        }
        Block& ranges = m_blocks[at.block];
        ranges.insert(ranges.begin() + at.index, range);
        if (ranges.size() > MaxBlock) {
            const auto half = ranges.begin() + ranges.size() / 2;
            Block upper_half(half, ranges.end());
            ranges.erase(half, ranges.end());
            m_blocks.insert(m_blocks.begin() + at.block + 1, std::move(upper_half));
            m_last_upper.insert(m_last_upper.begin() + at.block + 1, AddressType{});
            Refresh(at.block + 1);
        }
        Refresh(at.block);
    }

    /// Removes the ranges in [from, to), then the blocks this empties.
    void Erase(Pos from, Pos to) {
        if (from == to) {
            return;
        }
        if (from.block == to.block) {
            Block& ranges = m_blocks[from.block];
            ranges.erase(ranges.begin() + from.index, ranges.begin() + to.index);
            Settle(from.block, from.block + 1);
            return;
        }
        if (to.block < m_blocks.size()) {
            Block& last = m_blocks[to.block];
            last.erase(last.begin(), last.begin() + to.index);
        }
        Block& first = m_blocks[from.block];
        first.erase(first.begin() + from.index, first.end());
        m_blocks.erase(m_blocks.begin() + from.block + 1, m_blocks.begin() + to.block);
        m_last_upper.erase(m_last_upper.begin() + from.block + 1, m_last_upper.begin() + to.block);
        Settle(from.block, (std::min)(from.block + 2, m_blocks.size()));
    }

    /// Drops the empty blocks among [begin, end) and refreshes the others.
    void Settle(size_t begin, size_t end) {
        for (size_t block = end; block-- > begin;) {
            if (m_blocks[block].empty()) {
                m_blocks.erase(m_blocks.begin() + block);
                m_last_upper.erase(m_last_upper.begin() + block);
            } else {
                Refresh(block);
            }
        }
    }

    void Add(AddressType base_address, size_t size) {
        const AddressType end_address = base_address + static_cast<AddressType>(size);
        if (end_address <= base_address) {
            return;
        }
        // Ranges that touch the new one join it: the first ending at or after its start...
        const Pos first = FirstEnding<true>(base_address);
        if (first == End()) {
            Insert(first, Range{base_address, end_address});
            return;
        }
        // ...up to the first starting after its end.
        const Pos last = FirstStarting<false>(first, end_address);
        if (first == last) {
            Insert(first, Range{base_address, end_address});
            return;
        }
        const AddressType last_upper = At(Prev(last)).upper;
        Range& joined = At(first);
        joined.lower = (std::min)(joined.lower, base_address);
        joined.upper = (std::max)(last_upper, end_address);
        // The erase starts after `first`: its block stays where it is.
        Erase(Next(first), last);
        Refresh(first.block);
    }

    void Subtract(AddressType base_address, size_t size) {
        const AddressType end_address = base_address + static_cast<AddressType>(size);
        if (end_address <= base_address) {
            return;
        }
        const Pos first = FirstEnding<false>(base_address);
        if (first == End()) {
            return;
        }
        const Pos last = FirstStarting<true>(first, end_address);
        if (first == last) {
            return;
        }
        const Pos last_cut = Prev(last);
        const AddressType left_lower = At(first).lower;
        const AddressType right_upper = At(last_cut).upper;
        const bool keep_left = left_lower < base_address;
        const bool keep_right = end_address < right_upper;
        if (keep_left && keep_right && first == last_cut) {
            // Cut out of the middle of one range: it becomes two.
            At(first).upper = base_address;
            Refresh(first.block);
            Insert(Next(first), Range{end_address, right_upper});
            return;
        }
        Pos erase_from = first;
        Pos erase_to = last;
        if (keep_left) {
            At(first).upper = base_address;
            Refresh(first.block);
            erase_from = Next(first);
        }
        if (keep_right) {
            erase_to = last_cut;
            At(last_cut).lower = end_address;
        }
        Erase(erase_from, erase_to);
    }

    template <typename Func>
    void ForEach(Func&& func) const {
        for (const Block& ranges : m_blocks) {
            for (const Range& range : ranges) {
                func(range.lower, range.upper);
            }
        }
    }

    template <typename Func>
    void ForEachInRange(AddressType base_addr, size_t size, Func&& func) const {
        const AddressType start_address = base_addr;
        const AddressType end_address = start_address + static_cast<AddressType>(size);
        if (end_address <= start_address) {
            return;
        }
        for (Pos pos = FirstEnding<false>(start_address); pos != End(); pos = Next(pos)) {
            const Range& range = At(pos);
            if (range.lower >= end_address) {
                break;
            }
            func((std::max)(range.lower, start_address), (std::min)(range.upper, end_address));
        }
    }

    bool Intersects(AddressType base_address, size_t size) const {
        if (m_blocks.empty() || size == 0) {
            return false;
        }
        const AddressType end_address = base_address + static_cast<AddressType>(size);
        const Pos pos = FirstEnding<false>(base_address);
        return pos != End() && At(pos).lower < end_address;
    }

    void Clear() {
        m_blocks.clear();
        m_last_upper.clear();
    }

    void TakeFrom(RangeSetImpl& other) {
        m_blocks = std::move(other.m_blocks);
        m_last_upper = std::move(other.m_last_upper);
        // A moved-from container is only "valid but unspecified". Callers hand ranges over by
        // moving and then keep adding to the source (BufferCache::AccumulateFlushes).
        other.Clear();
    }

    std::vector<Block> m_blocks;
    /// Each block's last upper end, searched before the block itself.
    std::vector<AddressType> m_last_upper;
};

template <typename AddressType>
struct OverlapRangeSet<AddressType>::OverlapRangeSetImpl {
    using IntervalSet = boost::icl::split_interval_map<
        AddressType, s32, boost::icl::partial_enricher, std::less, boost::icl::inplace_plus,
        boost::icl::inter_section,
        ICL_INTERVAL_INSTANCE(ICL_INTERVAL_DEFAULT, AddressType, std::less), RangeSetsAllocator>;
    using IntervalType = typename IntervalSet::interval_type;

    OverlapRangeSetImpl() = default;
    ~OverlapRangeSetImpl() = default;

    void Add(AddressType base_address, size_t size) {
        AddressType end_address = base_address + static_cast<AddressType>(size);
        IntervalType interval{base_address, end_address};
        m_split_ranges_set += std::make_pair(interval, 1);
    }

    template <bool has_on_delete, typename Func>
    void Subtract(AddressType base_address, size_t size, s32 amount,
                  [[maybe_unused]] Func&& on_delete) {
        if (m_split_ranges_set.empty()) {
            return;
        }
        AddressType end_address = base_address + static_cast<AddressType>(size);
        IntervalType interval{base_address, end_address};
        m_split_ranges_set += std::make_pair(interval, -amount);
        // One pass, erasing the segments that dropped to zero or below in address order (the
        // search restarted after every erase: quadratic in the segments of the range, and the
        // GPU fencing thread does this for each downloaded buffer range while holding the buffer
        // cache's lock). Erasing a node leaves the other iterators valid.
        auto it = m_split_ranges_set.lower_bound(interval);
        const auto end_it = m_split_ranges_set.upper_bound(interval);
        while (it != end_it) {
            if (it->second > 0) {
                ++it;
                continue;
            }
            if constexpr (has_on_delete) {
                if (it->second == 0) {
                    on_delete(it->first.lower(), it->first.upper());
                }
            }
            m_split_ranges_set.erase(it++);
        }
    }

    template <typename Func>
    void ForEach(Func&& func) const {
        if (m_split_ranges_set.empty()) {
            return;
        }
        auto it = m_split_ranges_set.begin();
        auto end_it = m_split_ranges_set.end();
        for (; it != end_it; it++) {
            const AddressType inter_addr_end = it->first.upper();
            const AddressType inter_addr = it->first.lower();
            func(inter_addr, inter_addr_end, it->second);
        }
    }

    template <typename Func>
    void ForEachInRange(AddressType base_address, size_t size, Func&& func) const {
        if (m_split_ranges_set.empty()) {
            return;
        }
        const AddressType start_address = base_address;
        const AddressType end_address = start_address + size;
        const OverlapRangeSetImpl::IntervalType search_interval{start_address, end_address};
        auto it = m_split_ranges_set.lower_bound(search_interval);
        if (it == m_split_ranges_set.end()) {
            return;
        }
        auto end_it = m_split_ranges_set.upper_bound(search_interval);
        for (; it != end_it; it++) {
            auto& inter = it->first;
            AddressType inter_addr_end = inter.upper();
            AddressType inter_addr = inter.lower();
            if (inter_addr_end > end_address) {
                inter_addr_end = end_address;
            }
            if (inter_addr < start_address) {
                inter_addr = start_address;
            }
            func(inter_addr, inter_addr_end, it->second);
        }
    }

    IntervalSet m_split_ranges_set;
};

template <typename AddressType>
RangeSet<AddressType>::RangeSet() {
    m_impl = std::make_unique<RangeSet<AddressType>::RangeSetImpl>();
}

template <typename AddressType>
RangeSet<AddressType>::~RangeSet() = default;

template <typename AddressType>
RangeSet<AddressType>::RangeSet(RangeSet&& other) {
    m_impl = std::make_unique<RangeSet<AddressType>::RangeSetImpl>();
    m_impl->TakeFrom(*other.m_impl);
}

template <typename AddressType>
RangeSet<AddressType>& RangeSet<AddressType>::operator=(RangeSet&& other) {
    if (this != &other) {
        m_impl->TakeFrom(*other.m_impl);
    }
    return *this;
}

template <typename AddressType>
void RangeSet<AddressType>::Add(AddressType base_address, size_t size) {
    m_impl->Add(base_address, size);
}

template <typename AddressType>
void RangeSet<AddressType>::Subtract(AddressType base_address, size_t size) {
    m_impl->Subtract(base_address, size);
}

template <typename AddressType>
void RangeSet<AddressType>::Clear() {
    m_impl->Clear();
}

template <typename AddressType>
bool RangeSet<AddressType>::Empty() const {
    return m_impl->m_blocks.empty();
}

template <typename AddressType>
bool RangeSet<AddressType>::Intersects(AddressType base_address, size_t size) const {
    return m_impl->Intersects(base_address, size);
}

template <typename AddressType>
template <typename Func>
void RangeSet<AddressType>::ForEach(Func&& func) const {
    m_impl->ForEach(std::move(func));
}

template <typename AddressType>
template <typename Func>
void RangeSet<AddressType>::ForEachInRange(AddressType base_address, size_t size,
                                           Func&& func) const {
    m_impl->ForEachInRange(base_address, size, std::move(func));
}

template <typename AddressType>
OverlapRangeSet<AddressType>::OverlapRangeSet() {
    m_impl = std::make_unique<OverlapRangeSet<AddressType>::OverlapRangeSetImpl>();
}

template <typename AddressType>
OverlapRangeSet<AddressType>::~OverlapRangeSet() = default;

template <typename AddressType>
OverlapRangeSet<AddressType>::OverlapRangeSet(OverlapRangeSet&& other) {
    m_impl = std::make_unique<OverlapRangeSet<AddressType>::OverlapRangeSetImpl>();
    m_impl->m_split_ranges_set = std::move(other.m_impl->m_split_ranges_set);
}

template <typename AddressType>
OverlapRangeSet<AddressType>& OverlapRangeSet<AddressType>::operator=(OverlapRangeSet&& other) {
    m_impl->m_split_ranges_set = std::move(other.m_impl->m_split_ranges_set);
}

template <typename AddressType>
void OverlapRangeSet<AddressType>::Add(AddressType base_address, size_t size) {
    m_impl->Add(base_address, size);
}

template <typename AddressType>
void OverlapRangeSet<AddressType>::Subtract(AddressType base_address, size_t size) {
    m_impl->template Subtract<false>(base_address, size, 1, [](AddressType, AddressType) {});
}

template <typename AddressType>
template <typename Func>
void OverlapRangeSet<AddressType>::Subtract(AddressType base_address, size_t size,
                                            Func&& on_delete) {
    m_impl->template Subtract<true, Func>(base_address, size, 1, std::move(on_delete));
}

template <typename AddressType>
void OverlapRangeSet<AddressType>::DeleteAll(AddressType base_address, size_t size) {
    m_impl->template Subtract<false>(base_address, size, (std::numeric_limits<s32>::max)(),
                                     [](AddressType, AddressType) {});
}

template <typename AddressType>
void OverlapRangeSet<AddressType>::Clear() {
    m_impl->m_split_ranges_set.clear();
}

template <typename AddressType>
bool OverlapRangeSet<AddressType>::Empty() const {
    return m_impl->m_split_ranges_set.empty();
}

template <typename AddressType>
template <typename Func>
void OverlapRangeSet<AddressType>::ForEach(Func&& func) const {
    m_impl->ForEach(func);
}

template <typename AddressType>
template <typename Func>
void OverlapRangeSet<AddressType>::ForEachInRange(AddressType base_address, size_t size,
                                                  Func&& func) const {
    m_impl->ForEachInRange(base_address, size, std::move(func));
}

} // namespace Common
