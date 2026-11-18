#pragma once
#include "book.h"
#include "interface.h"
#include <functional>

class Dossier : public QWidget
{
    Book *book = nullptr;
    QListWidget *navigation = new QListWidget, *people = new QListWidget;
    QStackedWidget *details = new QStackedWidget;
    QLineEdit *title = new QLineEdit, *name = new QLineEdit;
    QPlainTextEdit *attrs = new QPlainTextEdit;
    QPlainTextEdit *synopsis = new QPlainTextEdit;
    QLineEdit *genre = new QLineEdit;
    QString savedSynopsis, savedGenre;
    QLabel *notice = new QLabel, *counts = new QLabel;
    QTableWidget *ledger = new QTableWidget;
    QComboBox *from = new QComboBox, *to = new QComboBox;
    QLineEdit *relation = new QLineEdit;
    QListWidget *edges = new QListWidget;
    int selected = 0;
    QString oldName, oldAttrs;
    static QPushButton *btn(QString text, QBoxLayout *layout)
    {
        auto *b = new QPushButton(text);
        layout->addWidget(b);
        return b;
    }
    QVBoxLayout *section(QString text)
    {
        auto *page = new QWidget;
        details->addWidget(page);
        auto *layout = new QVBoxLayout(page);
        layout->setContentsMargins(20, 12, 20, 12);
        layout->setSpacing(12);
        auto *header = new QLabel(text);
        header->setObjectName("title");
        layout->addWidget(header);
        return layout;
    }
    void safe(std::function<void()> action)
    {
        try
        {
            action();
        }
        catch (const std::exception &e)
        {
            notice->setText(QString("保存失败 · %1").arg(QString::fromUtf8(e.what())));
        }
    }
    void loadPerson(int row)
    {
        selected = row < 0 ? 0 : people->item(row)->data(Qt::UserRole).toInt();
        auto q = book->run("SELECT name,attributes FROM people WHERE id=?", {selected});
        oldName.clear();
        oldAttrs.clear();
        if (q.next())
        {
            oldName = q.value(0).toString();
            oldAttrs = q.value(1).toString();
        }
        name->setText(oldName);
        attrs->setPlainText(oldAttrs);
    }
    void refreshList(int id = 0)
    {
        QSignalBlocker block(people);
        people->clear();
        int row = -1;
        auto q = book->run("SELECT id,name FROM people ORDER BY id");
        while (q.next())
        {
            auto *item = new QListWidgetItem(q.value(1).toString(), people);
            item->setData(Qt::UserRole, q.value(0));
            if (q.value(0).toInt() == id)
                row = people->count() - 1;
        }
        if (!id && people->count())
            row = 0;
        people->setCurrentRow(row);
        loadPerson(row);
    }
    void flushPerson()
    {
        if (!book || (name->text() == oldName && attrs->toPlainText() == oldAttrs))
            return;
        QString n = name->text().trimmed();
        if (n.isEmpty())
            throw std::runtime_error("请填写人物姓名。");
        if (selected)
            book->run("UPDATE people SET name=?,attributes=? WHERE id=?", {n, attrs->toPlainText(), selected});
        else
            selected = book->run("INSERT INTO people(name,attributes) VALUES(?,?)", {n, attrs->toPlainText()})
                           .lastInsertId()
                           .toInt();
        oldName = n;
        oldAttrs = attrs->toPlainText();
        name->setText(n);
        for (int i = 0; i < people->count(); ++i)
            if (people->item(i)->data(Qt::UserRole).toInt() == selected)
            {
                people->item(i)->setText(n);
                return;
            }
        auto *item = new QListWidgetItem(n, people);
        item->setData(Qt::UserRole, selected);
    }
    void refreshPage(int page)
    {
        if (page == 0)
        {
            title->setText(book->value("SELECT title FROM meta").toString());
            auto info = book->run("SELECT genre,synopsis FROM book_profile WHERE id=1");
            info.next();
            savedGenre = info.value(0).toString();
            savedSynopsis = info.value(1).toString();
            genre->setText(savedGenre);
            synopsis->setPlainText(savedSynopsis);
            counts->setText(QString("%1 章     /     %2 位人物     /     %3 条事件")
                                .arg(book->value("SELECT count(*) FROM chapters").toInt())
                                .arg(book->value("SELECT count(*) FROM people").toInt())
                                .arg(book->value("SELECT count(*) FROM events").toInt()));
        }
        else if (page == 1)
            refreshList(selected);
        else if (page == 2)
        {
            edges->clear();
            from->clear();
            to->clear();
            relation->clear();
            auto p = book->run("SELECT id,name FROM people ORDER BY id");
            while (p.next())
            {
                from->addItem(p.value(1).toString(), p.value(0));
                to->addItem(p.value(1).toString(), p.value(0));
            }
            if (to->count() > 1)
                to->setCurrentIndex(1);
            auto q = book->run("SELECT p.name,r.label,t.name FROM relations r JOIN people p ON p.id=r.a JOIN people t "
                               "ON t.id=r.b ORDER BY r.id");
            while (q.next())
                edges->addItem(q.value(0).toString() + "   ── " + q.value(1).toString() + " ─→   " +
                               q.value(2).toString());
        }
        else
        {
            ledger->setRowCount(0);
            auto q = book->run("SELECT c.title,p.name,e.description,resource,delta,SUM(delta) OVER(PARTITION BY "
                               "person,resource ORDER BY chapter,e.id) FROM events e JOIN chapters c ON e.chapter=c.id "
                               "JOIN people p ON p.id=e.person ORDER BY chapter,e.id");
            while (q.next())
            {
                int row = ledger->rowCount();
                ledger->insertRow(row);
                for (int i = 0; i < 6; i++)
                    ledger->setItem(row, i, new QTableWidgetItem(q.value(i).toString()));
            }
        }
    }

