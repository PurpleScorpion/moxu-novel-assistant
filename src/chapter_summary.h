#pragma once
#include "book.h"
#include "chapter_text.h"
#include "downward_combo.h"
#include <QtWidgets>
#include <functional>

class ChapterSummary : public QWidget
{
    Book &book;
    QComboBox *chapters = new DownwardCombo;
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
        const int id = chapters->itemData(index).toInt();
        const auto body = book.value("SELECT body FROM chapters WHERE id=?", {id}).toString();
        const auto length = chapterWordCount(body);
        if (length < 1000)
        {
            ++shortCount;
            notice->setText(QString("%1 · 正文 %2 字，满 1000 字可生成摘要。").arg(chapters->itemText(index)).arg(length));
            return false;
        }
        chapters->setCurrentIndex(index);
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
            if (nextIndex >= chapters->count())
            {
                stopBatch();
                notice->setText(QString("已生成 %1 章，已有摘要 %2 章，不足 1000 字 %3 章。")
                                    .arg(savedCount).arg(existingCount).arg(shortCount));
                return;
            }
            const int index = nextIndex++;
            if (book.value("SELECT 1 FROM chapter_summaries WHERE chapter=?", {chapters->itemData(index)}).isValid())
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
    void flush()
    {
        delay.stop();
        if (!editor->document()->isModified())
            return;
        const auto text = editor->toPlainText().trimmed();
        if (text.isEmpty())
            book.run("DELETE FROM chapter_summaries WHERE chapter=?", {chapter});
        else
            book.run("INSERT INTO chapter_summaries(chapter,body) VALUES(?,?) "
                     "ON CONFLICT(chapter) DO UPDATE SET body=excluded.body", {chapter, text});
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
        cursor.insertText(QString("【%1】\n").arg(chapters->currentText()) + text.trimmed());
        if (!saveEdits()) return false;
        if (nextIndex >= 0)
        {
            ++savedCount;
            setEnabled(false);
            batchStep.start(0);
        }
        return true;
    }
    ChapterSummary(Book &source, const QMap<int, QString> &names, int current,
                   std::function<void(int, const QString &)> saved, std::function<bool(QString)> generate,
                   bool allChapters, std::function<void(bool)> setBatchBusy)
        : book(source), changed(std::move(saved)), request(std::move(generate)), batchBusy(std::move(setBatchBusy))
    {
        auto *layout = new QVBoxLayout(this);
        auto *row = new QHBoxLayout;
        layout->addLayout(row);
        row->addWidget(new QLabel("章节"));
        row->addWidget(chapters, 1);
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
        for (auto it = names.cbegin(); it != names.cend() && (allChapters || it.key() <= current); ++it)
            chapters->addItem(numberedChapter(++number, it.value()), it.key());
        auto load = [this] {
            int id = chapters->currentData().toInt();
            const auto text = book.value("SELECT body FROM chapter_summaries WHERE chapter=?", {id}).toString();
            chapter = id;
            editor->setPlainText(text);
            editor->document()->setModified(false);
            notice->clear();
        };
        chapters->setCurrentIndex(chapters->findData(current));
        load();
        editor->setPlaceholderText("手写摘要，或生成后修改。可记录人物状态、关键事件、线索和章末悬念。");
        editor->setObjectName("chapterSummaryText");
        layout->addWidget(editor, 1);
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
        connect(chapters, &QComboBox::currentIndexChanged, this, [this, load] {
            try { flush(); load(); }
            catch (const std::exception &e)
            {
                QSignalBlocker block(chapters);
                chapters->setCurrentIndex(chapters->findData(chapter));
                notice->setText(QString::fromUtf8(e.what()));
            }
        });
        connect(create, &QPushButton::clicked, this, [this] {
            try
            {
                flush();
                generateChapter(chapters->currentIndex());
            }
            catch (const std::exception &e) { notice->setText(QString::fromUtf8(e.what())); }
        });
    }
};
