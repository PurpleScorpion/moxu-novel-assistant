#include "window.h"
#include <QTest>

namespace
{
void check(bool ok, const char *message)
{
    if (!ok)
        throw std::runtime_error(message);
}
class DialogWatch : public QObject
{
  public:
    QStringList shown;
    bool eventFilter(QObject *object, QEvent *event) override
    {
        if (event->type() == QEvent::Show)
            if (auto *dialog = qobject_cast<QDialog *>(object))
            {
                shown.append(dialog->windowTitle());
                QTimer::singleShot(0, dialog, &QDialog::reject);
            }
        return false;
    }
};
template <class T> QList<T *> visible(QWidget &parent)
{
    QList<T *> result;
    for (auto *widget : parent.findChildren<T *>())
        if (widget->isVisible())
            result.append(widget);
    return result;
}
void click(QWidget &parent, QString label)
{
    for (auto *button : visible<QPushButton>(parent))
        if (button->text() == label)
        {
            button->click();
            return;
        }
    throw std::runtime_error(("未找到按钮 " + label).toStdString());
}
} // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    applyTheme(app);
    app.setOrganizationName("MoxuTests");
    app.setApplicationName("Focus");
    QTemporaryDir dir;
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, dir.path());
    try
    {
        auto path = dir.filePath("甲.moxu");
        {
            Book book(path, "甲");
            book.run("INSERT INTO people(name,attributes) VALUES('林澈','境界：筑基'),('苏禾','境界：金丹')");
            book.run("INSERT INTO templates(title,pattern) VALUES('简表','{{姓名}} / {{境界}} / {{积分}}')");
            book.save(1, "开头原文结尾");
        }
        QSettings().setValue("recent", QStringList{path});
        QSettings().setValue("booksDir", dir.path());
        Window window;
        window.show();
        auto *recent = visible<QListWidget>(window).value(0);
        check(recent, "缺少书架");
        recent->setCurrentRow(0);
        QTest::keyClick(recent, Qt::Key_Return);
        click(window, "进入专注写作");
        DialogWatch watch;
        app.installEventFilter(&watch);
        auto *editor = window.findChild<QPlainTextEdit *>("manuscript");
        auto action = [&](QString title) {
            for (auto *a : window.findChildren<QAction *>())
                if (a->text() == title)
                {
                    a->trigger();
                    return;
                }
            throw std::runtime_error(("未找到工具 " + title).toStdString());
        };
        auto sheet = [&]() -> QWidget & {
            QApplication::processEvents();
            for (auto *w : window.findChildren<QWidget *>("writerSheet"))
                if (w->isVisible())
                    return *w;
            throw std::runtime_error("未显示写作侧栏");
        };
        auto preview = [&] { return visible<QPlainTextEdit>(sheet()).at(0); };
        auto cursor = editor->textCursor();
        cursor.setPosition(2);
        cursor.setPosition(4, QTextCursor::KeepAnchor);
        editor->setTextCursor(cursor);
        action("属性面板");
        auto options = visible<QComboBox>(sheet());
        check(options.size() == 2, "人物与模板应在同一页");
        options.at(0)->setCurrentIndex(1);
        options.at(1)->setCurrentIndex(1);
        check(preview()->toPlainText() == "苏禾 / 金丹 / 0", "切换后预览错误");
        preview()->setPlainText("取消草稿");
        click(sheet(), "收起");
        check(editor->toPlainText() == "开头原文结尾", "取消后正文变更");
        action("属性面板");
        preview()->setPlainText("面板");
        click(sheet(), "插入属性面板");
        check(editor->toPlainText() == "开头面板结尾", "选区插入错误");
        action("记录事件");
        auto fields = visible<QLineEdit>(sheet());
        auto *desc = fields.last();
        desc->setText("完成任务");
        auto *amount = visible<QSpinBox>(sheet()).first();
        amount->setValue(-5);
        click(sheet(), "保存事件");
        Book verify(path);
        check(verify.value("SELECT COUNT(*) FROM events").toInt() == 0 && editor->toPlainText() == "开头面板结尾",
              "失败事件未回滚");
        check(sheet().findChild<QLabel *>("sheetMessage")->isVisible() && desc->text() == "完成任务", "失败未保留表单");
        amount->setValue(300);
        check(preview()->toPlainText().contains("0 → 300"), "余额预览错误");
        if (argc == 2)
            check(window.grab().save(QString::fromLocal8Bit(argv[1])), "截图失败");
        click(sheet(), "保存事件");
        check(verify.balance(1, "积分", 1) == 300 && verify.value("SELECT body FROM chapters WHERE id=1").toString() ==
                                                         "开头面板完成任务（积分+300，剩余300）结尾",
              "事件及正文未一起保存");
        auto before = editor->toPlainText();
        action("记录事件");
        visible<QLineEdit>(sheet()).last()->setText("购买丹药");
        visible<QSpinBox>(sheet()).first()->setValue(-60);
        visible<QCheckBox>(sheet()).first()->setChecked(false);
        click(sheet(), "保存事件");
        check(verify.balance(1, "积分", 1) == 240 && editor->toPlainText() == before, "仅记账改变了正文");
        action("历史版本");
        check(preview()->toPlainText() == "开头面板结尾", "历史预览错误");
        click(sheet(), "用此版本恢复本章");
        check(editor->toPlainText() == "开头面板结尾", "历史恢复错误");
        action("时间轴");
        check(preview()->toPlainText().contains("240"), "时间轴错误");
        action("关系图");
        check(!visible<QGraphicsView>(sheet()).isEmpty(), "关系图未嵌入");
        action("资料与导出");
        click(sheet(), "新建输出模板");
        visible<QLineEdit>(sheet()).first()->setText("门派面板");
        click(sheet(), "保存模板");
        check(verify.value("SELECT count(*) FROM templates WHERE title='门派面板'").toInt() == 1, "模板未保存");
        action("资料与导出");
        click(sheet(), "整书基础校对");
        check(preview()->isReadOnly(), "校对结果缺失");
        action("资料与导出");
        click(sheet(), "导出全书 TXT");
        auto out = dir.filePath("导出.txt");
        {
            QFile file(out);
            check(file.open(QIODevice::WriteOnly), "无法建立导出样本");
            file.write("keep");
        }
        visible<QLineEdit>(sheet()).first()->setText(out);
        click(sheet(), "导出文本");
        {
            QFile file(out);
            file.open(QIODevice::ReadOnly);
            check(file.readAll() == "keep", "已有文件被静默覆盖");
        }
        visible<QCheckBox>(sheet()).first()->setChecked(true);
        click(sheet(), "导出文本");
        {
            QFile file(out);
            file.open(QIODevice::ReadOnly);
            check(file.readAll().contains("开头面板结尾"), "TXT 导出失败");
        }
        action("资料与导出");
        click(sheet(), "备份整本书…");
        auto copy = dir.filePath("备份.moxu");
        visible<QLineEdit>(sheet()).first()->setText(copy);
        click(sheet(), "保存备份");
        {
            Book backup(copy);
            check(backup.balance(1, "积分", 1) == 240, "备份缺少事件");
        }
        click(window, "＋ 新章节");
        visible<QLineEdit>(sheet()).first()->setText("第二章");
        click(sheet(), "创建章节");
        action("人物名字");
        click(sheet(), "插入姓名");
        check(editor->toPlainText() == "林澈", "新章节人物名插入失败");
        check(watch.shown.isEmpty(), "专注写作中出现了对话框");
    }
    catch (const std::exception &e)
    {
        fprintf(stderr, "%s\n", e.what());
        return 1;
    }
    return 0;
}
