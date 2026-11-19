#pragma once
#include "book.h"
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
    int chapter = 0;
    std::function<void(int, const QString &)> changed;

  public:
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
    void generated(const QString &text)
    {
        QTextCursor cursor(editor->document());
        cursor.select(QTextCursor::Document);
        cursor.insertText(text.trimmed());
        saveEdits();
    }
    ChapterSummary(Book &source, const QMap<int, QString> &names, int current,
                   std::function<void(int, const QString &)> saved, std::function<void(QString)> generate)
        : book(source), changed(std::move(saved))
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
        for (auto it = names.cbegin(); it != names.cend() && it.key() <= current; ++it)
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
        connect(create, &QPushButton::clicked, this, [this, generate] {
            try
            {
                flush();
                auto body = book.value("SELECT body FROM chapters WHERE id=?", {chapter}).toString();
                if (body.trimmed().isEmpty()) notice->setText("本章尚无正文，可以先手写摘要。");
                else generate(body);
            }
            catch (const std::exception &e) { notice->setText(QString::fromUtf8(e.what())); }
        });
    }
};
