#pragma once
#include "downward_combo.h"
#include "book.h"
#include <QtWidgets>

class WriterSheet : public QScrollArea
{
  public:
    QStackedWidget *pages = new QStackedWidget;
    QVBoxLayout *form;
    QLabel *message;
    std::function<void()> refreshReferences;
    WriterSheet(const QString &title, std::function<void()> close)
    {
        setObjectName("writerSheet");
        setWidgetResizable(true);
        setMinimumWidth(290);
        auto *content = new QWidget;
        content->setObjectName("sheetContent");
        pages->addWidget(content);
        setWidget(pages);
        form = new QVBoxLayout(content);
        form->setContentsMargins(15, 14, 15, 16);
        form->setSpacing(10);
        form->setAlignment(Qt::AlignTop);
        auto *row = new QHBoxLayout;
        form->addLayout(row);
        auto *heading = new QLabel(title);
        heading->setWordWrap(true);
        heading->setObjectName("sectionTitle");
        row->addWidget(heading, 1);
        auto *back = new QPushButton("收起");
        row->addWidget(back);
        connect(back, &QPushButton::clicked, this, [close] { close(); });
        message = new QLabel;
        message->setObjectName("sheetMessage");
        message->setWordWrap(true);
        message->hide();
        form->addWidget(message);
    }
    void report(const QString &text)
    {
        message->setTextFormat(Qt::PlainText);
        message->setText(text);
        message->setVisible(!text.isEmpty());
        if (!text.isEmpty())
            ensureWidgetVisible(message);
    }
    QComboBox *choice(const QString &label, QSqlQuery rows)
    {
        form->addWidget(new QLabel(label));
        auto *list = new DownwardCombo;
        list->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
        form->addWidget(list);
        QHash<QString, int> counts;
        while (rows.next())
        {
            const auto label = rows.value(1).toString();
            list->addItem(label, rows.value(0));
            ++counts[label];
        }
        for (int i = 0; i < list->count(); ++i)
        {
            const auto label = list->itemText(i);
            if (counts.value(label) > 1)
                list->setItemText(i, label + "  ·  #" + list->itemData(i).toString());
        }
        return list;
    }
    QLineEdit *field(const QString &label, const QString &value = {})
    {
        form->addWidget(new QLabel(label));
        auto *input = new QLineEdit(value);
        form->addWidget(input);
        return input;
    }
    QPlainTextEdit *textArea(const QString &label, bool readOnly = false)
    {
        form->addWidget(new QLabel(label));
        auto *input = new QPlainTextEdit;
        input->setReadOnly(readOnly);
        form->addWidget(input, 1);
        return input;
    }
};
