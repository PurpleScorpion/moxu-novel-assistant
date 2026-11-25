#pragma once
#include "book.h"
#include "chapter_text.h"
#include "downward_combo.h"
#include <QtWidgets>
#include <functional>

class ChapterSummary : public QWidget
{
    Book &book;
    QTableWidget *chapters = new QTableWidget(0, 3);
    QHash<int, int> rows;
    QVariant requestedVersion;
    int chapterAt(int row) const { return chapters->item(row, 0)->data(Qt::UserRole).toInt(); }
    void showState(int row, bool exists, const QVariant &source, const QVariant &current)
    {
        auto *cell = chapters->item(row, 2);
        const bool stale = exists && source.toLongLong() != current.toLongLong();
        cell->setText(!exists ? "无摘要" : stale ? "正文已修改" : source.isNull() ? "旧摘要 · 待核对" : "已有摘要");
        cell->setForeground(stale ? QColor("#efbf74") : chapters->palette().color(QPalette::Text));
    }
    void refreshList()
    {
        book.refreshWordCounts();
        auto q = book.run("SELECT c.id,c.word_count,c.body_version,s.chapter,s.source_version "
                          "FROM chapters c LEFT JOIN chapter_summaries s ON s.chapter=c.id");
        while (q.next())
        {
            const int row = rows.value(q.value(0).toInt(), -1);
            if (row < 0) continue;
            chapters->item(row, 1)->setText(QString::number(q.value(1).toLongLong()));
            showState(row, !q.value(3).isNull(), q.value(4), q.value(2));
        }
    }
    void showEvent(QShowEvent *event) override
    {
        QWidget::showEvent(event);
        try { refreshList(); }
        catch (const std::exception &e) { notice->setText(QString::fromUtf8(e.what())); }
    }
    QPlainTextEdit *editor = new QPlainTextEdit;
    QLabel *notice = new QLabel;
    QTimer delay;
    QTimer batchStep;
    int nextIndex = -1, savedCount = 0, shortCount = 0, existingCount = 0;
    int chapter = 0;
    std::function<void(int, const QString &)> changed;
    std::function<bool(QString)> request;
    std::function<void(bool)> batchBusy;

    bool generateChapter(int index)
    {
        const int id = chapterAt(index);
        auto source = book.run("SELECT body,body_version FROM chapters WHERE id=?", {id});
        if (!source.next()) throw std::runtime_error("找不到所选章节。");
        const auto body = source.value(0).toString();
        const auto length = chapterWordCount(body);
        if (length < 1000)
        {
            ++shortCount;
            notice->setText(QString("%1 · 正文 %2 字，满 1000 字可生成摘要。").arg(chapters->item(index, 0)->text()).arg(length));
            return false;
        }
        chapters->setCurrentCell(index, 0);
        requestedVersion = source.value(1);
        if (chapter != id || !request(body))
        {
            stopBatch();
            return false;
        }
        return true;
    }
    void advanceBatch()
    {
        try
        {
            if (nextIndex >= chapters->rowCount())
            {
                stopBatch();
                notice->setText(QString("已生成 %1 章，已有摘要 %2 章，不足 1000 字 %3 章。")
                                    .arg(savedCount).arg(existingCount).arg(shortCount));
                return;
            }
            const int index = nextIndex++;
            if (book.value("SELECT 1 FROM chapter_summaries WHERE chapter=?", {chapterAt(index)}).isValid())
                ++existingCount;
            else if (generateChapter(index))
                return;
            if (nextIndex >= 0)
                batchStep.start(0);
        }
        catch (const std::exception &e)
        {
            stopBatch();
            notice->setText(QString::fromUtf8(e.what()));
        }
    }