  public:
    Dossier(std::function<void()> focus, std::function<void()> shelf, std::function<void()> graph,
            std::function<void(QString)> rename)
    {
        auto *root = new QHBoxLayout(this);
        root->setContentsMargins(0, 0, 0, 0);
        root->setSpacing(0);
        auto *rail = new QFrame;
        rail->setObjectName("rail");
        rail->setFixedWidth(192);
        auto *side = new QVBoxLayout(rail);
        side->setContentsMargins(14, 20, 14, 18);
        side->setSpacing(18);
        side->addWidget(new MatrixBanner("墨序"));
        navigation->setObjectName("navigation");
        navigation->addItems({"书籍概览", "人物档案", "人物关系", "事件账本"});
        side->addWidget(navigation);
        auto *write = btn("进入专注写作", side);
        write->setObjectName("primary");
        connect(write, &QPushButton::clicked, this, [=, this] {
            safe([&] {
                flush();
                focus();
            });
        });
        connect(btn("返回书架", side), &QPushButton::clicked, this, [=, this] {
            safe([&] {
                flush();
                shelf();
            });
        });
        root->addWidget(rail);
        auto *right = new QVBoxLayout;
        root->addLayout(right, 1);
        right->addWidget(details, 1);
        right->addWidget(notice);
        auto *overview = section("书籍概览");
        overview->addWidget(new MatrixBanner("基础资料"));
        overview->addWidget(counts);
        overview->addWidget(new QLabel("书名"));
        overview->addWidget(title);
        overview->addWidget(new QLabel("题材"));
        overview->addWidget(genre);
        overview->addWidget(new QLabel("作品简介"));
        overview->addWidget(synopsis, 1);
        connect(btn("保存书籍资料", overview), &QPushButton::clicked, this, [=, this] {
            safe([&] {
                flush();
                rename(title->text().trimmed());
                notice->setText("书籍资料已保存");
            });
        });
        auto *personPage = section("人物档案");
        auto *columns = new QHBoxLayout;
        personPage->addLayout(columns);
        auto *list = new QVBoxLayout;
        columns->addLayout(list, 1);
        list->addWidget(people);
        people->setObjectName("personList");
        auto *form = new QVBoxLayout;
        columns->addLayout(form, 2);
        form->addWidget(new QLabel("姓名"));
        form->addWidget(name);
        name->setObjectName("personName");
        form->addWidget(new QLabel("自定义属性"));
        form->addWidget(attrs);
        attrs->setPlaceholderText("境界：筑基\n所属势力：青云宗");
        attrs->setObjectName("personAttrs");
        connect(btn("新建人物", list), &QPushButton::clicked, this, [this] {
            safe([&] {
                flushPerson();
                QSignalBlocker b(people);
                people->setCurrentRow(-1);
                loadPerson(-1);
                name->setFocus();
                notice->setText("填写姓名和属性后保存");
            });
        });
        connect(btn("保存人物", form), &QPushButton::clicked, this, [this] {
            safe([&] {
                if (name->text().trimmed().isEmpty())
                {
                    notice->setText("请填写人物姓名");
                    return;
                }
                flushPerson();
                refreshList(selected);
                notice->setText("人物资料已保存");
            });
        });
        connect(people, &QListWidget::currentRowChanged, this, [this](int row) {
            safe([&] {
                try
                {
                    flushPerson();
                }
                catch (...)
                {
                    QSignalBlocker b(people);
                    int old = -1;
                    for (int i = 0; i < people->count(); ++i)
                        if (people->item(i)->data(Qt::UserRole).toInt() == selected)
                            old = i;
                    people->setCurrentRow(old);
                    throw;
                }
                loadPerson(row);
                notice->clear();
            });
        });
        auto *links = section("人物关系");
        links->addWidget(edges, 1);
        auto *line = new QHBoxLayout;
        links->addLayout(line);
        line->addWidget(from);
        line->addWidget(relation);
        relation->setPlaceholderText("关系，例如 师父");
        line->addWidget(to);
        connect(btn("保存关系", line), &QPushButton::clicked, this, [this] {
            safe([&] {
                if (!from->currentData().isValid() || from->currentData() == to->currentData() ||
                    relation->text().trimmed().isEmpty())
                {
                    notice->setText("请选择两位不同的人物并填写关系");
                    return;
                }
                book->run("INSERT INTO relations(a,b,label) VALUES(?,?,?)",
                          {from->currentData(), to->currentData(), relation->text().trimmed()});
                refreshPage(2);
                notice->setText("关系已保存");
            });
        });
        connect(btn("查看关系图", links), &QPushButton::clicked, this, [=, this] { safe(graph); });
        auto *log = section("事件账本");
        log->addWidget(ledger);
        ledger->setColumnCount(6);
        ledger->setHorizontalHeaderLabels({"章节", "人物", "事件", "资源", "变动", "结余"});
        ledger->setEditTriggers(QAbstractItemView::NoEditTriggers);
        ledger->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
        ledger->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
        connect(navigation, &QListWidget::currentRowChanged, this, [this](int page) {
            if (!book || page < 0)
                return;
            safe([&] {
                int current = details->currentIndex();
                try
                {
                    flush();
                }
                catch (...)
                {
                    QSignalBlocker b(navigation);
                    navigation->setCurrentRow(current);
                    throw;
                }
                details->setCurrentIndex(page);
                refreshPage(page);
                notice->clear();
            });
        });
    }
    void flush()
    {
        if (!book)
            return;
        if (details->currentIndex() == 1)
            flushPerson();
        if (details->currentIndex() == 0)
        {
            QString n = title->text().trimmed();
            if (n.isEmpty())
                throw std::runtime_error("请填写书名。");
            bool profileChanged = savedGenre != genre->text() || savedSynopsis != synopsis->toPlainText();
            if (profileChanged || book->value("SELECT title FROM meta").toString() != n)
            {
                book->atomic([&] {
                    book->run("UPDATE meta SET title=?", {n});
                    book->run("UPDATE book_profile SET genre=?,synopsis=? WHERE id=1",
                              {genre->text(), synopsis->toPlainText()});
                });
                savedGenre = genre->text();
                savedSynopsis = synopsis->toPlainText();
            }
        }
    }
    void enter(Book *source, int page = 0)
    {
        book = source;
        selected = 0;
        notice->clear();
        QSignalBlocker block(navigation);
        navigation->setCurrentRow(page);
        details->setCurrentIndex(page);
        refreshPage(page);
    }
};
