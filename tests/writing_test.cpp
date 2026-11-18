#include "window.h"
#include <QTest>

namespace
{
void require(bool ok, const char *message)
{
    if (!ok)
        throw std::runtime_error(message);
}

template <class T> T *visible(QWidget &window)
{
    for (auto *widget : window.findChildren<T *>())
        if (widget->isVisible())
            return widget;
    throw std::runtime_error("找不到页面控件");
}

void click(QWidget &window, QString label)
{
    for (auto *button : window.findChildren<QPushButton *>())
        if (button->isVisible() && button->text() == label)
        {
            button->click();
            return;
        }
    throw std::runtime_error(("找不到按钮 " + label).toStdString());
}
} // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    applyTheme(app);
    QTemporaryDir dir;
    app.setOrganizationName("MoxuTests");
    app.setApplicationName("Writing");
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, dir.path());
    try
    {
        auto db = QSqlDatabase::addDatabase("QSQLITE", "moxu-personal-writing-library");
        db.setDatabaseName(dir.filePath("skills.sqlite"));
        require(db.open(), "无法建立测试库");
        const QString path = dir.filePath("甲.moxu");
        {
            Book old(path, "甲");
            old.run("DROP TABLE writing_notes"); // Simulate a 0.2 book.
            old.save(1, "甲在山下。");
        }
        QSettings().setValue("recent", QStringList{path});
        QSettings().setValue("booksDir", dir.path());
        Window window;
        window.show();
        auto *recent = visible<QListWidget>(window);
        recent->setCurrentRow(0);
        QTest::keyClick(recent, Qt::Key_Return);
        auto *nav = window.findChild<QListWidget *>("navigation");
        auto edit = [&](int page, QString name, QString content) {
            nav->setCurrentRow(page);
            click(window, page == 6 ? "新增大纲条目" : "新增 Skill");
            visible<QLineEdit>(window)->setText(name);
            visible<QPlainTextEdit>(window)->setPlainText(content);
            if (page == 6)
                visible<QComboBox>(window)->setCurrentIndex(1);
            click(window, "保存");
        };
        edit(4, "同名规则", "少用副词");
        edit(5, "同名规则", "对白用短句");
        edit(5, "未选规则", "禁止外发的资料");
        edit(6, "入山", "在山门遇到师父");
        {
            Book other(dir.filePath("乙.moxu"), "乙");
            require(WritingNotes(other, NoteScope::Global).list().size() == 1, "全局 Skill 未共享");
            require(WritingNotes(other, NoteScope::Book).list().isEmpty() &&
                        WritingNotes(other, NoteScope::Outline).list().isEmpty(),
                    "本书资料串书");
        }
        click(window, "进入专注写作");
        auto *editor = window.findChild<QPlainTextEdit *>("manuscript");
        auto cursor = editor->textCursor();
        cursor.setPosition(2);
        cursor.setPosition(4, QTextCursor::KeepAnchor);
        editor->setTextCursor(cursor);
        auto openAssistant = [&] {
            for (auto *a : window.findChildren<QAction *>())
                if (a->text() == "AI 写作")
                {
                    a->trigger();
                    return;
                }
            throw std::runtime_error("找不到 AI 写作入口");
        };
        openAssistant();
        auto *tree = visible<QTreeWidget>(window);
        require(tree->topLevelItem(1)->childCount() == 2, "本书 Skill 列表错误");
        for (int i = 0; i < 3; ++i)
            tree->topLevelItem(i)->child(0)->setCheckState(0, Qt::Checked);
        if (argc == 2)
            window.grab().save(QString::fromLocal8Bit(argv[1]));
        QTcpServer server;
        require(server.listen(QHostAddress::LocalHost), "无法开启本机模拟服务");
        QByteArray wire;
        QObject::connect(&server, &QTcpServer::newConnection, &server, [&] {
            auto *socket = server.nextPendingConnection();
            QObject::connect(socket, &QTcpSocket::readyRead, socket, [&, socket] {
                wire += socket->readAll();
                auto split = wire.indexOf("\r\n\r\n");
                if (split < 0)
                    return;
                auto length = QRegularExpression("(?im)^Content-Length: (\\d+)")
                                  .match(QString::fromLatin1(wire.first(split)))
                                  .captured(1)
                                  .toInt();
                if (wire.size() - split - 4 < length)
                    return;
                auto response = QByteArray("{\"choices\":[{\"message\":{\"content\":\"山门前\"}}]}");
                socket->write("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: " +
                              QByteArray::number(response.size()) + "\r\nConnection: close\r\n\r\n" + response);
                socket->disconnectFromHost();
            });
        });
        auto *service = visible<QComboBox>(window);
        service->setCurrentIndex(service->findData("local"));
        visible<QSpinBox>(window)->setValue(server.serverPort());
        for (auto *field : window.findChildren<QLineEdit *>())
            if (field->isVisible() && field->placeholderText() == "模型标识")
                field->setText("mock");
        click(window, "发送给 AI");
        QDeadlineTimer timeout(5000);
        QPushButton *accept = nullptr;
        for (auto *b : window.findChildren<QPushButton *>())
            if (b->text() == "采纳并替换选区")
                accept = b;
        require(accept, "无采纳入口");
        while (!accept->isEnabled() && !timeout.hasExpired())
            QTest::qWait(20);
        require(accept->isEnabled(), "本机接口未返回结果");
        auto payload = QJsonDocument::fromJson(wire.mid(wire.indexOf("\r\n\r\n") + 4)).object();
        auto turns = payload["messages"].toArray();
        auto rules = turns.first().toObject()["content"].toString(),
             input = turns.last().toObject()["content"].toString();
        require(rules.contains("少用副词") && rules.contains("对白用短句") && !rules.contains("禁止外发") &&
                    !input.contains("禁止外发"),
                "规则勾选错误");
        require(input.contains("在山门遇到师父") && input.contains("山下") && !input.contains("甲在") &&
                    !wire.contains("Authorization:"),
                "大纲、选区或认证范围错误");
        require(editor->toPlainText() == "甲在山下。", "未采纳修改了正文");
        accept->click();
        Book check(path);
        require(check.value("SELECT body FROM chapters WHERE id=1").toString() == "甲在山门前。", "选区替换未保存");
        server.close();
        openAssistant();
        click(window, "发送给 AI");
        QDeadlineTimer failureTimeout(5000);
        bool failed = false;
        while (!failed && !failureTimeout.hasExpired())
        {
            QTest::qWait(20);
            for (auto *label : window.findChildren<QLabel *>())
                if (label->isVisible() && label->text().contains("请求失败"))
                    failed = true;
        }
        require(failed, "没有显示连接失败状态");
        QTcpServer slow;
        require(slow.listen(QHostAddress::LocalHost), "无法启动取消测试");
        visible<QSpinBox>(window)->setValue(slow.serverPort());
        click(window, "发送给 AI");
        click(window, "取消请求");
        click(window, "返回正文");
        require(editor->isVisible() && editor->toPlainText() == "甲在山门前。", "取消后正文发生变化");
        auto outline = WritingNotes(check, NoteScope::Outline).list().first();
        require(outline.chapter == 1, "大纲未关联章节");
        auto library = WritingNotes(check, NoteScope::Global);
        auto outdated = library.list().first(), changed = outdated;
        changed.body = "多用动词";
        changed = library.save(changed);
        bool conflict = false;
        try
        {
            library.save(outdated);
        }
        catch (const std::exception &)
        {
            conflict = true;
        }
        require(conflict, "并发修改未被识别");
        library.remove(changed);
        require(library.list().isEmpty(), "删除未保存");
    }
    catch (const std::exception &e)
    {
        qCritical("%s", e.what());
        return 1;
    }
    QSqlDatabase::removeDatabase("moxu-personal-writing-library");
    return 0;
}
