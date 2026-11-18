#pragma once
#include "book.h"
#include <QtWidgets>

using OpenResourcePage = std::function<void(std::function<void(const QString &)>)>;

class ResourcePicker : public QWidget
{
    bool includeAll;

  public:
    QComboBox *choices = new QComboBox;
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
            open([this](const QString &name) {
                choices->addItem(name, name);
                choices->setCurrentIndex(choices->count() - 1);
            });
        });
    }
    void load(Book &source, const QString &selection = {})
    {
        QSignalBlocker block(choices);
        choices->clear();
        if (includeAll)
            choices->addItem("全部资源");
        for (const auto &resource : source.resourceNames())
            choices->addItem(resource, resource);
        choices->setCurrentIndex(qMax(0, choices->findData(selection)));
    }
};
