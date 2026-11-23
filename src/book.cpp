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
    const bool outer = transactionDepth == 0;
    const QString point = "moxu_" + QString::number(transactionDepth);
    run(outer ? QStringLiteral("BEGIN IMMEDIATE") : "SAVEPOINT " + point);
    QScopedValueRollback depth(transactionDepth, transactionDepth + 1);
    try
    {
        operation();
        run(outer ? QStringLiteral("COMMIT") : "RELEASE " + point);
    }
    catch (...)
    {
        if (outer) db.rollback();
        else
        {
            run("ROLLBACK TO " + point);
            run("RELEASE " + point);
        }
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
        run("CREATE TABLE IF NOT EXISTS chapter_summaries(chapter INTEGER PRIMARY KEY REFERENCES chapters(id) "
            "ON DELETE CASCADE, body TEXT NOT NULL)");
        if (!value("SELECT 1 FROM pragma_table_info('chapters') WHERE name='position'").isValid())
            atomic([&] {
                run("ALTER TABLE chapters ADD COLUMN position INTEGER");
                run("WITH ranked AS MATERIALIZED (SELECT id,row_number() OVER(ORDER BY id) AS n FROM chapters) "
                    "UPDATE chapters SET position=ranked.n FROM ranked WHERE chapters.id=ranked.id");
                run("CREATE INDEX chapter_position ON chapters(position)");
                run("ALTER TABLE chapters ADD COLUMN body_version INTEGER NOT NULL DEFAULT 0");
                run("ALTER TABLE chapters ADD COLUMN word_count INTEGER");
                run("ALTER TABLE chapter_summaries ADD COLUMN source_version INTEGER");
                run("CREATE TRIGGER chapter_appended AFTER INSERT ON chapters WHEN NEW.position IS NULL BEGIN "
                    "UPDATE chapters SET position=(SELECT coalesce(max(position),0)+1 FROM chapters) WHERE id=NEW.id; END");
                run("CREATE TRIGGER chapter_edited AFTER UPDATE OF body ON chapters WHEN OLD.body<>NEW.body BEGIN "
                    "UPDATE chapters SET body_version=OLD.body_version+1,word_count=NULL WHERE id=NEW.id; END");
                // Only replace the exact heading supplied by the generator. Preserve the author's text below it.
                run("CREATE TRIGGER summary_heading AFTER UPDATE OF position,title ON chapters BEGIN "
                    "UPDATE chapter_summaries SET body='【第 '||NEW.position||' 章 · '||NEW.title||'】'||"
                    "substr(body,instr(body,char(10))) WHERE chapter=NEW.id AND substr(body,1,instr(body,char(10))-1)"
                    "='【第 '||OLD.position||' 章 · '||OLD.title||'】'; END");
            });
        if (!value("SELECT 1 FROM pragma_table_info('writing_notes') WHERE name='position'").isValid())
            atomic([&] {
                run("ALTER TABLE writing_notes ADD COLUMN position INTEGER");
                run("WITH ranked AS MATERIALIZED (SELECT id,row_number() OVER(ORDER BY coalesce(chapter,0),id) AS n "
                    "FROM writing_notes WHERE kind='outline') UPDATE writing_notes SET position=ranked.n FROM ranked "
                    "WHERE writing_notes.id=ranked.id");
                run("CREATE TRIGGER outline_appended AFTER INSERT ON writing_notes WHEN NEW.kind='outline' BEGIN "
                    "UPDATE writing_notes SET position=(SELECT coalesce(max(position),0)+1 FROM writing_notes "
                    "WHERE kind='outline') WHERE id=NEW.id; END");
            });
        if (!value("SELECT 1 FROM pragma_table_info('revisions') WHERE name='rolling'").isValid())
            atomic([&] {
                if (!value("SELECT 1 FROM pragma_table_info('revisions') WHERE name='rolling'").isValid())
                    run("ALTER TABLE revisions ADD COLUMN rolling INTEGER NOT NULL DEFAULT 0");
            });
        run("CREATE INDEX IF NOT EXISTS revision_history ON revisions(rolling,chapter,id)");
        run("CREATE TABLE IF NOT EXISTS resources(name TEXT PRIMARY KEY NOT NULL)");
        run("CREATE INDEX IF NOT EXISTS event_resource ON events(resource)");
        run("CREATE INDEX IF NOT EXISTS event_order ON events(chapter,id)");
        run("CREATE TABLE IF NOT EXISTS event_details(event_id INTEGER PRIMARY KEY REFERENCES events ON DELETE CASCADE,"
            "kind TEXT NOT NULL CHECK(kind IN ('note','acquire','transfer','learn','advance','use')),name TEXT,"
            "recipient INTEGER REFERENCES people,level TEXT,cooldown TEXT,uses INTEGER CHECK(uses>=0))");
        run("CREATE INDEX IF NOT EXISTS event_detail_name ON event_details(name,kind)");
        run("CREATE TABLE IF NOT EXISTS supplies(event_id INTEGER PRIMARY KEY REFERENCES events ON DELETE CASCADE,"
            "name TEXT NOT NULL,change INTEGER NOT NULL CHECK(change<>0),recipient INTEGER REFERENCES people)");
        run("CREATE INDEX IF NOT EXISTS supply_name ON supplies(name,event_id)");
        run("CREATE TABLE IF NOT EXISTS catalog_edits(kind TEXT NOT NULL,name TEXT NOT NULL,listed INTEGER NOT NULL,"
            "level TEXT,cooldown TEXT,uses INTEGER,PRIMARY KEY(kind,name))");
        run("CREATE VIEW IF NOT EXISTS resource_history AS SELECT 'numeric' AS kind,resource AS name FROM events WHERE "
            "resource<>'' "
            "UNION SELECT CASE WHEN kind IN ('acquire','transfer') THEN 'item' ELSE 'skill' END,name FROM "
            "event_details "
            "WHERE kind<>'note' UNION SELECT 'supply',name FROM supplies");
        run("CREATE VIEW IF NOT EXISTS book_catalog AS SELECT k.kind,k.name,c.level,c.cooldown,c.uses FROM "
            "(SELECT 'numeric' AS kind,'积分' AS name UNION SELECT 'numeric',name FROM resources UNION SELECT "
            "kind,name FROM resource_history "
            "UNION SELECT kind,name FROM catalog_edits) k LEFT JOIN catalog_edits c USING(kind,name) WHERE "
            "coalesce(c.listed,1)=1");
        run("CREATE VIEW IF NOT EXISTS supply_flows AS SELECT s.name,e.id,e.chapter,e.person,s.change "
            "FROM supplies s JOIN events e ON e.id=s.event_id UNION ALL "
            "SELECT s.name,e.id,e.chapter,s.recipient,-s.change FROM supplies s JOIN events e ON e.id=s.event_id "
            "WHERE s.recipient IS NOT NULL");
        run("CREATE TABLE IF NOT EXISTS task_message_templates(completed INTEGER PRIMARY KEY CHECK(completed IN (0,1)),"
            "pattern TEXT NOT NULL)");
        run("CREATE TABLE IF NOT EXISTS story_tasks(id INTEGER PRIMARY KEY AUTOINCREMENT,title TEXT NOT NULL,"
            "instructions TEXT NOT NULL,person INTEGER NOT NULL REFERENCES people(id),"
            "published INTEGER NOT NULL REFERENCES chapters(id),completed INTEGER REFERENCES chapters(id),"
            "revision INTEGER NOT NULL DEFAULT 1)");
        run("CREATE TABLE IF NOT EXISTS task_rewards(id INTEGER PRIMARY KEY,task INTEGER NOT NULL REFERENCES story_tasks(id) ON DELETE CASCADE,"
            "kind TEXT NOT NULL CHECK(kind IN ('numeric','supply','item','skill')),name TEXT NOT NULL,"
            "quantity INTEGER NOT NULL CHECK(quantity>0),level TEXT,cooldown TEXT,uses INTEGER NOT NULL,"
            "event INTEGER UNIQUE REFERENCES events(id),UNIQUE(task,kind,name))");
        run("CREATE TABLE IF NOT EXISTS foreshadows(id INTEGER PRIMARY KEY,title TEXT NOT NULL,status TEXT NOT NULL,"
            "expected INTEGER REFERENCES chapters(id), note TEXT NOT NULL DEFAULT '')");
        run("CREATE TABLE IF NOT EXISTS clue_quotes(clue INTEGER NOT NULL REFERENCES foreshadows(id) ON DELETE CASCADE,"
            "role TEXT NOT NULL,chapter INTEGER NOT NULL REFERENCES chapters(id),version INTEGER NOT NULL,"
            "offset INTEGER NOT NULL,quote TEXT NOT NULL,PRIMARY KEY(clue,role))");
        run("CREATE TABLE IF NOT EXISTS check_receipts(chapter INTEGER NOT NULL REFERENCES chapters(id),"
            "signature TEXT NOT NULL,event_id INTEGER NOT NULL REFERENCES events(id) ON DELETE CASCADE,"
            "PRIMARY KEY(chapter,signature))");
        run("CREATE TABLE IF NOT EXISTS plot_steps(id INTEGER PRIMARY KEY,arc TEXT NOT NULL,strand TEXT NOT NULL,"
            "chapter INTEGER NOT NULL REFERENCES chapters,phase TEXT NOT NULL CHECK(phase IN ('开始','进展','结束')),"
            "note TEXT NOT NULL,anchor INTEGER NOT NULL)");
        run("CREATE INDEX IF NOT EXISTS plot_history ON plot_steps(arc,chapter,id)");
        // The anchor orders a plot entry immediately after the latest existing character event.
        run("CREATE VIEW IF NOT EXISTS journal AS SELECT id,chapter,person,resource,delta,description,id AS anchor,"
            "0 AS plot FROM events UNION ALL SELECT id,chapter,NULL,'',0,arc || ' · ' || "
            "CASE WHEN strand='' THEN '主线' ELSE strand END || ' · ' || phase || "
            "CASE WHEN note='' THEN '' ELSE '：' || note END,anchor,1 FROM plot_steps");
        run("CREATE VIEW IF NOT EXISTS ordered_events AS SELECT e.*,c.position AS seq FROM events e "
            "JOIN chapters c ON c.id=e.chapter");
        run("CREATE VIEW IF NOT EXISTS ordered_stock AS SELECT f.*,c.position AS seq FROM supply_flows f "
            "JOIN chapters c ON c.id=f.chapter");
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
    return value("SELECT COALESCE(SUM(delta),0) FROM ordered_events WHERE person=? AND resource=? AND seq<=(SELECT position FROM chapters WHERE id=?)",
                 {person, resource, chapter})
        .toLongLong();
}
QStringList Book::resourceNames() const
{
    QStringList names;
    for (const auto &entry : catalog("numeric"))
        names << entry.name;
    return names;
}
QList<CatalogEntry> Book::catalog(const QString &kind, bool includeHistory) const
{
    QString source = "SELECT kind,name,level,cooldown,uses FROM book_catalog";
    if (includeHistory)
        source += " UNION ALL SELECT h.kind,h.name,NULL,NULL,NULL FROM resource_history h "
                  "JOIN catalog_edits c USING(kind,name) WHERE c.listed=0";
    auto sql = "SELECT kind,name,level,cooldown,uses FROM (" + source + ")";
    QVariantList args;
    if (!kind.isEmpty())
    {
        sql += " WHERE kind=?";
        args << kind;
    }
    auto q = run(sql + " ORDER BY kind!='numeric',kind,name!='积分',name", args);
    QList<CatalogEntry> entries;
    while (q.next())
        entries.append({q.value(0).toString(), q.value(1).toString(), q.value(2).toString(), q.value(3).toString(),
                        q.value(4).isNull() ? -1 : q.value(4).toInt()});
    return entries;
}
QMap<QString, QString> Book::resourceBalances(int person, int chapter) const
{
    QMap<QString, QString> balances;
    for (const auto &entry : catalog("numeric", true))
        balances.insert(entry.name, "0");
    auto rows =
        run("SELECT resource,sum(delta) FROM ordered_events WHERE person=? AND seq<=(SELECT position FROM chapters WHERE id=?) AND resource<>'' GROUP BY resource",
            {person, chapter});
    while (rows.next())
        balances.insert(rows.value(0).toString(), rows.value(1).toString());
    return balances;
}
void Book::createResource(const QString &name)
{
    saveCatalogEntry({"numeric", name});
}
void Book::removeCatalogEntry(const CatalogEntry &entry)
{
    run("INSERT INTO catalog_edits(kind,name,listed) VALUES(?,?,0) ON CONFLICT(kind,name) DO UPDATE SET listed=0",
        {entry.kind, entry.name});
}
void Book::saveCatalogEntry(const CatalogEntry &entry, const CatalogEntry &previous)
{
    const auto clean = entry.name.trimmed();
    if (clean.isEmpty())
        fail("请填写资源名。");
    if (!QStringList{"numeric", "item", "supply", "skill"}.contains(entry.kind))
        fail("请选择资源类型。");
    atomic([&] {
        if (previous.kind != entry.kind || previous.name != clean)
        {
            if (value("SELECT 1 FROM book_catalog WHERE kind=? AND name=?", {entry.kind, clean}).isValid())
                fail("本书已有同类型同名资源。");
            if (!previous.kind.isEmpty())
                removeCatalogEntry(previous);
        }
        run("INSERT OR REPLACE INTO catalog_edits(kind,name,listed,level,cooldown,uses) VALUES(?,?,1,?,?,?)",
            {entry.kind, clean, entry.level, entry.cooldown, entry.uses < 0 ? QVariant() : QVariant(entry.uses)});
    });
}
LedgerPage Book::ledgerPage(const LedgerFilter &filter, qint64 page) const
{
    QString from =
        " FROM journal e JOIN chapters c ON c.id=e.chapter LEFT JOIN people p ON p.id=e.person LEFT JOIN event_details d ON e.plot=0 AND d.event_id=e.id "
        "LEFT JOIN supplies s ON e.plot=0 AND s.event_id=e.id LEFT JOIN people target ON "
        "target.id=coalesce(d.recipient,s.recipient) WHERE 1";
    QVariantList args;
    if (!filter.person.isEmpty())
    {
        from += " AND (instr(lower(p.name),lower(?))>0 OR instr(lower(target.name),lower(?))>0)";
        args << filter.person << filter.person;
    }
    if (!filter.resource.isEmpty())
    {
        from += " AND coalesce(d.name,s.name,e.resource)=?";
        args << filter.resource;
        if (!filter.resourceKind.isEmpty())
        {
            from += " AND CASE WHEN s.name IS NOT NULL THEN 'supply' WHEN d.kind IN ('acquire','transfer') THEN 'item' "
                    "WHEN d.name IS NOT NULL THEN 'skill' ELSE 'numeric' END=?";
            args << filter.resourceKind;
        }
    }
    if (!filter.description.isEmpty())
    {
        from += " AND (instr(lower(e.description),lower(?))>0 OR instr(lower(coalesce(d.name,s.name)),lower(?))>0)";
        args << filter.description << filter.description;
    }
    LedgerPage result;
    result.total = value("SELECT count(*)" + from, args).toLongLong();
    result.pages = result.total ? 1 + (result.total - 1) / LedgerPage::pageSize : 1;
    result.number = qBound(qint64(1), page, result.pages);
    const QString order = filter.newestFirst ? " ORDER BY c.position DESC,e.anchor DESC,e.plot DESC,e.id DESC"
                                             : " ORDER BY c.position,e.anchor,e.plot,e.id";
    args << LedgerPage::pageSize << (result.number - 1) * LedgerPage::pageSize;
    // Page the matches first. Each returned balance includes all earlier events in its account.
    auto rows =
        run("WITH matches AS (SELECT e.*,coalesce(p.name,'—') || coalesce(' → ' || target.name,'') AS actor,"
            "CASE WHEN e.plot=1 THEN 'plot' WHEN s.event_id IS NOT NULL THEN 'supply' ELSE d.kind END AS kind,"
            "coalesce(d.name,s.name) AS subject,s.change AS quantity" +
                from + order +
                " LIMIT ? OFFSET ?),"
                " chapter_titles AS (SELECT id AS chapter_id,title,position FROM chapters)"
                " SELECT c.position,title,e.actor,CASE WHEN e.kind IS NULL THEN e.resource ELSE coalesce(e.subject,'—') END,"
                "CASE WHEN e.kind IS NULL THEN e.delta WHEN e.kind='supply' THEN e.quantity ELSE '—' END,"
                "CASE WHEN e.kind IS NULL THEN (SELECT sum(a.delta) FROM ordered_events a WHERE a.person=e.person AND "
                "a.resource=e.resource "
                " AND (a.seq,a.id)<=(c.position,e.id)) WHEN e.kind='supply' THEN "
                "(SELECT sum(f.change) FROM ordered_stock f WHERE f.person=e.person AND f.name=e.subject "
                "AND (f.seq,f.id)<=(c.position,e.id)) ELSE '—' END,e.description FROM matches e "
                "JOIN chapter_titles c ON c.chapter_id=e.chapter" +
                order,
            args);
    while (rows.next())
    {
        QStringList row{numberedChapter(rows.value(0).toLongLong(), rows.value(1).toString())};
        for (int col = 2; col < 7; ++col)
            row << rows.value(col).toString();
        result.rows << row;
    }
    return result;
}

