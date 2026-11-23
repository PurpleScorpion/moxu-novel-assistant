#include "story_tasks.h"
#include <QRegularExpression>
#include <limits>

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
void StoryTasks::addReward(QList<TaskReward> &rewards, const CatalogEntry &resource, int quantity)
{
    require(quantity > 0, "奖励数量必须大于零。");
    for (auto &reward : rewards)
    {
        if (reward.resource.kind != resource.kind || reward.resource.name != resource.name)
            continue;
        require(resource.kind == "numeric" || resource.kind == "supply", "这项奖励已在列表中。");
        require(reward.quantity <= std::numeric_limits<int>::max() - quantity, "奖励数量过大。");
        reward.quantity += quantity;
        return;
    }
    rewards.append({resource, quantity});
}
QString StoryTasks::rewardText(const TaskReward &reward)
{
    const auto &r = reward.resource;
    if (r.kind == "numeric" || r.kind == "supply")
        return r.name + " × " + QString::number(reward.quantity);
    if (r.kind != "skill")
        return r.name;
    QStringList attributes;
    if (!r.level.isEmpty())
        attributes << r.level;
    if (!r.cooldown.isEmpty())
        attributes << "冷却 " + r.cooldown;
    attributes << (r.uses < 0 ? QStringLiteral("次数不限") : QString("可用 %1 次").arg(r.uses));
    return r.name + "（" + attributes.join("，") + "）";
}
QString StoryTasks::defaultMessagePattern(bool completed)
{
    return (completed ? QStringLiteral("【系统任务完成】") : QStringLiteral("【系统任务发布】")) +
           "{{任务名}}\n承接人：{{承接人}}\n{{条件行}}" +
           (completed ? QStringLiteral("已发奖励：") : QStringLiteral("任务奖励：")) + "{{奖励}}";
}
QString StoryTasks::messagePattern(Book &book, bool completed)
{
    auto value = book.value("SELECT pattern FROM task_message_templates WHERE completed=?", {int(completed)});
    return value.isNull() ? defaultMessagePattern(completed) : value.toString();
}
void StoryTasks::saveMessagePattern(Book &book, bool completed, const QString &pattern)
{
    require(!pattern.trimmed().isEmpty(), "请填写提示模板，或使用默认格式。");
    if (pattern == defaultMessagePattern(completed))
        book.run("DELETE FROM task_message_templates WHERE completed=?", {int(completed)});
    else
        book.run("INSERT INTO task_message_templates(completed,pattern) VALUES(?,?) "
                 "ON CONFLICT(completed) DO UPDATE SET pattern=excluded.pattern",
                 {int(completed), pattern});
}
QString StoryTasks::manuscriptText(const StoryTask &task, const QString &actor, const QString &pattern)
{
    QStringList rewards;
    for (const auto &reward : task.rewards)
        rewards << rewardText(reward);
    const auto condition = task.instructions.trimmed();
    const QHash<QString, QString> values{{"任务名", task.title.trimmed()},
                                         {"承接人", actor},
                                         {"完成条件", condition},
                                         {"条件行", condition.isEmpty() ? QString() : "完成条件：" + condition + '\n'},
                                         {"奖励", rewards.isEmpty() ? QStringLiteral("无") : rewards.join("、")}};
    static const QRegularExpression field(QStringLiteral(R"(\{\{([^{}\r\n]+)\}\})"));
    QString result;
    qsizetype end = 0;
    for (auto match : field.globalMatch(pattern))
    {
        result += QStringView(pattern).sliced(end, match.capturedStart() - end);
        result += values.value(match.captured(1), match.captured());
        end = match.capturedEnd();
    }
    result += QStringView(pattern).sliced(end);
    return result;
}
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
