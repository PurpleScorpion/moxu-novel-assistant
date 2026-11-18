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
    auto books = (*recent)->findItems("测试书", Qt::MatchExactly);
    if (books.size() != 2)
        return 15;
    if (argc == 3 && !window.grab().save(QString::fromLocal8Bit(argv[2])))
        return 23;
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
    nav->setCurrentRow(1);
    auto *name = window.findChild<QLineEdit *>("personName");
    auto *attrs = window.findChild<QPlainTextEdit *>("personAttrs");
    if (!name->isVisible() || !click("新建人物"))
        return 10;
    name->setText("林澈");
    attrs->setPlainText("境界：筑基\n宗门：青云宗");
    if (!click("保存人物"))
        return 11;
    attrs->appendPlainText("身份：外门弟子");
    nav->setCurrentRow(2);
    nav->setCurrentRow(1);
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
    (*chapters)->setCurrentRow(1);
    if (!editor->toPlainText().isEmpty())
        return 4;
    editor->insertPlainText("买下丹药。");
    QTest::qWait(2100);
    Book verify(path);
    if (verify.value("SELECT body FROM chapters WHERE id=1").toString() != "林澈获得了300积分。" ||
        verify.value("SELECT body FROM chapters WHERE id=2").toString() != "买下丹药。")
        return 5;
    return dialogs.shown.isEmpty() ? 0 : 22;
}
