// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <filesystem>
#include "qt_common/abstract/frontend.h"
#include "qt_common/util/fs.h"
#include "ryujinx_dialog.h"
#include "ui_ryujinx_dialog.h"

RyujinxDialog::RyujinxDialog(std::filesystem::path eden_path, std::filesystem::path ryu_path,
                             QWidget* parent)
    : QDialog(parent), ui(new Ui::RyujinxDialog), m_eden(eden_path.make_preferred()),
      m_ryu(ryu_path.make_preferred()) {
    ui->setupUi(this);

    connect(ui->eden, &QPushButton::clicked, this, &RyujinxDialog::fromEden);
    connect(ui->ryujinx, &QPushButton::clicked, this, &RyujinxDialog::fromRyujinx);
    connect(ui->cancel, &QPushButton::clicked, this, &RyujinxDialog::reject);
}

RyujinxDialog::~RyujinxDialog() {
    delete ui;
}

void RyujinxDialog::fromEden() {
    accept();

    // Workaround: Ryujinx deletes and re-creates its directory structure???
    // So we just copy Eden's data to Ryujinx and then link the other way
    namespace fs = std::filesystem;
    // Linking deletes Eden's side, so the copy has to be whole before Ryujinx's data goes.
    fs::path copy = m_ryu;
    copy += ".eden-copy";
    std::error_code ec;
    fs::remove_all(copy, ec);
    try {
        fs::create_directories(copy);
        fs::copy(m_eden, copy, fs::copy_options::recursive);
        fs::remove_all(m_ryu);
        fs::rename(copy, m_ryu);
    } catch (std::exception& e) {
        fs::remove_all(copy, ec);
        QtCommon::Frontend::Critical(
            tr("Failed to link save data"),
            tr("OS returned error: %1").arg(QString::fromStdString(e.what())));
        return;
    }

    // ?ploo
    QtCommon::FS::LinkRyujinx(m_ryu, m_eden);
}

void RyujinxDialog::fromRyujinx() {
    accept();
    QtCommon::FS::LinkRyujinx(m_ryu, m_eden);
}
