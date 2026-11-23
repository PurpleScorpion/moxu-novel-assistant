#include "writer_tasks.h"
#include "resource_catalog.h"
#include <limits>

namespace
{
class WriterTasks : public QWidget
{
    Book &book;
    const int chapter;
    CommitWriterTask commit;
    QList<CatalogEntry> catalog;
    StoryTask draft, selected;
    QTabWidget *tabs = new QTabWidget;
    QLineEdit *title = new QLineEdit;
    QPlainTextEdit *conditions = new QPlainTextEdit;
    DownwardCombo *actor = new DownwardCombo, *resource = new DownwardCombo, *pending = new DownwardCombo;
    QSpinBox *amount = new QSpinBox;
    QListWidget *rewards = new QListWidget;
    QLabel *summary = new QLabel, *notice = new QLabel;
    QCheckBox *insert = new QCheckBox("将任务内容和奖励插入正文");
    QPushButton *submit = new QPushButton;

    void report(const std::function<void()> &action)
    {
        notice->clear();
        try
        {
            action();
        }
        catch (const std::exception &e)
        {
            notice->setText(QString::fromUtf8(e.what()));
        }
        notice->setVisible(!notice->text().isEmpty());
    }
    void showSelection()
    {
        selected = {};
        summary->clear();
        submit->setEnabled(false);
        if (pending->currentIndex() < 0)
        {
            summary->setText("截至本章没有可完成的任务。");
            return;
        }
        auto task = StoryTasks::get(book, pending->currentData().toInt());
        if (task.completed)
        {
            summary->setText("任务已完成。");
            return;
        }
        const auto name = book.value("SELECT name FROM people WHERE id=?", {task.person}).toString();
        summary->setText(StoryTasks::manuscriptText(task, name, false).section('\n', 1));
        selected = task;
        submit->setEnabled(true);
    }
    void refreshPending()
    {
        const auto previous = pending->currentData();
        QSignalBlocker block(pending);
        pending->clear();
        auto q = book.run("SELECT t.id,t.title,p.name,c.position FROM story_tasks t JOIN people p ON p.id=t.person "
                          "JOIN chapters c ON c.id=t.published WHERE t.completed IS NULL AND "
                          "c.position<=(SELECT position FROM chapters WHERE id=?) ORDER BY c.position DESC,t.id DESC",
                          {chapter});
        while (q.next())
            pending->addItem(
                QString("%1 · %2 · 第 %3 章").arg(q.value(1).toString(), q.value(2).toString()).arg(q.value(3).toInt()),
                q.value(0));
        if (pending->count())
            pending->setCurrentIndex(qMax(0, pending->findData(previous)));
        showSelection();
    }
    void refreshRewards()
    {
        rewards->clear();
        for (const auto &reward : draft.rewards)
            rewards->addItem(StoryTasks::rewardText(reward));
        if (rewards->count())
            rewards->setCurrentRow(rewards->count() - 1);
    }

