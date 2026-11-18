#pragma once
#include "resource_picker.h"

class EventForm : public QWidget
{
    enum Type
    {
        Numeric,
        Note,
        Acquire,
        Transfer,
        Learn,
        Advance,
        Use,
        ReceiveSupply,
        ConsumeSupply,
        GiveSupply
    };
    Book &book;
    int chapter;
    QFormLayout *fields = new QFormLayout;
    QComboBox *type = new QComboBox, *actor = new QComboBox, *recipient = new QComboBox, *known = new QComboBox;
    ResourcePicker *resources;
    QComboBox *supply = new QComboBox;
    QSpinBox *quantity = new QSpinBox;
    QMap<QString, qint64> inventory, recipientInventory;
    QSpinBox *delta = new QSpinBox, *uses = new QSpinBox;
    QLineEdit *subject = new QLineEdit, *level = new QLineEdit, *cooldown = new QLineEdit, *description = new QLineEdit;
    QPlainTextEdit *preview = new QPlainTextEdit;
    QLabel *subjectLabel = new QLabel, *knownLabel = new QLabel;
    QList<CharacterSkill> skills;

    Type mode() const
    {
        return static_cast<Type>(type->currentIndex());
    }
    int person() const
    {
        return actor->currentData().toInt();
    }
    QString resource() const
    {
        return resources->choices->currentData().toString();
    }
    CharacterSkill selectedSkill() const
    {
        return skills.value(known->currentIndex());
    }
    CharacterSkill enteredSkill() const
    {
        return {mode() == Learn ? subject->text().trimmed() : known->currentText(), level->text().trimmed(),
                cooldown->text().trimmed(), uses->value() ? uses->value() : -1};
    }
    static QString details(const CharacterSkill &skill, bool includeUses = true)
    {
        QStringList parts;
        if (!skill.level.isEmpty())
            parts << "等级：" + skill.level;
        if (!skill.cooldown.isEmpty())
            parts << "冷却：" + skill.cooldown;
        if (includeUses)
            parts << (skill.remaining < 0 ? QString("次数不限") : QString("剩余 %1 次").arg(skill.remaining));
        return parts.join("，");
    }
    void loadSelection()
    {
        if (mode() == Advance && known->currentIndex() >= 0)
        {
            const auto skill = selectedSkill();
            level->setText(skill.level);
            cooldown->setText(skill.cooldown);
        }
        refreshPreview();
    }
    void loadChoices()
    {
        try
        {
            const QSignalBlocker block(known);
            known->clear();
            skills.clear();
            if (mode() == Transfer)
                known->addItems(book.ownedItems(person(), chapter));
            if (mode() == Transfer || mode() == GiveSupply)
            {
                const QSignalBlocker blockRecipient(recipient);
                recipient->clear();
                for (int i = 0; i < actor->count(); ++i)
                    if (actor->itemData(i).toInt() != person())
                        recipient->addItem(actor->itemText(i), actor->itemData(i));
            }
            if (mode() >= ReceiveSupply)
            {
                const QSignalBlocker blockSupply(supply);
                const auto old = supply->currentText();
                inventory = book.supplyCounts(person(), chapter);
                supply->clear();
                supply->setEditable(mode() == ReceiveSupply);
                for (auto it = inventory.cbegin(); it != inventory.cend(); ++it)
                    if (mode() == ReceiveSupply || it.value() > 0)
                        supply->addItem(it.key());
                if (mode() == ReceiveSupply)
                {
                    supply->setEditText(old);
                    supply->lineEdit()->setPlaceholderText("输入或选择，如血瓶、回城卷轴");
                }
                else if (supply->findText(old) >= 0)
                    supply->setCurrentText(old);
                loadRecipientInventory();
            }
            if (mode() == Advance || mode() == Use)
            {
                skills = book.characterSkills(person(), chapter);
                for (const auto &skill : skills)
                    known->addItem(skill.name);
            }
            loadSelection();
        }
        catch (const std::exception &e)
        {
            preview->setPlainText(QString::fromUtf8(e.what()));
        }
    }
    void loadRecipientInventory()
    {
        if (mode() == GiveSupply)
            recipientInventory = book.supplyCounts(recipient->currentData().toInt(), chapter);
    }
    void changeMode()
    {
        const auto m = mode();
        fields->setRowVisible(resources, m == Numeric);
        fields->setRowVisible(delta, m == Numeric);
        fields->setRowVisible(subject, m == Acquire || m == Learn);
        fields->setRowVisible(known, m == Transfer || m == Advance || m == Use);
        fields->setRowVisible(recipient, m == Transfer || m == GiveSupply);
        fields->setRowVisible(supply, m >= ReceiveSupply);
        fields->setRowVisible(quantity, m >= ReceiveSupply);
        fields->setRowVisible(level, m == Learn || m == Advance);
        fields->setRowVisible(cooldown, m == Learn || m == Advance);
        fields->setRowVisible(uses, m == Learn);
        subjectLabel->setText(m == Acquire ? "道具名称" : "技能／功法名称");
        knownLabel->setText(m == Transfer ? "持有道具" : "已学技能／功法");
        description->setPlaceholderText(m == Note || m == Numeric ? "填写发生的事情" : "可补充缘由或经过");
        loadChoices();
    }
    void refreshPreview()
    {
        QString text;
        try
        {
            if (!person())
                text = "请先在人物档案中建立人物。";
            else if (mode() == Numeric)
            {
                auto before = book.balance(person(), resource(), chapter);
                text = QString("%1：%2 → %3").arg(resource()).arg(before).arg(before + delta->value());
                if (insert->isChecked())
                    text += "\n\n" + snippet();
            }
            else if (mode() == Transfer && !known->count())
                text = "该人物截至本章没有可转赠的道具。";
            else if (mode() == Transfer && !recipient->count())
                text = "请先在人物档案中建立另一位人物。";
            else if ((mode() == Advance || mode() == Use) && !known->count())
                text = "该人物截至本章尚未习得技能或功法。";
            else if ((mode() == Acquire || mode() == Learn) && subject->text().trimmed().isEmpty())
                text = "请填写名称。";
            else if (mode() >= ReceiveSupply)
            {
                const auto item = supply->currentText().trimmed();
                if (item.isEmpty())
                    text = mode() == ReceiveSupply ? "请输入或选择消耗品名称。" : "该人物截至本章没有可用的消耗品。";
                else if (mode() == GiveSupply && !recipient->count())
                    text = "请先在人物档案中建立另一位人物。";
                else
                {
                    auto held = inventory.value(item);
                    auto after = held + (mode() == ReceiveSupply ? quantity->value() : -quantity->value());
                    text = QString("%1 · %2：%3 → %4").arg(actor->currentText(), item).arg(held).arg(after);
                    if (mode() == GiveSupply)
                    {
                        auto other = recipientInventory.value(item);
                        text += QString("\n%1 · %2：%3 → %4")
                                    .arg(recipient->currentText(), item)
                                    .arg(other)
                                    .arg(other + quantity->value());
                    }
                    if (after < 0)
                        text += "\n库存不足";
                    if (insert->isChecked())
                        text += "\n\n" + snippet();
                }
            }
            else
            {
                text = snippet();
                if (mode() == Use)
                    text += "\n当前状态：" + details(selectedSkill());
            }
        }
        catch (const std::exception &e)
        {
            text = QString::fromUtf8(e.what());
        }
        preview->setPlainText(text);
    }

