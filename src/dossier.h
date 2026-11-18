#pragma once
#include "book.h"
#include "character_snapshot.h"
#include "resource_catalog.h"
#include "event_ledger.h"
#include "history_settings.h"
#include "interface.h"
#include "plot_page.h"
#include "note_editor.h"
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
    QString savedTitle, savedSynopsis, savedGenre, bookFile;
    SaveStatus *notice = new SaveStatus;
    QLabel *counts = new QLabel;
    HistorySettings *historySettings = new HistorySettings;
    std::function<void(QString)> renameBook;
    bool loading = false;
    EventLedger *ledger;
    PlotPage *plots = new PlotPage;
    ResourceCatalog *catalogPage = new ResourceCatalog;
    CharacterSnapshot *snapshot = new CharacterSnapshot;
    int preferredSnapshotChapter = 0;
    QComboBox *from = new QComboBox, *to = new QComboBox;
    QLineEdit *relation = new QLineEdit;
    QListWidget *edges = new QListWidget;
    QPushButton *saveRelation;
    int selected = 0;
    QList<NoteEditor *> notePages;
    QString oldName, oldAttrs;
    static QPushButton *btn(QString text, QBoxLayout *layout)
    {
        auto *b = new QPushButton(text);
        layout->addWidget(b);
        return b;
    }
    QVBoxLayout *section(QString text, bool framed = true)
    {
        auto *page = new QWidget;
        details->addWidget(page);
        auto *layout = new QVBoxLayout(page);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(12);
        auto *header = new QLabel(text);
        header->setObjectName("title");
        layout->addWidget(header);
        return framed ? panelColumn(layout, 1) : layout;
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
        QScopedValueRollback guard(loading, true);
        notice->pending.stop();
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
        snapshot->selectPerson(selected);
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
        QString n = name->text().trimmed();
        if (!book || (n == oldName && attrs->toPlainText() == oldAttrs))
            return;
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
        snapshot->selectPerson(selected);
        for (int i = 0; i < people->count(); ++i)
            if (people->item(i)->data(Qt::UserRole).toInt() == selected)
            {
                people->item(i)->setText(n);
                return;
            }
        QSignalBlocker block(people);
        auto *item = new QListWidgetItem(n, people);
        item->setData(Qt::UserRole, selected);
        if (people->currentRow() < 0)
            people->setCurrentItem(item);
    }
    void refreshPage(int page)
    {
        QScopedValueRollback guard(loading, true);
        if (page == 0)
        {
            historySettings->load(book);
            savedTitle = book->value("SELECT title FROM meta").toString();
            title->setText(savedTitle);
            auto info = book->run("SELECT genre,synopsis FROM book_profile WHERE id=1");
            info.next();
            savedGenre = info.value(0).toString();
            savedSynopsis = info.value(1).toString();
            genre->setText(savedGenre);
            synopsis->setPlainText(savedSynopsis);
            counts->setText(QString("%1 章     /     %2 位人物     /     %3 条事件")
                                .arg(book->value("SELECT count(*) FROM chapters").toInt())
                                .arg(book->value("SELECT count(*) FROM people").toInt())
                                .arg(book->value("SELECT count(*) FROM journal").toInt()));
        }
        else if (page == 1)
        {
            snapshot->load(*book, preferredSnapshotChapter);
            refreshList(selected);
        }
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
        else if (page == 3)
            ledger->load(*book);
        else if (page == plotIndex)
            plots->load(*book);
        else if (page == plotIndex + 1)
            catalogPage->load(*book);
        else
            notePages.at(page - 4)->enter(book);
    }

  public:
    static constexpr int plotIndex = 7;
    Dossier(std::function<void()> focus, std::function<void()> shelf, std::function<void()> graph,
            std::function<void(QString)> rename, OpenResourcePage open = {}, std::function<void()> record = {})
        : renameBook(std::move(rename)), ledger(new EventLedger(open, record))
    {
        auto *root = new QHBoxLayout(this);
        root->setContentsMargins(22, 16, 22, 16);
        root->setSpacing(14);
        auto *side = panelColumn(root, 0, true);
        side->parentWidget()->setFixedWidth(184);
        side->addWidget(new MatrixBanner("基础资料"));
        navigation->setObjectName("navigation");
        navigation->addItems({"书籍概览", "人物档案", "人物关系", "事件账本"});
        for (auto scope : {NoteScope::Global, NoteScope::Book, NoteScope::Outline})
            navigation->addItem(scopeName(scope));
        navigation->addItem("剧情大事");
        navigation->addItem("资源管理");
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
        auto *right = new QVBoxLayout;
        root->addLayout(right, 1);
        right->addWidget(details, 1);
        right->addWidget(notice);
        auto *overview = section("书籍概览");
        counts->setObjectName("sectionTitle");
        overview->addWidget(counts);
        auto *metadata = new QGridLayout;
        overview->addLayout(metadata);
        metadata->addWidget(new QLabel("书名"), 0, 0);
        metadata->addWidget(title, 1, 0);
        metadata->addWidget(new QLabel("题材"), 0, 1);
        metadata->addWidget(genre, 1, 1);
        metadata->setColumnStretch(0, 2);
        metadata->setColumnStretch(1, 1);
        overview->addWidget(new QLabel("作品简介"));
        overview->addWidget(synopsis, 1);
        auto *saveProfile = new QPushButton("保存书籍资料");
        saveProfile->setObjectName("primary");
        overview->addWidget(saveProfile, 0, Qt::AlignRight);
        connect(saveProfile, &QPushButton::clicked, this, &Dossier::saveNow);
        overview->addSpacing(4);
        overview->addWidget(historySettings);
        auto *personPage = section("人物档案", false);
        auto *columns = new QHBoxLayout;
        personPage->addLayout(columns, 1);
        columns->setSpacing(12);
        auto *list = panelColumn(columns, 1, true);
        list->addWidget(sectionLabel("人物列表"));
        list->addWidget(people);
        people->setObjectName("personList");
        auto *tabs = new QTabWidget;
        tabs->setObjectName("personTabs");
        columns->addWidget(tabs, 3);
        auto *profile = new QWidget;
        auto *form = new QVBoxLayout(profile);
        tabs->addTab(profile, "档案资料");
        tabs->addTab(snapshot, "章节快照");
        form->addWidget(new QLabel("姓名"));
        form->addWidget(name);
        name->setObjectName("personName");
        form->addWidget(new QLabel("自定义属性"));
        form->addWidget(attrs, 1);
        attrs->setPlaceholderText("境界：筑基\n所属势力：青云宗");
        attrs->setObjectName("personAttrs");
        connect(btn("新建人物", list), &QPushButton::clicked, this, [this, tabs] {
            safe([&] {
                flushPerson();
                QSignalBlocker b(people);
                people->setCurrentRow(-1);
                loadPerson(-1);
                tabs->setCurrentIndex(0);
                name->setFocus();
                notice->setText("填写姓名后，人物资料会自动保存");
            });
        });
        auto *savePerson = new QPushButton("保存人物");
        savePerson->setObjectName("primary");
        form->addWidget(savePerson, 0, Qt::AlignRight);
        connect(savePerson, &QPushButton::clicked, this, &Dossier::saveNow);
        auto edited = [this] {
            if (!loading)
                notice->edited();
        };
        for (auto *field : {title, genre, name})
            connect(field, &QLineEdit::textChanged, this, edited);
        for (auto *field : {attrs, synopsis})
            connect(field, &QPlainTextEdit::textChanged, this, edited);
        connect(&notice->pending, &QTimer::timeout, this, [this] {
            safe([this] {
                flush();
                notice->setText("已自动保存");
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
        auto *relationHeader = new QHBoxLayout;
        links->addLayout(relationHeader);
        relationHeader->addWidget(sectionLabel("关系列表"), 1);
        connect(btn("查看关系图", relationHeader), &QPushButton::clicked, this, [=, this] { safe(graph); });
        links->addWidget(edges, 1);
        auto *line = new QHBoxLayout;
        links->addLayout(line);
        line->addWidget(from);
        line->addWidget(relation);
        relation->setPlaceholderText("关系，例如 师父");
        line->addWidget(to);
        saveRelation = btn("保存关系", line);
        connect(saveRelation, &QPushButton::clicked, this, [this] {
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
                notice->confirm("人物关系");
            });
        });
        auto *log = section("事件账本");
        log->addWidget(ledger, 1);
        for (auto scope : {NoteScope::Global, NoteScope::Book, NoteScope::Outline})
        {
            auto *editor = new NoteEditor(scope, [this](NoteScope target, qint64 id) {
                int index = target == NoteScope::Global ? 0 : 1;
                navigation->setCurrentRow(4 + index);
                notePages.at(index)->showImported(id);
            });
            notePages.append(editor);
            section(scopeName(scope), false)->addWidget(editor, 1);
        }
        section("剧情大事")->addWidget(plots, 1);
        section("资源管理")->addWidget(catalogPage, 1);
        connect(navigation, &QListWidget::currentRowChanged, this, [this](int page) {
            if (!book || page < 0)
                return;
            safe([&] {
                int current = details->currentIndex();
                try
                {
                    flush();
                    refreshPage(page);
                }
                catch (...)
                {
                    QSignalBlocker b(navigation);
                    navigation->setCurrentRow(current);
                    throw;
                }
                details->setCurrentIndex(page);
                notice->clear();
            });
        });
    }
    void flush()
    {
        notice->pending.stop();
        if (!book)
            return;
        if (details->currentIndex() >= 4 && details->currentIndex() < plotIndex)
            notePages.at(details->currentIndex() - 4)->flush();
        if (details->currentIndex() == 1)
            flushPerson();
        if (details->currentIndex() == 0)
        {
            QString n = title->text().trimmed();
            if (n.isEmpty())
                throw std::runtime_error("请填写书名。");
            bool profileChanged = savedGenre != genre->text() || savedSynopsis != synopsis->toPlainText();
            if (profileChanged || savedTitle != n)
            {
                book->atomic([&] {
                    book->run("UPDATE meta SET title=?", {n});
                    book->run("UPDATE book_profile SET genre=?,synopsis=? WHERE id=1",
                              {genre->text(), synopsis->toPlainText()});
                });
                savedGenre = genre->text();
                savedSynopsis = synopsis->toPlainText();
                if (savedTitle != n)
                {
                    savedTitle = n;
                    renameBook(n);
                }
            }
        }
    }
    void savePending()
    {
        const int page = details->currentIndex();
        if (page >= 4 && page < plotIndex)
            notePages.at(page - 4)->savePending();
        else if (notice->pending.isActive())
            safe([this] {
                flush();
                notice->setText("已自动保存");
            });
    }
    void saveNow()
    {
        const int page = details->currentIndex();
        if (page >= 4 && page < plotIndex)
            notePages.at(page - 4)->saveNow();
        else if (page == plotIndex)
            plots->saveNow();
        else if (page == 2)
            saveRelation->click();
        else if (page == 0 || page == 1)
            safe([&] {
                if (page == 1 && name->text().trimmed().isEmpty())
                    throw std::runtime_error("请填写人物姓名。");
                flush();
                notice->confirm(page == 0 ? "书籍资料" : "人物资料");
            });
    }
    void enter(Book *source, int page = 0, int preferredChapter = 0)
    {
        book = source;
        preferredSnapshotChapter = preferredChapter;
        if (bookFile != source->filePath())
            selected = 0;
        bookFile = source->filePath();
        notice->clear();
        QSignalBlocker block(navigation);
        navigation->setCurrentRow(page);
        details->setCurrentIndex(page);
        if (page == plotIndex)
            plots->load(*book, preferredChapter);
        else
            refreshPage(page);
    }
};
