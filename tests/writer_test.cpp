#include "window.h"
#include <QTest>

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QTemporaryDir dir;
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, dir.path());
    app.setOrganizationName("MoxuTests");
    app.setApplicationName("Editor");
    const auto path = dir.filePath("测试书.moxu");
    {
        Book book(path, "测试书");
        book.run("INSERT INTO chapters(title) VALUES('第二章')");
    }
    QSettings().setValue("recent", QStringList{path});
    Window window;
    window.show();
    auto lists = window.findChildren<QListWidget *>();
    auto recent = std::find_if(lists.begin(), lists.end(), [](auto *w) { return w->isVisible(); });
    if (recent == lists.end())
        return 1;
    (*recent)->setCurrentRow(0);
    QTest::keyClick(*recent, Qt::Key_Return);
    auto *editor = window.findChild<QPlainTextEdit *>();
    if (!editor->isVisible() || !window.windowTitle().contains("测试书"))
        return 2;
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
    if (argc == 2 && !window.grab().save(QString::fromLocal8Bit(argv[1])))
        return 6;
    return 0;
}
