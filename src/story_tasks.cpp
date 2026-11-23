#include "story_tasks.h"

namespace
{
void require(bool condition, const char *message)
{
    if (!condition)
        throw std::runtime_error(message);
}
void requireOpen(const StoryTask &task, int revision)
{
    require(!task.completed, "任务已经完成，奖励已结算。");
    require(task.revision == revision, "任务已修改，请返回列表重新打开。");
}
} // namespace
StoryTask StoryTasks::get(Book &book, int id)
{
    auto q =
        book.run("SELECT title,instructions,person,published,completed,revision FROM story_tasks WHERE id=?", {id});
    require(q.next(), "找不到这项任务，请刷新列表。");
    StoryTask t;
    t.id = id;
    t.title = q.value(0).toString();
    t.instructions = q.value(1).toString();
    t.person = q.value(2).toInt();
    t.published = q.value(3).toInt();
    t.completed = q.value(4).toInt();
    t.revision = q.value(5).toInt();
    auto rewards =
        book.run("SELECT kind,name,level,cooldown,uses,quantity FROM task_rewards WHERE task=? ORDER BY id", {id});
    while (rewards.next())
        t.rewards.append({{rewards.value(0).toString(), rewards.value(1).toString(), rewards.value(2).toString(),
                           rewards.value(3).toString(), rewards.value(4).toInt()},
                          rewards.value(5).toInt()});
    return t;
}

int StoryTasks::save(Book &book, const StoryTask &t)
{
    int id = t.id;
    book.atomic([&] {
        const auto title = t.title.trimmed();
        require(!title.isEmpty(), "请填写任务名称。");
        require(book.value("SELECT 1 FROM people WHERE id=?", {t.person}).isValid(),
                "请先在人物档案中建立并选择任务承接人。");
        require(book.value("SELECT 1 FROM chapters WHERE id=?", {t.published}).isValid(), "请选择发布章节。");
        auto args =
            QVariantList{title, t.instructions.isNull() ? QStringLiteral("") : t.instructions, t.person, t.published};
        QList<TaskReward> previous;
        if (id)
        {
            const auto old = get(book, id);
            requireOpen(old, t.revision);
            previous = old.rewards;
            args << id;
            book.run(
                "UPDATE story_tasks SET title=?,instructions=?,person=?,published=?,revision=revision+1 WHERE id=?",
                args);
            book.run("DELETE FROM task_rewards WHERE task=?", {id});
        }
        else
            id = book.run("INSERT INTO story_tasks(title,instructions,person,published) VALUES(?,?,?,?)", args)
                     .lastInsertId()
                     .toInt();

        const auto catalog = book.catalog();
        QSet<QPair<QString, QString>> seen;
        for (const auto &reward : t.rewards)
        {
            const auto &key = reward.resource;
            const QPair<QString, QString> pair{key.kind, key.name};
            require(!seen.contains(pair), "同一种奖励请合并为一行。");
            seen.insert(pair);
            require(reward.quantity > 0, "奖励数量必须大于零。");
            require(key.kind == "numeric" || key.kind == "supply" || reward.quantity == 1,
                    "独有道具和功法每项只能发放一份。");
            // A published reward keeps its original name and skill settings when the catalog changes.
            auto matches = [&](const CatalogEntry &v) { return v.kind == key.kind && v.name == key.name; };
            auto old = std::find_if(previous.cbegin(), previous.cend(),
                                    [&](const TaskReward &v) { return matches(v.resource); });
            const CatalogEntry *saved = nullptr;
            if (old != previous.cend())
                saved = &old->resource;
            else
            {
                auto it = std::find_if(catalog.cbegin(), catalog.cend(), matches);
                require(it != catalog.cend(), "奖励资源已变动，请从当前目录重新选择。");
                saved = &*it;
            }
            book.run("INSERT INTO task_rewards(task,kind,name,quantity,level,cooldown,uses) VALUES(?,?,?,?,?,?,?)",
                     {id, key.kind, key.name, reward.quantity, saved->level, saved->cooldown, saved->uses});
        }
    });
    return id;
}

void StoryTasks::complete(Book &book, int id, int revision, int chapter)
{
    book.atomic([&] {
        auto t = get(book, id);
        requireOpen(t, revision);
        require(book.value("SELECT 1 FROM chapters a,chapters b WHERE a.id=? AND b.id=? AND a.position<=b.position",
                           {t.published, chapter})
                    .isValid(),
                "完成章节不能早于发布章节。");
        const QString reason = "任务奖励 · " + t.title;
        for (const auto &reward : t.rewards)
        {
            const auto &r = reward.resource;
            qint64 eventId;
            if (r.kind == "numeric")
                eventId = book.event(chapter, t.person, r.name, reward.quantity, reason);
            else if (r.kind == "supply")
                eventId =
                    book.supplyEvent(chapter, t.person, SupplyAction::Receive, r.name, reward.quantity, 0, reason);
            else if (r.kind == "item")
                eventId = book.storyEvent(chapter, t.person, StoryKind::Acquire, r.name, 0, reason);
            else if (r.kind == "skill")
                eventId = book.skillEvent(chapter, t.person, SkillAction::Learn, {r.name, r.level, r.cooldown, r.uses},
                                          reason);
            else
                throw std::runtime_error("无法识别奖励的资源类型。");
            book.run("UPDATE task_rewards SET event=? WHERE task=? AND kind=? AND name=?",
                     {eventId, id, r.kind, r.name});
        }
        book.run("UPDATE story_tasks SET completed=?,revision=revision+1 WHERE id=?", {chapter, id});
    });
}
void StoryTasks::remove(Book &book, int id, int revision)
{
    book.atomic([&] {
        requireOpen(get(book, id), revision);
        book.run("DELETE FROM story_tasks WHERE id=?", {id});
    });
}
