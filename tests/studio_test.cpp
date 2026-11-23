#include "window.h"
#include <QTest>

static void require(bool ok, const char *why)
{
    if (!ok)
        throw std::runtime_error(why);
}
template <class T> T *shown(QWidget &root, const QString &name)
{
    for (auto *w : root.findChildren<T *>(name))
        if (w->isVisible())
            return w;
    QStringList visibleButtons;
    for (auto *b : root.findChildren<QPushButton *>()) if (b->isVisible()) visibleButtons << b->text();
    throw std::runtime_error(("找不到可见控件：" + QString(T::staticMetaObject.className()) + " " + name +
                              "；当前按钮 " + visibleButtons.join(" / ")).toStdString());
}
static void click(QWidget &root, const QString &name)
{
    for (auto *b : root.findChildren<QPushButton *>())
        if (b->isVisible() && b->text() == name && b->isEnabled())
        {
            b->click();
            return;
        }
    throw std::runtime_error(("找不到按钮：" + name).toStdString());
}
static bool message(QWidget &root, const QString &text)
{
    for (auto *l : root.findChildren<QLabel *>())
        if (l->isVisible() && l->text().contains(text))
            return true;
    return false;
}

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    applyTheme(app);
    QTemporaryDir dir;
    QCoreApplication::setOrganizationName("MoxuTests");
    QCoreApplication::setApplicationName("Studio");
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, dir.path());
    QStandardPaths::setTestModeEnabled(true);
    try
    {
        QTcpServer server;
        require(server.listen(QHostAddress::LocalHost), "模拟站点未启动");
        QSettings settings;
        settings.setValue("booksDir", dir.path());
        settings.setValue("writing/provider", "local");
        settings.setValue(
            "aiSites/local",
            QJsonDocument(QJsonObject{{"name", "本机测试"},
                                      {"address", QString("http://127.0.0.1:%1/v1").arg(server.serverPort())},
                                      {"models", QJsonArray{"fixture"}}})
                .toJson());
        QString answer, sent;
        int calls = 0;
        QObject::connect(&server, &QTcpServer::newConnection, &server, [&] {
            auto *socket = server.nextPendingConnection();
            QObject::connect(socket, &QIODevice::readyRead, socket, [&, socket, buf = QByteArray()]() mutable {
                buf += socket->readAll();
                int at = buf.indexOf("\r\n\r\n");
                if (at < 0)
                    return;
                auto len = QRegularExpression("(?im)^Content-Length: (\\d+)")
                               .match(QString::fromLatin1(buf.first(at)))
                               .captured(1)
                               .toInt();
                if (buf.size() - at - 4 < len)
                    return;
                ++calls;
                sent = QString::fromUtf8(buf.mid(at + 4));
                QByteArray json =
                    QJsonDocument(
                        QJsonObject{{"choices", QJsonArray{QJsonObject{{"delta", QJsonObject{{"content", answer}}},
                                                                       {"finish_reason", "stop"}}}}})
                        .toJson(QJsonDocument::Compact);
                auto bytes = "data: " + json + "\n\ndata: [DONE]\n\n";
                if (buf.startsWith("POST /v1/messages "))
                {
                    auto delta = QJsonObject{{"type", "text_delta"}, {"text", answer}};
                    auto block = QJsonObject{{"type", "content_block_delta"}, {"delta", delta}};
                    bytes = "data: " + QJsonDocument(block).toJson(QJsonDocument::Compact) +
                            "\n\ndata: {\"type\":\"message_stop\"}\n\n";
                }
                socket->write(
                    "HTTP/1.1 200 OK\r\nContent-Type: text/event-stream\r\nConnection: close\r\nContent-Length: " +
                    QByteArray::number(bytes.size()) + "\r\n\r\n" + bytes);
                socket->disconnectFromHost();
            });
        });
        Window w;
        w.show();
        click(w, "从灵感建书");
        shown<QLineEdit>(w, "inspirationBookTitle")->setText("昆仑");
        for (auto *box : w.findChildren<QPlainTextEdit *>())
            if (box->isVisible() && box->placeholderText().contains("一句话"))
                box->setPlainText("玄都上山");
        answer = "玄都持玉珮拜师，山下却传来钟声。";
        click(w, "生成核心故事");
        require(QTest::qWaitFor([&] { return message(w, "请核对并编辑核心故事"); }), "核心故事生成失败");
        const QString path = dir.filePath("昆仑.moxu");
        require(!QFileInfo::exists(path) && calls == 1, "未经确认就创建了书");
        click(w, "确认故事，选择资料");
        for (auto *box : w.findChildren<QCheckBox *>())
            if (box->isVisible() && (box->text() == "关系" || box->text() == "世界观" || box->text() == "章节目录"))
                box->setChecked(true);
        answer = "@@关系|玄都|太清\n师徒\n@@人物|玄都\n游学者\n@@人物|太清\n老师\n@@人物|略过\n待选\n"
                 "@@世界观|修行\n清静无为\n@@章节目录|上山\n玄都上山\n@@章节目录|入门\n玄都拜师";
        click(w, "生成所选资料");
        require(QTest::qWaitFor([&] { return message(w, "请逐条核对"); }), "未出现逐条预览");
        auto *table = shown<QTableWidget>(w, "studioItems");
        table->item(3, 0)->setCheckState(Qt::Unchecked);
        qobject_cast<QPlainTextEdit *>(table->cellWidget(1, 4))->setPlainText("作者编辑的人物档案");
        require(!QFileInfo::exists(path) && sent.contains("山下却传来钟声"), "预览提前落库或未引用已确认故事");
        click(w, "创建书籍并写入");
        Book db(path);
        require(db.value("SELECT synopsis FROM book_profile").toString().contains("山下") &&
                    db.value("SELECT count(*) FROM people").toInt() == 2 &&
                    db.value("SELECT attributes FROM people WHERE name='玄都'") == "作者编辑的人物档案" &&
                    db.value("SELECT count(*) FROM relations").toInt() == 1 &&
                    db.value("SELECT count(*) FROM chapters").toInt() == 2 &&
                    db.value("SELECT body FROM chapters WHERE position=2").toString().isEmpty(),
                "新书内容或目录写入错误");
        auto *nav = shown<QListWidget>(w, "navigation");
        auto local = QJsonDocument::fromJson(settings.value("aiSites/local").toByteArray()).object();
        local["protocol"] = "messages-bearer";
        settings.setValue("aiSites/local", QJsonDocument(local).toJson());
        selectDossierPage(nav, Dossier::summaryIndex + 1);
        answer = "@@人物|新弟子\n未命名\n@@人物|玄都\n同名冲突\n@@章节目录|下山\n告别山门";
        for (auto *c : w.findChildren<QCheckBox *>())
            if (c->isVisible() && c->text() == "章节目录")
                c->setChecked(true);
        click(w, "生成所选资料");
        require(QTest::qWaitFor([&] { return message(w, "请逐条核对"); }) && sent.contains("作者编辑的人物档案") &&
                    QJsonDocument::fromJson(sent.toUtf8()).object().contains("system"),
                "现有书资料或 Messages 协议错误");
        click(w, "写入所选资料");
        require(message(w, "人物已存在") && db.value("SELECT count(*) FROM people").toInt() == 2,
                "重复人物没有整批回滚");
        table = shown<QTableWidget>(w, "studioItems");
        table->item(1, 0)->setCheckState(Qt::Unchecked);
        click(w, "写入所选资料");
        require(db.value("SELECT count(*) FROM people").toInt() == 3, "重试未成功");
        const int person = db.value("SELECT id FROM people WHERE name='玄都'").toInt();
        auto chapterId = [&](int pos) { return db.value("SELECT id FROM chapters WHERE position=?", {pos}).toInt(); };
        db.save(chapterId(1), "玄都接过灵石。");
        db.save(chapterId(2), "前文开端标记" + QString(1400, QChar(u'山')) + "玄都走到山门。");
        db.run("INSERT INTO chapter_summaries(chapter,body,source_version) VALUES(?,'玄都曾在山上修行',0)",
               {chapterId(1)});
        db.event(chapterId(2), person, "灵石", 120, "获赠");
        db.event(chapterId(3), person, "灵石", 30, "回山之后");
        click(w, "进入专注写作");
        auto *directory = shown<QListWidget>(w, "chapterList");
        require(directory->count() == 3, "目录没有更新");
        directory->setCurrentRow(2);
        for (auto *a : w.findChildren<QAction *>())
            if (a->text() == "AI 写作")
            {
                a->trigger();
                break;
            }
        auto *sources = static_cast<WritingSources *>(shown<QListWidget>(w, "writingSources"));
        auto *skill = sources->sections[WritingSources::BookSkill].entries.first();
        skill->setCheckState(Qt::Checked);
        const auto before = calls;
        click(w, "准备本章资料");
        auto &tail = sources->sections[WritingSources::Tail].entries;
        auto &snaps = sources->sections[WritingSources::Snapshot].entries;
        require(calls == before && tail.size() == 1 && tail[0]->checkState() == Qt::Checked && snaps.size() == 1 &&
                    snaps[0]->data(WritingSources::Body).toString().contains("灵石：120") &&
                    sources->sections[WritingSources::Summary].entries.last()->checkState() == Qt::Checked,
                "资料推荐、上章快照或本地准备失败");
        auto *full = sources->sections[WritingSources::Full].entries.first();
        full->setCheckState(Qt::Checked);
        click(w, "准备本章资料");
        require(full->checkState() == Qt::Checked && tail[0]->checkState() == Qt::Unchecked && tail.size() == 1,
                "推荐操作覆盖手选全文或重复增加条目");
        tail[0]->setCheckState(Qt::Checked);
        answer = "作者确认的候选稿";
        click(w, "发送给 AI");
        require(QTest::qWaitFor([&] { return message(w, "候选正文已就绪"); }) && sent.contains("灵石：120") &&
                    !sent.contains("灵石：150") && !sent.contains("前文开端标记"),
                "实际发送与选择不一致");
        db.atomic([&] { db.save(chapterId(3), "另一个编辑器保存的版本"); });
        auto confirm = [&] {
            click(w, "覆盖本章");
            auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
            require(box, "覆盖时缺少确认");
            box->button(QMessageBox::Ok)->click();
        };
        confirm();
        require(message(w, "原章已被修改") &&
                    db.value("SELECT body FROM chapters WHERE position=3") == "另一个编辑器保存的版本",
                "并发更新静默覆盖了章节");
        auto *tabs = shown<QTabWidget>(w, QString());
        for (int i = 0; i < tabs->count(); ++i)
            if (tabs->tabText(i) == "候选正文")
                tabs->setCurrentIndex(i);
        confirm();
        require(db.value("SELECT body FROM chapters WHERE position=3") == answer &&
                    db.value("SELECT count(*) FROM revisions WHERE body='另一个编辑器保存的版本'").toInt() == 1,
                "再次确认后未保存或未留存最新章节历史");
        auto tool = [&](const QString &label) {
            for (auto *a : w.findChildren<QAction *>())
                if (a->text() == label) { a->trigger(); QApplication::processEvents(); return; }
            throw std::runtime_error(label.toStdString());
        };
        auto *manuscript = shown<QPlainTextEdit>(w, "manuscript");
        const QString earned = "玄都得了十块灵石。", spent = "玄都花去九块灵石。";
        manuscript->setPlainText(earned + spent);
        tool("检查本章");
        const int beforeCheck = calls;
        answer = QString::fromUtf8(QJsonDocument(QJsonArray{
            QJsonObject{{"kind","数量变动"},{"person","玄都"},{"name","灵石"},{"amount",10},{"quote",earned}},
            QJsonObject{{"kind","数量变动"},{"person","玄都"},{"name","灵石"},{"amount",-999},{"quote",spent}}
        }).toJson());
        require(calls == beforeCheck, "进入检查页自动发起了请求");
        click(w, "检查本章");
        require(QTest::qWaitFor([&] { return message(w, "请核对 2 条建议"); }) && sent.contains(earned) &&
                    !sent.contains("前文开端标记"),
                "未能从本章生成检查预览");
        auto *review = shown<QTableWidget>(w, QString());
        const int baseline = db.value("SELECT count(*) FROM events").toInt();
        require(review->item(0, 0)->checkState() == Qt::Unchecked, "建议被默认采纳");
        review->item(0, 0)->setCheckState(Qt::Checked);
        review->item(1, 0)->setCheckState(Qt::Checked);
        click(w, "确认写入勾选项");
        require(message(w, "本次未写入") && baseline == db.value("SELECT count(*) FROM events").toInt(), "未回滚整批");
        review->setCurrentCell(1, 1);
        auto *quoteEditor = shown<QPlainTextEdit>(w, "checkQuote");
        require(quoteEditor->toPlainText() == spent && review->horizontalScrollBar()->maximum() == 0,
                "原句详情或建议列表布局错误");
        auto *modelBox = shown<QComboBox>(w, "aiModel");
        QPushButton *backButton = nullptr;
        for (auto *button : w.findChildren<QPushButton *>())
            if (button->isVisible() && button->text() == "返回正文")
                backButton = button;
        require(backButton &&
                    modelBox->mapTo(&w, QPoint()).y() > backButton->mapTo(&w, QPoint(0, backButton->height())).y(),
                "模型控件挤入了标题行");
        shown<QLineEdit>(w, "checkAmount")->setText("-9");
        review->setCurrentCell(0, 1);
        review->setCurrentCell(1, 1);
        require(shown<QLineEdit>(w, "checkAmount")->text() == "-9", "切换建议后遗失修改");
        click(w, "确认写入勾选项");
        require(message(w, "已入账 2 条") && baseline + 2 == db.value("SELECT count(*) FROM events").toInt(),
                "编辑后无法采纳");
        review->item(0, 0)->setCheckState(Qt::Checked);
        click(w, "确认写入勾选项");
        require(message(w, "跳过已有 1 条"), "重复录入");
        db.save(chapterId(3), earned + spent + "新增正文");
        review->item(0, 0)->setCheckState(Qt::Checked);
        click(w, "确认写入勾选项");
        require(message(w, "正文已修改"), "章节版本失效后仍可入账");
        click(w, "返回正文");
        QTextCursor cur(manuscript->document());
        cur.setPosition(earned.size(), QTextCursor::KeepAnchor);
        manuscript->setTextCursor(cur);
        tool("伏笔");
        shown<QLineEdit>(w, QString())->setText("灵石来历");
        click(w, "将选区标记为埋设");
        const auto clue = db.value("SELECT id FROM foreshadows WHERE title='灵石来历'").toInt();
        require(clue && db.value("SELECT quote FROM clue_quotes WHERE clue=?", {clue}) == earned, "未保留原始选区");
        tool("返回资料工作区");
        selectDossierPage(shown<QListWidget>(w, "navigation"), Dossier::clueIndex);
        click(w, "埋设原文 · 第 3 章 · 下山 · 定位");
        require(manuscript->isVisible() && manuscript->textCursor().selectedText() == earned, "伏笔未返回原句");
        db.moveChapter(chapterId(3), -1);
        require(
            db.value("SELECT position FROM chapters JOIN clue_quotes ON chapters.id=chapter WHERE clue=?", {clue}) == 2,
            "调序后原文指向其他章");
        require(Clue::locate("前言" + earned, 2, 1, 0, earned) == 2 &&
                    Clue::locate(earned + earned, 2, 1, 0, earned) == -1 && Clue::locate("改稿", 2, 1, 0, earned) == -1,
                "失效摘录定位错误");

        const int testChapter = chapterId(3);
        const QString evidence = "玄都得到太极图。太极图赠予太清。玄都得到三瓶丹药。服用一瓶丹药。赠给太清一瓶丹药。"
                                 "玄都习得清风诀。清风诀升至二层。施展一次清风诀。太清只是代持玉佩。";
        db.save(testChapter, evidence);
        ChapterCheck audit(db, testChapter,
                           [&](const QString &input) { require(input.contains(evidence), "未读取正文"); });
        audit.resize(690,680);
        audit.show();
        click(audit, "检查本章");
        auto entry = [](QString kind, QString name, QString quote) {
            return QJsonObject{{"kind", kind}, {"person", "玄都"}, {"name", name}, {"quote", quote}};
        };
        auto acquire = entry("获得道具", "太极图", "玄都得到太极图。");
        auto give = entry("转交道具", "太极图", "太极图赠予太清。");
        give["recipient"] = "太清";
        auto receive = entry("获得消耗品", "丹药", "玄都得到三瓶丹药。");
        receive["amount"] = 3;
        auto consume = entry("使用消耗品", "丹药", "服用一瓶丹药。");
        consume["amount"] = 1;
        auto share = entry("赠送消耗品", "丹药", "赠给太清一瓶丹药。");
        share["amount"] = 1;
        share["recipient"] = "太清";
        auto learn = entry("学会功法", "清风诀", "玄都习得清风诀。");
        learn["level"] = "一层";
        learn["uses"] = 2;
        auto level = entry("功法升级", "清风诀", "清风诀升至二层。");
        level["level"] = "二层";
        auto use = entry("使用功法", "清风诀", "施展一次清风诀。");
        auto custody = entry("转交道具", "玉佩", "太清只是代持玉佩。");
        custody["recipient"] = "太清";
        auto records = QString::fromUtf8(
            QJsonDocument(QJsonArray{acquire, give, receive, consume, share, learn, level, use, custody}).toJson());
        audit.review(records);
        auto *grid = shown<QTableWidget>(audit, QString());
        QApplication::processEvents();
        auto *excerpt = shown<QPlainTextEdit>(audit,"checkQuote");
        require(grid->horizontalScrollBar()->maximum() == 0 &&
                    grid->mapTo(&audit, QPoint(0, grid->height())).y() < excerpt->mapTo(&audit, QPoint()).y(),
                "窄窗口核对表与原文详情布局错误");
        grid->setCurrentCell(8, 1);
        auto *recipient = shown<QLineEdit>(audit, "checkRecipient");
        shown<QComboBox>(audit, "checkKind")->setCurrentText("普通事件");
        require(!recipient->isVisible(), "普通事件仍显示接收人");
        for (int r = 0; r < grid->rowCount(); ++r)
            grid->item(r, 0)->setCheckState(Qt::Checked);
        click(audit, "确认写入勾选项");
        require(message(audit, "已入账 9 条"), "复核多类型事件未能写入");
        const int master = db.value("SELECT id FROM people WHERE name='太清'").toInt();
        require(db.ownedItems(master, testChapter) == QStringList{"太极图"} &&
                    db.supplyCounts(person, testChapter).value("丹药") == 1 &&
                    db.supplyCounts(master, testChapter).value("丹药") == 1 &&
                    db.characterSkills(person, testChapter).first().remaining == 1 &&
                    db.characterSkills(person, testChapter).first().level == "二层",
                "章后检查写入了错误的快照");
        click(audit, "检查本章");
        audit.review(records);
        require(grid->item(8, 3)->toolTip() == "账本已有匹配", "重新分析丢失了作者纠正的事件类型");

        tool("返回资料工作区");
        nav = shown<QListWidget>(w, "navigation");
        const auto menu = nav->findItems("任务管理", Qt::MatchExactly);
        require(menu.size() == 1 && nav->item(nav->row(menu.first()) + 1)->text() == "资源管理" &&
                    nav->item(nav->row(menu.first()) + 2)->text() == "事件账本",
                "任务与资源菜单顺序错误");
        nav->setCurrentItem(menu.first());
        click(w, "新建任务");
        shown<QLineEdit>(w, "taskTitle")->setText("初入昆仑");
        shown<QComboBox>(w, "taskPerson")->setCurrentIndex(shown<QComboBox>(w, "taskPerson")->findData(person));
        auto *start = shown<QComboBox>(w, "taskPublished");
        start->setCurrentIndex(start->findData(chapterId(1)));
        auto *res = shown<QComboBox>(w, "taskResource");
        res->setCurrentText("数值资源 · 积分");
        shown<QSpinBox>(w, "taskQuantity")->setValue(300);
        click(w, "添加奖励");
        res->setCurrentText("消耗品 · 丹药");
        shown<QSpinBox>(w, "taskQuantity")->setValue(2);
        click(w, "添加奖励");
        shown<QPlainTextEdit>(w, "taskInstructions")->setPlainText("将玉符送抵昆仑，向师父复命。");
        nav->setCurrentItem(nav->findItems("资源管理", Qt::MatchExactly).first());
        nav->setCurrentItem(menu.first());
        require(shown<QTableWidget>(w, "taskRewards")->rowCount() == 2 &&
                    shown<QLineEdit>(w, "taskTitle")->text() == "初入昆仑",
                "切换资料页遗失任务草稿");
        click(w, "发布任务");
        require(message(w, "任务已发布") && db.balance(person, "积分", testChapter) == 0, "发布时提前发奖");
        click(w, "查看任务");
        auto *end = shown<QComboBox>(w, "taskCompleted");
        end->setCurrentIndex(end->findData(testChapter));
        click(w, "完成任务并发奖");
        require(message(w, "奖励已结算") && db.balance(person, "积分", testChapter) == 300 &&
                    db.balance(person, "积分", chapterId(1)) == 0,
                "任务奖励章节或余额错误");
        nav->setCurrentItem(nav->findItems("事件账本", Qt::MatchExactly).first());
        require(db.ledgerPage({"玄都", "积分", "初入昆仑", true}, 1).total == 1, "任务奖励没有进入账本");
        nav->setCurrentItem(menu.first());
        click(w, "查看任务");
        require(shown<QLineEdit>(w, "taskTitle")->isReadOnly() && message(w, "奖励已结算"), "已完成任务未锁定");

        AiSite glm{"zai-messages", "GLM", "https://api.z.ai/api/anthropic", {"glm-5.3"}, "messages-bearer"};
        QJsonObject payload;
        glm.applyEffort(payload, "glm-5.3");
        require(payload.isEmpty(), "默认档改变了请求");
        glm.efforts["glm-5.3"] = "low";
        glm.applyEffort(payload, "glm-5.3");
        require(payload["output_config"].toObject()["effort"] == "low" && !payload.contains("max_tokens"),
                "Messages 强度参数错误");
        glm.protocol = "chat";
        payload = {};
        glm.applyEffort(payload, "glm-5.3");
        require(payload["reasoning_effort"] == "low" && !payload.contains("output_config"), "Chat 强度参数错误");
        glm.address = "http://localhost:8080";
        payload = {};
        glm.applyEffort(payload, "glm-5.3");
        require(payload.isEmpty(), "未知站点收到强度参数");
        return 0;
    }
    catch (const std::exception &e)
    {
        qCritical("%s", e.what());
        return 1;
    }
}
