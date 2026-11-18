#pragma once
#include "downward_combo.h"
#include "book.h"
#include <QtWidgets>

inline QString resourceKindLabel(const QString &kind)
{
    if (kind == "item")
        return "道具";
    if (kind == "supply")
        return "消耗品";
    if (kind == "skill")
        return "技能／功法";
    return "数值资源";
}

class CatalogEditor : public QWidget
{
    QComboBox *kind = new DownwardCombo;
    QLineEdit *name = new QLineEdit, *level = new QLineEdit, *cooldown = new QLineEdit;
    QSpinBox *uses = new QSpinBox;
    QLabel *notice = new QLabel;
    QFormLayout *fields = new QFormLayout;

  public:
    CatalogEditor(Book &book, CatalogEntry previous, bool numericOnly, std::function<void(const CatalogEntry &)> saved,
                  std::function<void()> back)
    {
        auto *layout = new QVBoxLayout(this);
        auto *heading = new QLabel(previous.kind.isEmpty() ? "新建本书资源" : "编辑本书资源");
        heading->setObjectName("sectionTitle");
        layout->addWidget(heading);
        layout->addLayout(fields);
        kind->setObjectName("catalogKind");
        for (const QString &key : QStringList{"numeric", "item", "supply", "skill"})
            kind->addItem(resourceKindLabel(key), key);
        kind->setCurrentIndex(qMax(0, kind->findData(previous.kind)));
        kind->setEnabled(!numericOnly);
        fields->addRow("类型", kind);
        name->setObjectName("newResourceName");
        name->setText(previous.name);
        fields->addRow("名称", name);
        level->setText(previous.level);
        cooldown->setText(previous.cooldown);
        uses->setRange(0, 1000000000);
        uses->setSpecialValueText("不限");
        uses->setValue(qMax(0, previous.uses));
        fields->addRow("初始等级", level);
        fields->addRow("冷却时间", cooldown);
        fields->addRow("初始次数", uses);
        auto update = [=, this] {
            const bool skill = kind->currentData() == "skill";
            for (QWidget *w : QList<QWidget *>{level, cooldown, uses})
                fields->setRowVisible(w, skill);
            name->setPlaceholderText(kind->currentData() == "item"     ? "例如：盘古幡"
                                     : kind->currentData() == "supply" ? "例如：血瓶"
                                     : skill                           ? "例如：八九玄功"
                                                                       : "例如：灵石、声望");
        };
        connect(kind, &QComboBox::currentIndexChanged, this, update);
        update();
        notice->setTextFormat(Qt::PlainText);
        notice->setWordWrap(true);
        layout->addWidget(notice);
        auto *row = new QHBoxLayout;
        layout->addLayout(row);
        auto *cancel = new QPushButton("返回"),
             *commit = new QPushButton(previous.kind.isEmpty() ? "创建" : "保存修改");
        commit->setObjectName("primary");
        row->addStretch();
        row->addWidget(cancel);
        row->addWidget(commit);
        layout->addStretch();
        connect(cancel, &QPushButton::clicked, this, back);
        connect(name, &QLineEdit::returnPressed, commit, &QPushButton::click);
        connect(commit, &QPushButton::clicked, this, [=, &book, this] {
            const bool skill = kind->currentData() == "skill";
            CatalogEntry entry{
                kind->currentData().toString(), name->text().trimmed(), skill ? level->text().trimmed() : QString(),
                skill ? cooldown->text().trimmed() : QString(), skill && uses->value() > 0 ? uses->value() : -1};
            try
            {
                book.saveCatalogEntry(entry, previous);
                saved(entry);
            }
            catch (const std::exception &e)
            {
                notice->setText(QString::fromUtf8(e.what()));
            }
        });
        setFocusProxy(name);
    }
};

inline void openCatalogCreator(QStackedWidget *stack, Book &book, bool numericOnly,
                               std::function<void(const CatalogEntry &)> created)
{
    auto *origin = stack->currentWidget();
    auto back = [stack, origin] {
        auto *form = stack->currentWidget();
        stack->setCurrentWidget(origin);
        stack->removeWidget(form);
        form->deleteLater();
    };
    auto *form = new CatalogEditor(
        book, {}, numericOnly,
        [back, created](const CatalogEntry &entry) {
            back();
            created(entry);
        },
        back);
    stack->addWidget(form);
    stack->setCurrentWidget(form);
    form->setFocus();
}

