#pragma once
#include "downward_combo.h"
#include "inline_files.h"
#include "interface.h"
#include "save_status.h"
#include "skill_file.h"
#include <QtWidgets>

class NoteEditor : public QWidget
{
    NoteScope scope;
    Book *book = nullptr;
    InlineActions *actions;
    QListWidget *entries = new QListWidget;
    QLineEdit *title = new QLineEdit;
    QPlainTextEdit *body = new QPlainTextEdit;
    QComboBox *chapters = new DownwardCombo;
    SaveStatus *status = new SaveStatus;
    QPushButton *removeButton = new QPushButton("删除");
    WritingNote saved;
    QString displayedBody;
    QString bookFile;
    bool loading = false;
    QHash<qint64, WritingNote> records;

    QString skillFolder() const
    {
        auto folder = QSettings().value("skillFolder").toString();
        if (!folder.isEmpty() && QDir(folder).exists())
            return folder;
        return QSettings().value("booksDir", QDir::homePath()).toString();
    }
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
        QScopedValueRollback guard(loading, true);
        status->pending.stop();
        saved = records.value(id);
        title->setText(saved.title);
        body->setPlainText(saved.body);
        displayedBody = body->toPlainText();
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
    NoteEditor(NoteScope scope, std::function<void(NoteScope, qint64)> imported) : scope(scope)
    {
        const bool outline = scope == NoteScope::Outline;
        auto *home = new QWidget;
        actions = new InlineActions(home);
        auto *root = new QVBoxLayout(this);
        root->setContentsMargins(0, 0, 0, 0);
        root->addWidget(actions);
        auto *layout = new QVBoxLayout(home);
        layout->setContentsMargins(0, 0, 0, 0);
        auto *hint = new QLabel(outline ? "按全书或章节整理剧情。AI 写作时可选择要参考的条目。"
                                : scope == NoteScope::Global
                                    ? "保存在本机，所有书籍都可选用。适合叙事视角、行文节奏等常用写作要求。"
                                    : "随这本书保存。适合人物口吻、世界观约束等专属写作要求。");
        hint->setWordWrap(true);
        hint->setObjectName("hint");
        layout->addWidget(hint);
        auto *columns = new QHBoxLayout;
        layout->addLayout(columns, 1);
        columns->setSpacing(12);
        auto *list = panelColumn(columns, 1, true);
        list->addWidget(entries);
        auto *add = new QPushButton(outline ? "新增大纲条目" : "新增 Skill");
        list->addWidget(add);
        if (!outline)
        {
            auto *loadFile = new QPushButton("导入 SKILL.md…");
            auto *share = new QPushButton("导出 SKILL.md…");
            list->addWidget(loadFile);
            list->addWidget(share);
            share->setEnabled(false);
            connect(title, &QLineEdit::textChanged, share,
                    [share](const QString &name) { share->setEnabled(!name.trimmed().isEmpty()); });
            connect(loadFile, &QPushButton::clicked, this, [this, imported] {
                auto *fields = actions->begin("导入 SKILL.md");
                auto *picker = new InlinePathPicker(PathUse::OpenFile, skillFolder(), ".md");
                fields->addWidget(picker, 1);
                auto *destination = new DownwardCombo;
                destination->setObjectName("importScope");
                destination->addItems({scopeName(NoteScope::Global), scopeName(NoteScope::Book)});
                destination->setCurrentIndex(this->scope == NoteScope::Global ? 0 : 1);
                fields->addWidget(new QLabel("导入到"));
                fields->addWidget(destination);
                actions->buttons("确认导入", [this, picker, destination, imported] {
                    auto path = picker->checkedPath();
                    auto note = readSkillFile(path);
                    flush();
                    auto target = destination->currentIndex() == 0 ? NoteScope::Global : NoteScope::Book;
                    auto record = WritingNotes(*book, target).save(note);
                    QSettings().setValue("skillFolder", QFileInfo(path).absolutePath());
                    imported(target, record.id);
                });
            });
            connect(share, &QPushButton::clicked, this, [this] {
                auto *fields = actions->begin("导出 Skill · " + title->text());
                auto *picker = new InlinePathPicker(PathUse::SaveFile, skillFolder(), ".md", "SKILL.md");
                fields->addWidget(picker, 1);
                auto *replace = new QCheckBox("覆盖该路径的已有文件");
                fields->addWidget(replace);
                auto refresh = [picker, replace] {
                    replace->setChecked(false);
                    replace->setVisible(QFileInfo(picker->path()).isFile());
                };
                connect(picker->field(), &QLineEdit::textChanged, replace, refresh);
                refresh();
                actions->buttons("确认导出", [this, picker, replace] {
                    auto path = picker->checkedPath();
                    if (QFileInfo::exists(path) && !replace->isChecked())
                    {
                        replace->show();
                        throw std::runtime_error("目标文件已存在，请更改保存路径或勾选覆盖。");
                    }
                    flush();
                    writeSkillFile(path, saved, replace->isChecked());
                    QSettings().setValue("skillFolder", QFileInfo(path).absolutePath());
                    status->setText("已导出到 " + QDir::toNativeSeparators(path));
                });
            });
        }
        auto *form = panelColumn(columns, 3);
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
        buttons->addStretch();
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
        connect(commit, &QPushButton::clicked, this, &NoteEditor::saveNow);
        auto edited = [this] {
            if (!loading)
                status->edited();
        };
        connect(title, &QLineEdit::textChanged, this, edited);
        connect(body, &QPlainTextEdit::textChanged, this, edited);
        connect(chapters, &QComboBox::currentIndexChanged, this, edited);
        connect(&status->pending, &QTimer::timeout, this, [this] { attempt([this] { flush(); }); });
        connect(reset, &QPushButton::clicked, this, [this] {
            attempt([&] {
                reload(saved.id);
                status->clear();
            });
        });
        connect(removeButton, &QPushButton::clicked, this, [this] {
            auto *fields = actions->begin("删除" + scopeName(this->scope) + "条目");
            auto *prompt = new QLabel("确定删除「" + saved.title + "」？");
            prompt->setTextFormat(Qt::PlainText);
            prompt->setWordWrap(true);
            fields->addWidget(prompt);
            fields->addStretch();
            actions->buttons("确认删除", [this] {
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
        QScopedValueRollback guard(loading, true);
        actions->finish();
        book = source;
        const auto preferred = scope == NoteScope::Global || bookFile == book->filePath() ? saved.id : 0;
        bookFile = book->filePath();
        chapters->clear();
        chapters->addItem("全书", 0);
        if (scope == NoteScope::Outline)
        {
            auto q = book->run("SELECT id,title FROM chapters ORDER BY id");
            while (q.next())
                chapters->addItem(q.value(1).toString(), q.value(0));
        }
        reload(preferred);
        status->clear();
    }
    void showImported(qint64 id)
    {
        reload(id);
        status->setText("已导入到" + scopeName(scope) + " · " + saved.title);
    }
    void flush()
    {
        status->pending.stop();
        WritingNote draft = saved;
        draft.title = title->text().trimmed();
        const auto editedBody = body->toPlainText();
        if (editedBody != displayedBody)
            draft.body = editedBody;
        draft.chapter = chapters->currentData().toInt();
        if (draft.title == saved.title && draft.body == saved.body && draft.chapter == saved.chapter)
        {
            if (status->text() == "等待自动保存…")
                status->setText("没有待保存的更改");
            return;
        }
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
        displayedBody = editedBody;
        removeButton->setEnabled(true);
        status->setText("已自动保存");
    }
    void savePending()
    {
        if (status->pending.isActive())
            attempt([this] { flush(); });
    }
    void saveNow()
    {
        if (actions->currentIndex() != 0)
            return;
        attempt([this] {
            if (title->text().trimmed().isEmpty())
                throw std::runtime_error("请填写标题。");
            flush();
            status->confirm(scopeName(scope));
        });
    }
};
