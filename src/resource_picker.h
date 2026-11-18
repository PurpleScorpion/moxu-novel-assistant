#pragma once
#include "book.h"
#include <QtWidgets>

class ResourcePicker : public QWidget
{
    Book *book = nullptr;
    bool includeAll;
    QWidget *creation = new QWidget;
    QLineEdit *name = new QLineEdit;
    QLabel *error = new QLabel;
    QPushButton *add = new QPushButton("新建资源");

    void closeCreation()
    {
        creation->hide();
        error->hide();
        name->clear();
        add->setText("新建资源");
    }

  public:
    QComboBox *choices = new QComboBox;

    explicit ResourcePicker(bool includeAll) : includeAll(includeAll)
    {
        auto *layout = new QVBoxLayout(this);
        layout->setContentsMargins(0, 0, 0, 0);
        auto *row = new QHBoxLayout;
        layout->addLayout(row);
        choices->setAccessibleName(includeAll ? "筛选资源" : "事件资源");
        choices->setObjectName("resourceChoice");
        choices->setMaxVisibleItems(12);
        choices->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
        row->addWidget(choices, 1);
        row->addWidget(add);
        layout->addWidget(creation);
        auto *fields = new QHBoxLayout(creation);
        fields->setContentsMargins(0, 0, 0, 0);
        name->setPlaceholderText("新资源名称");
        name->setObjectName("newResourceName");
        fields->addWidget(name, 1);
        auto *create = new QPushButton("创建");
        fields->addWidget(create);
        error->setTextFormat(Qt::PlainText);
        error->setWordWrap(true);
        error->setObjectName("actionError");
        layout->addWidget(error);
        closeCreation();
        connect(add, &QPushButton::clicked, this, [this] {
            if (creation->isHidden())
            {
                creation->show();
                add->setText("取消");
                name->setFocus();
            }
            else
                closeCreation();
        });
        connect(name, &QLineEdit::returnPressed, create, &QPushButton::click);
        connect(create, &QPushButton::clicked, this, [this] {
            try
            {
                const auto resource = name->text().trimmed();
                book->createResource(resource);
                choices->addItem(resource, resource);
                choices->setCurrentIndex(choices->count() - 1);
                closeCreation();
            }
            catch (const std::exception &e)
            {
                error->setText(QString::fromUtf8(e.what()));
                error->show();
            }
        });
    }
    bool isCreating() const
    {
        return !creation->isHidden();
    }
    void load(Book &source, const QString &selection = {})
    {
        const auto names = source.resourceNames();
        book = &source;
        QSignalBlocker block(choices);
        closeCreation();
        choices->clear();
        if (includeAll)
            choices->addItem("全部资源");
        for (const auto &resource : names)
            choices->addItem(resource, resource);
        choices->setCurrentIndex(qMax(0, choices->findData(selection)));
    }
};