namespace
{
void validatePlot(const Book &book, const QString &title)
{
    auto rows = book.run("SELECT strand,phase FROM plot_steps s JOIN chapters c ON c.id=s.chapter WHERE arc=? ORDER BY c.position,s.id", {title});
    QSet<QString> started, active;
    while (rows.next())
    {
        const auto which = rows.value(0).toString(), step = rows.value(1).toString();
        if (!which.isEmpty() && !active.contains(QString()))
            fail("支线的所有节点都应在大事开始和结束之间。");
        if (step == "开始")
        {
            if (started.contains(which))
                fail("该大事或支线已有开始记录，请选择已有条目记录进展。");
            started.insert(which);
            active.insert(which);
        }
        else
        {
            if (!active.contains(which))
                fail("该章节不在这条剧情线的开始与结束之间。");
            if (step == "结束")
            {
                if (which.isEmpty() && active.size() > 1)
                    fail("请先收束仍在进行的支线，再结束整件大事。");
                active.remove(which);
            }
        }
    }
}
void validateItem(const Book &book, const QString &name)
{
    auto history =
        book.run("SELECT d.kind,e.person,d.recipient FROM event_details d JOIN ordered_events e ON e.id=d.event_id "
            "WHERE d.name=? AND d.kind IN ('acquire','transfer') ORDER BY e.seq,e.id",
            {name});
    int owner = 0;
    while (history.next())
    {
        const int actor = history.value(1).toInt();
        if (history.value(0).toString() == "acquire")
        {
            if (owner)
                fail("本书已记录这件道具的获得。易主请使用「转赠道具」。");
            owner = actor;
        }
        else
        {
            if (owner != actor)
                fail("此次记录与本章或后续章节的道具归属冲突。");
            owner = history.value(2).toInt();
        }
    }
}
} // namespace

