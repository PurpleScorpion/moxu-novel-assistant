#include "book.h"
int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QTemporaryDir dir;
    QString path = dir.filePath("test.moxu");
    try
    {
        {
            Book book(path, "测试");
            book.run("INSERT INTO people VALUES(1,'甲','')");
            book.run("INSERT INTO chapters(title) VALUES('第二章')");
            book.event(1, 1, "积分", 300, "奖励");
            QString text = "剩余180积分";
            book.event(2, 1, "积分", -120, "兑换", &text);
            book.event(2, 1, "积分", -80, "消费");
            try
            {
                QString wrong = "不能保存";
                book.event(1, 1, "积分", -150, "补录", &wrong);
                return 1;
            }
            catch (const std::runtime_error &)
            {
            }
            if (book.value("SELECT COUNT(*) FROM events").toInt() != 3 ||
                !book.value("SELECT body FROM chapters WHERE id=1").toString().isEmpty())
                return 2;
            // Reopen with the history schema and snapshots of a pre-0.12 book.
            book.run("DROP INDEX revision_history");
            book.run("ALTER TABLE revisions DROP COLUMN rolling");
            book.run("INSERT INTO revisions(chapter,body) VALUES(1,'旧版手稿')");
        }
        Book again(path);
        if (again.balance(1, "积分", 1) != 300 || again.balance(1, "积分", 2) != 100 ||
            again.value("SELECT body FROM chapters WHERE id=2").toString() != "剩余180积分")
            return 3;
        try
        {
            Book collision(path, "重名");
            return 4;
        }
        catch (const std::runtime_error &)
        {
        }
        again.atomic([&] { again.save(1, "第一句", Book::SaveMode::Automatic); });
        again.atomic([&] { again.save(1, "第二句", Book::SaveMode::Automatic); });
        auto count = [&] {
            return again.value("SELECT count(*) FROM revisions WHERE chapter=1 AND rolling=1").toInt();
        };
        if (count() != 1 || again.value("SELECT body FROM chapters WHERE id=1").toString() != "第二句")
            return 6;
        again.run("UPDATE revisions SET stamp=datetime('now','-6 minutes') WHERE chapter=1 AND rolling=1");
        again.atomic([&] { again.save(1, "第三句", Book::SaveMode::Automatic); });
        if (count() != 2 ||
            again.value("SELECT body FROM revisions WHERE chapter=1 ORDER BY id DESC LIMIT 1").toString() != "第二句")
            return 7;
        for (int i = 0; i < HistoryLimits{}.perChapter + 1; ++i)
            again.atomic([&] { again.save(1, QString::number(i)); });
        if (count() != HistoryLimits{}.perChapter ||
            again.value("SELECT body FROM revisions WHERE chapter=1 AND rolling=0").toString() != "旧版手稿" ||
            again.value("SELECT count(*) FROM revisions WHERE chapter=2").toInt() != 1)
            return 8;
        again.atomic([&] {
            for (int i = 0; i < 1000; ++i)
            {
                int chapter = i + 3;
                again.run("INSERT INTO chapters(id,title,body) VALUES(?, '测试章', '原稿')", {chapter});
                again.save(chapter, "修订");
            }
        });
        if (again.value("SELECT count(*) FROM revisions WHERE rolling=1").toInt() != HistoryLimits{}.perBook ||
            again.value("SELECT count(*) FROM chapters WHERE body='修订'").toInt() != 1000 ||
            again.historyCleanupCount() != 2)
            return 9;
        again.includeLegacyInHistoryLimit(2);
        if (again.value("SELECT count(*) FROM revisions").toInt() != HistoryLimits{}.perBook)
            return 10;
        again.atomic([&] {
            for (int chapter : {1, 2, 3})
            {
                again.run("UPDATE chapters SET body=hex(zeroblob(?)) WHERE id=?", {Book::historyBudget / 4, chapter});
                again.save(chapter, "大章修订");
            }
        });
        if (again.value("SELECT SUM(octet_length(body)) FROM revisions").toLongLong() > Book::historyBudget ||
            again.value("SELECT count(*) FROM chapters WHERE body='大章修订'").toInt() != 3)
            return 11;
        Book other(dir.filePath("other.moxu"), "乙");
        other.run("INSERT INTO revisions(chapter,body) VALUES(1,'旧稿')");
        other.save(1, "一");
        other.save(1, "二");
        const auto usage = other.historyUsage();
        if (usage.count != 3 || usage.bytes != 9 || usage.legacyCount != 1)
            return 12;
        try
        {
            other.setHistoryLimits({1, 2}, 0);
            return 13;
        }
        catch (const std::runtime_error &)
        {
        }
        if (other.historyUsage().count != 3 || other.historyLimits().perChapter != 10)
            return 14;
        other.setHistoryLimits({1, 2}, 1);
        other.run("INSERT INTO chapters(id,title,body) VALUES(2,'二','二'),(3,'三','三')");
        other.save(2, "新二");
        other.save(3, "新三");
        if (other.historyUsage().count != 3 || other.historyCleanupCount() != 1 ||
            other.value("SELECT count(*) FROM revisions WHERE chapter=1 AND rolling=1").toInt() != 0)
            return 15;
        Book reopened(other.filePath());
        if (reopened.historyLimits().perBook != 2 || again.historyLimits().perBook != 100)
            return 16;
        other.setHistoryLimits({0, 2}, other.historyRemovalCount({0, 2}));
        other.save(1, "照常保存");
        if (other.historyUsage().count != 1 ||
            other.value("SELECT body FROM chapters WHERE id=1").toString() != "照常保存")
            return 17;
    }
    catch (const std::exception &e)
    {
        qCritical("%s", e.what());
        return 5;
    }
}
