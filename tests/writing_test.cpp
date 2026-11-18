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
        QSettings().setValue(
            "aiSites/deepseek",
            QByteArray(R"({"name":"DeepSeek","address":"https://api.deepseek.com","models":["deepseek-flash"]})"));
        const auto oldDeepSeek = aiSites().first();
        require(!oldDeepSeek.messagesApi() && oldDeepSeek.models == QStringList{"deepseek-flash"} &&
                    oldDeepSeek.endpoint() == QUrl("https://api.deepseek.com/chat/completions"),
                "旧版 DeepSeek 配置不兼容");
        Window window;
        QSettings().setValue(
            "aiSites/zai-messages",
            QByteArray(
                R"({"name":"Z.AI（ZCode 接口）","address":"https://api.z.ai/api/anthropic","models":["GLM-5.2","自选模型"],"protocol":"messages-bearer"})"));
        QSettings().setValue(
            "aiSites/zai",
            QByteArray(R"({"name":"我的国际站","address":"https://api.z.ai/api/paas/v4","models":["glm-5.2"]})"));
        window.show();
        QTcpServer server;
        require(server.listen(QHostAddress::LocalHost), "无法开启本机模拟服务");
        const auto port = server.serverPort();
        auto catalogHandler = QObject::connect(&server, &QTcpServer::newConnection, &server, [&] {
            auto *socket = server.nextPendingConnection();
            QObject::connect(socket, &QTcpSocket::readyRead, socket, [socket] {
                if (!socket->canReadLine())
                    return;
                const auto path = socket->readLine();
                auto payload = path.startsWith("GET /v1/models ")
                                   ? QByteArray(R"({"data":[{"id":"mock-2"},{"id":"mock-1"}]})")
                                   : QByteArray("{}");
                socket->write(
                    "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nConnection: close\r\nContent-Length: " +
                    QByteArray::number(payload.size()) + "\r\n\r\n" + payload);
                socket->disconnectFromHost();
            });
        });
        click(window, "AI 配置");
        auto *siteList = window.findChild<QListWidget *>("siteList");
        siteList->setCurrentItem(siteList->findItems("本机兼容服务", Qt::MatchExactly).first());
        auto *address = window.findChild<QLineEdit *>("siteAddress");
        auto *siteKey = window.findChild<QLineEdit *>("siteKey");
        siteKey->setText("dummy-local-test");
        address->setText(QString("http://127.0.0.1:%1/v1").arg(port));
        require(siteKey->text().isEmpty(), "地址变化后保留了旧密钥");
        auto *modelField = window.findChild<QPlainTextEdit *>("siteModels");
        modelField->setPlainText("mock-1");
        click(window, "从站点读取模型");
        require(QTest::qWaitFor([&] { return modelField->toPlainText() == "mock-1\nmock-2"; }, 3000),
                "无法从兼容站点读取并合并模型");
        QObject::disconnect(catalogHandler);
        click(window, "保存配置");
        require(window.findChild<QLabel *>("siteNotice")->text().contains("已保存"), "站点未保存");
        click(window, "新增站点");
        window.findChild<QLineEdit *>("siteName")->setText("测试站点");
        auto *protocol = window.findChild<QComboBox *>("siteProtocol");
        protocol->setCurrentIndex(protocol->findData("messages"));
        address->setText("https://example.org/v1");
        modelField->setPlainText("model-a\nmodel-b");
        click(window, "保存配置");
        require(siteList->currentItem()->text() == "测试站点" &&
                    aiSites().last().models == QStringList{"model-a", "model-b"},
                "新增站点或模型未保存");
        const auto custom = aiSites().last();
        QNetworkRequest customRequest(custom.endpoint());
        custom.setHeaders(customRequest, "test-only");
        require(custom.protocol == "messages" && custom.endpoint().path() == "/v1/messages" &&
                    customRequest.rawHeader("x-api-key") == "test-only" && !customRequest.hasRawHeader("Authorization"),
                "自定义站点的 Messages 协议或密钥认证未保存");
        siteList->setCurrentItem(siteList->findItems("智谱 GLM（国内）", Qt::MatchExactly).first());
        address->setText(QString("http://127.0.0.1:%1/v1").arg(port));
        click(window, "保存配置");
        require(siteList->findItems("我的国际站", Qt::MatchExactly).size() == 1, "升级覆盖了自定义站点名");
        auto renamed = siteList->findItems("智谱 GLM（ZCode 兼容）", Qt::MatchExactly);
        require(renamed.size() == 1, "旧版预设站点未显示新名称");
        siteList->setCurrentItem(renamed.first());
        require(modelField->toPlainText().contains("自选模型"), "重命名丢失已保存的模型");
        require(protocol->currentData() == "messages-bearer" && address->text() == "https://api.z.ai/api/anthropic",
                "ZCode 预设协议或地址错误");
        address->setText(QString("http://127.0.0.1:%1/api/anthropic").arg(port));
        siteKey->setText("local-only-test");
        click(window, "保存配置");
        siteList->setCurrentItem(siteList->findItems("本机兼容服务", Qt::MatchExactly).first());
        click(window, "返回");
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
        QByteArray wire;
        QObject::connect(&server, &QTcpServer::newConnection, &server, [&] {
            wire.clear();
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
                const auto request = QJsonDocument::fromJson(wire.mid(split + 4)).object();
                const bool messages = wire.startsWith("POST /api/anthropic/v1/messages ");
                if (!request["stream"].toBool())
                {
                    QByteArray response = R"({"choices":[{"message":{"content":"OK"}}]})";
                    if (messages)
                        response =
                            R"({"type":"message","stop_reason":"end_turn","content":[{"type":"text","text":"OK"}]})";
                    if (request["model"].toString() == "glm-5.3")
                    {
                        if (request["thinking"].toObject()["type"].toString() == "disabled")
                        {
                            socket->write("HTTP/1.1 400 Bad Request\r\nConnection: close\r\n\r\n");
                            socket->disconnectFromHost();
                            return;
                        }
                        response =
                            R"({"choices":[{"finish_reason":"length","message":{"reasoning_content":"思考","content":null}}]})";
                    }
                    socket->write("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: " +
                                  QByteArray::number(response.size()) + "\r\nConnection: close\r\n\r\n" + response);
                    socket->disconnectFromHost();
                    return;
                }
                QByteArray response = "data: {\"choices\":[{\"delta\":{\"reasoning_content\":\"思考\"}}]}\r\n\r\ndata: "
                                      "{\"choices\":[{\"delta\":{\"content\":\"山门前\"}}]}\n\ndata: [DONE]\n\n";
                if (messages)
                    response =
                        R"(data: {"type":"content_block_delta","delta":{"type":"thinking_delta","thinking":"内部思考"}}

data: {"type":"content_block_delta","delta":{"type":"text_delta","text":"山门开了。"}}

data: {"type":"message_stop"}

)";
                int cut = response.indexOf("山") + 1;
                socket->write("HTTP/1.1 200 OK\r\nContent-Type: text/event-stream\r\nContent-Length: " +
                              QByteArray::number(response.size()) + "\r\nConnection: close\r\n\r\n" +
                              response.first(cut));
                QTimer::singleShot(30, socket, [socket, rest = response.mid(cut)] {
                    socket->write(rest);
                    socket->disconnectFromHost();
                });
            });
        });
        auto *service = window.findChild<QComboBox *>("aiService");
        auto *models = window.findChild<QComboBox *>("aiModel");
        auto waitStatus = [&](const QString &message) {
            return QTest::qWaitFor(
                [&] {
                    for (auto *l : window.findChildren<QLabel *>())
                        if (l->isVisible() && l->text().contains(message))
                            return true;
                    return false;
                },
                5000);
        };
        require(service->currentData() == "local" && models->count() == 2 && !models->isEditable(),
                "未恢复书架站点与模型列表");
        auto checkDropdownBelow = [](QComboBox *combo, const char *message) {
            require(combo->count() >= 2, "方向测试需要至少两个选项");
            combo->setCurrentIndex(combo->count() - 1);
            QApplication::processEvents();
            combo->showPopup();
            require(QTest::qWaitFor([combo] { return combo->view()->isVisible(); }, 2000) &&
                        combo->view()->window()->frameGeometry().top() >=
                            combo->mapToGlobal(QPoint(0, combo->height())).y() - 3,
                    message);
            combo->hidePopup();
        };
        checkDropdownBelow(service, "选择末项后服务列表未向下展开");
        checkDropdownBelow(models, "选择末项后模型列表未向下展开");
        require(service->findData("zai") < 0 && service->findData("glm") < 0, "写作页重复列出智谱接口");
        service->setCurrentIndex(service->findText("智谱 GLM"));
        require(service->currentData() == "zai-messages", "智谱默认接口应为 ZCode 兼容");
        click(window, "测试连接");
        require(waitStatus("连接成功") && wire.contains("Bearer local-only-test") &&
                    wire.toLower().contains("anthropic-version: 2023-06-01") && !wire.contains("少用副词") &&
                    !wire.contains("山下"),
                "Anthropic 测试接口、认证或外发范围错误");
        click(window, "发送给 AI");
        require(waitStatus("候选正文已就绪") && visible<QPlainTextEdit>(window)->toPlainText() == "山门开了。",
                "Anthropic 流式正文错误");
        const auto anthPayload = QJsonDocument::fromJson(wire.mid(wire.indexOf("\r\n\r\n") + 4)).object();
        require(anthPayload["model"] == "GLM-5.2" && anthPayload["max_tokens"].toInt() > 0 &&
                    anthPayload["system"].toString().contains("少用副词") &&
                    anthPayload["messages"].toArray().size() == 1 &&
                    anthPayload["messages"].toArray().first().toObject()["role"] == "user",
                "Anthropic 写作请求格式错误");
        auto chooseGlm = [&](const QString &label, bool activate) {
            click(window, "AI 配置");
            auto *list = visible<QListWidget>(window);
            list->setCurrentItem(list->findItems(label, Qt::MatchExactly).first());
            if (activate)
                click(window, "保存并设为写作接口");
            click(window, "返回");
        };
        chooseGlm("我的国际站", false);
        require(service->currentData() == "zai-messages", "仅浏览配置切换了接口");
        chooseGlm("我的国际站", true);
        require(service->currentText() == "智谱 GLM" && service->currentData() == "zai" &&
                    models->findText("glm-5.2") >= 0 && service->findData("zai-messages") < 0,
                "国际通用接口切换失败");
        chooseGlm("智谱 GLM（国内）", true);
        require(service->currentText() == "智谱 GLM" && service->currentData() == "glm" && activeGlmSite() == "glm",
                "国内接口切换失败");
        models->setCurrentText("glm-5.3");
        click(window, "测试连接");
        require(waitStatus("连接成功") && waitStatus("仅返回思考内容") && !wire.contains("local-only-test"),
                "国内接口的模型或密钥错误");
        service->setCurrentIndex(service->findData("local"));
        models->setCurrentIndex(1);
        click(window, "测试连接");
        require(waitStatus("连接成功") && !wire.contains("少用副词") && !wire.contains("山下"),
                "测试请求带入书稿或未成功");
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
        require(payload["model"].toString() == "mock-2" && payload["stream"].toBool(), "选定模型或流式请求未生效");
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
        require(slow.listen(QHostAddress::LocalHost, port), "无法启动取消测试");
        click(window, "测试连接");
        auto *activeTimer = static_cast<QTimer *>(nullptr);
        for (auto *timer : window.findChildren<QTimer *>("requestDeadline"))
            if (timer->isActive())
                activeTimer = timer;
        require(activeTimer && QMetaObject::invokeMethod(activeTimer, "timeout") && waitStatus("请求超时"),
                "请求超时未退出");
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
