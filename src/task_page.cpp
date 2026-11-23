#include "task_page.h"
#include "template_presets.h"
#include "interface.h"
#include "resource_catalog.h"
#include <limits>

namespace
{
void configureTable(QTableWidget *table, const QStringList &headers)
{
    table->setColumnCount(headers.size());
    table->setHorizontalHeaderLabels(headers);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::SingleSelection);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setAlternatingRowColors(true);
    table->setShowGrid(false);
    table->verticalHeader()->hide();
    table->horizontalHeader()->setDefaultAlignment(Qt::AlignLeft|Qt::AlignVCenter);
    table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
}
void cell(QTableWidget *table, int row, int column, const QString &text)
{
    auto *item = new QTableWidgetItem(text);
    item->setToolTip(text);
    table->setItem(row, column, item);
}
QString rewardDetail(const CatalogEntry &r)
{
    if (r.kind != "skill")
        return {};
    QStringList info;
    if (!r.level.isEmpty())
        info << r.level;
    if (!r.cooldown.isEmpty())
        info << "冷却 " + r.cooldown;
    info << (r.uses < 0 ? QStringLiteral("次数不限") : QString("%1 次").arg(r.uses));
    return info.join(" · ");
}
} // namespace

TaskPage::TaskPage()
    : person(new DownwardCombo), published(new DownwardCombo), completed(new DownwardCombo), resource(new DownwardCombo)
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->addWidget(pages, 1);
    root->addWidget(message);
    message->setTextFormat(Qt::PlainText);
    message->setWordWrap(true);
    pages->addWidget(listPage);
    auto *layout = new QVBoxLayout(listPage);
    layout->setContentsMargins(0, 0, 0, 0);
    auto *bar = new QHBoxLayout;
    layout->addLayout(bar);
    summary->setObjectName("hint");
    summary->setWordWrap(true);
    bar->addWidget(summary, 1);
    open = new QPushButton("查看任务");
    auto *templates = new QPushButton("提示模板");
    bar->addWidget(templates);
    connect(templates, &QPushButton::clicked, this, [this] {
        invoke([&] {
            reloadPatterns();
            pages->setCurrentWidget(templatePage);
        });
    });
    auto *create = new QPushButton("新建任务");
    create->setObjectName("primary");
    bar->addWidget(open);
    bar->addWidget(create);
    configureTable(list, {"任务名称", "承接人", "发布章节", "状态", "完成章节"});
    list->setObjectName("storyTaskList");
    layout->addWidget(list, 1);
    connect(list, &QTableWidget::currentCellChanged, this, [this](int r) { open->setEnabled(r >= 0); });
    connect(create, &QPushButton::clicked, this, [this] { invoke([&] { showForm(0); }); });
    connect(open, &QPushButton::clicked, this, [this] {
        invoke([&] {
            if (auto *item = list->item(list->currentRow(), 0))
                showForm(item->data(Qt::UserRole).toInt());
        });
    });
    connect(list, &QTableWidget::cellDoubleClicked, open, [this] { open->click(); });

    auto *scroll = new QScrollArea;
    scroll->setObjectName("formScroll");
    scroll->setWidgetResizable(true);
    scroll->setWidget(formPage);
    pages->addWidget(scroll);
    auto *form = new QVBoxLayout(formPage);
    form->setContentsMargins(2, 0, 10, 2);
    form->setSpacing(10);
    auto *top = new QHBoxLayout;
    form->addLayout(top);
    heading->setObjectName("sectionTitle");
    top->addWidget(heading, 1);
    auto *back = new QPushButton("返回任务列表");
    top->addWidget(back);
    connect(back, &QPushButton::clicked, this, [this] {
        invoke([&] {
            refreshList(task.id);
            message->clear();
        });
    });
    auto *grid = new QGridLayout;
    form->addLayout(grid);
    grid->setColumnStretch(1, 1);
    grid->setColumnStretch(3, 1);
    title->setObjectName("taskTitle");
    title->setPlaceholderText("例如：初入昆仑");
    person->setObjectName("taskPerson");
    published->setObjectName("taskPublished");
    description->setObjectName("taskInstructions");
    description->setMaximumHeight(80);
    description->setPlaceholderText("写下达成任务的条件");
    grid->addWidget(new QLabel("任务名称"), 0, 0);
    grid->addWidget(title, 0, 1, 1, 3);
    grid->addWidget(new QLabel("承接人"), 1, 0);
    grid->addWidget(person, 1, 1);
    grid->addWidget(new QLabel("发布章节"), 1, 2);
    grid->addWidget(published, 1, 3);
    grid->addWidget(new QLabel("完成条件"), 2, 0);
    grid->addWidget(description, 2, 1, 1, 3);
    form->addWidget(sectionLabel("任务奖励"));
    auto *tools = new QHBoxLayout(rewardTools);
    tools->setContentsMargins(0, 0, 0, 0);
    resource->setObjectName("taskResource");
    quantity->setObjectName("taskQuantity");
    quantity->setRange(1, std::numeric_limits<int>::max());
    tools->addWidget(resource, 1);
    tools->addWidget(new QLabel("数量"));
    tools->addWidget(quantity);
    addReward = new QPushButton("添加奖励");
    dropReward = new QPushButton("移除奖励");
    tools->addWidget(addReward);
    tools->addWidget(dropReward);
    form->addWidget(rewardTools);
    rewards->setObjectName("taskRewards");
    connect(published, &QComboBox::currentIndexChanged, this, [this](int start) {
        if (start >= 0 && completed->currentIndex() < start && start < completed->count())
            completed->setCurrentIndex(start);
    });
    configureTable(rewards, {"奖励名称", "类型", "数量", "功法属性"});
    rewards->horizontalHeaderItem(2)->setTextAlignment(Qt::AlignCenter);
    rewards->setMinimumHeight(105);
    form->addWidget(rewards, 1);
    connect(resource, &QComboBox::currentIndexChanged, this, [this](int index) {
        auto r = catalog.value(index);
        bool many = r.kind == "numeric" || r.kind == "supply";
        quantity->setEnabled(many);
        if (!many)
            quantity->setValue(1);
        addReward->setEnabled(index >= 0);
    });
    connect(addReward, &QPushButton::clicked, this, [this] {
        invoke([&] {
            const auto r = catalog.at(resource->currentIndex());
            StoryTasks::addReward(task.rewards,r,quantity->value());
            showRewards();
        });
    });
    connect(rewards, &QTableWidget::currentCellChanged, this, [this](int row) { dropReward->setEnabled(row >= 0); });
    connect(dropReward, &QPushButton::clicked, this, [this] {
        int r = rewards->currentRow();
        if (r >= 0)
        {
            task.rewards.removeAt(r);
            showRewards();
        }
    });
    auto *actions = new QHBoxLayout;
    form->addLayout(actions);
    discard = new QPushButton("删除任务");
    commit = new QPushButton;
    commit->setObjectName("primary");
    actions->addWidget(discard);
    actions->addStretch();
    actions->addWidget(commit);
    connect(commit, &QPushButton::clicked, this, [this] { saveNow(); });
    connect(discard, &QPushButton::clicked, this, [this] {
        invoke([&] {
            if (!confirmDelete)
            {
                confirmDelete = true;
                discard->setText("确认删除此任务");
                return;
            }
            StoryTasks::remove(*book, task.id, task.revision);
            refreshList();
            message->setText("任务已删除。");
        });
    });
    auto *finishLayout = new QHBoxLayout(settlement);
    finishLayout->setContentsMargins(0, 8, 0, 0);
    completed->setObjectName("taskCompleted");
    finishLayout->addWidget(new QLabel("完成章节"));
    finishLayout->addWidget(completed, 1);
    auto *finish = new QPushButton("完成任务并发奖");
    finishLayout->addWidget(finish);
    form->addWidget(settlement);
    connect(finish, &QPushButton::clicked, this, [this] { invoke([&] { save(true); }); });

    pages->addWidget(templatePage);
    auto *settings = new QVBoxLayout(templatePage);
    settings->setContentsMargins(0, 0, 0, 0);
    auto *header = new QHBoxLayout;
    settings->addLayout(header);
    header->addWidget(sectionLabel("本书任务提示模板"), 1);
    auto *backToList = new QPushButton("返回任务列表");
    header->addWidget(backToList);
    connect(backToList, &QPushButton::clicked, this, [this] { invoke([&] { refreshList(task.id); }); });
    templateTabs->setObjectName("taskTemplateTabs");
    settings->addWidget(templateTabs, 1);
    for (int mode = 0; mode < 2; ++mode)
    {
        auto *tab = new QWidget;
        auto *columns = new QHBoxLayout(tab);
        auto *input = new TemplatePresets::TaskInput;
        input->presets->setChoices(TemplatePresets::task(mode == 1));
        patterns[mode] = input->text;
        columns->addWidget(input, 3);
        auto *sample = new QVBoxLayout;
        columns->addLayout(sample, 2);
        sample->addWidget(new QLabel("插入效果 · 示例任务"));
        auto *preview = new QPlainTextEdit;
        preview->setReadOnly(true);
        preview->setObjectName("taskExamplePreview");
        sample->addWidget(preview, 1);
        connect(input->text, &QPlainTextEdit::textChanged, this, [input, preview] {
            preview->setPlainText(TemplatePresets::taskExample(input->text->toPlainText()));
        });
        templateTabs->addTab(tab, mode == 0 ? "发布提示" : "完成提示");
    }
    auto *saveFormats = new QPushButton("保存模板");
    saveFormats->setObjectName("primary");
    settings->addWidget(saveFormats, 0, Qt::AlignRight);
    connect(saveFormats, &QPushButton::clicked, this, [this] { saveNow(); });
}

