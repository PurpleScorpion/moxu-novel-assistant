#pragma once
#include "inline_files.h"
#include "template_presets.h"

class AttributeTemplates : public QWidget
{
    Book *book = nullptr;
    QListWidget *list = new QListWidget;
    QLineEdit *name = new QLineEdit;
    QPlainTextEdit *pattern = new QPlainTextEdit;
    QLabel *notice = new QLabel;
    QPushButton *remove = new QPushButton("删除模板");
    InlineActions *actions;
    int id = 0;
    QString savedName, savedText;

    int rowForId() const
    {
        for (int row = 0; row < list->count(); ++row)
            if (list->item(row)->data(Qt::UserRole).toInt() == id)
                return row;
        return -1;
    }
    void select(int row)
    {
        auto *item = list->item(row);
        id = item ? item->data(Qt::UserRole).toInt() : 0;
        savedName = item ? item->text() : QString();
        savedText = item ? item->data(Qt::UserRole + 1).toString() : TemplatePresets::attributes().first().second;
        name->setText(savedName);
        pattern->setPlainText(savedText);
        remove->setEnabled(item);
        notice->clear();
    }
    void save()
    {
        const QString title = name->text().trimmed(), text = pattern->toPlainText();
        if (title.isEmpty() || text.trimmed().isEmpty())
            throw std::runtime_error("请填写模板名称和内容，或点「放弃修改」。");
        if (id)
        {
            if (book->run("UPDATE templates SET title=?,pattern=? WHERE id=? AND title=? AND pattern=?",
                          {title, text, id, savedName, savedText}).numRowsAffected() != 1)
                throw std::runtime_error("这份模板已发生变化，请重新载入后编辑。");
        }
        else
            id = book->run("INSERT INTO templates(title,pattern) VALUES(?,?)", {title, text}).lastInsertId().toInt();
        savedName = title;
        savedText = text;
        name->setText(title);
        QSignalBlocker block(list);
        auto *item = list->item(rowForId());
        if (!item)
            item = new QListWidgetItem(list);
        item->setData(Qt::UserRole, id);
        item->setText(title);
        item->setData(Qt::UserRole + 1, text);
    }
    void report(const std::function<void()> &operation)
    {
        try
        {
            operation();
        }
        catch (const std::exception &e)
        {
            notice->setText(QString::fromUtf8(e.what()));
        }
    }
    void reload()
    {
        actions->finish();
        QSignalBlocker block(list);
        list->clear();
        auto q = book->run("SELECT id,title,pattern FROM templates ORDER BY id");
        int row = 0;
        while (q.next())
        {
            auto *item = new QListWidgetItem(q.value(1).toString(), list);
            item->setData(Qt::UserRole, q.value(0));
            item->setData(Qt::UserRole + 1, q.value(2));
            if (q.value(0).toInt() == id)
                row = list->count() - 1;
        }
        list->setCurrentRow(list->count() ? row : -1);
        select(list->currentRow());
    }

  public:
    AttributeTemplates()
    {
        auto *home = new QWidget;
        actions = new InlineActions(home);
        auto *layout = new QVBoxLayout(this);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->addWidget(actions);
        auto *columns = new QHBoxLayout(home);
        columns->setContentsMargins(0, 0, 0, 0);
        auto *left = new QVBoxLayout;
        columns->addLayout(left, 1);
        left->addWidget(new QLabel("本书已保存的模板"));
        list->setObjectName("attributeTemplateList");
        left->addWidget(list, 1);
        auto *create = new QPushButton("新建模板");
        left->addWidget(create);
        auto *right = new QVBoxLayout;
        columns->addLayout(right, 2);
        right->addWidget(new QLabel("模板名称"));
        name->setObjectName("attributeTemplateName");
        right->addWidget(name);
        auto *picker = new TemplatePresets::Picker(pattern);
        picker->setChoices(TemplatePresets::attributes());
        right->addWidget(picker);
        right->addWidget(new QLabel("模板内容"));
        pattern->setObjectName("attributeTemplatePattern");
        right->addWidget(pattern, 1);
        auto *bar = new QHBoxLayout;
        right->addLayout(bar);
        auto *discard = new QPushButton("放弃修改"), *apply = new QPushButton("保存模板");
        apply->setObjectName("primary");
        bar->addWidget(remove);
        bar->addStretch();
        bar->addWidget(discard);
        bar->addWidget(apply);
        notice->setTextFormat(Qt::PlainText);
        notice->setWordWrap(true);
        right->addWidget(notice);
        connect(list, &QListWidget::currentRowChanged, this, [this](int row) {
            if (row < 0)
                return;
            try
            {
                flush();
                select(row);
            }
            catch (const std::exception &e)
            {
                QSignalBlocker block(list);
                list->setCurrentRow(rowForId());
                notice->setText(QString::fromUtf8(e.what()));
            }
        });
        connect(create, &QPushButton::clicked, this, [this] {
            report([&] {
                flush();
                list->setCurrentRow(-1);
                select(-1);
                name->setFocus();
            });
        });
        connect(discard, &QPushButton::clicked, this, [this] { report([&] { reload(); }); });
        connect(apply, &QPushButton::clicked, this, [this] { saveNow(); });
        connect(remove, &QPushButton::clicked, this, [this] {
            auto *page = actions->begin("删除模板「" + savedName + "」？");
            page->addWidget(new QLabel("已插入章节的文字会保留。"));
            page->addStretch();
            actions->buttons("确认删除模板", [this] {
                if (book->run("DELETE FROM templates WHERE id=? AND title=? AND pattern=?", {id, savedName, savedText})
                        .numRowsAffected() != 1)
                    throw std::runtime_error("这份模板已发生变化，请取消后重新载入。");
                reload();
                notice->setText("模板已删除。");
            });
        });
    }
    void load(Book &source)
    {
        if (book != &source)
            id = 0;
        book = &source;
        reload();
    }
    void resetBook()
    {
        book = nullptr;
        id = 0;
        actions->finish();
    }
    void flush()
    {
        if (book && (name->text() != savedName || pattern->toPlainText() != savedText))
            save();
    }
    void saveNow()
    {
        if (actions->currentIndex() != 0)
            return;
        report([&] {
            save();
            reload();
            notice->setText("模板已保存。");
        });
    }
};
