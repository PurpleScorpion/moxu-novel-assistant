#pragma once
#include "writing_notes.h"
#include <QtWidgets>

class NoteEditor : public QWidget
{
    NoteScope scope;
    Book *book = nullptr;
    QListWidget *entries = new QListWidget;
    QLineEdit *title = new QLineEdit;
    QPlainTextEdit *body = new QPlainTextEdit;
    QComboBox *chapters = new QComboBox;
    QLabel *status = new QLabel;
    QPushButton *removeButton = new QPushButton("删除");
    WritingNote saved;
    QHash<qint64, WritingNote> records;

    QString label(const WritingNote &note) const
    {
        if (scope != NoteScope::Outline)
            return note.title;
        return chapters->itemText(chapters->findData(note.chapter)) + "  ·  " + note.title;
    }
    QListWidgetItem *findEntry(qint64 id) const
    {
        for (int i = 0; i < entries->count(); ++i)
            if (entries->item(i)->data(Qt::UserRole).toLongLong() == id)
                return entries->item(i);
        return nullptr;
    }
    void load(qint64 id)
    {
        saved = records.value(id);
        title->setText(saved.title);
        body->setPlainText(saved.body);
        chapters->setCurrentIndex(qMax(0, chapters->findData(saved.chapter)));
        removeButton->setEnabled(saved.id != 0);
    }
    void select(qint64 id)
    {
        QSignalBlocker block(entries);
        auto *item = findEntry(id);
        entries->setCurrentItem(item);
        load(item ? id : 0);
    }
    void reload(qint64 preferred)
    {
        auto notes = WritingNotes(*book, scope).list();
        QSignalBlocker block(entries);
        entries->clear();
        records.clear();
        for (const auto &note : notes)
        {
            records.insert(note.id, note);
            auto *item = new QListWidgetItem(label(note), entries);
            item->setData(Qt::UserRole, note.id);
        }
        select(records.contains(preferred) || notes.isEmpty() ? preferred : notes.first().id);
    }
    bool attempt(const std::function<void()> &action)
    {
        try
        {
            action();
            return true;
        }
        catch (const std::exception &e)
        {
            status->setText(QString::fromUtf8(e.what()));
            return false;
        }
    }

  public:
    explicit NoteEditor(NoteScope scope) : scope(scope)
    {
        const bool outline = scope == NoteScope::Outline;
        auto *layout = new QVBoxLayout(this);
        layout->setContentsMargins(0, 0, 0, 0);
        auto *hint = new QLabel(outline ? "按全书或章节整理剧情。AI 写作时可选择要参考的条目。"
                                : scope == NoteScope::Global
                                    ? "保存在本机，所有书籍都可选用。适合叙事视角、行文节奏等常用写作要求。"
                                    : "随这本书保存。适合人物口吻、世界观约束等专属写作要求。");
        hint->setWordWrap(true);
        layout->addWidget(hint);
        auto *columns = new QHBoxLayout;
        layout->addLayout(columns, 1);
        auto *list = new QVBoxLayout;
        columns->addLayout(list, 1);
        list->addWidget(entries);
        auto *add = new QPushButton(outline ? "新增大纲条目" : "新增 Skill");
        list->addWidget(add);
        auto *form = new QVBoxLayout;
        columns->addLayout(form, 2);
        form->addWidget(new QLabel("标题"));
        form->addWidget(title);
        if (outline)
        {
            form->addWidget(new QLabel("适用范围"));
            form->addWidget(chapters);
        }
        else
        {
            chapters->setParent(this);
            chapters->hide();
        }
        form->addWidget(new QLabel(outline ? "剧情安排" : "给 AI 的写作要求"));
        form->addWidget(body, 1);
        body->setPlaceholderText(outline ? "本段剧情的目标、冲突与转折…"
                                         : "例如：对话保留人物各自的口头禅，动作描写使用短句。");
        auto *buttons = new QHBoxLayout;
        form->addLayout(buttons);
        buttons->addWidget(removeButton);
        auto *reset = new QPushButton("还原");
        buttons->addWidget(reset);
        auto *commit = new QPushButton("保存");
        commit->setObjectName("primary");
        buttons->addWidget(commit);
        status->setWordWrap(true);
        layout->addWidget(status);
        connect(add, &QPushButton::clicked, this, [this] {
            attempt([&] {
                flush();
                select(0);
                status->clear();
                title->setFocus();
            });
        });
        connect(commit, &QPushButton::clicked, this, [this] {
            attempt([&] {
                if (title->text().trimmed().isEmpty())
                    throw std::runtime_error("请填写标题。");
                flush();
                status->setText("已保存");
            });
        });
        connect(reset, &QPushButton::clicked, this, [this] {
            attempt([&] {
                reload(saved.id);
                status->clear();
            });
        });
        connect(removeButton, &QPushButton::clicked, this, [this] {
            if (QMessageBox::question(this, "删除条目", "确定删除「" + saved.title + "」？",
                                      QMessageBox::Yes | QMessageBox::No, QMessageBox::No) == QMessageBox::Yes)
                attempt([&] {
                    WritingNotes(*book, this->scope).remove(saved);
                    reload(0);
                    status->setText("已删除");
                });
        });
        connect(entries, &QListWidget::currentItemChanged, this, [this](QListWidgetItem *current) {
            if (!current)
                return;
            const auto next = current->data(Qt::UserRole).toLongLong();
            if (!attempt([&] {
                    flush();
                    select(next);
                    status->clear();
                }))
            {
                QSignalBlocker block(entries);
                entries->setCurrentItem(findEntry(saved.id));
            }
        });
    }
    void enter(Book *source)
    {
        book = source;
        chapters->clear();
        chapters->addItem("全书", 0);
        if (scope == NoteScope::Outline)
        {
            auto q = book->run("SELECT id,title FROM chapters ORDER BY id");
            while (q.next())
                chapters->addItem(q.value(1).toString(), q.value(0));
        }
        reload(0);
        status->clear();
    }
    void flush()
    {
        WritingNote draft = saved;
        draft.title = title->text();
        draft.body = body->toPlainText();
        draft.chapter = chapters->currentData().toInt();
        if (draft.title == saved.title && draft.body == saved.body && draft.chapter == saved.chapter)
            return;
        draft = WritingNotes(*book, scope).save(draft);
        QSignalBlocker block(entries);
        auto *item = findEntry(draft.id);
        if (!item)
        {
            item = new QListWidgetItem(entries);
            item->setData(Qt::UserRole, draft.id);
        }
        item->setText(label(draft));
        records.insert(draft.id, draft);
        entries->setCurrentItem(item);
        saved = draft;
        title->setText(saved.title);
        removeButton->setEnabled(true);
        status->setText("已保存");
    }
};
