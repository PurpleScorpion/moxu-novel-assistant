#pragma once
#include "downward_combo.h"
#include "resource_catalog.h"
#include <QtWidgets>

using OpenResourcePage = std::function<void(bool, std::function<void(const CatalogEntry &)>)>;

class ResourcePicker : public QWidget
{
    bool includeAll;
    Book *book = nullptr;

  public:
    QComboBox *choices = new DownwardCombo;
    ResourcePicker(bool includeAll, OpenResourcePage open) : includeAll(includeAll)
    {
        auto *row = new QHBoxLayout(this);
        row->setContentsMargins(0, 0, 0, 0);
        choices->setAccessibleName(includeAll ? "筛选资源" : "事件资源");
        choices->setObjectName("resourceChoice");
        choices->setMaxVisibleItems(12);
        choices->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
        row->addWidget(choices, 1);
        auto *add = new QPushButton("新建资源");
        row->addWidget(add);
        add->setVisible(bool(open));
        connect(add, &QPushButton::clicked, this, [this, open] {
            open(!this->includeAll, [this](const CatalogEntry &entry) { load(*book, entry.name, entry.kind, true); });
        });
    }
    void load(Book &source, const QString &selection = {}, const QString &kind = {}, bool notify = false)
    {
        book = &source;
        QSignalBlocker block(choices);
        choices->clear();
        if (includeAll)
            choices->addItem("全部资源");
        auto entries = source.catalog(includeAll ? QString() : QString("numeric"), includeAll);
        QHash<QString, int> counts;
        for (const auto &entry : entries)
            ++counts[entry.name];
        int selected = 0;
        for (const auto &entry : entries)
        {
            choices->addItem(counts[entry.name] > 1 ? entry.name + " · " + resourceKindLabel(entry.kind) : entry.name,
                             entry.name);
            int index = choices->count() - 1;
            choices->setItemData(index, entry.kind, Qt::UserRole + 1);
            if (entry.name == selection && (kind.isEmpty() || entry.kind == kind))
                selected = index;
        }
        if (notify)
        {
            choices->setCurrentIndex(-1);
            block.unblock();
        }
        choices->setCurrentIndex(selected);
    }
};