void Book::plotStep(const QString &arc, const QString &strand, int chapter, const QString &phase, const QString &note)
{
    const auto title = arc.trimmed(), line = strand.trimmed();
    if (title.isEmpty())
        fail("请填写大事名称。");
    if (line == "主线")
        fail("「主线」是大事本身，请换一个支线名称。");
    if (phase == "进展" && note.trimmed().isEmpty())
        fail("请填写本次进展。");
    atomic([&] {
        run("INSERT INTO plot_steps(arc,strand,chapter,phase,note,anchor) "
            "VALUES(?,?,?,?,?,(SELECT coalesce(max(id),0) FROM events))",
            {title, line.isEmpty() ? QStringLiteral("") : line, chapter, phase,
             note.trimmed().isEmpty() ? QStringLiteral("") : note.trimmed()});
        validatePlot(*this, title);
    });
}
qint64 Book::event(int chapter, int person, const QString &resource, int delta, const QString &description,
                 const QString *body)
{
    qint64 id = 0;
    atomic([&] {
        id = run("INSERT INTO events(chapter,person,resource,delta,description) VALUES(?,?,?,?,?)",
            {chapter, person, resource, delta, description}).lastInsertId().toLongLong();
        if (value("SELECT MIN(total) FROM (SELECT SUM(delta) OVER(ORDER BY seq,id ROWS UNBOUNDED PRECEDING) total "
                  "FROM ordered_events WHERE person=? AND resource=?)",
                  {person, resource})
                .toLongLong() < 0)
            fail("此次变化会使本章或后续章节余额不足。");
        if (body)
            save(chapter, *body);
    });
    return id;
}

