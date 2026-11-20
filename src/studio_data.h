#pragma once
#include "writing_notes.h"

struct StudioItem
{
    QString kind, name, target, body;
};

inline QStringList studioKinds()
{
    return {"人物", "关系", "门派势力", "世界观", "大纲", "章节目录", "小说设定", "写作风格", "创作规范", "连续性档案"};
}

inline QList<StudioItem> parseStudioItems(const QString &text, const QStringList &allowed)
{
    // One heading per record; prose can contain arbitrary newlines without JSON
    // escaping.
    QList<StudioItem> rows;
    for (const auto &line : text.split('\n'))
    {
        if (line.trimmed().startsWith("@@"))
        {
            auto fields = line.trimmed().mid(2).replace(u'｜', u'|').split('|');
            for (auto &s : fields)
                s = s.trimmed();
            if (fields.size() < 2 || !allowed.contains(fields[0]) || fields[1].isEmpty() ||
                fields.size() != (fields[0] == "关系" ? 3 : 2))
                throw std::runtime_error("条目标题格式不完整，请重新生成。");
            rows.append({fields[0], fields[1], fields.value(2), {}});
        }
        else if (!rows.isEmpty())
            rows.last().body += line + '\n';
    }
    if (rows.isEmpty())
        throw std::runtime_error("没有识别到资料条目，请重试或缩小生成范围。");
    for (auto &r : rows)
    {
        r.body = r.body.trimmed();
        if (r.body.isEmpty())
            throw std::runtime_error("有条目缺少内容，请重新生成。");
    }
    return rows;
}

inline void applyStudioItems(Book &book, const QList<StudioItem> &rows, bool newBook = false, const QString &story = {})
{
    auto reject = [](const QString &message) { throw std::runtime_error(message.toStdString()); };
    book.atomic([&] {
        if (newBook)
            book.run("UPDATE book_profile SET synopsis=? WHERE id=1", {story});
        bool useFirst = newBook;
        for (const auto &row : rows)
        {
            if (!studioKinds().contains(row.kind) || row.name.trimmed().isEmpty() || row.body.trimmed().isEmpty())
                reject("请补齐所选条目的名称和内容。");
            if (row.kind == "关系")
                continue; // Resolve endpoints after inserting all selected people.
            if (row.kind == "人物")
            {
                if (book.value("SELECT 1 FROM people WHERE name=?", {row.name}).isValid())
                    reject("人物已存在，请取消勾选或改名：" + row.name);
                book.run("INSERT INTO people(name,attributes) VALUES(?,?)", {row.name, row.body});
                continue;
            }
            WritingNote note;
            note.title = row.name;
            note.body = row.body;
            bool outline = row.kind == "大纲" || row.kind == "章节目录";
            if (row.kind == "章节目录")
            {
                if (useFirst)
                {
                    note.chapter = book.value("SELECT id FROM chapters ORDER BY position LIMIT 1").toInt();
                    book.run("UPDATE chapters SET title=? WHERE id=?", {row.name, note.chapter});
                    useFirst = false;
                }
                else
                {
                    if (book.value("SELECT 1 FROM chapters WHERE title=?", {row.name}).isValid())
                        reject("章节已存在，请取消勾选或改名：" + row.name);
                    note.chapter = book.run("INSERT INTO chapters(title) VALUES(?)", {row.name}).lastInsertId().toInt();
                }
                note.title = "章节梗概 · " + row.name;
            }
            else if (!outline)
                note.title = row.kind + " · " + row.name;
            if (book.value("SELECT 1 FROM writing_notes WHERE kind=? AND title=? AND "
                           "coalesce(chapter,0)=?",
                           {outline ? "outline" : "skill", note.title, note.chapter})
                    .isValid())
                reject("同名资料已存在：" + note.title);
            WritingNotes(book, outline ? NoteScope::Outline : NoteScope::Book).save(note);
        }
        auto personId = [&](const QString &name) {
            auto q = book.run("SELECT id FROM people WHERE name=?", {name});
            if (!q.next())
                reject("关系缺少人物，请选中人物条目或修正姓名：" + name);
            int id = q.value(0).toInt();
            if (q.next())
                reject("同名人物无法确定关系归属：" + name);
            return id;
        };
        for (const auto &row : rows)
            if (row.kind == "关系")
            {
                int a = personId(row.name), b = personId(row.target);
                if (a == b)
                    reject("关系两端请选择不同的人物。");
                if (book.value("SELECT 1 FROM relations WHERE a=? AND b=? AND label=?", {a, b, row.body}).isValid())
                    reject("相同的人物关系已存在：" + row.name + "、" + row.target);
                book.run("INSERT INTO relations(a,b,label) VALUES(?,?,?)", {a, b, row.body});
            }
    });
}

inline QString studioBookContext(Book &book)
{
    QStringList parts{book.value("SELECT title FROM meta").toString(),
                      book.value("SELECT substr(synopsis,1,1800) FROM book_profile WHERE id=1").toString()};
    for (const auto &sql : QStringList{"SELECT name,substr(attributes,1,180) FROM people ORDER BY id LIMIT "
                                       "24",
                                       "SELECT title,substr(body,1,260) FROM writing_notes ORDER BY id "
                                       "LIMIT 30",
                                       "SELECT a.name || ' / ' || b.name,r.label FROM relations r JOIN "
                                       "people a ON a.id=r.a JOIN people b ON b.id=r.b LIMIT 24",
                                       "SELECT '章节',title FROM chapters ORDER BY position DESC LIMIT "
                                       "12"})
    {
        auto q = book.run(sql);
        while (q.next())
            parts << q.value(0).toString() + "：" + q.value(1).toString();
    }
    return parts.join('\n');
}

inline QString ledgerSnapshot(Book &book, int person, int chapter)
{
    QStringList parts;
    auto balances = book.run("SELECT resource,sum(delta) FROM ordered_events WHERE person=? AND "
                             "resource<>'' "
                             "AND seq<=(SELECT position FROM chapters WHERE id=?) GROUP BY resource",
                             {person, chapter});
    while (balances.next())
        parts << balances.value(0).toString() + "：" + balances.value(1).toString();
    for (const auto &name : book.ownedItems(person, chapter))
        parts << "持有：" + name;
    const auto supplies = book.supplyCounts(person, chapter);
    for (auto i = supplies.begin(); i != supplies.end(); ++i)
        if (i.value() > 0)
            parts << i.key() + " × " + QString::number(i.value());
    for (const auto &s : book.characterSkills(person, chapter))
        parts << QString("%1 等级：%2 冷却：%3 剩余次数：%4")
                     .arg(s.name, s.level, s.cooldown,
                          s.remaining < 0 ? QStringLiteral("不限") : QString::number(s.remaining));
    return parts.join('\n');
}
