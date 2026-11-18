#include "window.h"
#include <QTest>

int main(int argc, char **argv)
{
    QCoreApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
    QApplication app(argc, argv);
    DialogSizing dialogs;
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
    QTimer::singleShot(50, [&] {
        auto *dlg = qobject_cast<QFileDialog *>(app.activeModalWidget());
        if (dlg)
        {
            dlg->setDirectory(dir.path());
            dlg->selectFile(dir.path());
            QMetaObject::invokeMethod(dlg, "accept");
        }
    });
    if (!click("选择自定义目录…") || QDir(QSettings().value("booksDir").toString()) != QDir(dir.path()))
        return 9;
    bool dialogSized = false;
    QTimer::singleShot(100, [&] {
        if (auto *dialog = qobject_cast<QInputDialog *>(app.activeModalWidget()))
        {
            dialogSized = dialog->width() >= 500 && dialog->height() >= 180;
            dialog->setTextValue("新书");
            dialog->accept();
        }
    });
    if (!click("新建书籍") || !dialogSized || !QFileInfo::exists(dir.filePath("新书.moxu")) || !click("返回书架"))
        return 14;
    auto lists = window.findChildren<QListWidget *>();
    auto recent = std::find_if(lists.begin(), lists.end(), [](auto *w) { return w->isVisible(); });
    if (recent == lists.end())
        return 1;
    auto books = (*recent)->findItems(path, Qt::MatchExactly);
    if (books.isEmpty())
        return 15;
    (*recent)->setCurrentItem(books.first());
    QTest::keyClick(*recent, Qt::Key_Return);
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
    return 0;
}
