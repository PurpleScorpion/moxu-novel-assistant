#pragma once
#include "book.h"
#include <QtWidgets>
#include <cmath>
#include <memory>

using Action = std::function<void()>;
class Window : public QMainWindow
{
    std::unique_ptr<Book> book;
    std::unique_ptr<QLockFile> lock;
    QStackedWidget *pages = new QStackedWidget;
    QWidget *shelf = new QWidget;
    QListWidget *recent = new QListWidget;
    QListWidget *chapters = new QListWidget;
    QPlainTextEdit *editor = new QPlainTextEdit;
    QLabel *heading = new QLabel;
    QLabel *status = new QLabel;
    QTimer timer;
    QToolBar *tools;
    int chapter = 0;
    bool dirty = false;

    bool guard(const Action &action)
    {
        try
        {
            action();
            return true;
        }
        catch (const std::exception &e)
        {
            QMessageBox::warning(this, "墨序", QString::fromUtf8(e.what()));
            return false;
        }
    }
    void button(QBoxLayout *layout, const QString &title, Action action)
    {
        auto *btn = new QPushButton(title);
        layout->addWidget(btn);
        connect(btn, &QPushButton::clicked, this, [this, action] { guard(action); });
    }
    void tool(const QString &title, Action action)
    {
        connect(tools->addAction(title), &QAction::triggered, this, [this, action] { guard(action); });
    }
    QString ask(const QString &label, const QString &initial = {})
    {
        return QInputDialog::getText(this, "墨序", label, QLineEdit::Normal, initial).trimmed();
    }
    int pick(const QString &title, const QString &sql)
    {
        auto rows = book->run(sql);
        QStringList choices;
        QVector<int> ids;
        while (rows.next())
        {
            ids << rows.value(0).toInt();
            choices << QString("%1  ·  #%2").arg(rows.value(1).toString()).arg(ids.last());
        }
        if (choices.isEmpty())
        {
            QMessageBox::information(this, title, "请先建立相关资料。");
            return 0;
        }
        bool ok = false;
        auto choice = QInputDialog::getItem(this, title, "选择条目", choices, 0, false, &ok);
        return ok ? ids.at(choices.indexOf(choice)) : 0;
    }
    int person()
    {
        return pick("选择人物", "SELECT id,name FROM people ORDER BY name,id");
    }
    void save()
    {
        if (!dirty || !book || !chapter)
            return;
        book->atomic([&] { book->save(chapter, editor->toPlainText()); });
        dirty = false;
        status->setText("已保存");
    }
    void loadChapter(int id)
    {
        auto q = book->run("SELECT title,body FROM chapters WHERE id=?", {id});
        if (!q.next())
            throw std::runtime_error("找不到章节");
        chapter = id;
        QSignalBlocker noEdit(editor);
        heading->setText(q.value(0).toString());
        editor->setPlainText(q.value(1).toString());
        dirty = false;
        status->setText("已保存");
    }
    void addChapterItem(int id, const QString &name)
    {
        auto *item = new QListWidgetItem(name, chapters);
        item->setData(Qt::UserRole, id);
    }
    void showShelf()
    {
        save();
        timer.stop();
        chapter = 0;
        book.reset();
        lock.reset();
        pages->setCurrentWidget(shelf);
        tools->hide();
        status->clear();
        setWindowTitle("墨序 · 小说辅助系统");
        recent->clear();
        for (const auto &path : QSettings().value("recent").toStringList())
            recent->addItem(path);
    }
    void openBook(QString path, const QString &title = {})
    {
        if (path.isEmpty())
            return;
        auto candidate = std::make_unique<QLockFile>(path + ".lock");
        candidate->setStaleLockTime(0);
        if (!candidate->tryLock())
            throw std::runtime_error("无法锁定书籍，请检查是否已打开以及目录写入权限。");
        auto next = std::make_unique<Book>(path, title);
        auto name = next->value("SELECT title FROM meta").toString();
        auto q = next->run("SELECT id,title FROM chapters ORDER BY id");
        QSignalBlocker block(chapters);
        chapters->clear();
        while (q.next())
            addChapterItem(q.value(0).toInt(), q.value(1).toString());
        if (!chapters->count())
            throw std::runtime_error("书籍没有章节");
        book = std::move(next);
        lock = std::move(candidate);
        chapters->setCurrentRow(0);
        loadChapter(chapters->item(0)->data(Qt::UserRole).toInt());
        setWindowTitle(name + " · 墨序");
        pages->setCurrentIndex(1);
        tools->show();
        QSettings settings;
        auto paths = settings.value("recent").toStringList();
        paths.removeAll(path);
        paths.prepend(path);
        settings.setValue("recent", paths.mid(0, 15));
    }
    void newBook()
    {
        auto name = ask("书名");
        if (name.isEmpty())
            return;
        auto path = QFileDialog::getSaveFileName(this, "保存新书",
                                                 QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation) +
                                                     "/" + name + ".moxu",
                                                 "墨序书籍 (*.moxu)");
        openBook(path, name);
    }
    void editPerson(bool existing)
    {
        int id = existing ? person() : 0;
        if (existing && !id)
            return;
        auto name = ask("人物名", book->value("SELECT name FROM people WHERE id=?", {id}).toString());
        if (name.isEmpty())
            return;
        bool ok = false;
        auto attrs = QInputDialog::getMultiLineText(
            this, "人物资料", "自定义属性，每行一项（例如 境界：筑基）",
            book->value("SELECT attributes FROM people WHERE id=?", {id}).toString(), &ok);
        if (!ok)
            return;
        if (existing)
            book->run("UPDATE people SET name=?,attributes=? WHERE id=?", {name, attrs, id});
        else
            book->run("INSERT INTO people(name,attributes) VALUES(?,?)", {name, attrs});
    }
    void record()
    {
        int id = person();
        if (!id)
            return;
        auto resource = ask("资源名", "积分");
        if (resource.isEmpty())
            return;
        bool ok = false;
        int delta =
            QInputDialog::getInt(this, "资源变化", "收入填正数，支出填负数", 0, -1000000000, 1000000000, 1, &ok);
        if (!ok)
            return;
        auto desc = ask("事件描述");
        if (desc.isEmpty())
            return;
        auto choice = QMessageBox::question(this, "确认事件",
                                            QString("%1：%2 → %3\n同时将事件插入正文？")
                                                .arg(resource)
                                                .arg(book->balance(id, resource, chapter))
                                                .arg(book->balance(id, resource, chapter) + delta),
                                            QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel);
        if (choice == QMessageBox::Cancel)
            return;
        save();
        if (choice == QMessageBox::No)
        {
            book->event(chapter, id, resource, delta, desc);
            return;
        }
        QString snippet = QString("%1（%2%3%4，剩余%5）")
                              .arg(desc, resource, delta >= 0 ? "+" : "")
                              .arg(delta)
                              .arg(book->balance(id, resource, chapter) + delta);
        auto cursor = editor->textCursor();
        QString body = editor->toPlainText();
        body.replace(cursor.selectionStart(), cursor.selectionEnd() - cursor.selectionStart(), snippet);
        book->event(chapter, id, resource, delta, desc, &body);
        cursor.insertText(snippet);
        editor->setTextCursor(cursor);
        dirty = false;
        status->setText("事件和正文已保存");
    }
    void panel()
    {
        int id = person();
        if (!id)
            return;
        int t = pick("选择输出模板", "SELECT id,title FROM templates");
        if (!t)
            return;
        auto pattern = book->value("SELECT pattern FROM templates WHERE id=?", {t}).toString();
        auto q = book->run("SELECT name,attributes FROM people WHERE id=?", {id});
        q.next();
        QMap<QString, QString> fields{{"姓名", q.value(0).toString()},
                                      {"属性", q.value(1).toString()},
                                      {"积分", QString::number(book->balance(id, "积分", chapter))}};
        for (auto line : q.value(1).toString().split('\n'))
        {
            int sep = line.indexOf(QRegularExpression("[:：]"));
            if (sep > 0 && !fields.contains(line.left(sep).trimmed()))
                fields[line.left(sep).trimmed()] = line.mid(sep + 1).trimmed();
        }
        QString result;
        qsizetype start = 0;
        auto matches = QRegularExpression("\\{\\{([^{}]+)\\}\\}").globalMatch(pattern);
        while (matches.hasNext())
        {
            auto m = matches.next();
            result += pattern.mid(start, m.capturedStart() - start);
            result += fields.value(m.captured(1), "未记录");
            start = m.capturedEnd();
        }
        result += pattern.mid(start);
        bool ok = false;
        result = QInputDialog::getMultiLineText(this, "插入面板", "预览并插入本章正文", result, &ok);
        if (ok)
            editor->insertPlainText(result);
    }
    void showText(const QString &title, const QString &text)
    {
        QDialog dlg(this);
        dlg.setWindowTitle(title);
        dlg.resize(620, 460);
        QVBoxLayout layout(&dlg);
        QPlainTextEdit view(text);
        view.setReadOnly(true);
        layout.addWidget(&view);
        dlg.exec();
    }
    void timeline()
    {
        auto q = book->run("SELECT c.title,p.name,e.description,e.resource,e.delta,SUM(delta) OVER(PARTITION BY "
                           "person,resource ORDER BY chapter,e.id) FROM events e JOIN chapters c ON c.id=e.chapter "
                           "JOIN people p ON p.id=e.person ORDER BY chapter,e.id");
        QStringList rows;
        while (q.next())
            rows << QString("%1  ·  %2\n%3   %4 %5，结余 %6\n")
                        .arg(q.value(0).toString(), q.value(1).toString(), q.value(2).toString(), q.value(3).toString(),
                             q.value(4).toString(), q.value(5).toString());
        showText("时间轴与收支明细", rows.isEmpty() ? "尚未记录事件" : rows.join("\n"));
    }
    void graph()
    {
        QDialog dlg(this);
        dlg.setWindowTitle("人物关系图");
        dlg.resize(760, 550);
        QVBoxLayout layout(&dlg);
        QGraphicsScene scene;
        QGraphicsView view(&scene);
        view.setRenderHint(QPainter::Antialiasing);
        layout.addWidget(&view);
        auto q = book->run("SELECT id,name FROM people ORDER BY id");
        QVector<QPair<int, QString>> people;
        while (q.next())
            people.append({q.value(0).toInt(), q.value(1).toString()});
        QMap<int, QPointF> points;
        for (int i = 0; i < people.size(); ++i)
        {
            double angle = i * 6.283185307 / people.size(), r = qMax(130.0, people.size() * 22.);
            points[people[i].first] = {r * std::cos(angle), r * std::sin(angle)};
        }
        auto edges = book->run("SELECT a,b,label FROM relations ORDER BY id");
        while (edges.next())
        {
            QLineF edge(points[edges.value(0).toInt()], points[edges.value(1).toInt()]);
            scene.addLine(edge, QPen(QColor("#66837a"), 2));
            auto label = scene.addText(edges.value(2).toString());
            label->setPos(edge.center());
        }
        for (auto &p : people)
        {
            auto loc = points[p.first];
            auto label = scene.addText(p.second);
            auto box = label->boundingRect();
            label->setPos(loc - QPointF(box.width() / 2, box.height() / 2));
            label->setZValue(2);
            auto bg =
                scene.addRect(label->sceneBoundingRect().adjusted(-6, -2, 6, 2), QPen(Qt::NoPen), QColor("#d7e9df"));
            bg->setZValue(1);
        }
        dlg.exec();
    }
    void history()
    {
        save();
        auto id = pick("恢复章节历史", QString("SELECT id,datetime(stamp,'localtime') || '  ' || substr(body,1,28) "
                                               "FROM revisions WHERE chapter=%1 ORDER BY id DESC")
                                           .arg(chapter));
        if (!id)
            return;
        auto text = book->value("SELECT body FROM revisions WHERE id=?", {id}).toString();
        showText("历史正文预览", text);
        if (QMessageBox::question(this, "恢复", "将此版本恢复为当前正文？") == QMessageBox::Yes)
        {
            editor->setPlainText(text);
            save();
        }
    }

  public:
    Window()
    {
        setStyleSheet(R"(
            QMainWindow, QStackedWidget { background: #f3f2eb; color: #1d342c; }
            QToolBar { background: #21493d; spacing: 3px; padding: 7px; border: none; }
            QToolBar QToolButton { color: white; background: transparent; padding: 6px 9px; border-radius: 4px; }
            QToolBar QToolButton:hover { background: #376354; }
            QListWidget, QPlainTextEdit { background: #fffef9; color: #263a32; border: 1px solid #d9ded6; border-radius: 6px; }
            QListWidget { padding: 5px; }
            QListWidget::item { padding: 6px; }
            QListWidget::item:selected { background: #d9e9df; color: #21493d; }
            QPushButton { background: #e0ebe4; color: #21493d; padding: 7px 13px; border: 1px solid #c7d6cb; border-radius: 4px; }
            QPushButton:hover { background: #cbded1; }
        )");
        resize(1000, 710);
        setCentralWidget(pages);
        pages->addWidget(shelf);
        auto *intro = new QVBoxLayout(shelf);
        intro->setContentsMargins(70, 45, 70, 45);
        auto *brand = new QLabel("墨序");
        brand->setStyleSheet("font: bold 36pt 'Microsoft YaHei';color:#244b3e");
        intro->addWidget(brand);
        intro->addSpacing(18);
        intro->addWidget(new QLabel("最近书籍"));
        intro->addWidget(recent);
        auto *actions = new QHBoxLayout;
        intro->addLayout(actions);
        button(actions, "新建书籍", [this] { newBook(); });
        button(actions, "打开书籍…",
               [this] { openBook(QFileDialog::getOpenFileName(this, "打开书籍", {}, "墨序书籍 (*.moxu)")); });
        connect(recent, &QListWidget::itemActivated, this, [this](auto *i) { guard([&] { openBook(i->text()); }); });
        auto *work = new QWidget;
        pages->addWidget(work);
        auto *cols = new QHBoxLayout(work);
        auto *left = new QVBoxLayout;
        cols->addLayout(left, 1);
        left->addWidget(new QLabel("章节目录"));
        left->addWidget(chapters);
        button(left, "＋ 新章节", [this] {
            save();
            auto name = ask("章节名");
            if (name.isEmpty())
                return;
            auto id = book->run("INSERT INTO chapters(title) VALUES(?)", {name}).lastInsertId().toInt();
            addChapterItem(id, name);
            chapters->setCurrentRow(chapters->count() - 1);
        });
        auto *right = new QVBoxLayout;
        cols->addLayout(right, 4);
        right->addWidget(heading);
        right->addWidget(editor);
        heading->setStyleSheet("font-size:20px;font-weight:bold;color:#244b3e");
        editor->setFont(QFont("Microsoft YaHei", 13));
        editor->setPlaceholderText("从这里开始写作…");
        editor->document()->setDocumentMargin(26);
        tools = addToolBar("写作");
        tools->setMovable(false);
        tool("书架", [this] { showShelf(); });
        tool("保存", [this] { save(); });
        tool("人物名字", [this] {
            auto id = person();
            if (id)
                editor->insertPlainText(book->value("SELECT name FROM people WHERE id=?", {id}).toString());
        });
        tool("属性面板", [this] { panel(); });
        tool("记录事件", [this] { record(); });
        tool("时间轴", [this] { timeline(); });
        tool("关系图", [this] { graph(); });
        tool("历史版本", [this] { history(); });
        auto *menu = new QMenu("资料与导出", this);
        auto action = [&](QString name, Action fn) { menu->addAction(name, this, [this, fn] { guard(fn); }); };
        action("创建人物", [this] { editPerson(false); });
        action("编辑人物资料", [this] { editPerson(true); });
        action("新增关系", [this] {
            auto a = person();
            if (!a)
                return;
            auto b = person();
            if (!b)
                return;
            auto label = ask("两人关系");
            if (!label.isEmpty())
                book->run("INSERT INTO relations(a,b,label) VALUES(?,?,?)", {a, b, label});
        });
        action("新建输出模板", [this] {
            auto name = ask("模板名称");
            if (name.isEmpty())
                return;
            bool ok = false;
            auto pattern = QInputDialog::getMultiLineText(
                this, "输出模板", "可使用 {{姓名}}、{{积分}} 或 {{自定义属性名}}", "【{{姓名}}】\n{{属性}}", &ok);
            if (ok && !pattern.isEmpty())
                book->run("INSERT INTO templates(title,pattern) VALUES(?,?)", {name, pattern});
        });
        action("整书基础校对", [this] {
            save();
            QStringList issues;
            auto query = book->run("SELECT title,body FROM chapters ORDER BY id");
            QRegularExpression repeated("[，。；、]{2,}");
            while (query.next())
            {
                auto lines = query.value(1).toString().split('\n');
                for (qsizetype i = 0; i < lines.size(); ++i)
                {
                    auto hits = repeated.globalMatch(lines.at(i));
                    while (hits.hasNext())
                        issues << QString("%1，第 %2 行：请核对标点 %3")
                                      .arg(query.value(0).toString())
                                      .arg(i + 1)
                                      .arg(hits.next().captured());
                }
            }
            showText("整书基础校对 · 重复标点",
                     issues.isEmpty() ? "未发现连续的逗号、句号、分号或顿号。" : issues.join("\n"));
        });
        action("备份整本书…", [this] {
            save();
            auto path = QFileDialog::getSaveFileName(this, "保存独立备份", {}, "墨序书籍 (*.moxu)");
            if (path.isEmpty())
                return;
            if (QFileInfo::exists(path))
                throw std::runtime_error("请选择尚不存在的备份文件名。");
            book->run("VACUUM INTO ?", {path});
            status->setText("已保存书籍备份");
        });
        action("导出全书 TXT", [this] {
            save();
            auto path = QFileDialog::getSaveFileName(this, "导出全书", {}, "文本文件 (*.txt)");
            if (path.isEmpty())
                return;
            if (!path.endsWith(".txt", Qt::CaseInsensitive))
                throw std::runtime_error("导出文件请使用 .txt 后缀。");
            QSaveFile file(path);
            if (!file.open(QIODevice::WriteOnly))
                throw std::runtime_error("无法打开导出文件");
            auto q = book->run("SELECT title,body FROM chapters ORDER BY id");
            while (q.next())
            {
                auto bytes = (q.value(0).toString() + "\n\n" + q.value(1).toString() + "\n\n").toUtf8();
                if (file.write(bytes) != bytes.size())
                    throw std::runtime_error("写入失败");
            }
            if (!file.commit())
                throw std::runtime_error("导出失败");
        });
        auto *more = new QToolButton;
        more->setText("资料与导出");
        more->setMenu(menu);
        more->setPopupMode(QToolButton::InstantPopup);
        tools->addWidget(more);
        statusBar()->addPermanentWidget(status);
        connect(chapters, &QListWidget::currentItemChanged, this, [this](auto *item, auto *prev) {
            if (item && !guard([&] {
                    save();
                    loadChapter(item->data(Qt::UserRole).toInt());
                }))
            {
                QSignalBlocker b(chapters);
                chapters->setCurrentItem(prev);
            }
        });
        connect(editor, &QPlainTextEdit::textChanged, this, [this] {
            dirty = true;
            status->setText("待保存");
            if (!timer.isActive())
                timer.start(1800);
        });
        timer.setSingleShot(true);
        connect(&timer, &QTimer::timeout, this, [this] { guard([this] { save(); }); });
        connect(new QShortcut(QKeySequence::Save, this), &QShortcut::activated, this,
                [this] { guard([this] { save(); }); });
        showShelf();
    }
    void closeEvent(QCloseEvent *e) override
    {
        if (!guard([this] { save(); }))
            e->ignore();
        else
            e->accept();
    }
};
