#pragma once
#include "downward_combo.h"
#include "book.h"
#include <QtWidgets>

class CharacterSnapshot : public QWidget
{
    Book *book = nullptr;
    int person = 0;
    QString file;
    QComboBox *chapters = new DownwardCombo;
    QScrollArea *report = new QScrollArea;
    QLabel *message = new QLabel;
    QTableWidget *balancesTable, *itemsTable, *suppliesTable, *skillsTable;

    static void fitTableHeight(QTableWidget *table)
    {
        const auto *bar = table->horizontalScrollBar();
        const int scrollbar = bar->maximum() > bar->minimum() ? bar->sizeHint().height() : 0;
        table->setFixedHeight(table->horizontalHeader()->sizeHint().height() + table->verticalHeader()->length() +
                              2 * table->frameWidth() + scrollbar);
    }

    QTableWidget *addTable(QVBoxLayout *layout, const QString &title, const QString &id, const QStringList &headers)
    {
        auto *heading = new QLabel(title);
        heading->setObjectName("sectionTitle");
        layout->addWidget(heading);
        auto *table = new QTableWidget(0, headers.size());
        heading->setBuddy(table);
        table->setObjectName(id);
        table->setAccessibleName(title);
        table->setHorizontalHeaderLabels(headers);
        table->setEditTriggers(QAbstractItemView::NoEditTriggers);
        table->setSelectionBehavior(QAbstractItemView::SelectRows);
        table->setAlternatingRowColors(true);
        table->setWordWrap(false);
        table->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        table->setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
        table->verticalHeader()->hide();
        table->verticalHeader()->setSectionResizeMode(QHeaderView::Fixed);
        table->verticalHeader()->setDefaultSectionSize(table->fontMetrics().height() + 10);
        auto *header = table->horizontalHeader();
        header->setMinimumSectionSize(table->fontMetrics().horizontalAdvance("剩余次数") + 12);
        header->setSectionResizeMode(QHeaderView::Stretch);
        table->horizontalHeaderItem(0)->setTextAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        table->setStyleSheet("QTableWidget {padding:0; gridline-color:#345140;}"
                             "QTableWidget::item {padding:0 4px;}"
                             "QHeaderView::section {padding:4px; border-right:1px solid #345140;}");
        connect(table->horizontalScrollBar(), &QScrollBar::rangeChanged, table, [table] { fitTableHeight(table); });
        layout->addWidget(table);
        layout->addSpacing(6);
        return table;
    }

    static void fillTable(QTableWidget *table, const QList<QStringList> &rows)
    {
        table->clearSpans();
        table->clearContents();
        table->setRowCount(qMax(qsizetype(1), rows.size()));
        if (rows.isEmpty())
        {
            if (table->columnCount() > 1)
                table->setSpan(0, 0, 1, table->columnCount());
            auto *empty = new QTableWidgetItem("无");
            empty->setForeground(QColor("#a0bcac"));
            empty->setTextAlignment(Qt::AlignCenter);
            table->setItem(0, 0, empty);
        }
        for (int row = 0; row < rows.size(); ++row)
            for (int col = 0; col < rows[row].size(); ++col)
            {
                auto *cell = new QTableWidgetItem(rows[row][col]);
                cell->setToolTip(Qt::convertFromPlainText(cell->text()));
                cell->setTextAlignment(col == 0 ? Qt::AlignLeft | Qt::AlignVCenter : Qt::AlignCenter);
                table->setItem(row, col, cell);
            }
        fitTableHeight(table);
    }

    void showMessage(const QString &text)
    {
        message->setText(text);
        message->setVisible(!text.isEmpty());
        report->setVisible(text.isEmpty());
    }

    void refresh()
    {
        const auto chapter = chapters->currentData().toInt();
        if (!book || !person || !chapter)
        {
            showMessage("选择人物和章节后可查看当章状态。");
            return;
        }
        try
        {
            QList<QStringList> rows;
            const auto balances = book->resourceBalances(person, chapter);
            for (auto it = balances.cbegin(); it != balances.cend(); ++it)
                rows.append({it.key(), it.value()});
            fillTable(balancesTable, rows);
            rows.clear();
            for (const auto &name : book->ownedItems(person, chapter))
                rows.append(QStringList{name});
            fillTable(itemsTable, rows);
            rows.clear();
            const auto supplies = book->supplyCounts(person, chapter);
            for (auto it = supplies.cbegin(); it != supplies.cend(); ++it)
                if (it.value() > 0)
                    rows.append({it.key(), QString::number(it.value())});
            fillTable(suppliesTable, rows);
            rows.clear();
            for (const auto &skill : book->characterSkills(person, chapter))
                rows.append({skill.name, skill.level.isEmpty() ? "未填写" : skill.level,
                             skill.cooldown.isEmpty() ? "未填写" : skill.cooldown,
                             skill.remaining < 0 ? "不限" : QString::number(skill.remaining)});
            fillTable(skillsTable, rows);
            showMessage({});
        }
        catch (const std::exception &e)
        {
            showMessage(QString::fromUtf8(e.what()));
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
        report->setWidgetResizable(true);
        report->setFrameShape(QFrame::NoFrame);
        auto *body = new QWidget;
        auto *sections = new QVBoxLayout(body);
        sections->setContentsMargins(0, 0, 0, 0);
        balancesTable = addTable(sections, "资源余额", "snapshotBalances", {"名称", "余额"});
        itemsTable = addTable(sections, "持有道具", "snapshotItems", {"名称"});
        suppliesTable = addTable(sections, "消耗品", "snapshotSupplies", {"名称", "数量"});
        skillsTable = addTable(sections, "技能／功法", "snapshotSkills", {"名称", "等级", "冷却时间", "剩余次数"});
        sections->addStretch();
        report->setWidget(body);
        message->setTextFormat(Qt::PlainText);
        message->setAlignment(Qt::AlignTop | Qt::AlignLeft);
        message->setWordWrap(true);
        layout->addWidget(message);
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
