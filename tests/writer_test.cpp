#include "dialog_watch.h"
#include "window.h"
#include <QTest>

int main(int argc, char **argv)
{
    QCoreApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
    QApplication app(argc, argv);
    DialogWatch dialogs;
    app.installEventFilter(&dialogs);
    applyTheme(app);
    QTemporaryDir dir;
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, dir.path());
    app.setOrganizationName("MoxuTests");
    app.setApplicationName("Editor");
    const auto path = dir.filePath("测试书.moxu");
    {
        Book book(path, "测试书");
        book.run("INSERT INTO chapters(title) VALUES('第二章')");
        book.run("DROP TABLE book_profile"); // File layout used in version 0.1.
    }
    QSettings().setValue("recent", QStringList{path});
    Window window;
    window.show();
    auto click = [&](QString text) {
        QApplication::processEvents();
        for (auto *b : window.findChildren<QPushButton *>())
            if (b->isVisible() && b->text() == text)
            {
                b->click();
                return true;
            }
        return false;
    };
    if (!click("使用默认目录") || QSettings().value("booksDir").toString() != defaultBooksPath())
        return 7;
    if (!click("设置保存目录"))
        return 8;
    if (!click("选择自定义目录…"))
        return 9;
    auto *directory = visibleControl<QLineEdit>(window, "filePath");
    directory->setText(path); // A file cannot be used as a directory.
    if (!click("使用此目录") || !visibleControl<QLabel>(window, "actionError")->text().contains("目录"))
        return 16;
    directory->setText(dir.path());
    if (!click("使用此目录") || QDir(QSettings().value("booksDir").toString()) != QDir(dir.path()))
        return 9;
    if (!click("新建书籍"))
        return 14;
    auto *title = visibleControl<QLineEdit>(window, "newBookTitle");
    title->setText("测试书");
    if (!click("创建书籍") || !visibleControl<QLabel>(window, "actionError")->text().contains("同名") ||
        title->text() != "测试书")
        return 17;
    auto *target = visibleControl<QLineEdit>(window, "filePath");
    target->setText(dir.filePath("新书.moxu"));
    if (!click("创建书籍") || !QFileInfo::exists(dir.filePath("新书.moxu")) || !click("返回书架"))
        return 14;
    {
        Book old(path), copy(dir.filePath("新书.moxu"));
        if (old.value("SELECT count(*) FROM chapters").toInt() != 2 ||
            copy.value("SELECT title FROM meta").toString() != "测试书")
            return 18;
    }
    if (!click("新建书籍"))
        return 19;
    visibleControl<QLineEdit>(window, "newBookTitle")->setText("未创建");
    if (!click("取消") || QFileInfo::exists(dir.filePath("未创建.moxu")))
        return 19;
    if (!click("打开书籍…"))
        return 20;
    visibleControl<QLineEdit>(window, "filePath")->setText(dir.filePath("不存在.moxu"));
    if (!click("打开书籍") || !visibleControl<QLabel>(window, "actionError")->text().contains("找不到"))
        return 20;
    auto *tree = visibleControl<QTreeView>(window);
    auto *model = qobject_cast<QFileSystemModel *>(tree->model());
    tree->setCurrentIndex(model->index(path));
    if (!click("打开书籍") || !click("返回书架"))
        return 21;
    auto lists = window.findChildren<QListWidget *>();
    auto recent = std::find_if(lists.begin(), lists.end(), [](auto *w) { return w->isVisible(); });
    if (recent == lists.end())
        return 1;
    auto coverFor = [&](const QString &bookPath) -> BookCover * {
        for (int row = 0; row < (*recent)->count(); ++row)
        {
            auto *item = (*recent)->item(row);
            if (QFileInfo(item->data(Qt::UserRole).toString()) == QFileInfo(bookPath))
                return static_cast<BookCover *>((*recent)->itemWidget(item));
        }
        throw std::runtime_error("未找到封面");
    };
    auto editCover = [&] {
        for (auto *action : coverFor(path)->findChild<QMenu *>()->actions())
            if (action->text() == "更换封面")
            {
                action->trigger();
                return;
            }
        throw std::runtime_error("未找到更换封面入口");
    };
    const auto picture = dir.filePath("本地封面.png");
    QImage sample(320, 180, QImage::Format_RGB32);
    sample.fill(QColor("#893b37"));
    if (!sample.save(picture))
        return 31;
    editCover();
    auto *imagePath = visibleControl<QLineEdit>(window, "filePath");
    imagePath->setText(path);
    if (visibleControl<QPushButton>(window, "primary")->isEnabled())
        return 32;
    imagePath->setText(picture);
    if (!click("取消") || !coverFor(path)->coverImage().isNull())
        return 33;
    editCover();
    visibleControl<QLineEdit>(window, "filePath")->setText(picture);
    if (!click("保存封面") || !QFile::remove(picture))
        return 34;
    if (coverFor(path)->coverImage().toImage().pixelColor(10, 10) != QColor("#893b37") ||
        !coverFor(dir.filePath("新书.moxu"))->coverImage().isNull())
        return 35;
    if (argc == 3 && !window.grab().save(QString::fromLocal8Bit(argv[2])))
        return 23;
    editCover();
    if (!click("恢复默认封面") || !click("保存封面") || !coverFor(path)->coverImage().isNull())
        return 36;
    auto books = (*recent)->findItems("测试书", Qt::MatchExactly);
    if (books.size() != 2)
        return 15;
    const auto duplicatePath = dir.filePath("新书.moxu");
    for (auto *item : books)
    {
        if (QFileInfo(item->data(Qt::UserRole).toString()) != QFileInfo(duplicatePath))
            continue;
        auto *cover = (*recent)->itemWidget(item);
        auto *more = cover->findChild<QToolButton *>();
        QTest::mouseClick(more, Qt::LeftButton);
        auto *menu = cover->findChild<QMenu *>();
        if (!menu->isVisible() || menu->actions().first()->text() != "打开本地目录")
            return 24;
        menu->hide();
        menu->actions().last()->trigger();
        auto *input = visibleControl<QLineEdit>(window, "deleteBookTitle");
        auto *confirm = visibleControl<QPushButton>(window, "primary");
        if (confirm->isEnabled())
            return 25;
        input->setText("新书");
        if (confirm->isEnabled())
            return 26;
        input->setText("测试书");
        if (!confirm->isEnabled())
            return 27;
        QLockFile busy(duplicatePath + ".lock");
        if (!busy.tryLock())
            return 28;
        confirm->click();
        if (!QFileInfo::exists(duplicatePath) || visibleControl<QLabel>(window, "actionError")->text().isEmpty())
            return 29;
        busy.unlock();
        confirm->click();
        break;
    }
    if (QFileInfo::exists(duplicatePath) || !QFileInfo::exists(path) || (*recent)->count() != 1)
        return 30;
    QTest::mouseClick((*recent)->itemWidget((*recent)->item(0)), Qt::LeftButton);
    auto *editor = window.findChild<QPlainTextEdit *>("manuscript");
    if (editor->isVisible() || !window.windowTitle().contains("测试书"))
        return 2;
    auto *nav = window.findChild<QListWidget *>("navigation");
    selectDossierPage(nav, 1);
    auto *name = window.findChild<QLineEdit *>("personName");
    auto *attrs = window.findChild<QPlainTextEdit *>("personAttrs");
    if (!name->isVisible() || !click("新建人物"))
        return 10;
    name->setText("林澈");
    attrs->setPlainText("境界：筑基\n宗门：青云宗");
    Book verify(path);
    auto *personStatus = visibleControl<QLabel>(window, "saveStatus");
    if (!QTest::qWaitFor(
            [&] {
                return verify.value("SELECT attributes FROM people WHERE name='林澈'").toString() ==
                       attrs->toPlainText();
            },
            4000))
        return 40;
    if (!click("保存人物") || !personStatus->text().startsWith("✓ 人物资料已保存"))
        return 11;
    window.activateWindow();
    attrs->setFocus();
    if (!QTest::qWaitFor([&] { return window.isActiveWindow() && attrs->hasFocus(); }, 3000))
        return 42;
    attrs->appendPlainText("剑修");
    QTest::keyClick(attrs, Qt::Key_S, Qt::ControlModifier);
    if (!personStatus->text().startsWith("✓ 人物资料已保存") ||
        verify.value("SELECT attributes FROM people WHERE name='林澈'").toString() != attrs->toPlainText())
        return 41;
    attrs->appendPlainText("身份：外门弟子");
    selectDossierPage(nav, 2);
    selectDossierPage(nav, 1);
    if (name->text() != "林澈" || !attrs->toPlainText().contains("外门弟子") || app.activeModalWidget())
        return 12;
    if (argc == 2 && !window.grab().save(QString::fromLocal8Bit(argv[1])))
        return 6;
    if (!click("进入专注写作") || !editor->isVisible())
        return 13;
    auto chapters = std::find_if(lists.begin(), lists.end(), [](auto *w) { return w->isVisible(); });
    if (chapters == lists.end())
        return 3;
    editor->insertPlainText("林澈获得了300积分。");
    auto *wordCount = window.findChild<QLabel *>("chapterWordCount");
    if (!wordCount || wordCount->text() != "本章 10 字")
        return 51;
    (*chapters)->setCurrentRow(1);
    if (!editor->toPlainText().isEmpty() || wordCount->text() != "本章 0 字")
        return 4;
    editor->insertPlainText("𠀀 BLG！\nS16");
    if (wordCount->text() != "本章 7 字")
        return 52;
    editor->undo();
    if (wordCount->text() != "本章 0 字")
        return 53;
    editor->insertPlainText("买下丹药。");
    QTest::qWait(2100);
    if (verify.value("SELECT body FROM chapters WHERE id=1").toString() != "林澈获得了300积分。" ||
        verify.value("SELECT body FROM chapters WHERE id=2").toString() != "买下丹药。")
        return 5;
    auto saved = [&] {
        return verify.value("SELECT body FROM chapters WHERE id=2").toString() == editor->toPlainText();
    };
    editor->insertPlainText("又得一枚。");
    QTest::keyClick(editor, Qt::Key_S, Qt::ControlModifier);
    if (!saved() || !visibleControl<QLabel>(window, "saveStatus")->text().startsWith("✓ 当前章节已保存"))
        return 37;
    editor->insertPlainText("收入囊中。");
    window.findChild<QPushButton *>("saveChapter")->click();
    if (!saved() || !visibleControl<QLabel>(window, "saveStatus")->text().startsWith("✓ 当前章节已保存"))
        return 38;
    editor->insertPlainText("转身下山。");
    app.applicationStateChanged(Qt::ApplicationInactive);
    if (!saved())
        return 39;
    if (!click("＋ 新章节")) return 54;
    visibleControl<QLineEdit>(window)->setText("山中番外");
    visibleControl<QComboBox>(window, "chapterInsertPosition")->setCurrentIndex(1);
    if (!click("创建章节") || (*chapters)->currentRow() != 1 ||
        (*chapters)->currentItem()->text() != "第 2 章 · 山中番外\n未发布" || !editor->toPlainText().isEmpty())
        return 55;
    editor->insertPlainText("山中小记。");
    auto *publication = visibleControl<QComboBox>(window, "chapterPublicationStatus");
    publication->setCurrentText("待发布");
    if (!click("上移") || (*chapters)->currentRow() != 0 || (*chapters)->item(1)->text() != "第 2 章 · 第一章\n未发布" ||
        editor->toPlainText() != "山中小记。" || verify.value("SELECT title FROM chapters WHERE position=1") != "山中番外")
        return 56;
    if (!click("下移") || (*chapters)->currentItem()->text() != "第 2 章 · 山中番外\n待发布" ||
        verify.value("SELECT body FROM chapters WHERE position=2") != "山中小记。")
        return 57;
    (*chapters)->setCurrentRow(0);
    if (publication->currentText() != "未发布") return 58;
    publication->setCurrentText("已发布");
    editor->setPlainText("山。 A2 \n");
    (*chapters)->setCurrentRow(2);
    editor->setPlainText("海！");
    for (auto *action : window.findChildren<QAction *>())
        if (action->text() == "返回资料工作区") action->trigger();
    selectDossierPage(nav, Dossier::overviewIndex);
    auto matchesProgress = [&](const QStringList &counts, const QStringList &words) {
        for (int i = 0; i < 4; ++i)
        {
            const auto suffix = QString::number(i);
            if (visibleControl<QLabel>(window, "progressChapters" + suffix)->text() != counts[i] + " 章" ||
                visibleControl<QLabel>(window, "progressWords" + suffix)->text() != words[i])
                return false;
        }
        return true;
    };
    if (!matchesProgress({"3","1","1","1"}, {"8","3","4","1"})) return 59;
    selectDossierPage(nav, Dossier::personIndex);
    verify.atomic([&] {
        const QString body(1200, QChar(u'文'));
        verify.run("UPDATE chapters SET body=?", {body});
        for (int i = 0; i < 97; ++i)
            verify.run("INSERT INTO chapters(title,body,publication_status) VALUES(?,?,?)",
                       {QString::number(i + 4), body, i < 9 ? "已发布" : i < 13 ? "待发布" : "未发布"});
        verify.run("UPDATE chapters SET body=body||'末' WHERE position=1");
    });
    selectDossierPage(nav, Dossier::overviewIndex);
    if (!matchesProgress({"100","10","5","85"}, {"12万","1.2万","6000","10.2万"}) ||
        visibleControl<QLabel>(window,"progressWords0")->toolTip() != "120001 字") return 60;
    if (argc == 3 && QString::fromLocal8Bit(argv[1]) == "--overview-preview" &&
        !window.grab().save(QString::fromLocal8Bit(argv[2]))) return 61;
    selectDossierPage(nav, Dossier::personIndex);
    verify.run("UPDATE chapters SET publication_status='已发布'");
    selectDossierPage(nav, Dossier::overviewIndex);
    if (!matchesProgress({"100","100","0","0"}, {"12万","12万","0","0"})) return 62;
    return dialogs.shown.isEmpty() ? 0 : 22;
}