QStringList Book::ownedItems(int person, int chapter) const
{
    auto rows = run("SELECT name FROM (SELECT d.name,coalesce(d.recipient,e.person) AS owner,"
                    "row_number() OVER(PARTITION BY d.name ORDER BY e.seq DESC,e.id DESC) AS latest "
                    "FROM event_details d JOIN ordered_events e ON e.id=d.event_id "
                    "WHERE d.kind IN ('acquire','transfer') AND e.seq<=(SELECT position FROM chapters WHERE id=?)) WHERE latest=1 AND owner=? ORDER BY name",
                    {chapter, person});
    QStringList items;
    while (rows.next())
        items << rows.value(0).toString();
    return items;
}

namespace
{
qint64 insertStoryRow(Book &book, int chapter, int person, const QString &description)
{
    if (description.trimmed().isEmpty())
        fail("请填写事件描述。");
    return book
        .run("INSERT INTO events(chapter,person,resource,delta,description) VALUES(?,?,'',0,?)",
             {chapter, person, description})
        .lastInsertId()
        .toLongLong();
}
} // namespace

qint64 Book::storyEvent(int chapter, int person, StoryKind kind, const QString &item, int recipient,
                      const QString &description, const QString *body)
{
    const QString name = item.trimmed();
    qint64 id = 0;
    atomic([&] {
        if (kind != StoryKind::Note && name.isEmpty())
            fail("请选择或填写道具名称。");
        if (kind == StoryKind::Transfer && (!recipient || recipient == person))
            fail("请选择另一位人物作为接收人。");
        id = insertStoryRow(*this, chapter, person, description);
        const QString type = kind == StoryKind::Note ? "note" : kind == StoryKind::Acquire ? "acquire" : "transfer";
        run("INSERT INTO event_details(event_id,kind,name,recipient) VALUES(?,?,?,?)",
            {id, type, kind == StoryKind::Note ? QVariant() : QVariant(name),
             kind == StoryKind::Transfer ? QVariant(recipient) : QVariant()});
        if (kind != StoryKind::Note)
        {
            validateItem(*this, name);
        }
        if (body)
            save(chapter, *body);
    });
    return id;
}

