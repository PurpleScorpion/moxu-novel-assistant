#pragma once
#include "book.h"
#include <QtWidgets>

class CharacterSnapshot : public QWidget
{
    Book *book = nullptr;
    int person = 0;
    QString file;
    QComboBox *chapters = new QComboBox;
    QTextBrowser *report = new QTextBrowser;

    void refresh()
    {
        report->clear();
        const auto chapter = chapters->currentData().toInt();
        if (!book || !person || !chapter)
        {
            report->setPlainText("选择人物和章节后可查看当章状态。");
            return;
        }
        try
        {
            QString html;
            auto section = [&](const QString &title, const QStringList &headers, const QList<QStringList> &rows) {
                html += "<h3 style='color:#9ae9aa'>" + title + "</h3>";
                if (rows.isEmpty())
                {
                    html += "<p>无</p>";
                    return;
                }
                html += "<table cellspacing='0' cellpadding='5'><tr>";
                for (const auto &header : headers)
                    html += "<td><b>" + header + "</b></td>";
                html += "</tr>";
                for (const auto &row : rows)
                {
                    html += "<tr>";
                    for (const auto &cell : row)
                        html += "<td>" + cell.toHtmlEscaped() + "</td>";
                    html += "</tr>";
                }
                html += "</table>";
            };
            QList<QStringList> rows;
            const auto balances = book->resourceBalances(person, chapter);
            for (auto it = balances.cbegin(); it != balances.cend(); ++it)
                rows.append({it.key(), it.value()});
            section("资源余额", {"名称", "余额"}, rows);
            rows.clear();
            for (const auto &name : book->ownedItems(person, chapter))
                rows.append(QStringList{name});
            section("持有道具", {"名称"}, rows);
            rows.clear();
            const auto supplies = book->supplyCounts(person, chapter);
            for (auto it = supplies.cbegin(); it != supplies.cend(); ++it)
                if (it.value() > 0)
                    rows.append({it.key(), QString::number(it.value())});
            section("消耗品", {"名称", "数量"}, rows);
            rows.clear();
            for (const auto &skill : book->characterSkills(person, chapter))
                rows.append({skill.name, skill.level.isEmpty() ? "未填写" : skill.level,
                             skill.cooldown.isEmpty() ? "未填写" : skill.cooldown,
                             skill.remaining < 0 ? "不限" : QString::number(skill.remaining)});
            section("技能／功法", {"名称", "等级", "冷却时间", "剩余次数"}, rows);
            report->setHtml(html);
        }
        catch (const std::exception &e)
        {
            report->setPlainText(QString::fromUtf8(e.what()));
        }
    }

  public:
    CharacterSnapshot()
    {
        auto *layout = new QVBoxLayout(this);
        auto *row = new QHBoxLayout;
        layout->addLayout(row);
        row->addWidget(new QLabel("截至章末"));
        chapters->setObjectName("snapshotChapter");
        chapters->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
        row->addWidget(chapters, 1);
        report->setObjectName("snapshotReport");
        layout->addWidget(report, 1);
        connect(chapters, &QComboBox::currentIndexChanged, this, &CharacterSnapshot::refresh);
    }
    void load(Book &source, int preferredChapter)
    {
        const int selected = file == source.filePath() ? chapters->currentData().toInt() : 0;
        file = source.filePath();
        book = &source;
        QSignalBlocker block(chapters);
        chapters->clear();
        auto q = source.run("SELECT id,title FROM chapters ORDER BY id");
        while (q.next())
            chapters->addItem(numberedChapter(chapters->count() + 1, q.value(1).toString()), q.value(0));
        int index = chapters->findData(selected ? selected : preferredChapter);
        chapters->setCurrentIndex(index < 0 ? chapters->count() - 1 : index);
    }
    void selectPerson(int id)
    {
        person = id;
        if (isVisible())
            refresh();
    }
    void showEvent(QShowEvent *event) override
    {
        QWidget::showEvent(event);
        refresh();
    }
};
