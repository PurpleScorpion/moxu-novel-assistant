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

        other.run("INSERT INTO people(id,name,attributes) VALUES(1,'林澈',''),(2,'苏禾','')");
        other.run("INSERT INTO chapters(id,title) VALUES(9,'山门')");
        other.run("INSERT INTO events(chapter,person,resource,delta,description) VALUES(9,1,'旧灵石',7,'山门礼')");
        other.createResource("  声望  ");
        for (const auto &name : QStringList{"积分", "旧灵石", "声望", " "})
        {
            try
            {
                other.createResource(name);
                return 18;
            }
            catch (const std::runtime_error &)
            {
            }
        }
        if (!reopened.resourceNames().contains("声望") || !other.resourceNames().contains("旧灵石") ||
            again.resourceNames().contains("声望"))
            return 19;
        other.atomic([&] {
            for (int i = 1; i <= 65; ++i)
                other.run("INSERT INTO events(chapter,person,resource,delta,description) VALUES(1,1,'旧灵石',1,?)",
                          {QString("任务%1").arg(i)});
        });
        other.event(1, 2, "旧灵石", 5, "任务66");
        other.event(1, 1, "声望", 2, "任务67");
        LedgerFilter query{"林", "旧灵石", "任务", false};
        const auto second = other.ledgerPage(query, 2);
        if (second.total != 65 || second.pages != 3 || second.rows.size() != 30 ||
            second.rows.first().last() != "任务31" || second.rows.first()[4] != "31")
            return 20;
        query.newestFirst = true;
        query.description.clear();
        const auto newest = other.ledgerPage(query, 1);
        if (newest.rows.first() != QStringList{"第 4 章 · 山门", "林澈", "旧灵石", "7", "72", "山门礼"} ||
            other.ledgerPage(query, 99).rows.last().last() != "任务1")
            return 21;
        query.description = "任务6";
        const auto partial = other.ledgerPage(query, 3);
        if (partial.number != 1 || partial.total != 7 || partial.rows.last()[4] != "6")
            return 22;
        query.description = "%";
        if (other.ledgerPage(query, 1).total != 0)
            return 23;
        auto rejected = [](auto attempt) {
            try
            {
                attempt();
            }
            catch (const std::runtime_error &)
            {
                return true;
            }
            return false;
        };
        if (other.resourceBalances(1, 1).value("旧灵石") != "65" || other.resourceBalances(2, 1).value("声望") != "0")
            return 24;
        other.storyEvent(1, 1, StoryKind::Acquire, "太极图", 0, "得宝");
        other.storyEvent(3, 1, StoryKind::Transfer, "太极图", 2, "山门赠宝");
        QString wrong = "错误正文";
        const auto oldBody = other.value("SELECT body FROM chapters WHERE id=2");
        if (!rejected([&] { other.storyEvent(2, 1, StoryKind::Transfer, "太极图", 2, "冲突", &wrong); }) ||
            other.ownedItems(1, 2) != QStringList{"太极图"} || reopened.ownedItems(2, 3) != QStringList{"太极图"} ||
            !other.ownedItems(1, 3).isEmpty() || other.value("SELECT body FROM chapters WHERE id=2") != oldBody)
            return 25;
        auto gifts = other.ledgerPage({"苏", "", "太极图"}, 1);
        if (gifts.total != 1 ||
            gifts.rows.first().mid(1) != QStringList{"林澈 → 苏禾", "太极图", "—", "—", "山门赠宝"} ||
            other.resourceNames().contains(QStringLiteral("")))
            return 26;
        CharacterSkill art{"一气化三清", "入门", "三日", 1};
        other.skillEvent(1, 1, SkillAction::Learn, art, "习得神通");
        art.level = "大成";
        other.skillEvent(2, 1, SkillAction::Advance, art, "突破");
        other.skillEvent(3, 1, SkillAction::Use, art, "施展神通");
        if (other.characterSkills(1, 1).first().level != "入门" || other.characterSkills(1, 2).first().remaining != 1 ||
            reopened.characterSkills(1, 3).first().remaining != 0 ||
            !rejected([&] { other.skillEvent(2, 1, SkillAction::Use, art, "提前施展", &wrong); }) ||
            !rejected([&] { other.skillEvent(1, 2, SkillAction::Use, art, "尚未习得"); }))
            return 27;
        art.remaining = -1;
        other.skillEvent(2, 2, SkillAction::Learn, art, "传功");
        for (int i = 0; i < 4; ++i)
            other.skillEvent(2, 2, SkillAction::Use, art, "切磋");
        if (other.characterSkills(2, 2).first().remaining != -1 || !other.characterSkills(2, 1).isEmpty())
            return 28;
        other.storyEvent(1, 1, StoryKind::Note, {}, 0, "拜入山门");
        other.plotStep("玄武门之变", {}, 1, "开始", {});
        other.plotStep("玄武门之变", "宫门部署", 1, "开始", "调兵");
        other.plotStep("玄武门之变", "朝堂反应", 1, "开始", "入朝");
        other.plotStep("玄武门之变", "宫门部署", 2, "结束", "宫门已定");
        if (!rejected([&] { other.plotStep("玄武门之变", {}, 2, "结束", {}); }))
            return 29;
        other.plotStep("玄武门之变", "朝堂反应", 3, "结束", "宣诏");
        other.plotStep("玄武门之变", {}, 3, "结束", "尘埃落定");
        if (!rejected([&] { other.plotStep("玄武门之变", "宫门部署", 3, "进展", "越界"); }) ||
            reopened.ledgerPage({"", "", "玄武门之变"}, 1).total != 6)
            return 30;
        other.supplyEvent(1, 1, SupplyAction::Receive, "血瓶", 5, 0, "购药");
        other.supplyEvent(2, 1, SupplyAction::Consume, "血瓶", 2, 0, "疗伤");
        other.supplyEvent(3, 1, SupplyAction::Give, "血瓶", 1, 2, "赠药");
        other.supplyEvent(3, 2, SupplyAction::Consume, "血瓶", 1, 0, "服药");
        if (other.supplyCounts(1, 1).value("血瓶") != 5 || reopened.supplyCounts(1, 3).value("血瓶") != 2 ||
            other.supplyCounts(2, 3).value("血瓶") != 0 ||
            !rejected([&] { other.supplyEvent(2, 1, SupplyAction::Consume, "血瓶", 3, 0, "提前透支", &wrong); }) ||
            other.ledgerPage({"苏", "", "赠药"}, 1).rows.first().mid(1, 4) !=
                QStringList{"林澈 → 苏禾", "血瓶", "-1", "2"} ||
            other.ledgerPage({"苏", "", "服药"}, 1).rows.first().at(4) != "0")
            return 31;
        other.event(1, 1, "太极图", 8, "同名数值");
        if (other.ledgerPage({"", "太极图", "", true, "item"}, 1).total != 2 ||
            other.ledgerPage({"", "太极图", "", true, "numeric"}, 1).total != 1 ||
            other.ledgerPage({"", "血瓶", "", true, "supply"}, 1).total != 4 ||
            other.ledgerPage({"", "一气化三清", "", true, "skill"}, 1).total != 8)
            return 32;
        other.saveCatalogEntry({"item", "太极图摹本"}, {"item", "太极图"});
        other.removeCatalogEntry({"numeric", "太极图"});
        other.removeCatalogEntry({"supply", "血瓶"});
        if (!reopened.catalog("supply").isEmpty() || reopened.catalog("supply", true).first().name != "血瓶" ||
            reopened.catalog("item").first().name != "太极图摹本" ||
            reopened.ownedItems(2, 3) != QStringList{"太极图"} ||
            reopened.resourceBalances(1, 1).value("太极图") != "8" || reopened.supplyCounts(1, 2).value("血瓶") != 3 ||
            reopened.ledgerPage({"", "太极图", "", true, "item"}, 1).total != 2)
            return 33;
    }
    catch (const std::exception &e)
    {
        qCritical("%s", e.what());
        return 5;
    }
}
