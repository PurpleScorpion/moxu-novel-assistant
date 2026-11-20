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

void answerReplacement(const QString &action, bool confirm)
{
    auto *dialog = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
    require(dialog && dialog->button(QMessageBox::Ok)->text() == action &&
                dialog->defaultButton() == dialog->button(QMessageBox::Cancel), "替换确认弹窗或默认选项错误");
    dialog->button(confirm ? QMessageBox::Ok : QMessageBox::Cancel)->click();
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
        const auto changes = ChapterDiff::render("不变\n从 S5 起七年。\n收尾", "不变\n从 S5 起十一年。\n收尾");
        require(changes.contains(">七</span>") && changes.contains(">十一</span>") && !changes.contains("收尾"),
                "没有准确标记段内改字或重复展示未改段落");
        require(ChapterDiff::render("", "<山门>").contains("&lt;山门&gt;") &&
                    ChapterDiff::render("正文\n", "正文").contains("（空行）") &&
                    ChapterDiff::render("相同", "相同").contains("完全一致"),
                "特殊字符、空行或相同文本的对比显示错误");
        {
            QWidget host(nullptr, Qt::FramelessWindowHint);
            DownwardCombo combo;
            combo.setParent(&host);
            const auto desktop = QGuiApplication::primaryScreen()->availableGeometry();
            host.setGeometry(desktop.center().x() - 100, desktop.bottom() - 95, 200, 92);
            combo.setGeometry(6, 12, 180, 32);
            for (int i = 0; i < 80; ++i)
                combo.addItem(QString("第 %1 章").arg(i + 1));
            host.show();
            for (bool editable : {false, true})
            {
                combo.setEditable(editable);
                combo.setCurrentIndex(79);
                combo.showPopup();
                require(QTest::qWaitFor([&] { return combo.view()->isVisible(); }), "长列表未展开");
                const auto rect = combo.view()->window()->frameGeometry();
                require(rect.top() >= combo.mapToGlobal(QPoint(0, combo.height())).y() - 1 &&
                            rect.bottom() <= desktop.bottom() + 1 && combo.view()->verticalScrollBar()->maximum() > 0,
                        "屏幕底部的长列表未在下方限高滚动");
                QTest::keyClick(combo.view(), Qt::Key_Up);
                QTest::keyClick(combo.view(), Qt::Key_Return);
                require(combo.currentIndex() == 78, "下拉列表未保留键盘选项操作");
            }
        }
        auto db = QSqlDatabase::addDatabase("QSQLITE", "moxu-personal-writing-library");
        db.setDatabaseName(dir.filePath("skills.sqlite"));
        require(db.open(), "无法建立测试库");
        const QString path = dir.filePath("甲.moxu");
        {
            Book old(path, "甲");
            old.run("DROP TABLE writing_notes"); // Simulate a 0.2 book.
            old.save(1, "甲在山下。");
            old.run("INSERT INTO chapters(id,title,body) VALUES(4,'渡河',''),(9,'入城',''),(12,'尾声','未来剧情标记')");
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
        auto *limitModel = window.findChild<QComboBox *>("limitModel");
        auto *outputLimit = window.findChild<QButtonGroup *>("modelOutputLimits");
        limitModel->setCurrentText("mock-2");
        outputLimit->button(16384)->click();
        limitModel->setCurrentText("mock-1");
        require(outputLimit->checkedId() == 0, "一个模型的输出上限影响了另一个模型");
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
        limitModel->setCurrentText("GLM-5.2");
        require(outputLimit->checkedId() == 0 && outputLimit->checkedButton()->text().contains("4,096"),
                "旧版兼容接口默认输出上限错误");
        outputLimit->button(8192)->click();
        require(modelField->toPlainText().contains("自选模型"), "重命名丢失已保存的模型");
        require(protocol->currentData() == "messages-bearer" && address->text() == "https://api.z.ai/api/anthropic",
                "ZCode 预设协议或地址错误");
        address->setText(QString("http://127.0.0.1:%1/api/anthropic").arg(port));
        siteKey->setText("local-only-test");
        click(window, "保存配置");
        siteList->setCurrentItem(siteList->findItems("本机兼容服务", Qt::MatchExactly).first());
        limitModel->setCurrentText("mock-2");
        require(outputLimit->checkedId() == 16384, "切换站点后原模型的输出上限未恢复");
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
        Book verify(path);
        for (auto page : {4, 5, 6})
        {
            nav->setCurrentRow(page);
            auto *body = visible<QPlainTextEdit>(window);
            const QString original = body->toPlainText();
            body->appendPlainText("自动保存测试");
            const auto scope = page == 4 ? NoteScope::Global : page == 5 ? NoteScope::Book : NoteScope::Outline;
            auto stored = [&] { return WritingNotes(verify, scope).list().last().body; };
            require(QTest::qWaitFor([&] { return stored() == body->toPlainText(); }, 4000),
                    "资料未定时落盘或切页后选错条目");
            body->setPlainText(original + "！");
            nav->setCurrentRow(1);
            nav->setCurrentRow(page);
            require(body->toPlainText() == original + "！", "切回页面丢失修改");
            body->setPlainText(original);
            window.activateWindow();
            body->setFocus();
            require(QTest::qWaitFor([&] { return body->hasFocus() && window.isActiveWindow(); }, 3000),
                    "快捷键测试未获得编辑器焦点");
            QTest::keyClick(body, Qt::Key_S, Qt::ControlModifier);
            require(stored() == original, "资料快捷保存失败");
            auto feedback = [&] {
                for (auto *label : window.findChildren<QLabel *>("saveStatus"))
                    if (label->isVisible() && label->text().startsWith("✓ " + scopeName(scope)))
                        return true;
                return false;
            };
            require(feedback(), "资料快捷保存无反馈");
            click(window, "保存");
            require(feedback(), "重复点击保存无反馈");
        }
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
        auto writingSources = [&]() {
            for (auto *list : window.findChildren<QListWidget *>("writingSources"))
                if (list->isVisible()) return static_cast<WritingSources *>(list);
            throw std::runtime_error("缺少写作资料");
        };
        auto *tree = writingSources();
        require(tree->sections[WritingSources::BookSkill].entries.size() == 2, "本书 Skill 列表错误");
        for (int i = 0; i < 3; ++i)
            tree->sections[i].entries.first()->setCheckState(Qt::Checked);
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
                const auto lastInput = request["messages"].toArray().last().toObject()["content"].toString();
                if (lastInput == "测试截断" || lastInput.contains("修改本章全文，保留原意和情节"))
                    response = "data: {\"choices\":[{\"delta\":{\"content\":\"山\"},\"finish_reason\":\"length\"}]}\n\n"
                               "data: [DONE]\n\n";
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
        auto *draft = window.findChild<QPlainTextEdit *>("candidateDraft");
        auto *feedback = window.findChild<QPlainTextEdit *>("revisionFeedback");
        require(waitStatus("候选正文已就绪") && draft->toPlainText() == "山门开了。",
                "Anthropic 流式正文错误");
        const auto anthPayload = QJsonDocument::fromJson(wire.mid(wire.indexOf("\r\n\r\n") + 4)).object();
        require(anthPayload["model"] == "GLM-5.2" && anthPayload["max_tokens"].toInt() == 8192 &&
                    anthPayload["system"].toString().contains("少用副词") &&
                    anthPayload["messages"].toArray().size() == 1 &&
                    anthPayload["messages"].toArray().first().toObject()["role"] == "user",
                "Anthropic 写作请求格式错误");
        auto sentMessages = [&] {
            return QJsonDocument::fromJson(wire.mid(wire.indexOf("\r\n\r\n") + 4)).object()["messages"].toArray();
        };
        auto *compression = window.findChild<QCheckBox *>("compressRevisionHistory");
        require(compression && compression->isChecked(), "修订上下文应默认压缩");
        const QString oldDraft = QStringLiteral("作者手改的初稿") + QString(2500, QChar(u'雪'));
        draft->setPlainText(oldDraft);
        feedback->setPlainText("天气按十一月中旬处理");
        click(window, "发送修改意见");
        require(feedback->toPlainText().isEmpty(), "发送后旧意见仍留在输入框");
        require(waitStatus("第 1 轮修改已完成") && feedback->toPlainText().isEmpty() && sentMessages().size() == 3 &&
                    sentMessages()[1].toObject()["role"] == "assistant" &&
                    sentMessages()[1].toObject()["content"] == oldDraft &&
                    sentMessages()[2].toObject()["content"] == "天气按十一月中旬处理",
                "首次追改没有携带当前候选稿和修改意见");
        const QString latest = "山门开了。\n作者手改了结尾。";
        const QString opinion = "  观赛时长改成十一年\n保留结尾。  ";
        draft->setPlainText(latest);
        feedback->setPlainText(opinion);
        auto *revisionTabs = visible<QTabWidget>(window);
        revisionTabs->setCurrentIndex(1);
        auto *revisionPreview = qobject_cast<QPlainTextEdit *>(revisionTabs->currentWidget());
        require(revisionPreview && revisionPreview->toPlainText().contains("早期修订摘要　≈") &&
                    revisionPreview->toPlainText().contains(latest) && !revisionPreview->toPlainText().contains(oldDraft),
                "预览压缩或摘要用量错误");
        compression->setChecked(false);
        require(revisionPreview->toPlainText().contains(oldDraft), "关闭压缩后预览未恢复早期候选稿");
        compression->setChecked(true);
        click(window, "发送修改意见");
        require(waitStatus("第 2 轮修改已完成") && sentMessages().size() == 3 &&
                    sentMessages()[0].toObject()["content"].toString().contains("第 1 轮：天气按十一月中旬处理") &&
                    !wire.contains(oldDraft.toUtf8()) && sentMessages()[1].toObject()["content"] == latest &&
                    sentMessages()[2].toObject()["content"] == opinion,
                "再次追改应发送早期摘要、手改候选稿和本轮意见");
        compression->setChecked(false);
        feedback->setPlainText("调整末句");
        click(window, "发送修改意见");
        require(waitStatus("第 3 轮修改已完成") && sentMessages().size() == 7 &&
                    sentMessages()[1].toObject()["content"] == oldDraft &&
                    sentMessages()[4].toObject()["content"] == opinion && !wire.contains("早期修订摘要"),
                "关闭压缩后发送的完整历史有误");
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
        visible<QTabWidget>(window)->setCurrentIndex(0);
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
        require(payload["model"].toString() == "mock-2" && payload["stream"].toBool() &&
                    payload["max_tokens"].toInt() == 16384, "选定模型、输出上限或流式请求未生效");
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
        feedback->setPlainText("保留结尾");
        click(window, "发送修改意见");
        require(waitStatus("第 1 轮修改已完成") && sentMessages().size() == 4 &&
                    sentMessages()[0].toObject()["role"] == "system" &&
                    sentMessages()[2].toObject()["content"] == "山门前" &&
                    sentMessages()[3].toObject()["content"] == "保留结尾", "新任务未重置对话或通用接口追改错误");
        compression->setChecked(true);
        feedback->setPlainText("测试截断");
        click(window, "发送修改意见");
        require(waitStatus("候选正文可能没有写完") && draft->toPlainText() == "山" &&
                    feedback->toPlainText().isEmpty() && sentMessages().size() == 4 &&
                    sentMessages()[1].toObject()["content"].toString().contains("第 1 轮：保留结尾"),
                "通用接口修订压缩或截断提示错误");
        compression->setChecked(false);
        draft->setPlainText("山门前");
        server.close();
        feedback->setPlainText("再改一次");
        click(window, "发送修改意见");
        require(waitStatus("请求失败") && feedback->toPlainText() == "再改一次" && draft->toPlainText() == "山门前" &&
                    !draft->isReadOnly() && accept->isEnabled(), "修改失败丢失上一版候选正文或修改意见");
        accept->click();
        Book check(path);
        answerReplacement("确认替换", false);
        require(draft->isVisible() && draft->toPlainText() == "山门前" &&
                    check.value("SELECT body FROM chapters WHERE id=1") == "甲在山下。", "取消替换时修改了正文或离开了候选稿");
        accept->click();
        answerReplacement("确认替换", true);
        require(editor->isVisible() && check.value("SELECT body FROM chapters WHERE id=1") == "甲在山门前。", "确认替换后未保存或未返回正文");
        require(server.listen(QHostAddress::LocalHost, port), "无法启动整章改稿测试");
        const QString passage = "旧第一段𠀀。\n\n旧第二段。";
        const QString prefix = passage + "\n分界。\n", suffix = "\n尾声。";
        const QString source = prefix + passage + suffix;
        editor->setPlainText(source);
        auto reverse = editor->textCursor();
        reverse.setPosition(prefix.size() + passage.size());
        reverse.setPosition(prefix.size(), QTextCursor::KeepAnchor);
        editor->setTextCursor(reverse);
        openAssistant();
        auto *reopenedCompression = window.findChild<QCheckBox *>("compressRevisionHistory");
        require(reopenedCompression && !reopenedCompression->isChecked(), "重新打开写作页丢失压缩设置");
        reopenedCompression->setChecked(true);
        click(window, "发送给 AI");
        require(waitStatus("候选正文已就绪"), "选段润色未生成候选稿");
        auto selectedRequest = sentMessages().last().toObject()["content"].toString();
        require(selectedRequest.endsWith("正文上下文\n" + passage) && !selectedRequest.contains("分界") &&
                    !selectedRequest.contains("尾声"), "选段润色带入了高亮以外的原文");
        auto *revised = qobject_cast<QPlainTextEdit *>(visible<QTabWidget>(window)->currentWidget());
        require(revised && revised->objectName() == "candidateDraft", "缺少选段候选稿");
        const QString polished = "润色一。\n润色二。";
        revised->setPlainText(polished);
        editor->setPlainText(source + "后续改动");
        click(window, "采纳并替换选区");
        answerReplacement("确认替换", true);
        require(revised->isVisible() && editor->toPlainText() == source + "后续改动",
                "原文已变化时仍然写入了旧选区");
        // Reloading the same text invalidates live QTextCursor anchors, even though the manuscript is identical.
        editor->setPlainText(source);
        click(window, "采纳并替换选区");
        answerReplacement("确认替换", true);
        require(editor->toPlainText() == prefix + polished + suffix &&
                    check.value("SELECT body FROM chapters WHERE id=1").toString() == editor->toPlainText(),
                "多段润色没有准确替换最初选中的第二处原文");
        editor->undo();
        require(editor->toPlainText() == source, "撤销选区替换没有恢复原文");
        const QString originalChapter = "章首\n" + QString(3200, QChar(u'甲')) + "\n章末";
        editor->setPlainText(originalChapter);
        auto middle = editor->textCursor();
        middle.setPosition(1500);
        editor->setTextCursor(middle);
        openAssistant();
        click(window, "发送给 AI");
        require(waitStatus("候选正文已就绪") && !sentMessages().last().toObject()["content"].toString().contains("正文上下文") &&
                    !wire.contains("章首") && !wire.contains("甲甲") && !wire.contains("章末"),
                "仅有光标时仍然发送了章节正文");
        click(window, "采纳并插入光标处");
        require(editor->toPlainText() == originalChapter.left(1500) + "山门前" + originalChapter.mid(1500),
                "无选区创作没有插入原光标处");
        editor->undo();
        editor->setTextCursor(middle);
        openAssistant();
        click(window, "使用本章全文");
        auto *checkedSources = writingSources();
        auto *plotA = checkedSources->addEntry(WritingSources::Outline, "视角", "山中");
        auto *plotB = checkedSources->addEntry(WritingSources::Outline, "转折", "回到山门");
        plotA->setCheckState(Qt::Checked);
        plotB->setCheckState(Qt::Checked);
        auto *previewTabs = visible<QTabWidget>(window);
        previewTabs->setCurrentIndex(1);
        const auto *budgetPreview = qobject_cast<QPlainTextEdit *>(previewTabs->currentWidget());
        require(budgetPreview && budgetPreview->toPlainText().contains("视角　≈ 2 token") &&
                    budgetPreview->toPlainText().contains("转折　≈ 3 token") &&
                    budgetPreview->toPlainText().contains("本章全文 · 第 1 章") &&
                    budgetPreview->toPlainText().contains("当前输出档位　16,384 token"), "发送预览的分项估算或输出档位错误");
        plotB->setCheckState(Qt::Unchecked);
        require(!budgetPreview->toPlainText().contains("转折　≈"), "取消勾选后仍计算了该大纲");
        click(window, "发送给 AI");
        require(waitStatus("候选正文已就绪") && !wire.contains("本次输入约") && !wire.contains("当前输出档位") &&
                    sentMessages().last().toObject()["content"].toString().contains(originalChapter),
                "整章改稿未发送完整上下文");
        auto *chapterDraft = qobject_cast<QPlainTextEdit *>(visible<QTabWidget>(window)->currentWidget());
        require(chapterDraft && chapterDraft->objectName() == "candidateDraft", "找不到整章候选稿");
        chapterDraft->setPlainText("新开头\n新结尾。");
        auto *draftTabs = visible<QTabWidget>(window);
        auto *diff = draftTabs->findChild<QTextBrowser *>("chapterComparison");
        QPushButton *overwrite = nullptr;
        for (auto *button : window.findChildren<QPushButton *>())
            if (button->isVisible() && button->text() == "覆盖本章") overwrite = button;
        require(overwrite && overwrite->isEnabled() && waitStatus("候选正文可能没有写完"), "达到上限的候选稿应提示并允许覆盖本章");
        require(diff && draftTabs->isTabVisible(draftTabs->indexOf(diff)), "本地对比标签未显示");
        draftTabs->setCurrentWidget(diff);
        require(diff->isReadOnly() && !overwrite->isVisible(), "整章对比应仅供查看");
        require(diff->toPlainText().contains("原文 3204 字") && diff->toPlainText().contains("新稿 6 字") &&
                    diff->toPlainText().contains("－ 原文") && diff->toPlainText().contains("＋ 新稿") &&
                    diff->toPlainText().contains("章末") && diff->toPlainText().contains("新结尾。") &&
                    check.value("SELECT body FROM chapters WHERE id=1").toString() == originalChapter,
                "整章对比内容或字数错误，或预览时提前覆盖了原文");
        draftTabs->setCurrentWidget(chapterDraft);
        chapterDraft->appendPlainText("BLG 𠀀！");
        require(waitStatus("候选稿 10 字"), "候选稿编辑后字数未同步");
        draftTabs->setCurrentWidget(diff);
        require(diff->toPlainText().contains("新稿 10 字") && diff->toPlainText().contains("BLG 𠀀！"),
                "再次对比显示过期草稿");
        draftTabs->setCurrentWidget(chapterDraft);
        click(window, "覆盖本章");
        answerReplacement("确认覆盖", false);
        require(chapterDraft->isVisible() && check.value("SELECT body FROM chapters WHERE id=1") == originalChapter,
                "取消整章覆盖时修改了正文或离开了候选稿");
        click(window, "覆盖本章");
        answerReplacement("确认覆盖", true);
        require(editor->isVisible() && editor->toPlainText() == "新开头\n新结尾。\nBLG 𠀀！" &&
                    window.findChild<QLabel *>("chapterWordCount")->text() == "本章 10 字" &&
                    check.value("SELECT body FROM chapters WHERE id=1").toString() == editor->toPlainText() &&
                    check.value("SELECT body FROM revisions WHERE chapter=1 ORDER BY id DESC LIMIT 1") == originalChapter,
                "覆盖全文未完整替换、保存或记录旧稿");
        editor->undo();
        require(editor->toPlainText() == originalChapter, "整章覆盖无法一步撤销");
        auto *directory = window.findChild<QListWidget *>("chapterList");
        directory->setCurrentRow(1);
        openAssistant();
        auto priorGroup = [&] { return writingSources()->sections[WritingSources::Full].entries; };
        auto prior = priorGroup();
        require(prior.size() == 1 && prior.at(0)->checkState() == Qt::Unchecked,
                "第二章应仅提供第一章且由作者选择");
        click(window, "引用上一章");
        click(window, "发送给 AI");
        require(waitStatus("候选正文已就绪"), "引用上一章后生成失败");
        auto submitted = sentMessages().last().toObject()["content"].toString();
        require(submitted.contains(originalChapter) && submitted.contains("第 2 章 · 渡河") &&
                    !submitted.contains("未来剧情标记"), "第二章未正确引用第一章全文");
        auto *secondDraft = qobject_cast<QPlainTextEdit *>(visible<QTabWidget>(window)->currentWidget());
        require(secondDraft, "缺少续写候选正文");
        secondDraft->setPlainText("她已将令牌交给渡船人。");
        click(window, "采纳并插入光标处");
        require(check.value("SELECT body FROM chapters WHERE id=1") == originalChapter &&
                    check.value("SELECT body FROM chapters WHERE id=4") == "她已将令牌交给渡船人。",
                "采纳续写时写入了错误章节");
        directory->setCurrentRow(2);
        openAssistant();
        prior = priorGroup();
        require(prior.size() == 2 && prior.at(0)->text() == "第 2 章 · 渡河",
                "多章选择的章节范围或编号错误");
        auto *materials = writingSources();
        for (int i = 0; i < 5; ++i)
            materials->addEntry(WritingSources::Outline, QString("剧情条目 %1").arg(i));
        const auto *global = materials->sections[WritingSources::GlobalSkill].entries.first();
        materials->sections[WritingSources::GlobalSkill].entries.first()->setCheckState(Qt::Checked);
        auto toggleGroup = [&](WritingSources::Group id) {
            const auto &group = materials->sections[id];
            materials->scrollToItem(group.heading);
            QApplication::processEvents();
            const bool expanded = group.expanded;
            QTest::mouseClick(materials->viewport(), Qt::LeftButton, Qt::NoModifier,
                              materials->visualItemRect(group.heading).center());
            QApplication::processEvents();
            require(group.expanded != expanded && group.entries.first()->isHidden() == expanded,
                    "点击分组后没有正确收起或展开资料");
        };
        for (int i = 0; i < 3; ++i)
        {
            toggleGroup(WritingSources::BookSkill);
            toggleGroup(WritingSources::GlobalSkill);
            toggleGroup(WritingSources::Outline);
            require(global->checkState() == Qt::Checked && waitStatus("已选 1 项"), "收起后丢失资料勾选");
            toggleGroup(WritingSources::Outline);
            toggleGroup(WritingSources::GlobalSkill);
            toggleGroup(WritingSources::BookSkill);
        }
        for (auto id : {WritingSources::GlobalSkill, WritingSources::BookSkill, WritingSources::Outline})
        {
            auto *entry = materials->sections[id].entries.first();
            require(entry->toolTip().isEmpty(), "资料条目仍含正文悬停提示");
            materials->scrollToItem(entry);
            auto pos = materials->visualItemRect(entry).center();
            QHelpEvent hover(QEvent::ToolTip, pos, materials->viewport()->mapToGlobal(pos));
            QApplication::sendEvent(materials->viewport(), &hover);
            require(!QToolTip::isVisible(), "鼠标悬停时出现资料提示");
        }
        materials->sections[WritingSources::GlobalSkill].entries.first()->setCheckState(Qt::Unchecked);
        auto *child = materials->sections[WritingSources::BookSkill].entries.first();
        materials->scrollToItem(child);
        const auto row = materials->visualItemRect(child);
        auto clickRowAt = [&](int offset) {
            QTest::mouseClick(materials->viewport(), Qt::LeftButton, Qt::NoModifier,
                              QPoint(row.left() + offset, row.center().y()));
        };
        clickRowAt(10);
        require(child->checkState() == Qt::Unchecked, "缩进留白误触了复选框");
        clickRowAt(31);
        require(child->checkState() == Qt::Checked, "缩进后的复选框无法点击");
        clickRowAt(31);
        click(window, "引用上一章");
        prior.at(1)->setCheckState(Qt::Checked);
        auto *writingTabs = visible<QTabWidget>(window);
        writingTabs->setCurrentIndex(1);
        auto *requestPreview = qobject_cast<QPlainTextEdit *>(writingTabs->currentWidget());
        require(requestPreview && requestPreview->toPlainText().contains(originalChapter) &&
                    requestPreview->toPlainText().contains("前章全文 · 第 2 章 · 渡河　≈") &&
                    requestPreview->toPlainText().contains("她已将令牌交给渡船人。"), "预览中缺少选定章节");
        click(window, "测试连接");
        require(waitStatus("连接成功") && !wire.contains("令牌") && !wire.contains("章首"), "测试连接外发了前文章节");
        click(window, "发送给 AI");
        require(waitStatus("候选正文已就绪"), "引用多章后生成失败");
        submitted = sentMessages().last().toObject()["content"].toString();
        require(submitted.contains("第 3 章 · 入城") && submitted.contains(originalChapter) &&
                    submitted.indexOf("她已将令牌交给渡船人。") > submitted.indexOf(originalChapter) &&
                    !submitted.contains("未来剧情标记"), "多章参考缺失、乱序或含有未来正文");
        writingTabs->setCurrentIndex(0);
        prior.at(1)->setCheckState(Qt::Unchecked);
        click(window, "发送给 AI");
        require(waitStatus("候选正文已就绪") && !wire.contains("章首") && wire.contains("她已将令牌交给渡船人。"),
                "取消勾选后仍然发送了该章");
        auto *summaryBox = writingTabs->findChild<QTableWidget *>("summaryChapter");
        auto *summaryText = writingTabs->findChild<QPlainTextEdit *>("chapterSummaryText");
        auto openSummaries = [&] {
            for (int i = 0; i < writingTabs->count(); ++i)
                if (writingTabs->tabText(i) == "剧情摘要") writingTabs->setCurrentIndex(i);
        };
        openSummaries();
        summaryBox->setCurrentCell(0, 0);
        click(window, "生成剧情摘要");
        require(waitStatus("剧情摘要已生成") && summaryText->toPlainText() == "【" + summaryBox->currentItem()->text() + "】\n山门前" &&
                    sentMessages().last().toObject()["content"] == originalChapter &&
                    !wire.contains("她已将令牌交给渡船人。"), "生成摘要时混入写作资料或未返回可编辑摘要");
        summaryText->selectAll();
        summaryText->insertPlainText("她在山门取得令牌。");
        summaryBox->setCurrentCell(1, 0);
        require(summaryText->toPlainText().isEmpty() &&
                    check.value("SELECT body FROM chapter_summaries WHERE chapter=1") == "她在山门取得令牌。",
                "切换章节丢失手改摘要或摘要串章");
        const auto previousWire = wire;
        click(window, "生成剧情摘要");
        require(waitStatus("满 1000 字可生成摘要") && wire == previousWire, "短章节仍发起摘要请求");
        writingTabs->setCurrentIndex(0);
        auto *firstSummary = writingSources()->sections[WritingSources::Summary].entries.at(1);
        prior.at(1)->setCheckState(Qt::Checked);
        firstSummary->setCheckState(Qt::Checked);
        require(prior.at(1)->checkState() == Qt::Unchecked, "勾选摘要后仍选中了同章全文");
        writingTabs->setCurrentIndex(1);
        require(!requestPreview->toPlainText().contains(originalChapter) &&
                    requestPreview->toPlainText().contains("她在山门取得令牌。"), "预览没有切换为手改摘要");
        click(window, "发送给 AI");
        require(waitStatus("候选正文已就绪") && !wire.contains("章首") && !wire.contains("未来剧情标记") &&
                    wire.contains("她在山门取得令牌。") && wire.contains("她已将令牌交给渡船人。") &&
                    wire.indexOf("她在山门取得令牌。") < wire.indexOf("她已将令牌交给渡船人。"),
                "摘要和最近一章全文未按顺序发送，或泄漏了旧章正文");
        prior.at(1)->setCheckState(Qt::Checked);
        require(firstSummary->checkState() == Qt::Unchecked, "切回全文时未移除摘要");
        toggleGroup(WritingSources::GlobalSkill);
        click(window, "返回正文");
        directory->setCurrentRow(0);
        check.run("UPDATE chapters SET body=? WHERE id=9", {QString(999, QChar(u'字')) + QString(50, QChar(u'。'))});
        check.run("UPDATE chapters SET body=? WHERE id=12", {QString(1000, QChar(u'文'))});
        for (auto *action : window.findChildren<QAction *>())
            if (action->text() == "返回资料工作区") action->trigger();
        nav->setCurrentRow(Dossier::summaryIndex);
        click(window, "一键生成缺失摘要");
        require(waitStatus("已生成 1 章，已有摘要 1 章，不足 1000 字 2 章") &&
                    check.value("SELECT body FROM chapter_summaries WHERE chapter=12") == "【第 4 章 · 尾声】\n山门前" &&
                    check.value("SELECT body FROM chapter_summaries WHERE chapter=1") == "她在山门取得令牌。" &&
                    check.value("SELECT count(*) FROM chapter_summaries").toInt() == 2,
                "全书批量摘要没有按字数、已有摘要或章节序号正确处理");
        check.run("UPDATE chapters SET body=? WHERE id=9", {QString(1001, QChar(u'甲'))});
        for (auto *box : window.findChildren<QTableWidget *>("summaryChapter"))
            if (box->isVisible()) box->setCurrentCell(2, 0);
        click(window, "生成剧情摘要");
        require(QTest::qWaitFor([&] {
                    return check.value("SELECT body FROM chapter_summaries WHERE chapter=9") == "【第 3 章 · 入城】\n山门前";
                }), "基础资料指定章节生成失败");
        check.run("UPDATE chapters SET body=? WHERE id=9", {QString(1001, QChar(u'乙'))});
        nav->setCurrentRow(0);
        nav->setCurrentRow(Dossier::summaryIndex);
        auto *summaryList = visible<QTableWidget>(window);
        require(summaryList->rowCount() == 4 && summaryList->item(2, 1)->text() == "1001" &&
                    summaryList->item(2, 2)->text() == "正文已修改" && summaryList->item(1, 2)->text() == "无摘要",
                "未区分等字数改稿和无摘要章节");
        summaryList->setCurrentCell(2, 0);
        click(window, "生成剧情摘要");
        require(QTest::qWaitFor([&] { return summaryList->item(2, 2)->text() == "已有摘要"; }),
                "重新生成后仍然提示正文已修改");
        edit(6, "境界", "从练气开始");
        click(window, "上移");
        require(WritingNotes(check, NoteScope::Outline).list().first().title == "境界", "大纲未上移");
        nav->setCurrentRow(0);
        nav->setCurrentRow(6);
        click(window, "下移");
        require(WritingNotes(check, NoteScope::Outline).list().first().title == "入山", "大纲未下移或选项未恢复");
        click(window, "在选中条目前插入");
        visible<QLineEdit>(window)->setText("过渡剧情");
        click(window, "保存");
        require(WritingNotes(check, NoteScope::Outline).list().at(1).title == "过渡剧情", "大纲插入位置错误");
        click(window, "进入专注写作");
        server.close();
        openAssistant();
        const auto &restored = writingSources()->sections[WritingSources::GlobalSkill];
        require(!restored.expanded && restored.entries.first()->isHidden() &&
                    writingSources()->sections[WritingSources::BookSkill].expanded, "重新打开 AI 写作后分组状态错误");
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
        require(editor->isVisible() && editor->toPlainText() == originalChapter, "取消后正文发生变化");
        directory->setCurrentRow(2);
        click(window, "上移");
        directory->setCurrentRow(2);
        openAssistant();
        click(window, "引用上一章");
        const auto reordered = priorGroup();
        require(reordered.size() == 2 && reordered.first()->data(WritingSources::ChapterId).toInt() == 9 &&
                    reordered.first()->checkState() == Qt::Checked && reordered.last()->data(WritingSources::ChapterId).toInt() == 1,
                "调序后引用了错误的上一章");
        click(window, "返回正文");
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