  public:
    bool batchActive() const { return nextIndex >= 0; }
    void stopBatch()
    {
        batchStep.stop();
        if (!batchActive()) return;
        nextIndex = -1;
        batchBusy(false);
    }
    void flush(const QVariant &generatedFrom = {})
    {
        delay.stop();
        if (!editor->document()->isModified() && !generatedFrom.isValid())
            return;
        const auto text = editor->toPlainText().trimmed();
        const auto currentVersion = book.value("SELECT body_version FROM chapters WHERE id=?", {chapter});
        if (text.isEmpty())
            book.run("DELETE FROM chapter_summaries WHERE chapter=?", {chapter});
        else
            book.run("INSERT INTO chapter_summaries(chapter,body,source_version) VALUES(?,?,?) "
                     "ON CONFLICT(chapter) DO UPDATE SET body=excluded.body,"
                     "source_version=coalesce(?,chapter_summaries.source_version)",
                     {chapter, text, generatedFrom.isValid() ? generatedFrom : currentVersion, generatedFrom});
        const auto source = book.value("SELECT source_version FROM chapter_summaries WHERE chapter=?", {chapter});
        showState(rows.value(chapter), !text.isEmpty(), source, currentVersion);
        editor->document()->setModified(false);
        notice->setText("摘要已保存");
        changed(chapter, text);
    }
    bool saveEdits()
    {
        try { flush(); return true; }
        catch (const std::exception &e) { notice->setText(QString::fromUtf8(e.what())); return false; }
    }
    bool generated(const QString &text)
    {
        QTextCursor cursor(editor->document());
        cursor.select(QTextCursor::Document);
        cursor.insertText(QString("【%1】\n").arg(chapters->item(chapters->currentRow(), 0)->text()) + text.trimmed());
        try { flush(requestedVersion); }
        catch (const std::exception &e) { notice->setText(QString::fromUtf8(e.what())); return false; }
        if (nextIndex >= 0)
        {
            ++savedCount;
            setEnabled(false);
            batchStep.start(0);
        }
        return true;
    }
    ChapterSummary(Book &source, int current,
                   std::function<void(int, const QString &)> saved, std::function<bool(QString)> generate,
                   bool allChapters, std::function<void(bool)> setBatchBusy)
        : book(source), changed(std::move(saved)), request(std::move(generate)), batchBusy(std::move(setBatchBusy))
    {
        auto *layout = new QVBoxLayout(this);
        auto *row = new QHBoxLayout;
        layout->addLayout(row);
        row->addWidget(new QLabel("章节列表"), 1);
        chapters->setObjectName("summaryChapter");
        auto *create = new QPushButton("生成剧情摘要");
        create->setToolTip("将所选章节全文发送到当前模型。生成成功后可编辑或撤销。");
        row->addWidget(create);
        int number = 0;
        if (allChapters)
        {
            auto *batch = new QPushButton("一键生成缺失摘要");
            batch->setToolTip("逐章生成本书满 1000 字且没有摘要的章节；已有摘要保留。");
            row->addWidget(batch);
            batchStep.setSingleShot(true);
            connect(&batchStep, &QTimer::timeout, this, &ChapterSummary::advanceBatch);
            connect(batch, &QPushButton::clicked, this, [this] {
                if (!saveEdits()) return;
                nextIndex = savedCount = existingCount = shortCount = 0;
                batchBusy(true);
                advanceBatch();
            });
        }
        chapters->setHorizontalHeaderLabels({"章节", "正文字数", "摘要状态"});
        chapters->verticalHeader()->hide();
        chapters->setEditTriggers(QAbstractItemView::NoEditTriggers);
        chapters->setSelectionBehavior(QAbstractItemView::SelectRows);
        chapters->setSelectionMode(QAbstractItemView::SingleSelection);
        chapters->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
        chapters->setColumnWidth(1, 84);
        chapters->setColumnWidth(2, 135);
        auto *split = new QSplitter(Qt::Vertical);
        split->setChildrenCollapsible(false);
        split->addWidget(chapters);
        layout->addWidget(split, 1);
        auto list = book.run("SELECT id,title FROM chapters ORDER BY position");
        while (list.next())
        {
            const int id = list.value(0).toInt(), row = chapters->rowCount();
            chapters->insertRow(row);
            auto *label = new QTableWidgetItem(numberedChapter(++number, list.value(1).toString()));
            label->setData(Qt::UserRole, id);
            chapters->setItem(row, 0, label);
            chapters->setItem(row, 1, new QTableWidgetItem);
            chapters->item(row, 1)->setTextAlignment(Qt::AlignCenter);
            chapters->setItem(row, 2, new QTableWidgetItem);
            rows.insert(id, row);
            if (!allChapters && id == current) break;
        }
        auto load = [this] {
            int id = chapterAt(chapters->currentRow());
            const auto text = book.value("SELECT body FROM chapter_summaries WHERE chapter=?", {id}).toString();
            chapter = id;
            editor->setPlainText(text);
            editor->document()->setModified(false);
            notice->clear();
        };
        chapters->setCurrentCell(rows.value(current), 0);
        load();
        editor->setPlaceholderText("手写摘要，或生成后修改。可记录人物状态、关键事件、线索和章末悬念。");
        editor->setObjectName("chapterSummaryText");
        split->addWidget(editor);
        split->setSizes({195, 260});
        auto *footer = new QHBoxLayout;
        layout->addLayout(footer);
        notice->setTextFormat(Qt::PlainText);
        footer->addWidget(notice, 1);
        auto *save = new QPushButton("保存摘要");
        footer->addWidget(save);
        delay.setSingleShot(true);
        delay.setInterval(700);
        connect(editor, &QPlainTextEdit::textChanged, this, [this] { delay.start(); });
        connect(&delay, &QTimer::timeout, this, [this] { saveEdits(); });
        connect(save, &QPushButton::clicked, this, [this] {
            if (saveEdits()) notice->setText("摘要已保存");
        });
        connect(chapters, &QTableWidget::currentCellChanged, this, [this, load](int row, int, int previous, int) {
            if (row < 0 || row == previous) return;
            try { flush(); load(); }
            catch (const std::exception &e)
            {
                QSignalBlocker block(chapters);
                chapters->setCurrentCell(rows.value(chapter), 0);
                notice->setText(QString::fromUtf8(e.what()));
            }
        });
        connect(create, &QPushButton::clicked, this, [this] {
            try
            {
                flush();
                generateChapter(chapters->currentRow());
            }
            catch (const std::exception &e) { notice->setText(QString::fromUtf8(e.what())); }
        });
    }
};
