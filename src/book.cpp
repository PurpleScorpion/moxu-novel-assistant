#include "book.h"

namespace
{
void fail(const QString &message)
{
    throw std::runtime_error(message.toStdString());
}
} // namespace
QSqlQuery Book::run(const QString &sql, const QVariantList &args) const
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
void Book::save(int chapter, const QString &body)
{
    auto previous = value("SELECT body FROM chapters WHERE id=?", {chapter});
    if (!previous.isValid())
        fail("章节不存在。");
    if (previous.toString() == body)
        return;
    run("INSERT INTO revisions(chapter,body) VALUES(?,?)", {chapter, previous});
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
