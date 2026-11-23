#pragma once
#include "downward_combo.h"
#include "story_tasks.h"

namespace TemplatePresets
{
using Choices = QList<QPair<QString, QString>>;

inline Choices attributes()
{
    return {{"人物名片", "【{{姓名}}】\n{{属性}}"},
            {"系统状态栏", "宿主：{{姓名}}\n当前积分：{{积分}}\n随身物品：{{持有道具}}"},
            {"修行手札", "{{姓名}}的修行记录\n灵石：{{灵石}}\n已习功法：{{已学技能}}"},
            {"冒险者背包", "【{{姓名}} · 补给清单】\n消耗品：{{消耗品}}\n血瓶余量：{{血瓶.数量}}"}};
}

inline Choices task(bool completed)
{
    return {{"系统面板", StoryTasks::defaultMessagePattern(completed)},
            {"门派传令", completed ? "【宗门复命】{{承接人}}已了结「{{任务名}}」。\n所成之事：{{完成条件}}\n论功行赏：{{奖励}}"
                                   : "【宗门传令】命{{承接人}}领「{{任务名}}」一令。\n所托之事：{{完成条件}}\n事成赐予：{{奖励}}"},
            {"游戏提示", completed ? "叮！{{承接人}}完成了「{{任务名}}」。\n达成目标：{{完成条件}}\n获得奖励：{{奖励}}"
                                   : "叮！{{承接人}}触发了「{{任务名}}」。\n通关目标：{{完成条件}}\n通关奖励：{{奖励}}"},
            {"委托公告", completed ? "委托结清｜{{任务名}}\n执行人：{{承接人}}\n验收记录：{{完成条件}}\n已付报酬：{{奖励}}"
                                   : "委托招领｜{{任务名}}\n受托人：{{承接人}}\n交付要求：{{完成条件}}\n约定报酬：{{奖励}}"}};
}

inline QString taskExample(const QString &pattern)
{
    StoryTask example;
    example.title = "寻回玉简";
    example.instructions = "将玉简交还师门";
    example.rewards.append({{"numeric", "积分"}, 100});
    return StoryTasks::manuscriptText(example, "主角", pattern);
}

class Picker : public DownwardCombo
{
  public:
    explicit Picker(QPlainTextEdit *target)
    {
        setObjectName("templatePreset");
        setPlaceholderText("选择预设，填入模板…");
        setAccessibleName("预设模板");
        connect(this, &QComboBox::currentIndexChanged, this, [this, target](int index) {
            if (index < 0)
                return;
            auto cursor = target->textCursor();
            cursor.select(QTextCursor::Document);
            cursor.insertText(itemData(index).toString());
            target->setTextCursor(cursor);
            setCurrentIndex(-1);
        });
    }
    void setChoices(const Choices &choices)
    {
        QSignalBlocker block(this);
        clear();
        for (const auto &[name, pattern] : choices)
            addItem(name, pattern);
        setCurrentIndex(-1);
    }
};

class TaskInput : public QWidget
{
  public:
    QPlainTextEdit *text = new QPlainTextEdit;
    Picker *presets = new Picker(text);
    TaskInput()
    {
        auto *layout = new QVBoxLayout(this);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->addWidget(presets);
        auto *help = new QLabel("可用字段：{{任务名}}、{{承接人}}、{{完成条件}}、{{奖励}}。"
                                "{{条件行}} 会在填写了条件时加上「完成条件：」并换行。");
        help->setWordWrap(true);
        help->setTextInteractionFlags(Qt::TextSelectableByMouse);
        layout->addWidget(help);
        text->setObjectName("taskMessagePattern");
        layout->addWidget(text, 1);
    }
};
} // namespace TemplatePresets
