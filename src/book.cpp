#include "book.h"

namespace
{
void fail(const QString &message)
{
    throw std::runtime_error(message.toStdString());
}
} // namespace
QSqlQuery executeSql(const QSqlDatabase &db, const QString &sql, const QVariantList &args)
{
    QSqlQuery q(db);
    if (!q.prepare(sql))
        fail(q.lastError().text());
    for (const auto &v : args)
        q.addBindValue(v);
    if (!q.exec())
        fail(q.lastError().text());
    return q;
}
void ensureSkillHeaderColumn(const QSqlDatabase &db)
{
    auto hasColumn = [&] {
        return executeSql(db, "SELECT 1 FROM pragma_table_info('writing_notes') WHERE name='skill_header'").next();
    };
    if (hasColumn())
        return;
    // Another application window can upgrade the shared library at the same time.
    try
    {
        executeSql(db, "ALTER TABLE writing_notes ADD COLUMN skill_header TEXT NOT NULL DEFAULT ''");
    }
    catch (...)
    {
        if (!hasColumn())
            throw;
    }
}
QSqlQuery Book::run(const QString &sql, const QVariantList &args) const
{
    return executeSql(db, sql, args);
}
QVariant Book::value(const QString &sql, const QVariantList &args) const
{
    auto q = run(sql, args);
    return q.next() ? q.value(0) : QVariant();
}
void Book::atomic(const std::function<void()> &operation)
{
    run("BEGIN IMMEDIATE");
    try
    {
        operation();
        run("COMMIT");
    }
    catch (...)
    {
        db.rollback();
        throw;
    }
}
Book::Book(const QString &path, const QString &newTitle)
    : connection(QUuid::createUuid().toString()), db(QSqlDatabase::addDatabase("QSQLITE", connection))
{
    bool created = false;
    try
    {
        if (!newTitle.isEmpty())
        {
            QFile f(path);
            if (!f.open(QIODevice::WriteOnly | QIODevice::NewOnly))
                fail("无法创建书籍，请选择新的文件名。");
            created = true;
        }
        else if (!QFileInfo(path).isFile())
            fail("找不到书籍文件。");
        db.setDatabaseName(path);
        if (!db.open())
            fail(db.lastError().text());
        run("PRAGMA foreign_keys=ON");
        if (created)
            atomic([&] {
                for (const char *sql :
                     {"CREATE TABLE meta(title TEXT NOT NULL,format INTEGER NOT NULL)",
                      "CREATE TABLE chapters(id INTEGER PRIMARY KEY,title TEXT NOT NULL,body TEXT NOT NULL DEFAULT '')",
                      "CREATE TABLE revisions(id INTEGER PRIMARY KEY,chapter INTEGER REFERENCES chapters,body TEXT NOT "
                      "NULL,stamp TEXT DEFAULT CURRENT_TIMESTAMP)",
                      "CREATE TABLE people(id INTEGER PRIMARY KEY,name TEXT NOT NULL,attributes TEXT NOT NULL)",
                      "CREATE TABLE relations(id INTEGER PRIMARY KEY,a INTEGER REFERENCES people,b INTEGER REFERENCES "
                      "people,label TEXT NOT NULL,CHECK(a<>b))",
                      "CREATE TABLE events(id INTEGER PRIMARY KEY,chapter INTEGER NOT NULL REFERENCES chapters,person "
                      "INTEGER NOT NULL REFERENCES people,resource TEXT NOT NULL,delta INTEGER NOT NULL,description "
                      "TEXT NOT NULL)",
                      "CREATE INDEX account_history ON events(person,resource,chapter,id)",
                      "CREATE TABLE templates(id INTEGER PRIMARY KEY,title TEXT NOT NULL,pattern TEXT NOT NULL)"})
                    run(sql);
                run("INSERT INTO meta VALUES(?,1)", {newTitle});
                run("INSERT INTO chapters(title) VALUES(?)", {"第一章"});
                run("INSERT INTO templates(title,pattern) VALUES(?,?)",
                    {"人物面板", "【{{姓名}}】\n{{属性}}\n积分：{{积分}}"});
            });
        if (value("SELECT format FROM meta").toInt() != 1)
            fail("无法识别此版本的墨序书籍。");
        if (!value("SELECT 1 FROM sqlite_master WHERE type='table' AND name='book_profile'").isValid())
            atomic([&] {
                run("CREATE TABLE IF NOT EXISTS book_profile(id INTEGER PRIMARY KEY CHECK(id=1),genre TEXT NOT "
                    "NULL,synopsis TEXT NOT NULL)");
                run("INSERT OR IGNORE INTO book_profile VALUES(1,'','')");
            });
        if (!value("SELECT 1 FROM sqlite_master WHERE type='table' AND name='writing_notes'").isValid())
            run("CREATE TABLE writing_notes(id INTEGER PRIMARY KEY AUTOINCREMENT, kind TEXT NOT NULL CHECK(kind IN "
                "('skill','outline')), title TEXT NOT NULL, body TEXT NOT NULL, chapter INTEGER REFERENCES chapters, "
                "revision INTEGER NOT NULL DEFAULT 1,skill_header TEXT NOT NULL DEFAULT '')");
        ensureSkillHeaderColumn(db);
        if (!value("SELECT 1 FROM pragma_table_info('revisions') WHERE name='rolling'").isValid())
            atomic([&] {
                if (!value("SELECT 1 FROM pragma_table_info('revisions') WHERE name='rolling'").isValid())
                    run("ALTER TABLE revisions ADD COLUMN rolling INTEGER NOT NULL DEFAULT 0");
            });
        run("CREATE INDEX IF NOT EXISTS revision_history ON revisions(rolling,chapter,id)");
        run("CREATE TABLE IF NOT EXISTS history_settings(id INTEGER PRIMARY KEY CHECK(id=1),"
            "per_chapter INTEGER NOT NULL CHECK(per_chapter>=0),per_book INTEGER NOT NULL CHECK(per_book>=0))");
    }
    catch (...)
    {
        db.close();
        db = {};
        QSqlDatabase::removeDatabase(connection);
        if (created)
            QFile::remove(path);
        throw;
    }
}
Book::~Book()
{
    db.close();
    db = {};
    QSqlDatabase::removeDatabase(connection);
}
HistoryLimits Book::historyLimits() const
{
    auto q = run("SELECT per_chapter,per_book FROM history_settings WHERE id=1");
    if (q.next())
        return {q.value(0).toInt(), q.value(1).toInt()};
    return {};
}
HistoryUsage Book::historyUsage() const
{
    auto q =
        run("SELECT count(*),coalesce(sum(octet_length(body)),0),count(*) FILTER (WHERE rolling=0) FROM revisions");
    q.next();
    return {q.value(0).toLongLong(), q.value(1).toLongLong(), q.value(2).toLongLong()};
}
QList<qint64> Book::historiesToKeep(bool includeLegacy, HistoryLimits limits) const
{
    if (limits.perChapter < 0 || limits.perBook < 0)
        fail("历史份数不能小于零。");
    if (!limits.perBook || !limits.perChapter)
        return {};
    // SQLite can read octet_length of a stored column without loading its text.
    QString sql = "SELECT id,chapter,octet_length(body) FROM revisions";
    if (!includeLegacy)
        sql += " WHERE rolling=1";
    auto rows = run(sql + " ORDER BY id DESC");
    QList<qint64> keep;
    QHash<int, int> chapterCounts;
    qint64 remaining = historyBudget;
    while (keep.size() < limits.perBook && rows.next())
    {
        const int chapter = rows.value(1).toInt();
        const auto bytes = rows.value(2).toLongLong();
        if (chapterCounts.value(chapter) >= limits.perChapter || bytes > remaining)
            continue;
        keep.append(rows.value(0).toLongLong());
        ++chapterCounts[chapter];
        remaining -= bytes;
    }
    return keep;
}
void Book::trimHistory(bool includeLegacy, const QList<qint64> &keep)
{
    QStringList keepIds;
    for (const auto id : keep)
        keepIds.append(QString::number(id));
    QString sql = includeLegacy ? "DELETE FROM revisions WHERE 1" : "DELETE FROM revisions WHERE rolling=1";
    if (!keepIds.isEmpty())
        sql += " AND id NOT IN (" + keepIds.join(',') + ")";
    run(sql);
}
qint64 Book::historyCleanupCount() const
{
    return value("SELECT count(*) FROM revisions").toLongLong() - historiesToKeep(true, historyLimits()).size();
}
qint64 Book::historyRemovalCount(HistoryLimits limits) const
{
    return value("SELECT count(*) FROM revisions WHERE rolling=1").toLongLong() - historiesToKeep(false, limits).size();
}
void Book::setHistoryLimits(HistoryLimits limits, qint64 expectedRemoval)
{
    atomic([&] {
        const auto keep = historiesToKeep(false, limits);
        const auto remove = value("SELECT count(*) FROM revisions WHERE rolling=1").toLongLong() - keep.size();
        if (remove != expectedRemoval)
            fail("历史版本已变化，请重新查看将移除的份数。");
        run("INSERT INTO history_settings(id,per_chapter,per_book) VALUES(1,?,?) ON CONFLICT(id) DO UPDATE SET "
            "per_chapter=excluded.per_chapter,per_book=excluded.per_book",
            {limits.perChapter, limits.perBook});
        if (remove)
            trimHistory(false, keep);
    });
}
void Book::includeLegacyInHistoryLimit(qint64 expectedRemoval)
{
    atomic([&] {
        const auto keep = historiesToKeep(true, historyLimits());
        if (value("SELECT count(*) FROM revisions").toLongLong() - keep.size() != expectedRemoval)
            fail("历史版本已变化，请返回后重新预览。");
        trimHistory(true, keep);
        run("UPDATE revisions SET rolling=1 WHERE rolling=0");
    });
}
void Book::save(int chapter, const QString &body, SaveMode mode)
{
    auto previous = value("SELECT body FROM chapters WHERE id=?", {chapter});
    if (!previous.isValid())
        fail("章节不存在。");
    if (previous.toString() == body)
        return;
    const bool checkpoint = mode == SaveMode::Checkpoint ||
                            (!previous.toString().isEmpty() &&
                             !value("SELECT COALESCE((SELECT stamp > datetime('now','-5 minutes') FROM revisions "
                                    "WHERE chapter=? AND rolling=1 ORDER BY id DESC LIMIT 1),0)",
                                    {chapter})
                                  .toBool());
    if (checkpoint)
    {
        const auto limits = historyLimits();
        if (limits.perChapter && limits.perBook)
        {
            auto inserted = run("INSERT INTO revisions(chapter,body,rolling) SELECT id,body,1 FROM chapters "
                                "WHERE id=? AND octet_length(body)<=?",
                                {chapter, historyBudget});
            if (inserted.numRowsAffected())
                trimHistory(false, historiesToKeep(false, limits));
        }
    }
    run("UPDATE chapters SET body=? WHERE id=?", {body, chapter});
}
qint64 Book::balance(int person, const QString &resource, int chapter) const
{
    return value("SELECT COALESCE(SUM(delta),0) FROM events WHERE person=? AND resource=? AND chapter<=?",
                 {person, resource, chapter})
        .toLongLong();
}
void Book::event(int chapter, int person, const QString &resource, int delta, const QString &description,
                 const QString *body)
{
    atomic([&] {
        run("INSERT INTO events(chapter,person,resource,delta,description) VALUES(?,?,?,?,?)",
            {chapter, person, resource, delta, description});
        if (value("SELECT MIN(total) FROM (SELECT SUM(delta) OVER(ORDER BY chapter,id ROWS UNBOUNDED PRECEDING) total "
                  "FROM events WHERE person=? AND resource=?)",
                  {person, resource})
                .toLongLong() < 0)
            fail("此次变化会使本章或后续章节余额不足。");
        if (body)
            save(chapter, *body);
    });
}
