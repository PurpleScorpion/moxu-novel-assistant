#pragma once
#include "dossier.h"
#include "writer_sheet.h"
#include "writing_assistant.h"
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
    Dossier *dossier;
    QWidget *writer = new QWidget, *storage = new QWidget;
    QLabel *location = new QLabel, *directoryMessage = new QLabel;
    QListWidget *recent = new QListWidget;
    QListWidget *chapters = new QListWidget;
    QPlainTextEdit *editor = new QPlainTextEdit;
    QLabel *heading = new QLabel;
    QLabel *status = new QLabel;
    QHBoxLayout *writerColumns;
    WriterSheet *sheet = nullptr;
    QTextCursor insertion;
    QString bookPath;
    QTimer timer;
    QToolBar *tools;
    int chapter = 0;
    bool dirty = false;
    QHash<QString, QString> sessionKeys;

    void closeSheet()
    {
        if (!sheet)
            return;
        sheet->hide();
        writerColumns->removeWidget(sheet);
        sheet->deleteLater();
        sheet = nullptr;
        editor->setReadOnly(false);
        editor->setTextCursor(insertion);
        insertion = QTextCursor();
        editor->setFocus();
    }
    void beginSheet(const QString &title)
    {
        closeSheet();
        insertion = editor->textCursor();
        editor->setReadOnly(true);
        sheet = new WriterSheet(title, [this] { closeSheet(); });
        writerColumns->addWidget(sheet, 3);
    }
    void aiWriting()
    {
        closeSheet();
        save();
        auto target = editor->textCursor();
        QString excerpt =
            target.hasSelection() ? target.selectedText() : editor->toPlainText().left(target.position()).right(3000);
        excerpt.replace(QChar::ParagraphSeparator, '\n');
        auto back = [this] {
            auto *panel = pages->currentWidget();
            pages->setCurrentWidget(writer);
            pages->removeWidget(panel);
            panel->deleteLater();
            tools->show();
            statusBar()->show();
            editor->setFocus();
        };
        auto accept = [this, target, back](const QString &text) mutable {
            target.insertText(text);
            editor->setTextCursor(target);
            back();
            guard([this] { save(); });
        };
        auto *panel = new WritingAssistant(*book, chapter, excerpt, target.hasSelection(), sessionKeys, back, accept);
        pages->addWidget(panel);
        pages->setCurrentWidget(panel);
        tools->hide();
        statusBar()->hide();
    }

    bool guard(const Action &action)
    {
        try
        {
            action();
            return true;
        }
        catch (const std::exception &e)
        {
            const QString error = QString::fromUtf8(e.what());
            if (sheet)
                sheet->report(error);
            else
            {
                statusBar()->show();
                statusBar()->showMessage(error, 12000);
            }
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
    QComboBox *personChoice()
    {
        auto *people = sheet->choice("人物", book->run("SELECT id,name FROM people ORDER BY name,id"));
        if (!people->count())
            sheet->report("请先在资料工作区建立人物档案。");
        return people;
    }
    void insertPerson()
    {
        beginSheet("插入人物名字");
        auto *people = personChoice();
        button(sheet->form, "插入姓名", [this, people] {
            if (people->currentIndex() < 0)
                return;
            insertion.insertText(book->value("SELECT name FROM people WHERE id=?", {people->currentData()}).toString());
            closeSheet();
        });
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
        closeSheet();
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
        statusBar()->hide();
        status->clear();
        setWindowTitle("墨序 · 小说辅助系统");
        location->setText(QDir::toNativeSeparators(QSettings().value("booksDir", defaultBooksPath()).toString()));
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
        bookPath = QFileInfo(path).absoluteFilePath();
        book = std::move(next);
        lock = std::move(candidate);
        chapters->setCurrentRow(0);
        loadChapter(chapters->item(0)->data(Qt::UserRole).toInt());
        setWindowTitle(name + " · 墨序");
        enterDossier();
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
        QString filename = name;
        filename.replace(QRegularExpression(R"rx([<>:"/\\|?*\x00-\x1f])rx"), "_");
        auto dir = QDir(QSettings().value("booksDir", defaultBooksPath()).toString());
        auto path = dir.filePath(filename + ".moxu");
        if (QFileInfo::exists(path))
            path = QFileDialog::getSaveFileName(this, "书籍已存在，请另选文件名", path, "墨序书籍 (*.moxu)");
        openBook(path, name);
    }
    void enterDossier(int page = 0)
    {
        save();
        closeSheet();
        dossier->enter(book.get(), page);
        setWindowTitle(book->value("SELECT title FROM meta").toString() + " · 墨序");
        pages->setCurrentWidget(dossier);
        tools->hide();
        statusBar()->hide();
    }
    void setDirectory(QString path)
    {
        if (path.isEmpty())
            return;
        path = QDir::cleanPath(QFileInfo(path).absoluteFilePath());
        if (!QDir().mkpath(path))
        {
            directoryMessage->setText("无法创建书籍目录，请选择有写入权限的位置。");
            return;
        }
        QTemporaryFile probe(QDir(path).filePath(".moxu-write-XXXXXX"));
        if (!probe.open())
        {
            directoryMessage->setText("该目录无法写入，请选择其他目录。");
            return;
        }
        QSettings settings;
        settings.setValue("booksDir", path);
        settings.sync();
        if (settings.status() != QSettings::NoError)
        {
            directoryMessage->setText("保存目录设置失败");
            return;
        }
        showShelf();
    }
    void chooseStorage()
    {
        pages->setCurrentWidget(storage);
        tools->hide();
        directoryMessage->setText("默认目录  " + QDir::toNativeSeparators(defaultBooksPath()));
    }
    void record()
    {
        beginSheet("记录事件");
        auto *people = personChoice();
        auto *resource = sheet->field("资源名", "积分");
        sheet->form->addWidget(new QLabel("变动数额（收入为正，支出为负）"));
        auto *delta = new QSpinBox;
        delta->setRange(-1000000000, 1000000000);
        sheet->form->addWidget(delta);
        auto *description = sheet->field("事件描述");
        auto *insert = new QCheckBox("同时插入本章正文");
        insert->setChecked(true);
        sheet->form->addWidget(insert);
        auto *preview = sheet->textArea("余额与正文预览", true);
        preview->setMaximumHeight(110);
        auto refresh = [=, this] {
            guard([&] {
                if (people->currentIndex() < 0 || resource->text().trimmed().isEmpty())
                {
                    preview->clear();
                    return;
                }
                QString unit = resource->text().trimmed();
                sheet->report({});
                auto balance = book->balance(people->currentData().toInt(), unit, chapter);
                QString text = QString("%1：%2 → %3").arg(unit).arg(balance).arg(balance + delta->value());
                if (insert->isChecked())
                    text += "\n\n" + eventText(description->text().trimmed(), unit, delta->value(), balance);
                preview->setPlainText(text);
            });
        };
        connect(people, &QComboBox::currentIndexChanged, sheet, refresh);
        connect(delta, &QSpinBox::valueChanged, sheet, refresh);
        connect(resource, &QLineEdit::textChanged, sheet, refresh);
        connect(description, &QLineEdit::textChanged, sheet, refresh);
        connect(insert, &QCheckBox::toggled, sheet, refresh);
        refresh();
        button(sheet->form, "保存事件", [=, this] {
            const int id = people->currentData().toInt();
            const auto unit = resource->text().trimmed(), desc = description->text().trimmed();
            if (!id || unit.isEmpty() || desc.isEmpty())
                throw std::runtime_error("请选择人物，并填写资源名和事件描述。");
            save();
            if (insert->isChecked())
            {
                auto snippet = eventText(desc, unit, delta->value(), book->balance(id, unit, chapter));
                auto body = editor->toPlainText();
                body.replace(insertion.selectionStart(), insertion.selectionEnd() - insertion.selectionStart(),
                             snippet);
                book->event(chapter, id, unit, delta->value(), desc, &body);
                insertion.insertText(snippet);
                dirty = false;
            }
            else
                book->event(chapter, id, unit, delta->value(), desc);
            status->setText(insert->isChecked() ? "事件和正文已保存" : "事件已保存");
            closeSheet();
        });
    }
    static QString eventText(const QString &description, const QString &unit, int delta, qint64 before)
    {
        return QString("%1（%2%3%4，剩余%5）")
            .arg(description, unit, delta >= 0 ? "+" : "", QString::number(delta), QString::number(before + delta));
    }
    QString renderPanel(int id, int t)
    {
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
        return result;
    }
    void panel()
    {
        beginSheet("属性面板");
        auto *people = personChoice();
        auto *templates = sheet->choice("输出模板", book->run("SELECT id,title FROM templates ORDER BY id"));
        auto *preview = sheet->textArea("预览（可直接编辑）");
        auto refresh = [=, this] {
            guard([&] {
                preview->setPlainText(people->count() && templates->count()
                                          ? renderPanel(people->currentData().toInt(), templates->currentData().toInt())
                                          : QString());
            });
        };
        connect(people, &QComboBox::currentIndexChanged, sheet, refresh);
        connect(templates, &QComboBox::currentIndexChanged, sheet, refresh);
        refresh();
        button(sheet->form, "插入属性面板", [=, this] {
            if (!people->count() || !templates->count() || preview->toPlainText().isEmpty())
                throw std::runtime_error("请先选择人物和模板，并填写插入内容。");
            insertion.insertText(preview->toPlainText());
            closeSheet();
        });
    }
    void showText(const QString &title, const QString &text)
    {
        beginSheet(title);
        auto *view = new QPlainTextEdit(text);
        view->setReadOnly(true);
        sheet->form->addWidget(view, 1);
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
        QGraphicsView *view = new QGraphicsView;
        auto *ownedScene = new QGraphicsScene(view);
        view->setScene(ownedScene);
        auto &scene = *ownedScene;
        view->setRenderHint(QPainter::Antialiasing);
        if (pages->currentWidget() == writer)
        {
            beginSheet("人物关系图");
            sheet->form->addWidget(view, 1);
        }
        else
        {
            auto *page = new QWidget;
            auto *layout = new QVBoxLayout(page);
            layout->addWidget(view, 1);
            button(layout, "返回人物关系", [this, page] {
                pages->setCurrentWidget(dossier);
                pages->removeWidget(page);
                page->deleteLater();
            });
            pages->addWidget(page);
            pages->setCurrentWidget(page);
        }
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
            scene.addLine(edge, QPen(QColor("#68bd83"), 2));
            auto label = scene.addText(edges.value(2).toString());
            label->setDefaultTextColor(QColor("#a4f2aa"));
            label->setPos(edge.center());
        }
        for (auto &p : people)
        {
            auto loc = points[p.first];
            auto label = scene.addText(p.second);
            label->setDefaultTextColor(QColor("#e0f6e6"));
            auto box = label->boundingRect();
            label->setPos(loc - QPointF(box.width() / 2, box.height() / 2));
            label->setZValue(2);
            auto bg =
                scene.addRect(label->sceneBoundingRect().adjusted(-6, -2, 6, 2), QPen(Qt::NoPen), QColor("#254834"));
            bg->setZValue(1);
        }
        view->setMinimumHeight(240);
    }
    void history()
    {
        save();
        beginSheet("历史版本");
        auto rows = book->run("SELECT id,datetime(stamp,'localtime') || '  ' || substr(body,1,28) FROM revisions WHERE "
                              "chapter=? ORDER BY id DESC",
                              {chapter});
        auto *versions = sheet->choice("选择版本", std::move(rows));
        if (!versions->count())
        {
            sheet->report("本章尚无历史版本。");
            return;
        }
        auto *preview = sheet->textArea("历史正文预览", true);
        auto refresh = [=, this] {
            guard([&] {
                preview->setPlainText(
                    book->value("SELECT body FROM revisions WHERE id=?", {versions->currentData()}).toString());
            });
        };
        connect(versions, &QComboBox::currentIndexChanged, sheet, refresh);
        refresh();
        button(sheet->form, "用此版本恢复本章", [=, this] {
            auto text =
                book->value("SELECT body FROM revisions WHERE id=? AND chapter=?", {versions->currentData(), chapter})
                    .toString();
            book->atomic([&] { book->save(chapter, text); });
            closeSheet();
            loadChapter(chapter);
        });
    }

    void proofread()
    {
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
    }
    void newTemplate()
    {
        beginSheet("新建输出模板");
        auto *name = sheet->field("模板名称");
        auto *pattern = sheet->textArea("模板内容（可用 {{姓名}}、{{积分}} 或 {{自定义属性名}}）");
        pattern->setPlainText("【{{姓名}}】\n{{属性}}");
        button(sheet->form, "保存模板", [=, this] {
            if (name->text().trimmed().isEmpty() || pattern->toPlainText().isEmpty())
                throw std::runtime_error("请填写模板名称和内容。");
            book->run("INSERT INTO templates(title,pattern) VALUES(?,?)",
                      {name->text().trimmed(), pattern->toPlainText()});
            closeSheet();
            status->setText("模板已保存");
        });
    }
    void fileOutput(bool backup)
    {
        beginSheet(backup ? "备份整本书" : "导出全书 TXT");
        auto *model = new QFileSystemModel(sheet);
        model->setFilter(QDir::AllDirs | QDir::NoDotAndDotDot | QDir::Drives);
        model->setRootPath("");
        auto *dirs = new QTreeView;
        dirs->setModel(model);
        dirs->setHeaderHidden(true);
        for (int c = 1; c < model->columnCount(); ++c)
            dirs->hideColumn(c);
        sheet->form->addWidget(new QLabel("目标目录"));
        sheet->form->addWidget(dirs, 1);
        QFileInfo source(bookPath);
        auto index = model->index(source.absolutePath());
        dirs->setCurrentIndex(index);
        dirs->scrollTo(index);
        auto filename = source.completeBaseName() +
                        (backup ? QDateTime::currentDateTime().toString("-yyyyMMdd-HHmmss") + ".moxu" : ".txt");
        auto *path = sheet->field("保存路径", QDir::toNativeSeparators(source.dir().filePath(filename)));
        connect(dirs, &QTreeView::clicked, sheet, [=](const QModelIndex &index) {
            auto name = QFileInfo(path->text()).fileName();
            path->setText(
                QDir::toNativeSeparators(QDir(model->filePath(index)).filePath(name.isEmpty() ? filename : name)));
        });
        auto *overwrite = new QCheckBox("覆盖此路径已有的 TXT 文件");
        sheet->form->addWidget(overwrite);
        auto refresh = [=] {
            overwrite->setChecked(false);
            overwrite->setVisible(!backup && QFileInfo::exists(path->text()));
        };
        connect(path, &QLineEdit::textChanged, sheet, refresh);
        refresh();
        button(sheet->form, backup ? "保存备份" : "导出文本", [=, this] {
            auto target = QDir::fromNativeSeparators(path->text());
            if (!QDir::isAbsolutePath(target))
                throw std::runtime_error("请填写完整的保存路径。");
            save();
            if (backup)
            {
                if (QFileInfo::exists(target))
                    throw std::runtime_error("该备份已存在，请在保存路径中更改文件名。");
                book->run("VACUUM INTO ?", {target});
            }
            else
            {
                if (!target.endsWith(".txt", Qt::CaseInsensitive))
                    throw std::runtime_error("请使用 .txt 文件后缀。");
                if (QFileInfo::exists(target) && !overwrite->isChecked())
                {
                    overwrite->show();
                    throw std::runtime_error("目标文件已存在，请改名或勾选覆盖。");
                }
                QSaveFile file(target);
                if (!file.open(QIODevice::WriteOnly))
                    throw std::runtime_error("无法打开导出文件。");
                auto q = book->run("SELECT title,body FROM chapters ORDER BY id");
                while (q.next())
                {
                    auto bytes = (q.value(0).toString() + "\n\n" + q.value(1).toString() + "\n\n").toUtf8();
                    if (file.write(bytes) != bytes.size())
                        throw std::runtime_error("写入失败。");
                }
                if (!file.commit())
                    throw std::runtime_error("导出失败。");
            }
            closeSheet();
            status->setText(backup ? "已保存书籍备份" : "全书 TXT 已导出");
        });
    }

  public:
    Window()
    {
        resize(1120, 740);
        setCentralWidget(pages);
        pages->addWidget(shelf);
        auto *intro = new QVBoxLayout(shelf);
        intro->setContentsMargins(70, 45, 70, 45);
        intro->addWidget(new MatrixBanner("墨序 · 书架"));
        intro->addSpacing(18);
        intro->addWidget(new QLabel("最近书籍"));
        intro->addWidget(recent);
        intro->addWidget(location);
        location->setObjectName("hint");
        auto *actions = new QHBoxLayout;
        intro->addLayout(actions);
        button(actions, "新建书籍", [this] { newBook(); });
        button(actions, "打开书籍…",
               [this] { openBook(QFileDialog::getOpenFileName(this, "打开书籍", {}, "墨序书籍 (*.moxu)")); });
        button(actions, "设置保存目录", [this] { chooseStorage(); });
        connect(recent, &QListWidget::itemActivated, this, [this](auto *i) { guard([&] { openBook(i->text()); }); });
        pages->addWidget(storage);
        auto *setup = new QVBoxLayout(storage);
        setup->setContentsMargins(60, 40, 60, 40);
        setup->setSpacing(18);
        setup->addStretch();
        setup->addWidget(new MatrixBanner("小说保存位置"));
        setup->addWidget(directoryMessage);
        auto *explanation = new QLabel("新书默认保存在这里。你可以随时从书架更改目录，已有书籍仍保留在原位置。");
        explanation->setWordWrap(true);
        setup->addWidget(explanation);
        auto *choices = new QHBoxLayout;
        setup->addLayout(choices);
        button(choices, "使用默认目录", [this] { setDirectory(defaultBooksPath()); });
        button(choices, "选择自定义目录…", [this] {
            setDirectory(QFileDialog::getExistingDirectory(
                this, "选择小说保存目录",
                QSettings().value("booksDir", QCoreApplication::applicationDirPath()).toString()));
        });
        button(choices, "返回书架", [this] {
            if (!QSettings().contains("booksDir"))
            {
                directoryMessage->setText("首次使用请先选择保存目录。默认目录  " +
                                          QDir::toNativeSeparators(defaultBooksPath()));
                return;
            }
            showShelf();
        });
        setup->addStretch();
        auto *work = writer;
        pages->addWidget(work);
        writerColumns = new QHBoxLayout(work);
        auto *cols = writerColumns;
        auto *left = new QVBoxLayout;
        cols->addLayout(left, 1);
        left->addWidget(new QLabel("章节目录"));
        left->addWidget(chapters);
        button(left, "＋ 新章节", [this] {
            save();
            beginSheet("新建章节");
            auto *name = sheet->field("章节名");
            name->setFocus();
            button(sheet->form, "创建章节", [this, name] {
                QString title = name->text().trimmed();
                if (title.isEmpty())
                    throw std::runtime_error("请填写章节名。");
                auto id = book->run("INSERT INTO chapters(title) VALUES(?)", {title}).lastInsertId().toInt();
                addChapterItem(id, title);
                chapters->setCurrentRow(chapters->count() - 1);
            });
        });
        auto *right = new QVBoxLayout;
        cols->addLayout(right, 4);
        right->addWidget(heading);
        right->addWidget(editor);
        heading->setObjectName("title");
        editor->setObjectName("manuscript");
        editor->setFont(QFont("Microsoft YaHei", 13));
        editor->setPlaceholderText("从这里开始写作…");
        editor->document()->setDocumentMargin(26);
        tools = addToolBar("写作");
        tools->setMovable(false);
        tool("返回资料工作区", [this] { enterDossier(); });
        tool("保存", [this] { save(); });
        tool("AI 写作", [this] { aiWriting(); });
        tool("人物名字", [this] { insertPerson(); });
        tool("属性面板", [this] { panel(); });
        tool("记录事件", [this] { record(); });
        tool("时间轴", [this] { timeline(); });
        tool("关系图", [this] { graph(); });
        tool("历史版本", [this] { history(); });
        tool("资料与导出", [this] {
            beginSheet("资料与导出");
            button(sheet->form, "人物档案", [this] { enterDossier(1); });
            button(sheet->form, "人物关系", [this] { enterDossier(2); });
            button(sheet->form, "新建输出模板", [this] { newTemplate(); });
            button(sheet->form, "整书基础校对", [this] { proofread(); });
            button(sheet->form, "备份整本书…", [this] { fileOutput(true); });
            button(sheet->form, "导出全书 TXT", [this] { fileOutput(false); });
        });
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
        connect(new QShortcut(QKeySequence::Save, this), &QShortcut::activated, this, [this] {
            guard([this] {
                if (pages->currentWidget() == dossier)
                {
                    dossier->flush();
                    setWindowTitle(book->value("SELECT title FROM meta").toString() + " · 墨序");
                }
                else
                    save();
            });
        });
        dossier = new Dossier(
            [this] {
                setWindowTitle(book->value("SELECT title FROM meta").toString() + " · 墨序");
                pages->setCurrentWidget(writer);
                tools->show();
                statusBar()->show();
                editor->setFocus();
            },
            [this] { showShelf(); }, [this] { graph(); }, [this](QString n) { setWindowTitle(n + " · 墨序"); });
        pages->addWidget(dossier);
        showShelf();
        if (!QSettings().contains("booksDir"))
            chooseStorage();
    }
    void closeEvent(QCloseEvent *e) override
    {
        if (!guard([this] {
                if (pages->currentWidget() == dossier)
                    dossier->flush();
                save();
            }))
            e->ignore();
        else
            e->accept();
    }
};
