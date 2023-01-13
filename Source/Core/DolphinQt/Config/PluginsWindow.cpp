// Copyright 2015 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "DolphinQt/Config/PluginsWindow.h"

#include <QDialogButtonBox>
#include <QPushButton>
#include <QTabWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QRadioButton>
#include <QPushButton>
#include <string>
#include <vector>
#include <iostream>

#include "Common/FileUtil.h"
#include "Plugins/PluginHost.h"
#include "Plugins/PluginManager.h"


PluginsWindow::PluginsWindow(QWidget* parent) : QDialog(parent)
{
    setWindowTitle(tr("Plugins"));
    setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);

    QVBoxLayout* layout = new QVBoxLayout();
    QHBoxLayout* h_layout = new QHBoxLayout();

    m_plugins_list = new QListWidget();

    QDialogButtonBox* close = new QDialogButtonBox(QDialogButtonBox::Close);
    QPushButton* refresh = new QPushButton(this);
    refresh->setText(tr("Refresh"));
    connect(close, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(refresh, &QPushButton::pressed, this, &PluginsWindow::RefreshPluginsList);

    layout->addWidget(m_plugins_list);
    h_layout->addWidget(refresh);
    h_layout->addWidget(close);

    layout->addLayout(h_layout);

    connect(m_plugins_list, &QListWidget::itemChanged, this, &PluginsWindow::PluginItemChanged);

    setLayout(layout);
}

void PluginsWindow::LoadPluginsList()
{
    pluginsList = PluginManager::getPlugins();
    for(const auto& plugin : *pluginsList)
    {
        QListWidgetItem* item = new QListWidgetItem();;
        if(plugin.isScriptHost == 1) {
            item->setText(QString::fromStdString(plugin.name + " - ScriptHost"));
        }
        else {
            item->setText(QString::fromStdString(plugin.name));
        }
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        std::cerr << "isScriptHost: "<<plugin.isScriptHost << std::endl;
        item->setData(Qt::UserRole, QString::fromStdString(plugin.mainfile));
        item->setCheckState(plugin.Active ? Qt::Checked : Qt::Unchecked);
        m_plugins_list->addItem(item);
    }
}

void PluginsWindow::PluginItemChanged(QListWidgetItem* item)
{
    const auto plugin_path = item->data(Qt::UserRole).toString();

    for(u32 i = 0; i < pluginsList->size(); i++)
    {
        if(pluginsList->at(i).mainfile == plugin_path.toStdString())
        {
            if(!pluginsList->at(i).Active)
                Plugins::LoadPlugin(i);
            if(pluginsList->at(i).Active)
                Plugins::ShutdownPlugin(i);
        }
    }
    RefreshPluginsList();
}