QMap<QString, qint64> Book::supplyCounts(int person, int chapter) const
{
    auto rows = run("WITH stock AS (SELECT name,sum(change) AS amount FROM ordered_stock "
                    "WHERE person=? AND seq<=(SELECT position FROM chapters WHERE id=?) GROUP BY name) "
                    "SELECT names.name,coalesce(stock.amount,0) FROM (SELECT DISTINCT name FROM supplies) names "
                    "LEFT JOIN stock ON stock.name=names.name ORDER BY names.name",
                    {person, chapter});
    QMap<QString, qint64> counts;
    while (rows.next())
        counts.insert(rows.value(0).toString(), rows.value(1).toLongLong());
    return counts;
}

qint64 Book::supplyEvent(int chapter, int person, SupplyAction action, const QString &name, int amount, int recipient,
                       const QString &description, const QString *body)
{
    const auto item = name.trimmed();
    if (item.isEmpty() || amount <= 0)
        fail("请选择或填写消耗品名称，并填写正整数数量。");
    if (action == SupplyAction::Give && (!recipient || recipient == person))
        fail("请选择另一位人物作为接收人。");
    qint64 id = 0;
    atomic([&] {
        id = insertStoryRow(*this, chapter, person, description);
        run("INSERT INTO supplies(event_id,name,change,recipient) VALUES(?,?,?,?)",
            {id, item, action == SupplyAction::Receive ? amount : -amount,
             action == SupplyAction::Give ? QVariant(recipient) : QVariant()});
        if (action != SupplyAction::Receive && value("SELECT min(total) FROM (SELECT sum(change) OVER(ORDER BY "
                                                     "seq,id ROWS UNBOUNDED PRECEDING) AS total "
                                                     "FROM ordered_stock WHERE name=? AND person=?)",
                                                     {item, person})
                                                       .toLongLong() < 0)
            fail("消耗品数量不足，无法完成本次操作。请核对本章和后续章节的库存。");
        if (body)
            save(chapter, *body);
    });
    return id;
}

