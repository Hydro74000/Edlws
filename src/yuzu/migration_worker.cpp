// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include "common/fs/symlink.h"
#include "migration_worker.h"

#include <filesystem>
#include <QStringList>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>
#include <vector>
#include <QMap>
#include <boost/algorithm/string/predicate.hpp>
#include <boost/algorithm/string/replace.hpp>

#include "common/fs/path_util.h"

namespace {

namespace fs = std::filesystem;

// ProfileManager creates its profile file before the migration runs (see UserDataMigrator). Past
// that, this is someone's data: the wizard opens whenever the config directory is missing, and its
// own cancel message suggests deleting that directory to see it again.
bool HoldsUserData(const fs::path& dir) {
    std::error_code ec;
    for (fs::recursive_directory_iterator it{dir, ec}, end; !ec && it != end; it.increment(ec)) {
        if (!it->is_directory(ec) && it->path().filename() != "profiles.dat") {
            return true;
        }
    }
    // What can't be read can't be called empty.
    return ec && ec != std::errc::no_such_file_or_directory;
}

fs::path SetAside(const fs::path& dir) {
    fs::path backup = dir;
    backup += "-backup";
    for (int i = 2; fs::exists(fs::symlink_status(backup)); ++i) {
        backup = dir;
        backup += "-backup-" + std::to_string(i);
    }
    fs::rename(dir, backup);
    return backup;
}

void MoveDirectory(const fs::path& from, const fs::path& to) {
    std::error_code ec;
    fs::rename(from, to, ec);
    if (ec == std::errc::cross_device_link) {
        try {
            fs::copy(from, to, fs::copy_options::recursive | fs::copy_options::copy_symlinks);
        } catch (const fs::filesystem_error&) {
            fs::remove_all(to, ec);
            throw;
        }
        // The data is safe at `to`: what can't be deleted here only stays behind.
        fs::remove_all(from, ec);
    } else if (ec) {
        throw fs::filesystem_error("rename", from, to, ec);
    }
}

// In a portable install config and cache are children of the data directory.
bool IsInside(const fs::path& path, const fs::path& dir) {
    const fs::path relative = path.lexically_relative(dir);
    return !relative.empty() && *relative.begin() != "..";
}

} // namespace

MigrationWorker::MigrationWorker(const Emulator selected_emu_, const bool clear_shader_cache_,
                                 const MigrationStrategy strategy_)
    : QObject(), selected_emu(selected_emu_), clear_shader_cache(clear_shader_cache_),
      strategy(strategy_) {}

void MigrationWorker::process() {
    constexpr auto copy_options = fs::copy_options::update_existing | fs::copy_options::recursive;

    const fs::path legacy_user_dir = selected_emu.get_user_dir();
    const fs::path legacy_config_dir = selected_emu.get_config_dir();
    const fs::path legacy_cache_dir = selected_emu.get_cache_dir();

    // TODO(crueter): Make these constexpr since they're defaulted
    fs::path eden_dir = Common::FS::GetEdenPath(Common::FS::EdenPath::EdenDir);
    fs::path config_dir = Common::FS::GetEdenPath(Common::FS::EdenPath::ConfigDir);
    fs::path cache_dir = Common::FS::GetEdenPath(Common::FS::EdenPath::CacheDir);
    fs::path shader_dir = Common::FS::GetEdenPath(Common::FS::EdenPath::ShaderDir);

    eden_dir.make_preferred();
    config_dir.make_preferred();
    cache_dir.make_preferred();
    shader_dir.make_preferred();

    // Each directory to migrate, from and to.
    std::vector<std::pair<fs::path, fs::path>> dirs{{legacy_user_dir, eden_dir}};

    // Windows doesn't need more than that, because cache and config are already children of the
    // root directory
#ifndef WIN32
    if (fs::is_directory(legacy_config_dir)) {
        dirs.emplace_back(legacy_config_dir, config_dir);
    }
    if (fs::is_directory(legacy_cache_dir)) {
        dirs.emplace_back(legacy_cache_dir, cache_dir);
    }
#endif

    fs::path kept_data;
    size_t started = 0;
    std::error_code ec;
    try {
        if (HoldsUserData(eden_dir)) {
            kept_data = SetAside(eden_dir);
        } else {
            fs::remove_all(eden_dir, ec);
        }

        for (const auto& [from, to] : dirs) {
            ++started;
            if (!IsInside(to, eden_dir)) {
                // A cache is all that can be there, and every strategy needs the path free.
                fs::remove_all(to, ec);
            }
            switch (strategy) {
            case MigrationStrategy::Link:
                // Create symlinks/directory junctions if requested
                if (!Common::FS::CreateSymlink(from, to)) {
                    throw std::runtime_error("could not link " + to.string());
                }
                break;
            case MigrationStrategy::Move:
                // Rename directories if deletion is requested (achieves the same result)
                MoveDirectory(from, to);
                break;
            case MigrationStrategy::Copy:
            default:
                // Default behavior: copy
                fs::copy(from, to, copy_options);
                break;
            }
        }
    } catch (const std::exception& e) {
        // Newest first, down to the one that failed, which left nothing at `to`. Moved data has no
        // other copy: it goes back, and if it can't, the user has to know where it is.
        QStringList left_behind;
        for (size_t i = started; i-- > 0;) {
            const auto& [from, to] = dirs[i];
            if (strategy == MigrationStrategy::Link) {
                // NB: do NOT use remove_all, as Windows treats this as a remove_all to the target
                fs::remove(to, ec);
            } else if (strategy == MigrationStrategy::Move) {
                if (fs::exists(fs::symlink_status(to))) {
                    try {
                        MoveDirectory(to, from);
                    } catch (const std::exception&) {
                        left_behind.append(QString::fromStdString(to.string()));
                    }
                }
            } else {
                fs::remove_all(to, ec);
            }
        }
        if (!kept_data.empty()) {
            fs::rename(kept_data, eden_dir, ec);
            if (ec) {
                left_behind.append(QString::fromStdString(kept_data.string()));
            }
        }
        QString message = tr("The migration failed. Linking the old directory may need "
                             "administrative privileges on Windows.\nOS gave error: %1")
                              .arg(QString::fromStdString(e.what()));
        if (!left_behind.isEmpty()) {
            message.append(tr("\n\nIt could not be fully undone. Your data is safe in:\n%1")
                               .arg(left_behind.join(QLatin1Char('\n'))));
        }
        emit error(message);
        return;
    }

    if (strategy == MigrationStrategy::Link) {
        success_text.append(tr("\n\nNote that your configuration and data will be shared with %1.\n"
                               "If this is not desirable, delete the following files:\n%2\n%3\n%4")
                                .arg(selected_emu.name(), QString::fromStdString(eden_dir.string()),
                                     QString::fromStdString(config_dir.string()),
                                     QString::fromStdString(cache_dir.string())));
    } else if (strategy != MigrationStrategy::Move) {
        success_text.append(tr("\n\nIf you wish to clean up the files which were left in the old "
                               "data location, you can do so by deleting the following directory:\n"
                               "%1")
                                .arg(QString::fromStdString(legacy_user_dir.string())));
    }

    if (!kept_data.empty()) {
        success_text.append(tr("\n\nThe data that was already here has been kept in:\n%1")
                                .arg(QString::fromStdString(kept_data.string())));
    }

    // Delete and re-create shader dir
    if (clear_shader_cache) {
        fs::remove_all(shader_dir, ec);
        fs::create_directories(shader_dir, ec);
    }

    emit finished(success_text, legacy_user_dir.string());
}
