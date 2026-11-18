#pragma once
#include "resource_picker.h"

class EventLedger : public QWidget
{
    Book *book = nullptr;
    QString bookFile;
    QLineEdit *person = new QLineEdit, *description = new QLineEdit;
    ResourcePicker *resources;
    QComboBox *sort = new QComboBox;
    QTableWidget *table = new QTableWidget;
    QLabel *summary = new QLabel;
    QPushButton *previous = new QPushButton("上一页"), *next = new QPushButton("下一页");
    LedgerFilter applied;
    qint64 page = 1;

    void showPage(qint64 requested)
    {
        try
        {
            const auto result = book->ledgerPage(applied, requested);
            page = result.number;
            table->setRowCount(result.rows.size());
            for (int r = 0; r < result.rows.size(); ++r)
                for (int c = 0; c < result.rows[r].size(); ++c)
                {
                    const auto &text = result.rows[r][c];
                    auto *item = new QTableWidgetItem(text);
                    item->setToolTip(Qt::convertFromPlainText(text));
                    if (c >= 1 && c <= 4)
                        item->setTextAlignment(Qt::AlignCenter);
                    table->setItem(r, c, item);
                }
            table->scrollToTop();
            summary->setText(QString("共 %1 条 · 每页 %2 条 · 第 %3 / %4 页")
                                 .arg(result.total)
                                 .arg(LedgerPage::pageSize)
                                 .arg(page)
                                 .arg(result.pages));
            previous->setEnabled(page > 1);
            next->setEnabled(page < result.pages);
        }
        catch (const std::exception &e)
        {
            table->setRowCount(0);
            previous->setEnabled(false);
            next->setEnabled(false);
            summary->setText("读取账本失败 · " + QString::fromUtf8(e.what()));
        }
    }
    void search()
    {
        applied = {person->text().trimmed(), resources->choices->currentData().toString(),
                   description->text().trimmed(), sort->currentIndex() == 0};
        showPage(1);
    }

  public:
    EventLedger(OpenResourcePage open, std::function<void()> record) : resources(new ResourcePicker(true, open))
    {
        auto *layout = new QVBoxLayout(this);
        layout->setContentsMargins(0, 0, 0, 0);
        auto *filters = new QGridLayout;
        layout->addLayout(filters);
        person->setPlaceholderText("人物姓名关键词");
        person->setObjectName("ledgerPerson");
        description->setPlaceholderText("事件内容关键词");
        description->setObjectName("ledgerDescription");
        person->setClearButtonEnabled(true);
        description->setClearButtonEnabled(true);
        filters->addWidget(person, 0, 0);
        filters->addWidget(description, 0, 1);
        auto *find = new QPushButton("搜索");
        filters->addWidget(find, 0, 2);
        filters->addWidget(resources, 1, 0);
        sort->addItems({"章节倒序", "章节正序"});
        sort->setObjectName("ledgerSort");
        sort->setAccessibleName("事件排序");
        filters->addWidget(sort, 1, 1, Qt::AlignLeft | Qt::AlignTop);
        auto *reset = new QPushButton("重置");
        filters->addWidget(reset, 1, 2, Qt::AlignTop);
        filters->setColumnStretch(0, 1);
        filters->setColumnStretch(1, 1);
        table->setObjectName("eventTable");
        table->setColumnCount(6);
        table->setHorizontalHeaderLabels({"章节", "人物", "资源", "变动", "结余", "事件"});
        table->verticalHeader()->hide();
        table->setAlternatingRowColors(true);
        table->setShowGrid(false);
        table->setSelectionBehavior(QAbstractItemView::SelectRows);
        table->setEditTriggers(QAbstractItemView::NoEditTriggers);
        table->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
        const int widths[] = {170, 90, 84, 68, 80};
        for (int c = 0; c < 5; ++c)
            table->setColumnWidth(c, widths[c]);
        table->horizontalHeader()->setStretchLastSection(true);
        layout->addWidget(table, 1);
        auto *pages = new QHBoxLayout;
        layout->addLayout(pages);
        summary->setTextFormat(Qt::PlainText);
        pages->addWidget(summary, 1);
        if (record)
        {
            auto *add = new QPushButton("记录事件");
            pages->addWidget(add);
            connect(add, &QPushButton::clicked, this, record);
        }
        pages->addWidget(previous);
        pages->addWidget(next);
        connect(find, &QPushButton::clicked, this, &EventLedger::search);
        connect(person, &QLineEdit::returnPressed, this, &EventLedger::search);
        connect(description, &QLineEdit::returnPressed, this, &EventLedger::search);
        connect(sort, &QComboBox::currentIndexChanged, this, &EventLedger::search);
        connect(resources->choices, &QComboBox::currentIndexChanged, this, &EventLedger::search);
        connect(previous, &QPushButton::clicked, this, [this] { showPage(page - 1); });
        connect(next, &QPushButton::clicked, this, [this] { showPage(page + 1); });
        connect(reset, &QPushButton::clicked, this, [this] {
            person->clear();
            description->clear();
            QSignalBlocker order(sort), resource(resources->choices);
            sort->setCurrentIndex(0);
            resources->choices->setCurrentIndex(0);
            search();
        });
    }
    void load(Book &source)
    {
        if (source.filePath() != bookFile)
        {
            applied = {};
            page = 1;
        }
        bookFile = source.filePath();
        book = &source;
        person->setText(applied.person);
        description->setText(applied.description);
        QSignalBlocker order(sort);
        sort->setCurrentIndex(applied.newestFirst ? 0 : 1);
        resources->load(source, applied.resource);
        showPage(page);
    }
};