namespace
{
QList<CharacterSkill> readSkills(const Book &book, int person, int chapter, const QString &only = {})
{
    QString sql =
        "SELECT d.name,d.kind,d.level,d.cooldown,d.uses FROM event_details d JOIN ordered_events e ON e.id=d.event_id "
        "WHERE d.kind IN ('learn','advance','use') AND e.person=?";
    QVariantList args{person};
    if (chapter > 0)
    {
        sql += " AND e.seq<=(SELECT position FROM chapters WHERE id=?)";
        args << chapter;
    }
    if (!only.isEmpty())
    {
        sql += " AND d.name=?";
        args << only;
    }
    auto rows = book.run(sql + " ORDER BY e.seq,e.id", args);
    QMap<QString, CharacterSkill> skills;
    while (rows.next())
    {
        const QString name = rows.value(0).toString(), type = rows.value(1).toString();
        if (type == "learn")
        {
            if (skills.contains(name))
                fail("该人物已有同名技能或功法的习得记录。");
            skills.insert(name, {name, rows.value(2).toString(), rows.value(3).toString(),
                                 rows.value(4).isNull() ? -1 : rows.value(4).toInt()});
            continue;
        }
        if (!skills.contains(name))
            fail("该人物在此章尚未习得这项技能或功法。");
        auto &skill = skills[name];
        if (type == "advance")
        {
            skill.level = rows.value(2).toString();
            skill.cooldown = rows.value(3).toString();
        }
        else if (skill.remaining == 0)
            fail("该技能的可用次数不足，请核对本章及后续章节的施展记录。");
        else if (skill.remaining > 0)
            --skill.remaining;
    }
    return skills.values();
}
} // namespace