class ResourceCatalog : public QStackedWidget
{
    Book *book = nullptr;
    QTableWidget *table = new QTableWidget;
    QLabel *notice = new QLabel;
    QList<CatalogEntry> entries;
    QPushButton *edit = new QPushButton("编辑"), *remove = new QPushButton("删除");
    void closeEditor()
    {
        if (count() == 1)
            return;
        auto *form = widget(1);
        setCurrentIndex(0);
        removeWidget(form);
        form->deleteLater();
    }
    void refresh(const CatalogEntry &selected = {})
    {
        entries = book->catalog();
        table->setRowCount(entries.size());
        table->clearSelection();
        table->setCurrentItem(nullptr);
        for (int i = 0; i < entries.size(); ++i)
        {
            const auto &e = entries[i];
            const bool skill = e.kind == "skill";
            QStringList cells{resourceKindLabel(e.kind), e.name, skill && !e.level.isEmpty() ? e.level : "—",
                              skill && !e.cooldown.isEmpty() ? e.cooldown : "—",
                              skill ? (e.uses < 0 ? QString("不限") : QString::number(e.uses)) : QString("—")};
            for (int c = 0; c < cells.size(); ++c)
                table->setItem(i, c, new QTableWidgetItem(cells[c]));
            if (e.kind == selected.kind && e.name == selected.name)
                table->setCurrentCell(i, 1);
        }
    }
    void openEditor(CatalogEntry previous)
    {
        auto *editor = new CatalogEditor(
            *book, previous, false,
            [this](const CatalogEntry &e) {
                closeEditor();
                refresh(e);
                notice->setText("资源目录已保存");
            },
            [this] { closeEditor(); });
        addWidget(editor);
        setCurrentWidget(editor);
        editor->setFocus();
    }

  public:
    ResourceCatalog()
    {
        auto *page = new QWidget;
        auto *layout = new QVBoxLayout(page);
        layout->setContentsMargins(0, 0, 0, 0);
        auto *bar = new QHBoxLayout;
        layout->addLayout(bar);
        auto *add = new QPushButton("新建资源");
        add->setObjectName("primary");
        bar->addWidget(add);
        bar->addStretch();
        bar->addWidget(edit);
        bar->addWidget(remove);
        table->setObjectName("catalogTable");
        table->setColumnCount(5);
        table->setHorizontalHeaderLabels({"类型", "名称", "初始等级", "冷却时间", "初始次数"});
        table->setSelectionBehavior(QAbstractItemView::SelectRows);
        table->setSelectionMode(QAbstractItemView::SingleSelection);
        table->setEditTriggers(QAbstractItemView::NoEditTriggers);
        table->setAlternatingRowColors(true);
        table->verticalHeader()->hide();
        table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
        layout->addWidget(table, 1);
        layout->addWidget(notice);
        addWidget(page);
        edit->setEnabled(false);
        remove->setEnabled(false);
        connect(table, &QTableWidget::currentCellChanged, this, [this](int row) {
            edit->setEnabled(row >= 0);
            remove->setEnabled(row >= 0);
        });
        connect(add, &QPushButton::clicked, this, [this] { openEditor({}); });
        connect(edit, &QPushButton::clicked, this, [this] { openEditor(entries.at(table->currentRow())); });
        connect(table, &QTableWidget::cellDoubleClicked, edit, [this] { edit->click(); });
        connect(remove, &QPushButton::clicked, this, [this] {
            try
            {
                book->removeCatalogEntry(entries.at(table->currentRow()));
                refresh();
                notice->setText("已从本书资源目录删除");
            }
            catch (const std::exception &e)
            {
                notice->setText(QString::fromUtf8(e.what()));
            }
        });
    }
    void resetBook()
    {
        closeEditor();
        book = nullptr;
    }
    void load(Book &source)
    {
        if (book == &source && currentIndex() != 0)
            return;
        CatalogEntry selected;
        if (book == &source && table->currentRow() >= 0)
            selected = entries.value(table->currentRow());
        closeEditor();
        book = &source;
        notice->clear();
        refresh(selected);
    }
};
