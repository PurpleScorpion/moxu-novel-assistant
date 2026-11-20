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
    throw std::runtime_error(("找不到可见控件：" + name).toStdString());
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
        nav->setCurrentRow(Dossier::summaryIndex + 1);
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
        return 0;
    }
    catch (const std::exception &e)
    {
        qCritical("%s", e.what());
        return 1;
    }
}