  public:
    WriterTasks(Book &source, int chapterId, CommitWriterTask save)
        : book(source), chapter(chapterId), commit(std::move(save)), catalog(book.catalog())
    {
        auto *layout = new QVBoxLayout(this);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->addWidget(tabs);
        auto *publish = new QWidget;
        auto *form = new QVBoxLayout(publish);
        title->setObjectName("writerTaskTitle");
        title->setPlaceholderText("例如：初入昆仑");
        actor->setObjectName("writerTaskActor");
        auto people = book.run("SELECT id,name,count(*) OVER(PARTITION BY name) FROM people ORDER BY name,id");
        while (people.next())
        {
            auto name = people.value(1).toString();
            if (people.value(2).toInt() > 1)
                name += " · #" + people.value(0).toString();
            actor->addItem(name, people.value(0));
        }
        auto *fields = new QFormLayout;
        fields->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
        form->addLayout(fields);
        fields->addRow("任务名称", title);
        fields->addRow("承接人", actor);
        conditions->setObjectName("writerTaskConditions");
        conditions->setPlaceholderText("写下本次任务的完成条件");
        conditions->setFixedHeight(64);
        fields->addRow("完成条件", conditions);
        form->addWidget(new QLabel("任务奖励"));
        resource->setObjectName("writerTaskResource");
        amount->setObjectName("writerTaskQuantity");
        for (const auto &entry : catalog)
            resource->addItem(resourceKindLabel(entry.kind) + " · " + entry.name);
        resource->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
        form->addWidget(resource);
        amount->setRange(1, std::numeric_limits<int>::max());
        auto updateQuantity = [this] {
            auto kind = catalog.value(resource->currentIndex()).kind;
            const bool many = kind == "numeric" || kind == "supply";
            amount->setEnabled(many);
            if (!many)
                amount->setValue(1);
        };
        connect(resource, &QComboBox::currentIndexChanged, this, updateQuantity);
        updateQuantity();
        auto *bar = new QHBoxLayout;
        form->addLayout(bar);
        bar->addWidget(new QLabel("数量"));
        bar->addWidget(amount, 1);
        auto *add = new QPushButton("添加奖励"), *remove = new QPushButton("移除");
        bar->addWidget(add);
        bar->addWidget(remove);
        add->setEnabled(!catalog.isEmpty());
        remove->setEnabled(false);
        rewards->setFixedHeight(84);
        form->addWidget(rewards);
        connect(add, &QPushButton::clicked, this, [this] {
            report([&] {
                StoryTasks::addReward(draft.rewards, catalog.at(resource->currentIndex()), amount->value());
                refreshRewards();
            });
        });
        connect(rewards, &QListWidget::currentRowChanged, remove, [remove](int r) { remove->setEnabled(r >= 0); });
        connect(remove, &QPushButton::clicked, this, [this] {
            if (rewards->currentRow() >= 0)
                draft.rewards.removeAt(rewards->currentRow());
            refreshRewards();
        });
        form->addStretch();
        tabs->addTab(publish, "发布任务");
        auto *finish = new QWidget;
        auto *detail = new QVBoxLayout(finish);
        detail->addWidget(new QLabel("可在本章完成的任务"));
        pending->setObjectName("writerTaskPending");
        pending->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
        detail->addWidget(pending);
        summary->setTextFormat(Qt::PlainText);
        summary->setWordWrap(true);
        summary->setAlignment(Qt::AlignLeft | Qt::AlignTop);
        detail->addWidget(summary, 1);
        tabs->addTab(finish, "完成任务");
        connect(pending, &QComboBox::currentIndexChanged, this, [this] { report([&] { showSelection(); }); });
        insert->setObjectName("writerTaskInsert");
        layout->addWidget(insert);
        notice->setTextFormat(Qt::PlainText);
        notice->setWordWrap(true);
        notice->hide();
        layout->addWidget(notice);
        submit->setObjectName("primary");
        layout->addWidget(submit);
        auto changeMode = [this](int mode) {
            insert->setChecked(false);
            submit->setText(mode == 0 ? "在本章发布任务" : "完成任务并发奖");
            report([&] {
                if (mode == 1)
                    refreshPending();
                else
                {
                    submit->setEnabled(actor->count() > 0);
                    if (!actor->count())
                        notice->setText("本书尚无人物，请先建立承接人的档案。");
                }
            });
        };
        connect(tabs, &QTabWidget::currentChanged, this, changeMode);
        changeMode(0);
        connect(submit, &QPushButton::clicked, this, [this] {
            report([&] {
                if (tabs->currentIndex() == 0)
                {
                    draft.title = title->text();
                    draft.instructions = conditions->toPlainText();
                    draft.person = actor->currentData().toInt();
                    draft.published = chapter;
                    commit(draft, insert->isChecked());
                }
                else if (selected.id)
                    commit(selected, insert->isChecked());
            });
        });
    }
};
} // namespace
QWidget *writerTaskForm(Book &book, int chapter, CommitWriterTask commit)
{
    return new WriterTasks(book, chapter, std::move(commit));
}
