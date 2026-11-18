#pragma once
#include "book.h"
#include <QtWidgets>
#include <limits>

class HistorySettings : public QWidget
{
    Book *book = nullptr;
    QLabel *usage = new QLabel;
    QSpinBox *chapter = new QSpinBox, *wholeBook = new QSpinBox;
    QWidget *form = new QWidget;
    QPushButton *edit = new QPushButton("设置历史上限"), *apply = new QPushButton;
    HistoryLimits current;
    QLabel *message = new QLabel;
    qint64 removal = 0;

    HistoryLimits fields() const
    {
        return {chapter->value(), wholeBook->value()};
    }
    void preview()
    {
        try
        {
            removal = book->historyRemovalCount(fields());
            apply->setText(removal ? QString("应用并移除 %1 份").arg(removal) : QString("应用上限"));
            apply->setEnabled(true);
            message->setText(!chapter->value() || !wholeBook->value() ? "设为 0 后暂停记录新历史，章节正文照常保存。"
                             : removal                                ? "将优先保留近期版本，移除的历史无法恢复。"
                                                                      : QString());
        }
        catch (const std::exception &e)
        {
            message->setText(QString::fromUtf8(e.what()));
            apply->setEnabled(false);
        }
    }

  public:
    HistorySettings()
    {
        auto *layout = new QVBoxLayout(this);
        layout->setContentsMargins(0, 0, 0, 0);
        auto *row = new QHBoxLayout;
        layout->addLayout(row);
        usage->setObjectName("historyUsage");
        usage->setWordWrap(true);
        row->addWidget(usage, 1);
        row->addWidget(edit);
        layout->addWidget(form);
        auto *options = new QHBoxLayout(form);
        options->setContentsMargins(0, 0, 0, 0);
        chapter->setObjectName("chapterHistoryLimit");
        wholeBook->setObjectName("bookHistoryLimit");
        auto field = [&](QString label, QSpinBox *input) {
            options->addWidget(new QLabel(label));
            input->setAccessibleName(label);
            input->setRange(0, std::numeric_limits<int>::max());
            options->addWidget(input);
            connect(input, &QSpinBox::valueChanged, this, [this] { preview(); });
        };
        field("每章最多", chapter);
        field("全书最多", wholeBook);
        options->addStretch();
        auto *cancel = new QPushButton("取消");
        options->addWidget(cancel);
        options->addWidget(apply);
        message->setWordWrap(true);
        layout->addWidget(message);
        form->hide();
        message->hide();
        connect(cancel, &QPushButton::clicked, this, [this] {
            form->hide();
            message->hide();
            edit->show();
        });
        connect(edit, &QPushButton::clicked, this, [this] {
            QSignalBlocker blockChapter(chapter), blockBook(wholeBook);
            chapter->setValue(current.perChapter);
            wholeBook->setValue(current.perBook);
            form->show();
            edit->hide();
            message->show();
            preview();
        });
        connect(apply, &QPushButton::clicked, this, [this] {
            try
            {
                book->setHistoryLimits(fields(), removal);
                load(book);
                message->setText("✓ 本书历史上限已保存");
                message->show();
            }
            catch (const std::exception &e)
            {
                preview();
                message->setText(QString::fromUtf8(e.what()));
            }
        });
    }
    void load(Book *source)
    {
        book = source;
        current = book->historyLimits();
        const auto stats = book->historyUsage();
        QString text = QString("历史版本共 %1 份 · 历史正文占用 %2")
                           .arg(stats.count)
                           .arg(QLocale().formattedDataSize(stats.bytes));
        if (stats.legacyCount)
            text += QString("（含 %1 份受保护的旧版历史）").arg(stats.legacyCount);
        usage->setText(text + QString("\n每章最多 %1 份 · 全书最多 %2 份 · 容量上限 %3 MiB")
                                  .arg(current.perChapter)
                                  .arg(current.perBook)
                                  .arg(Book::historyBudgetMiB));
        usage->setToolTip(QString("所有历史版本正文合计 %1 字节。").arg(stats.bytes));
        form->hide();
        message->hide();
        edit->show();
    }
};
