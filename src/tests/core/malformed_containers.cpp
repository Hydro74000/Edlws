// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <cstring>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "common/common_types.h"
#include "core/file_sys/partition_filesystem.h"
#include "core/file_sys/romfs.h"
#include "core/file_sys/vfs/vfs_vector.h"
#include "core/loader/loader.h"

namespace {

using FileSys::PartitionFilesystem;

constexpr u32 NoEntry = 0xFFFFFFFF;

template <typename T>
void Put(std::vector<u8>& bytes, size_t offset, T value) {
    std::memcpy(bytes.data() + offset, &value, sizeof(T));
}

FileSys::VirtualFile File(std::vector<u8> bytes) {
    return std::make_shared<FileSys::VectorVfsFile>(std::move(bytes), "probe");
}

// One name, "a", without a terminator: it ends with the string table.
std::vector<u8> Pfs(u32 entries, u32 name_offset) {
    std::vector<u8> bytes(0x10 + size_t{entries} * 0x18 + 1);
    std::memcpy(bytes.data(), "PFS0", 4);
    Put<u32>(bytes, 0x04, entries);
    Put<u32>(bytes, 0x08, 1);
    for (u32 i = 0; i < entries; ++i) {
        Put<u32>(bytes, 0x10 + size_t{i} * 0x18 + 0x10, name_offset);
    }
    bytes.back() = 'a';
    return bytes;
}

// A root directory followed by one file.
std::vector<u8> RomFs(u32 dir_sibling, u32 child_dir, u32 child_file, u32 file_sibling) {
    std::vector<u8> bytes(0x88);
    Put<u64>(bytes, 0x00, 0x50); // header size
    Put<u64>(bytes, 0x18, 0x50); // directory metadata
    Put<u64>(bytes, 0x20, 0x18);
    Put<u64>(bytes, 0x38, 0x68); // file metadata
    Put<u64>(bytes, 0x40, 0x20);
    Put<u64>(bytes, 0x48, 0x88); // data
    Put<u32>(bytes, 0x54, dir_sibling);
    Put<u32>(bytes, 0x58, child_dir);
    Put<u32>(bytes, 0x5c, child_file);
    Put<u32>(bytes, 0x6c, file_sibling);
    return bytes;
}

} // namespace

TEST_CASE("PFS keeps a name that ends with the string table", "[file_sys]") {
    const PartitionFilesystem pfs{File(Pfs(1, 0))};
    REQUIRE(pfs.GetStatus() == Loader::ResultStatus::Success);
    REQUIRE(pfs.GetFiles().size() == 1);
    REQUIRE(pfs.GetFiles()[0]->GetName() == "a");
}

TEST_CASE("PFS rejects a name outside the string table", "[file_sys]") {
    const PartitionFilesystem pfs{File(Pfs(1, 0x7fffffff))};
    REQUIRE(pfs.GetStatus() == Loader::ResultStatus::ErrorBadPFSHeader);
}

TEST_CASE("PFS reads more entries than 16 bits count", "[file_sys]") {
    const PartitionFilesystem pfs{File(Pfs(0x10000, 0))};
    REQUIRE(pfs.GetStatus() == Loader::ResultStatus::Success);
    REQUIRE(pfs.GetFiles().size() == 0x10000);
}

TEST_CASE("PFS rejects metadata larger than its file before reading it", "[file_sys]") {
    auto bytes = Pfs(1, 0);
    Put<u32>(bytes, 0x04, 0xffffffff);
    const PartitionFilesystem pfs{File(std::move(bytes))};
    REQUIRE(pfs.GetStatus() == Loader::ResultStatus::ErrorIncorrectPFSFileSize);
}

TEST_CASE("RomFS reads a well formed table", "[file_sys]") {
    const auto root = FileSys::ExtractRomFS(File(RomFs(NoEntry, NoEntry, 0, NoEntry)));
    REQUIRE(root != nullptr);
    REQUIRE(root->GetFiles().size() == 1);
}

TEST_CASE("RomFS rejects cycles", "[file_sys]") {
    REQUIRE(FileSys::ExtractRomFS(File(RomFs(NoEntry, 0, NoEntry, NoEntry))) == nullptr);
    REQUIRE(FileSys::ExtractRomFS(File(RomFs(0, NoEntry, NoEntry, NoEntry))) == nullptr);
    REQUIRE(FileSys::ExtractRomFS(File(RomFs(NoEntry, NoEntry, 0, 0))) == nullptr);
}

TEST_CASE("RomFS rejects offsets outside its tables", "[file_sys]") {
    REQUIRE(FileSys::ExtractRomFS(File(RomFs(0x1000, NoEntry, NoEntry, NoEntry))) == nullptr);
    REQUIRE(FileSys::ExtractRomFS(File(RomFs(NoEntry, 0x1000, NoEntry, NoEntry))) == nullptr);
    REQUIRE(FileSys::ExtractRomFS(File(RomFs(NoEntry, NoEntry, 0, 0x1000))) == nullptr);
}

TEST_CASE("RomFS rejects metadata larger than its file before reading it", "[file_sys]") {
    auto bytes = RomFs(NoEntry, NoEntry, NoEntry, NoEntry);
    Put<u64>(bytes, 0x20, u64{1} << 60);
    REQUIRE(FileSys::ExtractRomFS(File(std::move(bytes))) == nullptr);
}