  public:
    QCheckBox *insert = new QCheckBox("同时插入本章正文");
    EventForm(Book &book, int chapter, OpenResourcePage open)
        : book(book), chapter(chapter), resources(new ResourcePicker(false, open))
    {
        auto *layout = new QVBoxLayout(this);
        layout->setContentsMargins(0, 0, 0, 0);
        fields->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
        fields->setRowWrapPolicy(QFormLayout::WrapLongRows);
        layout->addLayout(fields);
        type->setObjectName("eventKind");
        type->addItems({"资源收支", "普通事件", "获得道具", "转赠道具", "习得技能／功法", "提升技能／功法",
                        "施展技能／功法", "获得消耗品", "使用消耗品", "转赠消耗品"});
        type->setMaxVisibleItems(12);
        fields->addRow("事件类型", type);
        actor->setObjectName("eventActor");
        auto people = book.run("SELECT id,name,count(*) OVER(PARTITION BY name) FROM people ORDER BY name,id");
        while (people.next())
        {
            auto label = people.value(1).toString();
            if (people.value(2).toInt() > 1)
                label += " · #" + people.value(0).toString();
            actor->addItem(label, people.value(0));
        }
        fields->addRow("人物", actor);
        resources->load(book);
        fields->addRow("资源", resources);
        delta->setRange(-1000000000, 1000000000);
        fields->addRow("变动（收＋支－）", delta);
        subject->setObjectName("eventSubject");
        fields->addRow(subjectLabel, subject);
        known->setObjectName("eventKnown");
        fields->addRow(knownLabel, known);
        supply->setObjectName("supplyName");
        supply->setInsertPolicy(QComboBox::NoInsert);
        quantity->setObjectName("supplyQuantity");
        quantity->setRange(1, 1000000000);
        fields->addRow("消耗品名称", supply);
        fields->addRow("数量", quantity);
        recipient->setObjectName("eventRecipient");
        fields->addRow("接收人", recipient);
        level->setPlaceholderText("例如：第三重");
        cooldown->setPlaceholderText("例如：三个时辰");
        fields->addRow("等级", level);
        fields->addRow("冷却时间", cooldown);
        uses->setRange(0, 1000000000);
        uses->setSpecialValueText("不限");
        fields->addRow("可用次数", uses);
        description->setObjectName("eventDescription");
        fields->addRow("事件描述", description);
        insert->setChecked(true);
        layout->addWidget(insert);
        layout->addWidget(new QLabel("事件预览"));
        preview->setReadOnly(true);
        preview->setMaximumHeight(110);
        layout->addWidget(preview);
        connect(type, &QComboBox::currentIndexChanged, this, &EventForm::changeMode);
        connect(actor, &QComboBox::currentIndexChanged, this, &EventForm::loadChoices);
        connect(known, &QComboBox::currentIndexChanged, this, &EventForm::loadSelection);
        connect(resources->choices, &QComboBox::currentIndexChanged, this, &EventForm::refreshPreview);
        connect(recipient, &QComboBox::currentIndexChanged, this, [this] {
            try
            {
                loadRecipientInventory();
                refreshPreview();
            }
            catch (const std::exception &e)
            {
                preview->setPlainText(QString::fromUtf8(e.what()));
            }
        });
        connect(supply, &QComboBox::currentTextChanged, this, &EventForm::refreshPreview);
        for (auto *field : {subject, level, cooldown, description})
            connect(field, &QLineEdit::textChanged, this, &EventForm::refreshPreview);
        for (auto *number : {uses, delta, quantity})
            connect(number, &QSpinBox::valueChanged, this, &EventForm::refreshPreview);
        connect(insert, &QCheckBox::toggled, this, &EventForm::refreshPreview);
        changeMode();
    }
    QString snippet() const
    {
        const auto note = description->text().trimmed(), name = actor->currentText();
        QString text;
        switch (mode())
        {
        case Numeric:
            return QString("%1（%2%3%4，剩余%5）")
                .arg(note, resource(), delta->value() >= 0 ? "+" : "", QString::number(delta->value()),
                     QString::number(book.balance(person(), resource(), chapter) + delta->value()));
        case Note:
            return note;
        case Acquire:
            text = name + "获得了" + subject->text().trimmed();
            break;
        case Transfer:
            text = name + "将" + known->currentText() + "赠给了" + recipient->currentText();
            break;
        case Learn:
            text = name + "习得" + subject->text().trimmed() + "（" + details(enteredSkill()) + "）";
            break;
        case Advance: {
            const auto skill = enteredSkill();
            text = name + "提升了" + skill.name;
            const auto properties = details(skill, false);
            if (!properties.isEmpty())
                text += "（" + properties + "）";
            break;
        }
        case Use:
            text = name + "施展了" + known->currentText();
            break;
        case ReceiveSupply:
        case ConsumeSupply:
        case GiveSupply: {
            const auto stack = supply->currentText().trimmed() + " × " + QString::number(quantity->value());
            if (mode() == GiveSupply)
                text = name + "将" + stack + "赠给了" + recipient->currentText();
            else
                text = name + (mode() == ReceiveSupply ? "获得了" : "使用了") + stack;
            break;
        }
        }
        return note.isEmpty() ? text : text + "，" + note;
    }
    void persist(const QString *body)
    {
        if (!person())
            throw std::runtime_error("请选择人物。");
        const auto note = description->text().trimmed();
        switch (mode())
        {
        case Numeric:
            if (note.isEmpty() || resource().isEmpty())
                throw std::runtime_error("请选择资源，并填写事件描述。");
            book.event(chapter, person(), resource(), delta->value(), note, body);
            break;
        case Note:
            book.storyEvent(chapter, person(), StoryKind::Note, {}, 0, note, body);
            break;
        case Acquire:
            book.storyEvent(chapter, person(), StoryKind::Acquire, subject->text(), 0, snippet(), body);
            break;
        case Transfer:
            book.storyEvent(chapter, person(), StoryKind::Transfer, known->currentText(),
                            recipient->currentData().toInt(), snippet(), body);
            break;
        case Learn:
        case Advance:
        case Use:
            book.skillEvent(chapter, person(),
                            mode() == Learn     ? SkillAction::Learn
                            : mode() == Advance ? SkillAction::Advance
                                                : SkillAction::Use,
                            enteredSkill(), snippet(), body);
            break;
        case ReceiveSupply:
        case ConsumeSupply:
        case GiveSupply:
            book.supplyEvent(chapter, person(),
                             mode() == ReceiveSupply   ? SupplyAction::Receive
                             : mode() == ConsumeSupply ? SupplyAction::Consume
                                                       : SupplyAction::Give,
                             supply->currentText(), quantity->value(), recipient->currentData().toInt(), snippet(),
                             body);
            break;
        }
    }
};