QList<CharacterSkill> Book::characterSkills(int person, int chapter) const
{
    return readSkills(*this, person, chapter);
}

qint64 Book::skillEvent(int chapter, int person, SkillAction action, const CharacterSkill &skill,
                      const QString &description, const QString *body)
{
    const auto name = skill.name.trimmed();
    if (name.isEmpty())
        fail("请选择或填写技能／功法名称。");
    if (action == SkillAction::Learn && (skill.remaining == 0 || skill.remaining < -1))
        fail("可用次数请选择不限或至少一次。");
    qint64 id = 0;
    atomic([&] {
        id = insertStoryRow(*this, chapter, person, description);
        const QString type = action == SkillAction::Learn     ? "learn"
                             : action == SkillAction::Advance ? "advance"
                                                              : "use";
        run("INSERT INTO event_details(event_id,kind,name,level,cooldown,uses) VALUES(?,?,?,?,?,?)",
            {id, type, name, skill.level, skill.cooldown,
             action == SkillAction::Learn && skill.remaining >= 0 ? QVariant(skill.remaining) : QVariant()});
        readSkills(*this, person, 0, name); // Backdated uses must leave later chapters valid.
        if (body)
            save(chapter, *body);
    });
    return id;
}

int Book::insertChapter(const QString &title, int anchor, bool after)
{
    int id = 0;
    atomic([&] {
        int position = 0;
        if (anchor)
        {
            const auto old = value("SELECT position FROM chapters WHERE id=?", {anchor});
            if (!old.isValid()) fail("找不到要插入的章节位置。");
            position = old.toInt() + int(after);
            run("UPDATE chapters SET position=position+1 WHERE position>=?", {position});
        }
        id = run("INSERT INTO chapters(title,position) VALUES(?,?)",
                 {title, position ? QVariant(position) : QVariant()}).lastInsertId().toInt();
    });
    return id;
}

