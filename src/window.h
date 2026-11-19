#pragma once
#include "chapter_text.h"
#include "book_cover.h"
#include "dossier.h"
#include "event_form.h"
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
    InlineActions *shelfActions, *storageActions;
    Dossier *dossier = nullptr;
    QWidget *writer = new QWidget, *storage = new QWidget;
    QLabel *location = new QLabel, *directoryMessage = new QLabel;
    QListWidget *recent = new QListWidget;
    QLabel *shelfHeading = new QLabel;
    QLabel *emptyShelf = new QLabel("点击左侧「新建书籍」开始写作，或打开已有的 .moxu 书籍。");
    QListWidget *chapters = new QListWidget;
    QPlainTextEdit *editor = new QPlainTextEdit;
    QLabel *heading = new QLabel;
    QLabel *chapterPosition = new QLabel;
    QLabel *chapterLength = new QLabel("本章 0 字");
    SaveStatus *status = new SaveStatus;
    QHBoxLayout *writerColumns;
    WriterSheet *sheet = nullptr;
    QPointer<QWidget> writerFocus;
    QAction *activeSheetTool = nullptr;
    QTextCursor insertion;
    QString bookPath;
    QToolBar *tools;
    int chapter = 0;
    bool dirty = false;
    QHash<QString, QString> sessionKeys;

    void closeSheet()
    {
        if (!sheet)
            return;
        if (activeSheetTool)
        {
            activeSheetTool->setChecked(false);
            activeSheetTool = nullptr;
        }
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
        save();
        QPointer<QWidget> previousFocus = QApplication::focusWidget();
        const auto selection = editor->textCursor();
        const int start = selection.selectionStart(), end = selection.selectionEnd();
        const auto original = editor->toPlainText();
        QString excerpt = selection.selectedText();
        excerpt.replace(QChar::ParagraphSeparator, '\n');
        auto back = [this, previousFocus] {
            auto *panel = pages->currentWidget();
            pages->setCurrentWidget(writer);
            pages->removeWidget(panel);
            panel->deleteLater();
            tools->show();
            statusBar()->show();
            if (previousFocus && writer->isAncestorOf(previousFocus) && previousFocus->isVisible())
                previousFocus->setFocus();
            else
                editor->setFocus();
        };
        auto accept = [this, start, end, original, sourceChapter = chapter, back](const QString &text, bool wholeChapter) {
            if (chapter != sourceChapter || editor->toPlainText() != original)
            {
                statusBar()->show();
                statusBar()->showMessage("原章节内容已变化，请复制候选稿，返回正文重新选择要修改的位置。");
                return;
            }
            const auto newBody = wholeChapter ? text : original.left(start) + text + original.mid(end);
            if (!guard([&] { book->atomic([&] { book->save(chapter, newBody, Book::SaveMode::Checkpoint); }); }))
                return;
            QTextCursor target(editor->document());
            target.setPosition(wholeChapter ? 0 : start);
            target.setPosition(wholeChapter ? editor->document()->characterCount() - 1 : end, QTextCursor::KeepAnchor);
            target.beginEditBlock();
            target.removeSelectedText();
            target.insertText(text);
            target.endEditBlock();
            if (sheet)
                insertion = target;
            editor->setTextCursor(target);
            back();
            dirty = false;
            status->pending.stop();
            statusBar()->clearMessage();
            status->confirm(wholeChapter ? "本章全文" : start == end ? "插入正文" : "选区替换");
        };
        const bool entireSelection = start != end && start == 0 && end == editor->document()->characterCount() - 1;
        auto *panel = new WritingAssistant(*book, chapter, excerpt, selection.hasSelection(), entireSelection,
                                           sessionKeys, back, accept);
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
            if (sheet && pages->currentWidget() == writer)
                sheet->report(error);
            else if (pages->currentWidget() == storage)
                directoryMessage->setText(error);
            else
            {
                statusBar()->show();
                statusBar()->showMessage(error, 12000);
            }
            return false;
        }
    }
    QPushButton *button(QBoxLayout *layout, const QString &title, Action action)
    {
        auto *btn = new QPushButton(title);
        layout->addWidget(btn);
        connect(btn, &QPushButton::clicked, this, [this, action] { guard(action); });
        return btn;
    }
    void tool(const QString &title, Action action, bool opensSheet = true)
    {
        auto *entry = tools->addAction(title);
        entry->setCheckable(opensSheet);
        connect(entry, &QAction::triggered, this, [=, this] {
            if (opensSheet && activeSheetTool == entry && sheet)
            {
                entry->setChecked(true);
                return;
            }
            const bool completed = guard(action);
            if (opensSheet)
            {
                entry->setChecked(completed && sheet);
                if (completed && sheet)
                    activeSheetTool = entry;
            }
        });
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
        })->setObjectName("primary");
    }
    void save(Book::SaveMode mode = Book::SaveMode::Automatic)
    {
        if (!dirty || !book || !chapter)
            return;
        book->atomic([&] { book->save(chapter, editor->toPlainText(), mode); });
        status->pending.stop();
        dirty = false;
        status->setText("已保存");
    }
    void refreshChapterMoves()
    {
        const int row = chapters->currentRow();
        writer->findChild<QPushButton *>("chapterUp")->setEnabled(row > 0);
        writer->findChild<QPushButton *>("chapterDown")->setEnabled(row >= 0 && row + 1 < chapters->count());
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
        refreshChapterMoves();
        chapterPosition->setText(QString("第 %1 章 / 共 %2 章").arg(chapters->currentRow() + 1).arg(chapters->count()));
        editor->setPlainText(q.value(1).toString());
        dirty = false;
        status->setText("已保存");
    }
    void addChapterItem(int id, const QString &name, int index = -1)
    {
        auto *item = new QListWidgetItem;
        chapters->insertItem(index < 0 ? chapters->count() : index, item);
        setChapterLabel(item, name);
        item->setData(Qt::UserRole, id);
    }
    void setChapterLabel(QListWidgetItem *item, const QString &name)
    {
        item->setText(numberedChapter(chapters->row(item) + 1, name));
        item->setToolTip(name);
    }
    void renameChapter()
    {
        save();
        beginSheet("修改章节名");
        auto *name = sheet->field("章节名", heading->text());
        name->selectAll();
        name->setFocus();
        button(sheet->form, "保存章节名", [this, name] {
            const auto title = name->text().trimmed();
            if (title.isEmpty())
                throw std::runtime_error("请填写章节名。");
            book->run("UPDATE chapters SET title=? WHERE id=?", {title, chapter});
            setChapterLabel(chapters->currentItem(), title);
            heading->setText(title);
            closeSheet();
            status->setText("章节名已保存");
        });
    }
    void showShelf()
    {
        save();
        status->pending.stop();
        closeSheet();
        writerFocus.clear();
        if (dossier)
            dossier->resetBook();
        chapter = 0;
        book.reset();
        lock.reset();
        shelfActions->finish();
        pages->setCurrentWidget(shelf);
        tools->hide();
        statusBar()->hide();
        status->clear();
        setWindowTitle("墨序 · 小说辅助系统");
        location->setText(QDir::toNativeSeparators(QSettings().value("booksDir", defaultBooksPath()).toString()));
        refreshCovers();
    }
    static QString shelfTitle(const QString &path, QByteArray *artwork = nullptr)
    {
        if (!QFileInfo(path).isFile())
            return {};
        const auto connection = QUuid::createUuid().toString();
        QString title;
        {
            auto db = QSqlDatabase::addDatabase("QSQLITE", connection);
            db.setDatabaseName(path);
            db.setConnectOptions("QSQLITE_OPEN_READONLY");
            if (db.open())
            {
                QSqlQuery q(db);
                if (q.exec("SELECT title FROM meta WHERE format=1 LIMIT 1") && q.next())
                    title = q.value(0).toString();
                if (artwork && db.tables().contains("book_cover") &&
                    q.exec("SELECT image FROM book_cover WHERE id=1") && q.next())
                    *artwork = q.value(0).toByteArray();
            }
        }
        QSqlDatabase::removeDatabase(connection);
        return title;
    }
    void changeCover(const QString &path)
    {
        QByteArray original;
        const auto title = shelfTitle(path, &original);
        if (title.isEmpty())
            throw std::runtime_error("找不到书籍，请刷新书架。");
        auto *form = shelfActions->begin("更换封面 · " + title);
        auto *columns = new QHBoxLayout;
        form->addLayout(columns, 1);
        auto *picker =
            new InlinePathPicker(PathUse::OpenFile, QStandardPaths::writableLocation(QStandardPaths::PicturesLocation),
                                 {}, {}, {"*.png", "*.jpg", "*.jpeg", "*.bmp"});
        columns->addWidget(picker, 1);
        auto *side = new QVBoxLayout;
        columns->addLayout(side);
        auto *preview = new BookCover(title, path);
        preview->setFixedSize(preview->sizeHint());
        preview->useAsPreview();
        QPixmap previous;
        if (!original.isEmpty())
            previous.loadFromData(original, "PNG");
        preview->setArtwork(previous);
        side->addWidget(preview);
        auto *reset = new QPushButton("恢复默认封面");
        side->addWidget(reset);
        side->addStretch();
        auto *hint = new QLabel("选择本地 PNG、JPG 或 BMP 图片。");
        form->addWidget(hint);
        auto *confirm = shelfActions->buttons("保存封面", [this, path, title, original, preview] {
            QByteArray data;
            if (!preview->coverImage().isNull())
            {
                QBuffer buffer(&data);
                if (!buffer.open(QIODevice::WriteOnly) || !preview->coverImage().save(&buffer, "PNG"))
                    throw std::runtime_error("图片转换失败，请重新选择。");
            }
            QLockFile editing(path + ".lock");
            editing.setStaleLockTime(0);
            if (!editing.tryLock())
                throw std::runtime_error("书籍正在使用或目录无法写入，请关闭书籍后重试。");
            QByteArray current;
            if (shelfTitle(path, &current) != title || current != original)
                throw std::runtime_error("书籍信息已变动，请返回书架重新选择。");
            Book target(path);
            target.atomic([&] {
                target.run(
                    "CREATE TABLE IF NOT EXISTS book_cover(id INTEGER PRIMARY KEY CHECK(id=1), image BLOB NOT NULL)");
                if (data.isEmpty())
                    target.run("DELETE FROM book_cover WHERE id=1");
                else
                    target.run("INSERT OR REPLACE INTO book_cover(id,image) VALUES(1,?)", {data});
            });
            refreshCovers();
        });
        confirm->setEnabled(false);
        connect(picker->field(), &QLineEdit::textChanged, picker, [=] {
            confirm->setEnabled(false);
            preview->setArtwork(previous);
            hint->setText("选择本地 PNG、JPG 或 BMP 图片。");
            if (picker->path().isEmpty())
                return;
            QImageReader reader(picker->path());
            reader.setAutoTransform(true);
            auto size = reader.size();
            if (size.isValid() && (size.width() > 960 || size.height() > 960))
                reader.setScaledSize(size.scaled(960, 960, Qt::KeepAspectRatio).expandedTo(QSize(1, 1)));
            auto image = reader.read();
            if (image.isNull())
            {
                hint->setText("无法读取这张图片，请重新选择。");
                return;
            }
            if (image.width() > 960 || image.height() > 960)
                image = image.scaled(960, 960, Qt::KeepAspectRatio, Qt::SmoothTransformation);
            preview->setArtwork(QPixmap::fromImage(image));
            hint->setText("图片将随书籍保存。");
            confirm->setEnabled(true);
        });
        connect(reset, &QPushButton::clicked, picker, [=] {
            picker->field()->clear();
            preview->setArtwork({});
            hint->setText("已预览默认封面。");
            confirm->setEnabled(!original.isEmpty());
        });
    }
    void deleteFromShelf(const QString &path, const QString &title)
    {
        auto *form = shelfActions->begin("删除书籍");
        auto *details =
            new QLabel("《" + title + "》将从书架移除，本地书籍文件将移入回收站。\n" + QDir::toNativeSeparators(path));
        details->setTextFormat(Qt::PlainText);
        details->setWordWrap(true);
        form->addWidget(details);
        form->addWidget(new QLabel("请输入完整书名以确认删除"));
        auto *input = new QLineEdit;
        input->setObjectName("deleteBookTitle");
        form->addWidget(input);
        form->addStretch();
        auto *confirm = shelfActions->buttons("删除书籍", [this, path, title, input] {
            if (input->text() != title)
                throw std::runtime_error("输入的书名不匹配。");
            QLockFile deleting(path + ".lock");
            deleting.setStaleLockTime(0);
            if (!deleting.tryLock())
                throw std::runtime_error("书籍正在使用或目录无法写入，请关闭书籍后重试。");
            if (shelfTitle(path) != title)
                throw std::runtime_error("书籍已更名或文件已变动，请返回书架重新选择。");
            if (QFileInfo::exists(path + "-journal") || QFileInfo(path + "-wal").size() > 0)
                throw std::runtime_error("书籍还有待处理的数据库日志，请先打开并正常关闭后再删除。");
            QSettings settings;
            auto paths = settings.value("recent").toStringList();
            paths.removeIf([&](const QString &entry) { return QFileInfo(entry) == QFileInfo(path); });
            if (!QFile::moveToTrash(path))
                throw std::runtime_error("无法将文件移入回收站，请检查文件是否被占用或所在磁盘是否支持回收站。");
            settings.setValue("recent", paths);
            refreshCovers();
        });
        confirm->setEnabled(false);
        connect(input, &QLineEdit::textChanged, confirm,
                [confirm, title](const QString &text) { confirm->setEnabled(text == title); });
        input->setFocus();
    }
    void refreshCovers()
    {
        recent->clear();
        QSettings settings;
        QStringList paths = settings.value("recent").toStringList();
        QDir folder(settings.value("booksDir", defaultBooksPath()).toString());
        for (const auto &file : folder.entryInfoList({"*.moxu"}, QDir::Files, QDir::Name))
            paths.append(file.absoluteFilePath());
        QSet<QString> seen;
        for (const auto &entry : paths)
        {
            auto path = QFileInfo(entry).canonicalFilePath();
            if (path.isEmpty() || seen.contains(path))
                continue;
            seen.insert(path);
            QByteArray artwork;
            const auto title = shelfTitle(path, &artwork);
            if (title.isEmpty())
                continue;
            auto *item = new QListWidgetItem(title, recent);
            item->setData(Qt::UserRole, path);
            auto *cover = new BookCover(title, path);
            item->setSizeHint(cover->sizeHint());
            if (!artwork.isEmpty())
            {
                QPixmap image;
                image.loadFromData(artwork, "PNG");
                cover->setArtwork(image);
            }
            recent->setItemWidget(item, cover);
            connect(cover, &QPushButton::clicked, this, [this, path] { guard([&] { openBook(path); }); });
            auto *menu = cover->actionsMenu();
            menu->addAction("打开本地目录", this, [this, path] {
                guard([&] {
                    if (!QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(path).absolutePath())))
                        throw std::runtime_error("无法打开书籍所在目录。");
                });
            });
            menu->addAction("更换封面", this, [this, path] { guard([&] { changeCover(path); }); });
            menu->addAction("删除书籍", this, [this, path, title] { deleteFromShelf(path, title); });
        }
        shelfHeading->setText(QString("我的书籍 · %1 本").arg(recent->count()));
        recent->setVisible(recent->count() > 0);
        emptyShelf->setVisible(recent->count() == 0);
    }
    void openBook(QString path, const QString &title = {})
    {
        if (path.isEmpty())
            return;
        if (QFileInfo(path).exists())
            path = QFileInfo(path).canonicalFilePath();
        auto candidate = std::make_unique<QLockFile>(path + ".lock");
        candidate->setStaleLockTime(0);
        if (!candidate->tryLock())
            throw std::runtime_error("无法锁定书籍，请检查是否已打开以及目录写入权限。");
        auto next = std::make_unique<Book>(path, title);
        auto name = next->value("SELECT title FROM meta").toString();
        auto q = next->run("SELECT id,title FROM chapters ORDER BY position");
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
        settings.setValue("recent", paths);
    }
    void newBook()
    {
        QString folder = QSettings().value("booksDir", defaultBooksPath()).toString();
        auto *fields = shelfActions->begin("新建书籍");
        auto *title = new QLineEdit;
        title->setObjectName("newBookTitle");
        fields->addWidget(new QLabel("书名"));
        fields->addWidget(title);
        auto *picker = new InlinePathPicker(PathUse::SaveFile, folder, ".moxu", "新书.moxu");
        fields->addWidget(picker, 1);
        connect(title, &QLineEdit::textChanged, picker,
                [picker, previous = QString("新书.moxu")](const QString &text) mutable {
                    auto current = QFileInfo(picker->path());
                    auto name = text.trimmed();
                    name.replace(QRegularExpression(R"rx([<>:"/\\|?*\x00-\x1f])rx"), "_");
                    auto filename = (name.isEmpty() ? QString("新书") : name) + ".moxu";
                    if (current.fileName() == previous)
                        picker->setPath(current.dir().filePath(filename));
                    previous = filename;
                });
        shelfActions->buttons("创建书籍", [this, title, picker] {
            auto name = title->text().trimmed();
            if (name.isEmpty())
                throw std::runtime_error("请填写书名。");
            auto path = picker->checkedPath();
            if (QFileInfo::exists(path))
                throw std::runtime_error("已有同名文件。可以保留书名，在保存路径中修改文件名或选择其他目录。");
            openBook(path, name);
        });
        title->setFocus();
    }
    void browseBooks()
    {
        auto *fields = shelfActions->begin("打开已有书籍");
        auto *picker = new InlinePathPicker(PathUse::OpenFile,
                                            QSettings().value("booksDir", defaultBooksPath()).toString(), ".moxu");
        fields->addWidget(picker, 1);
        shelfActions->buttons("打开书籍", [this, picker] { openBook(picker->checkedPath()); });
    }
    void enterDossier(int page = 0)
    {
        save();
        if (pages->currentWidget() == writer)
            writerFocus = writer->focusWidget();
        dossier->enter(book.get(), page, chapter);
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
            throw std::runtime_error("无法创建书籍目录，请选择有写入权限的位置。");
        QTemporaryFile probe(QDir(path).filePath(".moxu-write-XXXXXX"));
        if (!probe.open())
            throw std::runtime_error("该目录无法写入，请选择其他目录。");
        QSettings settings;
        settings.setValue("booksDir", path);
        settings.sync();
        if (settings.status() != QSettings::NoError)
            throw std::runtime_error("保存目录设置失败");
        showShelf();
    }
    void chooseStorage()
    {
        storageActions->finish();
        pages->setCurrentWidget(storage);
        tools->hide();
        directoryMessage->setText("默认目录  " + QDir::toNativeSeparators(defaultBooksPath()));
    }
    void record()
    {
        beginSheet("记录事件 · 第 " + QString::number(chapters->currentRow() + 1) + " 章");
        auto *form = new EventForm(*book, chapter, [this](bool numericOnly, auto done) {
            openCatalogCreator(sheet->pages, *book, numericOnly, done);
        });
        sheet->form->addWidget(form);
        button(sheet->form, "保存事件", [this, form] {
            save();
            const auto snippet = form->snippet();
            auto body = editor->toPlainText();
            if (form->insert->isChecked())
                body.replace(insertion.selectionStart(), insertion.selectionEnd() - insertion.selectionStart(),
                             snippet);
            form->persist(form->insert->isChecked() ? &body : nullptr);
            if (form->insert->isChecked())
            {
                insertion.insertText(snippet);
                dirty = false;
            }
            status->setText(form->insert->isChecked() ? "事件和正文已保存" : "事件已保存");
            closeSheet();
        })->setObjectName("primary");
    }
    QString renderPanel(int id, int t)
    {
        auto pattern = book->value("SELECT pattern FROM templates WHERE id=?", {t}).toString();
        auto q = book->run("SELECT name,attributes FROM people WHERE id=?", {id});
        q.next();
        const auto resourceValues = book->resourceBalances(id, chapter);
        auto fields = resourceValues;
        fields["姓名"] = q.value(0).toString();
        fields["属性"] = q.value(1).toString();
        auto items = book->ownedItems(id, chapter);
        const auto inventory = book->supplyCounts(id, chapter);
        QStringList supplies;
        for (auto it = inventory.cbegin(); it != inventory.cend(); ++it)
        {
            fields[it.key() + ".数量"] = QString::number(it.value());
            if (!fields.contains(it.key()))
                fields[it.key()] = QString::number(it.value());
            if (it.value() > 0)
                supplies << it.key() + " × " + QString::number(it.value());
        }
        fields["消耗品"] = supplies.isEmpty() ? "无" : supplies.join("、");
        items.append(supplies);
        fields["持有道具"] = items.isEmpty() ? "无" : items.join("、");
        QStringList learned;
        for (const auto &skill : book->characterSkills(id, chapter))
        {
            learned << skill.name;
            fields[skill.name + ".等级"] = skill.level.isEmpty() ? "未记录" : skill.level;
            fields[skill.name + ".冷却"] = skill.cooldown.isEmpty() ? "未记录" : skill.cooldown;
            fields[skill.name + ".剩余次数"] = skill.remaining < 0 ? "不限" : QString::number(skill.remaining);
        }
        fields["已学技能"] = learned.isEmpty() ? "无" : learned.join("、");
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
            const auto key = m.captured(1);
            if (key.startsWith("资源:") && resourceValues.contains(key.mid(3)))
                result += resourceValues.value(key.mid(3));
            else
                result += fields.value(key, "未记录");
            start = m.capturedEnd();
        }
        result += pattern.mid(start);
        return result;
    }
    void panel()
    {
        beginSheet("属性面板");
        auto *people = personChoice();
        button(sheet->form, "查看章节快照", [this, people] {
            auto *stack = sheet->pages;
            auto *detail = new QWidget;
            auto *layout = new QVBoxLayout(detail);
            button(layout, "返回属性面板", [stack, detail] {
                stack->setCurrentIndex(0);
                stack->removeWidget(detail);
                detail->deleteLater();
            });
            auto *title = new QLabel(people->currentText() + " · 章节快照");
            title->setTextFormat(Qt::PlainText);
            title->setObjectName("sectionTitle");
            layout->addWidget(title);
            auto *snapshot = new CharacterSnapshot;
            layout->addWidget(snapshot, 1);
            snapshot->load(*book, chapter);
            snapshot->selectPerson(people->currentData().toInt());
            stack->addWidget(detail);
            stack->setCurrentWidget(detail);
        });
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
        })->setObjectName("primary");
    }
    void timeline()
    {
        auto q = book->run("WITH numbered AS (SELECT id,title,position AS seq FROM chapters) "
                           "SELECT c.title,p.name,e.description,e.resource,e.delta,SUM(delta) OVER(PARTITION BY "
                           "person,resource ORDER BY c.seq,e.anchor,e.plot,e.id),c.id,c.seq FROM journal e JOIN "
                           "numbered c ON c.id=e.chapter "
                           "LEFT JOIN people p ON p.id=e.person ORDER BY c.seq,e.anchor,e.plot,e.id");
        beginSheet("时间轴与收支明细");
        auto *view = new QTextEdit;
        view->setReadOnly(true);
        view->document()->setDocumentMargin(14);
        sheet->form->addWidget(view, 1);
        QString html;
        QVariant lastChapter;
        while (q.next())
        {
            if (lastChapter != q.value(6))
            {
                lastChapter = q.value(6);
                html += QString("<h3 style='color:#a3eeb1'>%1</h3>")
                            .arg(numberedChapter(q.value(7).toLongLong(), q.value(0).toString()).toHtmlEscaped());
            }
            if (q.value(3).toString().isEmpty())
            {
                html += QString("<p><b>%1</b>　%2</p>")
                            .arg(q.value(1).toString().toHtmlEscaped(),
                                 q.value(2).toString().toHtmlEscaped().replace('\n', "<br>"));
                continue;
            }
            auto amount = q.value(4).toString();
            if (q.value(4).toLongLong() > 0)
                amount.prepend('+');
            html += QString("<p><b>%1</b>　%2<br><span style='color:#afcbbb'>%3 %4　·　结余 %5</span></p>")
                        .arg(q.value(1).toString().toHtmlEscaped(),
                             q.value(2).toString().toHtmlEscaped().replace('\n', "<br>"),
                             q.value(3).toString().toHtmlEscaped(), amount, q.value(5).toString());
        }
        view->setHtml(html.isEmpty() ? QStringLiteral("尚未记录事件") : html);
    }
    void graph()
    {
        QGraphicsView *view = new QGraphicsView;
        auto *ownedScene = new QGraphicsScene(view);
        view->setScene(ownedScene);
        view->setRenderHint(QPainter::Antialiasing);
        view->setMinimumHeight(240);
        if (pages->currentWidget() == writer)
        {
            beginSheet("人物关系图");
            view->setObjectName("writerRelationGraph");
            sheet->form->addWidget(view, 1);
        }
        else
            dossier->showRelationGraph(view);
        drawRelations(*ownedScene);
    }
    void drawRelations(QGraphicsScene &scene)
    {
        scene.clear();
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
    }
    void reviewHistoryCleanup()
    {
        const auto remove = book->historyCleanupCount();
        beginSheet("整理全书旧历史");
        auto *explanation =
            new QLabel(QString("确认后，旧版历史也会计入全书上限。这次将永久移除 %1 份历史快照，各章当前正文会保留。")
                           .arg(remove));
        explanation->setWordWrap(true);
        sheet->form->addWidget(explanation);
        button(sheet->form, "返回历史版本", [this] { history(); });
        button(sheet->form, "确认整理", [this, remove] {
            book->includeLegacyInHistoryLimit(remove);
            history();
            sheet->report(QString("全书历史已整理，已移除 %1 份。").arg(remove));
        });
    }
    void history()
    {
        save();
        beginSheet("历史版本");
        auto rows = book->run("SELECT id,datetime(stamp,'localtime') FROM revisions WHERE "
                              "chapter=? ORDER BY id DESC",
                              {chapter});
        auto *versions = new QListWidget;
        versions->setObjectName("historyVersions");
        versions->setAccessibleName("选择历史版本");
        versions->setMaximumHeight(155);
        versions->setWordWrap(true);
        versions->setTextElideMode(Qt::ElideNone);
        const auto limits = book->historyLimits();
        auto *summary = new QLabel(QString("每章最多 %1 份，全书共用 %2 份、%3 MiB 历史正文额度。优先保留近期版本。")
                                       .arg(limits.perChapter)
                                       .arg(limits.perBook)
                                       .arg(Book::historyBudgetMiB));
        summary->setWordWrap(true);
        sheet->form->addWidget(summary);
        if (book->value("SELECT 1 FROM revisions WHERE rolling=0 LIMIT 1").isValid())
            button(sheet->form, "整理全书旧版历史…", [this] { reviewHistoryCleanup(); });
        sheet->form->addWidget(versions);
        while (rows.next())
        {
            auto *item =
                new QListWidgetItem(rows.value(1).toString() + "   ·   #" + rows.value(0).toString(), versions);
            item->setData(Qt::UserRole, rows.value(0));
        }
        if (!versions->count())
        {
            versions->hide();
            sheet->report("本章尚无历史版本。");
            return;
        }
        auto *preview = sheet->textArea("历史正文预览", true);
        auto selectedBody = [this, versions] {
            const auto *item = versions->currentItem();
            if (!item)
                throw std::runtime_error("请先选择历史版本。");
            const auto text =
                book->value("SELECT body FROM revisions WHERE id=? AND chapter=?", {item->data(Qt::UserRole), chapter});
            if (!text.isValid())
                throw std::runtime_error("找不到所选历史版本，请重新打开历史版本。");
            return text.toString();
        };
        connect(versions, &QListWidget::currentRowChanged, sheet,
                [this, preview, selectedBody] { guard([&] { preview->setPlainText(selectedBody()); }); });
        versions->setCurrentRow(0);
        button(sheet->form, "用此版本恢复本章", [=, this] {
            const auto text = selectedBody();
            book->atomic([&] { book->save(chapter, text); });
            closeSheet();
            loadChapter(chapter);
        });
    }

    void newTemplate()
    {
        beginSheet("新建输出模板");
        auto *name = sheet->field("模板名称");
        auto *pattern =
            sheet->textArea("模板内容（如 {{姓名}}、{{灵石}}、{{持有道具}}、{{血瓶.数量}}、{{上清仙法.等级}}）");
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
        QFileInfo source(bookPath);
        auto filename = source.completeBaseName() +
                        (backup ? QDateTime::currentDateTime().toString("-yyyyMMdd-HHmmss") + ".moxu" : ".txt");
        auto *picker =
            new InlinePathPicker(PathUse::SaveFile, source.absolutePath(), backup ? ".moxu" : ".txt", filename);
        sheet->form->addWidget(picker, 1);
        auto *path = picker->field();
        auto *overwrite = new QCheckBox("覆盖此路径已有的 TXT 文件");
        sheet->form->addWidget(overwrite);
        auto refresh = [=] {
            overwrite->setChecked(false);
            overwrite->setVisible(!backup && QFileInfo::exists(picker->path()));
        };
        connect(path, &QLineEdit::textChanged, sheet, refresh);
        refresh();
        button(sheet->form, backup ? "保存备份" : "导出文本", [=, this] {
            auto target = picker->checkedPath();
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
                auto q = book->run("SELECT title,body FROM chapters ORDER BY position");
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
        })->setObjectName("primary");
    }

  public:
    Window()
    {
        resize(1120, 740);
        setCentralWidget(pages);
        pages->addWidget(shelf);
        auto *intro = new QVBoxLayout(shelf);
        intro->setContentsMargins(22, 16, 22, 16);
        auto *shelfHome = new QWidget;
        shelfActions = new InlineActions(shelfHome);
        intro->addWidget(shelfActions, 1);
        auto *shelfColumns = new QHBoxLayout(shelfHome);
        shelfColumns->setContentsMargins(0, 0, 0, 0);
        shelfColumns->setSpacing(14);
        auto *actions = panelColumn(shelfColumns, 0, true);
        actions->parentWidget()->setFixedWidth(190);
        actions->addWidget(new MatrixBanner("墨序 · 书架"));
        button(actions, "新建书籍", [this] { newBook(); })->setObjectName("primary");
        button(actions, "打开书籍…", [this] { browseBooks(); });
        actions->addStretch();
        button(actions, "设置保存目录", [this] { chooseStorage(); });
        button(actions, "AI 配置", [this] {
            auto *page = new AiConfig(sessionKeys, QSettings().value("writing/provider", "deepseek").toString(),
                                      [this](const QString &selected) {
                                          if (!selected.isEmpty())
                                              QSettings().setValue("writing/provider", selected);
                                          auto *old = pages->currentWidget();
                                          pages->setCurrentWidget(shelf);
                                          pages->removeWidget(old);
                                          old->deleteLater();
                                      });
            pages->addWidget(page);
            pages->setCurrentWidget(page);
        });
        auto *library = panelColumn(shelfColumns, 1);
        shelfHeading->setObjectName("title");
        library->addWidget(shelfHeading);
        recent->setViewMode(QListView::IconMode);
        recent->setMovement(QListView::Static);
        recent->setResizeMode(QListView::Adjust);
        recent->setSpacing(12);
        recent->setStyleSheet("QListWidget {background:transparent;border:none;padding:0;} "
                              "QListWidget::item {color:transparent;padding:0;border:0;} QListWidget::item:selected "
                              "{background:#20392b;color:transparent;}");
        library->addWidget(recent, 1);
        emptyShelf->setObjectName("hint");
        emptyShelf->setWordWrap(true);
        emptyShelf->setAlignment(Qt::AlignCenter);
        library->addWidget(emptyShelf, 1);
        library->addWidget(location);
        location->setObjectName("hint");
        location->setWordWrap(true);
        connect(recent, &QListWidget::itemActivated, this,
                [this](auto *i) { guard([&] { openBook(i->data(Qt::UserRole).toString()); }); });
        pages->addWidget(storage);
        auto *storageHome = new QWidget;
        storageActions = new InlineActions(storageHome);
        auto *storageLayout = new QVBoxLayout(storage);
        storageLayout->addWidget(storageActions);
        auto *setup = new QVBoxLayout(storageHome);
        setup->setContentsMargins(22, 16, 22, 16);
        setup->addStretch();
        auto *setupRow = new QHBoxLayout;
        setup->addLayout(setupRow);
        setupRow->addStretch();
        auto *setupCard = panelColumn(setupRow, 0);
        setupCard->parentWidget()->setMaximumWidth(720);
        setupRow->addStretch();
        setupCard->addWidget(sectionLabel("小说保存位置"));
        setupCard->addWidget(directoryMessage);
        directoryMessage->setWordWrap(true);
        auto *explanation = new QLabel("新书默认保存在这里。你可以随时从书架更改目录，已有书籍仍保留在原位置。");
        explanation->setWordWrap(true);
        explanation->setObjectName("hint");
        setupCard->addWidget(explanation);
        auto *choices = new QHBoxLayout;
        setupCard->addLayout(choices);
        button(choices, "使用默认目录", [this] { setDirectory(defaultBooksPath()); })->setObjectName("primary");
        button(choices, "选择自定义目录…", [this] {
            auto *fields = storageActions->begin("自定义小说保存目录");
            auto *picker =
                new InlinePathPicker(PathUse::Directory, QSettings().value("booksDir", defaultBooksPath()).toString());
            fields->addWidget(picker, 1);
            storageActions->buttons("使用此目录", [this, picker] { setDirectory(picker->checkedPath()); });
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
        cols->setContentsMargins(16, 12, 16, 8);
        cols->setSpacing(12);
        auto *left = panelColumn(cols, 0, true);
        left->parentWidget()->setFixedWidth(178);
        chapters->setObjectName("chapterList");
        auto *directoryHeader = new QHBoxLayout;
        left->addLayout(directoryHeader);
        directoryHeader->addWidget(sectionLabel("章节目录"), 1);
        auto *collapse = new QPushButton("收起");
        collapse->setAccessibleName("收起章节目录");
        collapse->setObjectName("collapseChapters");
        directoryHeader->addWidget(collapse);
        left->addWidget(chapters);
        auto *order = new QHBoxLayout;
        left->addLayout(order);
        for (int direction : {-1, 1})
        {
            auto *move = button(order, direction < 0 ? "上移" : "下移", [this, direction] {
                save();
                const int row = chapters->currentRow(), target = row + direction;
                if (target < 0 || target >= chapters->count()) return;
                book->moveChapter(chapter, direction);
                QSignalBlocker block(chapters);
                auto *item = chapters->takeItem(row);
                chapters->insertItem(target, item);
                chapters->setCurrentItem(item);
                for (int i : {row, target}) setChapterLabel(chapters->item(i), chapters->item(i)->toolTip());
                closeSheet();
                chapterPosition->setText(QString("第 %1 章 / 共 %2 章").arg(target + 1).arg(chapters->count()));
                status->setText("章节顺序已保存");
                refreshChapterMoves();
            });
            move->setObjectName(direction < 0 ? "chapterUp" : "chapterDown");
        }
        button(left, "＋ 新章节", [this] {
            save();
            beginSheet("新建章节");
            auto *name = sheet->field("章节名");
            auto *position = new DownwardCombo;
            position->addItems({"书末", "当前章之前", "当前章之后"});
            position->setObjectName("chapterInsertPosition");
            sheet->form->addWidget(new QLabel("插入位置"));
            sheet->form->addWidget(position);
            name->setFocus();
            button(sheet->form, "创建章节", [this, name, position] {
                const auto title = name->text().trimmed();
                if (title.isEmpty())
                    throw std::runtime_error("请填写章节名。");
                save();
                const int where = position->currentIndex();
                const int row = where ? chapters->currentRow() + int(where == 2) : chapters->count();
                const int id = book->insertChapter(title, where ? chapter : 0, where == 2);
                addChapterItem(id, title, row);
                for (int i = row + 1; i < chapters->count(); ++i)
                    setChapterLabel(chapters->item(i), chapters->item(i)->toolTip());
                chapters->setCurrentRow(row);
            });
        });
        auto *right = panelColumn(cols, 5);
        auto *titleBlock = new QVBoxLayout;
        titleBlock->setSpacing(2);
        auto *chapterHeader = new QHBoxLayout;
        right->addLayout(chapterHeader);
        auto *expand = new QPushButton("展开目录");
        chapterHeader->addWidget(expand);
        auto *directory = left->parentWidget();
        auto setCollapsed = [directory, expand](bool collapsed) {
            directory->setVisible(!collapsed);
            expand->setVisible(collapsed);
        };
        setCollapsed(QSettings().value("writing/chapterDirectoryCollapsed", false).toBool());
        connect(collapse, &QPushButton::clicked, this, [this, setCollapsed] {
            setCollapsed(true);
            if (!sheet)
                editor->setFocus();
            QSettings().setValue("writing/chapterDirectoryCollapsed", true);
        });
        connect(expand, &QPushButton::clicked, this, [setCollapsed] {
            setCollapsed(false);
            QSettings().setValue("writing/chapterDirectoryCollapsed", false);
        });
        heading->setTextFormat(Qt::PlainText);
        heading->setWordWrap(true);
        chapterHeader->addLayout(titleBlock, 1);
        chapterPosition->setObjectName("hint");
        auto *chapterMeta = new QHBoxLayout;
        titleBlock->addLayout(chapterMeta);
        chapterMeta->addWidget(chapterPosition);
        chapterLength->setObjectName("chapterWordCount");
        chapterLength->setToolTip("每个汉字、字母和数字各计一字，空白、标点和符号不计。");
        chapterMeta->addSpacing(10);
        chapterMeta->addWidget(chapterLength);
        chapterMeta->addStretch();
        connect(editor->document(), &QTextDocument::contentsChanged, this, [this] {
            chapterLength->setText(QString("本章 %1 字").arg(chapterWordCount(editor->toPlainText())));
        });
        titleBlock->addWidget(heading);
        button(chapterHeader, "改章名", [this] { renameChapter(); });
        auto *saveChapter = new QPushButton("保存");
        saveChapter->setObjectName("saveChapter");
        saveChapter->setToolTip("立即保存当前章节（Ctrl+S）");
        chapterHeader->addWidget(saveChapter);
        auto saveNow = [this] {
            save(Book::SaveMode::Checkpoint);
            status->confirm("当前章节");
        };
        connect(saveChapter, &QPushButton::clicked, this, [this, saveNow] { guard(saveNow); });
        connect(chapters, &QListWidget::itemDoubleClicked, this, [this] { guard([this] { renameChapter(); }); });
        right->addWidget(editor);
        heading->setObjectName("title");
        editor->setObjectName("manuscript");
        editor->setFont(QFont("Microsoft YaHei", 13));
        editor->setPlaceholderText("从这里开始写作…");
        editor->document()->setDocumentMargin(26);
        tools = addToolBar("写作");
        tools->setMovable(false);
        tool("返回资料工作区", [this] { enterDossier(dossier->currentPage()); }, false);
        tool("AI 写作", [this] { aiWriting(); }, false);
        tools->addSeparator();
        tool("人物名字", [this] { insertPerson(); });
        tool("属性面板", [this] { panel(); });
        tool("记录事件", [this] { record(); });
        tools->addSeparator();
        tool("剧情大事", [this] {
            beginSheet("剧情大事");
            auto *plots = new PlotPage;
            sheet->form->addWidget(plots, 1);
            plots->load(*book, chapter);
        });
        tool("时间轴", [this] { timeline(); });
        tool("关系图", [this] { graph(); });
        tool("历史版本", [this] { history(); });
        tool("资料与导出", [this] {
            beginSheet("资料与导出");
            button(sheet->form, "编辑人物档案（资料区）", [this] { enterDossier(1); });
            button(sheet->form, "编辑人物关系（资料区）", [this] { enterDossier(2); });
            button(sheet->form, "新建输出模板", [this] { newTemplate(); });
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
            status->edited();
        });
        connect(&status->pending, &QTimer::timeout, this, [this] { guard([this] { save(); }); });
        connect(qApp, &QGuiApplication::applicationStateChanged, this, [this](Qt::ApplicationState state) {
            if (state != Qt::ApplicationActive && dirty && pages->currentWidget() == writer)
                guard([this] { save(); });
            else if (state != Qt::ApplicationActive && pages->currentWidget() == dossier)
                dossier->savePending();
        });
        connect(new QShortcut(QKeySequence::Save, this), &QShortcut::activated, this, [this, saveNow] {
            guard([this, saveNow] {
                if (pages->currentWidget() == dossier)
                    dossier->saveNow();
                else if (pages->currentWidget() == writer)
                    saveNow();
            });
        });
        auto focusWriter = [this] {
            setWindowTitle(book->value("SELECT title FROM meta").toString() + " · 墨序");
            if (sheet)
                if (auto *view = sheet->findChild<QGraphicsView *>("writerRelationGraph"))
                    drawRelations(*view->scene());
            pages->setCurrentWidget(writer);
            tools->show();
            statusBar()->show();
            if (writerFocus && writerFocus->isVisible() && writerFocus->isEnabled())
                writerFocus->setFocus();
            else
                editor->setFocus();
        };
        dossier = new Dossier(
            sessionKeys, focusWriter, [this] { showShelf(); }, [this] { graph(); },
            [this](QString n) { setWindowTitle(n + " · 墨序"); },
            [this](int target, const QString &addition) {
                if (target != chapter)
                    return;
                QSignalBlocker block(editor);
                const int scroll = editor->verticalScrollBar()->value();
                const auto cursor = editor->textCursor();
                const int anchor = cursor.anchor(), pos = cursor.position();
                if (sheet)
                    insertion.setKeepPositionOnInsert(true);
                QTextCursor tail(editor->document());
                tail.movePosition(QTextCursor::End);
                tail.insertText(addition);
                if (sheet)
                    insertion.setKeepPositionOnInsert(false);
                QTextCursor restored(editor->document());
                restored.setPosition(anchor);
                restored.setPosition(pos, QTextCursor::KeepAnchor);
                editor->setTextCursor(restored);
                editor->verticalScrollBar()->setValue(scroll);
            });
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
                if (auto *assistantPage = dynamic_cast<WritingAssistant *>(pages->currentWidget()))
                    assistantPage->flushSummary();
                save();
            }))
            e->ignore();
        else
            e->accept();
    }
};
