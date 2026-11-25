#pragma once

#include "book.h"
#include <QtWidgets>
#include <array>

class BookProgress : public QFrame
{
    const QStringList columns{"全文", "已发布", "待发布", "未发布"};
    std::array<QLabel *, 4> chapterCounts{}, wordCounts{};

    static QString displayWords(qint64 words)
    {
        if (words < 10000)
            return QString::number(words);
        QString number = QString::number(words / 10000.0, 'f', 2);
        while (number.endsWith('0'))
            number.chop(1);
        if (number.endsWith('.'))
            number.chop(1);
        return number + "万";
    }

  public:
    BookProgress()
    {
        setObjectName("bookProgress");
        setAccessibleName("书籍写作进度");
        setStyleSheet(R"(
            QFrame#bookProgress { background:#10231a; border:1px solid #355140; border-radius:8px; }
            QLabel { background:transparent; color:#b4cbbd; }
            QLabel[metric="true"] { color:#e0eee5; font-size:19px; font-weight:600; }
            QLabel[metric="true"][total="true"] { color:#abefbc; }
            QLabel[header="true"] { font-weight:600; }
            QFrame#progressRule { background:#304a3a; border:none; }
        )");
        auto *grid = new QGridLayout(this);
        grid->setContentsMargins(16, 12, 16, 12);
        grid->setHorizontalSpacing(8);
        grid->setVerticalSpacing(6);
        grid->addWidget(new QLabel("写作进度"), 0, 0);
        grid->addWidget(new QLabel("章节"), 2, 0);
        grid->addWidget(new QLabel("字数"), 3, 0);
        auto *rule = new QFrame;
        rule->setObjectName("progressRule");
        rule->setFixedHeight(1);
        grid->addWidget(rule, 1, 0, 1, 5);
        for (int i = 0; i < columns.size(); ++i)
        {
            auto *header = new QLabel(columns[i]);
            header->setProperty("header", true);
            header->setAlignment(Qt::AlignCenter);
            grid->addWidget(header, 0, i + 1);
            grid->setColumnStretch(i + 1, 1);
            auto metric = [&](int row, const QString &name) {
                auto *label = new QLabel;
                label->setObjectName(name + QString::number(i));
                label->setAccessibleName(columns[i] + (row == 2 ? "章节数" : "字数"));
                label->setAlignment(Qt::AlignCenter);
                label->setProperty("metric", true);
                label->setProperty("total", i == 0);
                grid->addWidget(label, row, i + 1);
                return label;
            };
            chapterCounts[i] = metric(2, "progressChapters");
            wordCounts[i] = metric(3, "progressWords");
        }
    }

    void load(Book &book)
    {
        book.refreshWordCounts();
        std::array<qint64, 4> chapters{}, words{};
        auto groups = book.run("SELECT publication_status,count(*),sum(word_count) FROM chapters GROUP BY publication_status");
        while (groups.next())
        {
            const auto i = columns.indexOf(groups.value(0).toString());
            if (i <= 0)
                throw std::runtime_error("无法识别章节的发布状态。");
            chapters[i] = groups.value(1).toLongLong();
            words[i] = groups.value(2).toLongLong();
            chapters[0] += chapters[i];
            words[0] += words[i];
        }
        for (int i = 0; i < columns.size(); ++i)
        {
            chapterCounts[i]->setText(QString::number(chapters[i]) + " 章");
            wordCounts[i]->setText(displayWords(words[i]));
            const auto exact = QString::number(words[i]) + " 字";
            wordCounts[i]->setToolTip(exact);
            wordCounts[i]->setAccessibleDescription(exact);
        }
    }
};