void Book::moveChapter(int id, int direction)
{
    if (direction != -1 && direction != 1) fail("请选择上移或下移。");
    atomic([&] {
        const int position = value("SELECT position FROM chapters WHERE id=?", {id}).toInt();
        const int other = value("SELECT id FROM chapters WHERE position=?", {position + direction}).toInt();
        if (!position || !other) fail("已经位于目录边界。");
        run("UPDATE chapters SET position=CASE id WHEN ? THEN ? ELSE ? END WHERE id IN (?,?)",
            {id, position + direction, position, id, other});
        if (value("SELECT 1 FROM story_tasks t JOIN chapters a ON a.id=t.published JOIN chapters b ON b.id=t.completed "
                  "WHERE a.position>b.position AND t.published IN (?,?) LIMIT 1", {id,other}).isValid())
            fail("无法调序：调整后任务的完成章节会早于发布章节。");
        // An adjacent swap only changes the relative order of events in these two chapters.
        if (value("SELECT count(DISTINCT chapter) FROM journal WHERE chapter IN (?,?)", {id, other}).toInt() < 2)
            return;
        if (value("SELECT 1 FROM (SELECT sum(delta) OVER(PARTITION BY person,resource ORDER BY seq,id) n "
                  "FROM ordered_events WHERE resource<>'') WHERE n<0 LIMIT 1").isValid())
            fail("无法调序：调整后会先支出后获得，导致资源余额不足。");
        if (value("SELECT 1 FROM (SELECT sum(change) OVER(PARTITION BY person,name ORDER BY seq,id) n "
                  "FROM ordered_stock) WHERE n<0 LIMIT 1").isValid())
            fail("无法调序：调整后会导致消耗品库存不足。");
        auto items = run("SELECT DISTINCT d.name FROM event_details d JOIN events e ON e.id=d.event_id "
                         "WHERE e.chapter=? AND d.kind IN ('acquire','transfer')", {id});
        while (items.next()) validateItem(*this, items.value(0).toString());
        auto skills = run("SELECT DISTINCT e.person,d.name FROM event_details d JOIN events e ON e.id=d.event_id "
                          "WHERE e.chapter=? AND d.kind IN ('learn','advance','use')", {id});
        while (skills.next()) readSkills(*this, skills.value(0).toInt(), 0, skills.value(1).toString());
        auto arcs = run("SELECT DISTINCT arc FROM plot_steps WHERE chapter=?", {id});
        while (arcs.next()) validatePlot(*this, arcs.value(0).toString());
    });
}
