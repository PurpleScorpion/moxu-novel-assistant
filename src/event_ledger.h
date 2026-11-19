#pragma once
#include "downward_combo.h"
#include "resource_picker.h"
#include "event_form.h"

class EventLedger : public QStackedWidget
{
    Book *book = nullptr;
    QString bookFile;
    QLineEdit *person = new QLineEdit, *description = new QLineEdit;
    ResourcePicker *resources;
    QComboBox *sort = new DownwardCombo;
    QTableWidget *table = new QTableWidget;
    QLabel *summary = new QLabel;
    QPushButton *previous = new QPushButton("上一页"), *next = new QPushButton("下一页");
    LedgerFilter applied;
    qint64 page = 1;
    int preferredChapter = 0;
    std::function<void(int, const QString &)> appended;

    OpenResourcePage createResource()
    {
        return [this](bool numericOnly, auto done) { openCatalogCreator(this, *book, numericOnly, done); };
    }
    void closeRecord()
    {
        auto *record = currentWidget();
        setCurrentIndex(0);
        removeWidget(record);
        record->deleteLater();
    }
    void openRecord()
    {
        auto *scroll = new QScrollArea(this);
        scroll->setWidgetResizable(true);
        auto *content = new QWidget;
        scroll->setWidget(content);
        auto *layout = new QVBoxLayout(content);
        auto *bar = new QHBoxLayout;
        layout->addLayout(bar);
        auto *title = new QLabel("记录事件");
        title->setObjectName("sectionTitle");
        bar->addWidget(title, 1);
        auto *cancel = new QPushButton("返回账本");
        bar->addWidget(cancel);
        auto *chapter = new DownwardCombo;
        chapter->setObjectName("ledgerChapter");
        auto rows = book->run("SELECT id,title FROM chapters ORDER BY position");
        while (rows.next())
            chapter->addItem(numberedChapter(chapter->count() + 1, rows.value(1).toString()), rows.value(0));
        chapter->setCurrentIndex(qMax(0, chapter->findData(preferredChapter)));
        auto *when = new QFormLayout;
        layout->addLayout(when);
        when->addRow("记录到章节", chapter);
        auto *form = new EventForm(*book, chapter->currentData().toInt(), createResource());
        form->insert->setText("同时追加到所选章末");
        form->insert->setChecked(false);
        layout->addWidget(form);
        auto *message = new QLabel;
        message->setObjectName("actionError");
        message->setTextFormat(Qt::PlainText);
        message->setWordWrap(true);
        layout->addWidget(message);
        auto *save = new QPushButton("保存事件");
        save->setObjectName("primary");
        layout->addWidget(save, 0, Qt::AlignRight);
        layout->addStretch();
        connect(chapter, &QComboBox::currentIndexChanged, form,
                [form, chapter] { form->setChapter(chapter->currentData().toInt()); });
        connect(cancel, &QPushButton::clicked, this, &EventLedger::closeRecord);
        connect(save, &QPushButton::clicked, form, [=, this] {
            try
            {
                const int id = chapter->currentData().toInt();
                QString body, addition;
                if (form->insert->isChecked())
                {
                    body = book->value("SELECT body FROM chapters WHERE id=?", {id}).toString();
                    addition = body.isEmpty() || body.endsWith('\n') ? QString() : QStringLiteral("\n");
                    addition += form->snippet();
                    body += addition;
                }
                form->persist(form->insert->isChecked() ? &body : nullptr);
                if (appended && !addition.isEmpty())
                    appended(id, addition);
                preferredChapter = id;
                closeRecord();
                resources->load(*book, applied.resource, applied.resourceKind);
                showPage(page, true);
                summary->setText("事件已保存　" + summary->text());
            }
            catch (const std::exception &e)
            {
                message->setText(QString::fromUtf8(e.what()));
                scroll->ensureWidgetVisible(message);
            }
        });
        addWidget(scroll);
        setCurrentWidget(scroll);
    }

    void showPage(qint64 requested, bool keepPosition = false)
    {
        try
        {
            const int position = table->verticalScrollBar()->value();
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
            table->verticalScrollBar()->setValue(keepPosition ? position : 0);
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
                   description->text().trimmed(), sort->currentIndex() == 0,
                   resources->choices->currentData(Qt::UserRole + 1).toString()};
        showPage(1);
    }

  public:
    explicit EventLedger(std::function<void(int, const QString &)> onAppend)
        : resources(new ResourcePicker(true, createResource())), appended(std::move(onAppend))
    {
        auto *listPage = new QWidget;
        addWidget(listPage);
        auto *layout = new QVBoxLayout(listPage);
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
        auto *add = new QPushButton("记录事件");
        pages->addWidget(add);
        connect(add, &QPushButton::clicked, this, [this] {
            try
            {
                openRecord();
            }
            catch (const std::exception &e)
            {
                summary->setText(QString::fromUtf8(e.what()));
            }
        });
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
    void resetBook()
    {
        while (count() > 1)
        {
            auto *old = widget(count() - 1);
            removeWidget(old);
            old->deleteLater();
        }
        bookFile.clear();
    }
    void load(Book &source, int chapter)
    {
        const bool sameBook = source.filePath() == bookFile;
        if (!sameBook)
        {
            resetBook();
            applied = {};
            page = 1;
            person->clear();
            description->clear();
        }
        preferredChapter = chapter;
        bookFile = source.filePath();
        book = &source;
        QSignalBlocker order(sort);
        sort->setCurrentIndex(applied.newestFirst ? 0 : 1);
        resources->load(source, applied.resource, applied.resourceKind);
        if (resources->choices->currentIndex() == 0 && !applied.resource.isEmpty())
        {
            applied.resource.clear();
            applied.resourceKind.clear();
            page = 1;
        }
        showPage(page, sameBook);
    }
};
