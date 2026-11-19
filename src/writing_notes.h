#pragma once
#include "book.h"

enum class NoteScope
{
    Global,
    Book,
    Outline
};

inline QString scopeName(NoteScope scope)
{
    switch (scope)
    {
    case NoteScope::Global:
        return "全局 Skill";
    case NoteScope::Book:
        return "本书 Skill";
    case NoteScope::Outline:
        return "创作大纲";
    }
    return {};
}

struct WritingNote
{
    qint64 id = 0;
    QString title, body;
    int chapter = 0;
    int revision = 0;
    QString skillHeader;
};

class WritingNotes
{
    Book &book;
    NoteScope scope;

    static QSqlDatabase globalDatabase()
    {
        const QString name = "moxu-personal-writing-library";
        auto db = QSqlDatabase::contains(name) ? QSqlDatabase::database(name, false)
                                               : QSqlDatabase::addDatabase("QSQLITE", name);
        if (!db.isOpen())
        {
            QDir dir(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation));
            if (!dir.mkpath("."))
                throw std::runtime_error("无法创建全局 Skill 库目录");
            db.setDatabaseName(dir.filePath("skills.sqlite"));
            if (!db.open())
                throw std::runtime_error("无法打开全局 Skill 库");
        }
        executeSql(db,
                   "CREATE TABLE IF NOT EXISTS writing_notes(id INTEGER PRIMARY KEY AUTOINCREMENT, kind TEXT NOT NULL, "
                   "title TEXT NOT NULL, body TEXT NOT NULL, chapter INTEGER, revision INTEGER NOT NULL DEFAULT 1, "
                   "skill_header TEXT NOT NULL DEFAULT '')");
        ensureSkillHeaderColumn(db);
        return db;
    }

    QSqlQuery query(const QString &sql, const QVariantList &args) const
    {
        return scope == NoteScope::Global ? executeSql(globalDatabase(), sql, args) : book.run(sql, args);
    }
    QString kind() const
    {
        return scope == NoteScope::Outline ? "outline" : "skill";
    }

  public:
    WritingNotes(Book &book, NoteScope scope) : book(book), scope(scope)
    {
    }
    QList<WritingNote> list() const
    {
        QList<WritingNote> result;
        const QString order = scope == NoteScope::Outline ? "position,id" : "COALESCE(chapter,0),id";
        auto q = query("SELECT id,title,body,chapter,revision,skill_header FROM writing_notes WHERE kind=? ORDER BY " + order,
                       {kind()});
        while (q.next())
            result.append({q.value(0).toLongLong(), q.value(1).toString(), q.value(2).toString(), q.value(3).toInt(),
                           q.value(4).toInt(), q.value(5).toString()});
        return result;
    }
    WritingNote save(WritingNote note, qint64 beforeOutline = 0) const
    {
        note.title = note.title.trimmed();
        if (note.body.isNull())
            note.body = QStringLiteral("");
        if (note.title.isEmpty())
            throw std::runtime_error("请先填写标题，或点击「还原」放弃本次编辑。");
        const QVariant chapter = scope == NoteScope::Outline && note.chapter ? QVariant(note.chapter) : QVariant();
        if (!note.id)
        {
            auto insert = [&] {
                note.id = query("INSERT INTO writing_notes(kind,title,body,chapter,skill_header) "
                                "VALUES(?,?,?,?,COALESCE(?,''))",
                                {kind(), note.title, note.body, chapter, note.skillHeader}).lastInsertId().toLongLong();
            };
            if (scope == NoteScope::Outline && beforeOutline)
                book.atomic([&] {
                    const auto at = book.value("SELECT position FROM writing_notes WHERE id=? AND kind='outline'", {beforeOutline});
                    if (!at.isValid()) throw std::runtime_error("找不到要插入的大纲位置。");
                    insert();
                    book.run("UPDATE writing_notes SET position=position+1 WHERE kind='outline' AND position>=? AND id<>?",
                             {at, note.id});
                    book.run("UPDATE writing_notes SET position=? WHERE id=?", {at, note.id});
                });
            else
                insert();
        }
        else
        {
            auto updated = query("UPDATE writing_notes SET title=?, body=?,chapter=?,revision=revision+1 WHERE id=? "
                                 "AND kind=? AND revision=?",
                                 {note.title, note.body, chapter, note.id, kind(), note.revision});
            if (updated.numRowsAffected() != 1)
                throw std::runtime_error("此条目已在其他窗口更改。请复制需要保留的文字，再点击「还原」载入最新内容。");
        }
        ++note.revision;
        return note;
    }
    void moveOutline(qint64 id, int direction) const
    {
        if (scope != NoteScope::Outline || (direction != 1 && direction != -1)) return;
        book.atomic([&] {
            const auto position = book.value("SELECT position FROM writing_notes WHERE id=? AND kind='outline'", {id});
            const QString next = direction < 0 ? "position<? ORDER BY position DESC" : "position>? ORDER BY position";
            auto row = book.run("SELECT id,position FROM writing_notes WHERE kind='outline' AND " + next + " LIMIT 1",
                                {position});
            if (!row.next()) return;
            book.run("UPDATE writing_notes SET position=CASE id WHEN ? THEN ? ELSE ? END WHERE id IN (?,?)",
                     {id, row.value(1), position, id, row.value(0)});
        });
    }
    void remove(const WritingNote &note) const
    {
        if (query("DELETE FROM writing_notes WHERE id=? AND kind=? AND revision=?", {note.id, kind(), note.revision})
                .numRowsAffected() != 1)
            throw std::runtime_error("条目已发生变化，请点击「还原」后重试。");
    }
};