void TaskPage::reloadPatterns()
{
    for (int mode = 0; mode < 2; ++mode)
        if (patterns[mode]->toPlainText() == savedPatterns[mode])
        {
            savedPatterns[mode] = StoryTasks::messagePattern(*book, mode == 1);
            if (patterns[mode]->toPlainText() != savedPatterns[mode])
                patterns[mode]->setPlainText(savedPatterns[mode]);
        }
}
void TaskPage::savePatterns()
{
    book->atomic([&] {
        for (int mode = 0; mode < 2; ++mode)
            if (patterns[mode]->toPlainText() != savedPatterns[mode])
                StoryTasks::saveMessagePattern(*book, mode == 1, patterns[mode]->toPlainText());
    });
    for (int mode = 0; mode < 2; ++mode)
        savedPatterns[mode] = patterns[mode]->toPlainText();
    message->setText("本书提示模板已保存。");
}
void TaskPage::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    if (book && pages->currentWidget() == templatePage)
        invoke([&] { reloadPatterns(); });
}

void TaskPage::invoke(const std::function<void()> &action)
{
    message->clear();
    try
    {
        action();
    }
    catch (const std::exception &e)
    {
        message->setText(QString::fromUtf8(e.what()));
    }
}
void TaskPage::resetBook()
{
    pages->setCurrentIndex(0);
    for (int mode = 0; mode < 2; ++mode)
    {
        patterns[mode]->setPlainText({});
        savedPatterns[mode].clear();
    }
    templateTabs->setCurrentIndex(0);
    book = nullptr;
    task = {};
    catalog.clear();
    list->setRowCount(0);
    message->clear();
}
void TaskPage::load(Book &source, int currentChapter)
{
    if (book != &source)
        resetBook();
    book = &source;
    preferredChapter = currentChapter;
    if (pages->currentIndex() == 1)
        reloadChoices(person->currentData().toInt(), published->currentData().toInt(),
                      completed->currentData().toInt());
    else if (pages->currentWidget() == templatePage)
        reloadPatterns();
    else
        refreshList(task.id);
}
void TaskPage::refreshList(int select)
{
    pages->setCurrentIndex(0);
    list->setRowCount(0);
    open->setEnabled(false);
    auto q = book->run("SELECT t.id,t.title,p.name,c.position,c.title,d.position,d.title FROM story_tasks t JOIN "
                       "people p ON p.id=t.person "
                       "JOIN chapters c ON c.id=t.published LEFT JOIN chapters d ON d.id=t.completed ORDER BY "
                       "t.completed IS NOT NULL,c.position,t.id");
    int selected = -1, active = 0;
    while (q.next())
    {
        if (q.value(5).isNull())
            ++active;
        int r = list->rowCount();
        list->insertRow(r);
        cell(list, r, 0, q.value(1).toString());
        list->item(r, 0)->setData(Qt::UserRole, q.value(0));
        cell(list, r, 1, q.value(2).toString());
        cell(list, r, 2, numberedChapter(q.value(3).toInt(), q.value(4).toString()));
        cell(list, r, 3, q.value(5).isNull() ? "进行中" : "已完成");
        cell(list, r, 4,
             q.value(5).isNull() ? QStringLiteral("—") : numberedChapter(q.value(5).toInt(), q.value(6).toString()));
        if (q.value(0).toInt() == select)
            selected = r;
    }
    summary->setText(list->rowCount()
                         ? QString("%1 项进行中   ·   %2 项已完成").arg(active).arg(list->rowCount() - active)
                         : QStringLiteral("还没有任务，点击「新建任务」发布第一项。"));
    if (list->rowCount())
        list->setCurrentCell(qMax(0, selected), 0);
}
void TaskPage::reloadChoices(int actor, int start, int end)
{
    person->clear();
    published->clear();
    completed->clear();
    auto p = book->run("SELECT id,name,count(*) OVER(PARTITION BY name) FROM people ORDER BY name,id");
    while (p.next())
        person->addItem(p.value(1).toString() + (p.value(2).toInt() > 1 ? " · #" + p.value(0).toString() : QString()),
                        p.value(0));
    if (actor)
        person->setCurrentIndex(person->findData(actor));
    auto ch = book->run("SELECT id,position,title FROM chapters ORDER BY position");
    while (ch.next())
    {
        const auto label = numberedChapter(ch.value(1).toInt(), ch.value(2).toString());
        published->addItem(label, ch.value(0));
        completed->addItem(label, ch.value(0));
    }
    published->setCurrentIndex(qMax(0, published->findData(start)));
    completed->setCurrentIndex(qMax(published->currentIndex(), completed->findData(end)));
    resource->clear();
    catalog = book->catalog();
    for (const auto &entry : catalog)
        resource->addItem(resourceKindLabel(entry.kind) + " · " + entry.name);
    addReward->setEnabled(resource->count() > 0);
}
void TaskPage::showRewards()
{
    rewards->setRowCount(0);
    dropReward->setEnabled(false);
    for (const auto &reward : task.rewards)
    {
        auto r = rewards->rowCount();
        rewards->insertRow(r);
        cell(rewards, r, 0, reward.resource.name);
        cell(rewards, r, 1, resourceKindLabel(reward.resource.kind));
        cell(rewards, r, 2, QString::number(reward.quantity));
        rewards->item(r,2)->setTextAlignment(Qt::AlignCenter);
        cell(rewards, r, 3, rewardDetail(reward.resource));
    }
}
void TaskPage::showForm(int id)
{
    task = id ? StoryTasks::get(*book, id) : StoryTask{};
    bool locked = task.completed != 0;
    reloadChoices(task.person, task.published ? task.published : preferredChapter,
                  locked ? task.completed : preferredChapter);
    title->setText(task.title);
    description->setPlainText(task.instructions);
    title->setReadOnly(locked);
    description->setReadOnly(locked);
    person->setEnabled(!locked);
    published->setEnabled(!locked);
    heading->setText(locked ? "已完成任务" : id ? "进行中的任务" : "新建任务");
    rewardTools->setVisible(!locked);
    settlement->setVisible(id && !locked);
    discard->setVisible(id && !locked);
    commit->setVisible(!locked);
    confirmDelete = false;
    discard->setText("删除任务");
    commit->setText(id ? "保存任务" : "发布任务");
    quantity->setValue(1);
    showRewards();
    pages->setCurrentIndex(1);
    if (!person->count())
        message->setText("请先在「人物档案」中创建任务承接人。");
    if (locked)
        message->setText("完成于" + completed->currentText() + "，奖励已结算。");
}
void TaskPage::save(bool finish)
{
    StoryTask edited = task;
    edited.title = title->text();
    edited.instructions = description->toPlainText();
    edited.person = person->currentData().toInt();
    edited.published = published->currentData().toInt();
    int id = 0;
    book->atomic([&] {
        id = StoryTasks::save(*book, edited);
        if (finish)
            StoryTasks::complete(*book, id, edited.revision + 1, completed->currentData().toInt());
    });
    task.id = id;
    refreshList(id);
    message->setText(finish ? "任务已完成，奖励已结算。" : edited.id ? "任务修改已保存。" : "任务已发布。");
}
void TaskPage::saveNow()
{
    if (pages->currentWidget() == templatePage)
        invoke([&] { savePatterns(); });
    else if (pages->currentIndex() == 1 && !task.completed)
        invoke([&] { save(false); });
}
