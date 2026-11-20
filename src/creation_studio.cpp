#include "creation_studio.h"

namespace
{
QPushButton *action(QBoxLayout *layout, const QString &name, const std::function<void()> &run)
{
    auto *b = new QPushButton(name);
    layout->addWidget(b);
    QObject::connect(b, &QPushButton::clicked, b, run);
    return b;
}
} // namespace

CreationStudio::CreationStudio(QHash<QString, QString> &keys, Book *book, Create create, std::function<void()> back)
    : book(book), create(std::move(create)), keys(keys)
{
    setObjectName("creationStudio");
    auto *root = new QVBoxLayout(this);
    if (!book)
    {
        auto *header = new QHBoxLayout;
        auto *label = new QLabel("从灵感建书");
        label->setObjectName("title");
        header->addWidget(label, 1);
        action(header, "返回书架", back);
        root->addLayout(header);
    }
    settings = new QWidget;
    auto *bar = new QHBoxLayout(settings);
    bar->setContentsMargins(0, 0, 0, 0);
    bar->addWidget(new QLabel("服务"));
    bar->addWidget(provider);
    bar->addWidget(new QLabel("模型"));
    bar->addWidget(model, 1);
    key->setEchoMode(QLineEdit::Password);
    key->setPlaceholderText("本站点 API 密钥");
    bar->addWidget(key, 1);
    root->addWidget(settings);
    sites = aiSites();
    sites.removeIf([](const AiSite &s) { return isGlmSite(s.id) && s.id != activeGlmSite(); });
    for (const auto &s : sites)
        provider->addItem(isGlmSite(s.id) ? QStringLiteral("智谱 GLM") : s.name, s.id);
    auto last = QSettings().value("writing/provider", "deepseek").toString();
    provider->setCurrentIndex(qMax(0, provider->findData(isGlmSite(last) ? activeGlmSite() : last)));
    loadModel();
    connect(provider, &QComboBox::currentIndexChanged, this, [this] { loadModel(); });
    root->addWidget(pages, 1);
    auto page = [&] {
        auto *w = new QWidget;
        pages->addWidget(w);
        return new QVBoxLayout(w);
    };
    auto *start = page();
    title->setPlaceholderText("书名（创建前填写）");
    title->setObjectName("inspirationBookTitle");
    start->addWidget(title);
    idea->setPlaceholderText("用一句话描述这本书的灵感");
    start->addWidget(idea, 1);
    action(start, "生成核心故事", [this] { generate(true); });
    story->setPlaceholderText("核心故事可以手动编辑，也可以修改灵感后重新生成。");
    start->addWidget(story, 2);
    action(start, "确认故事，选择资料", [this] {
        if (title->text().trimmed().isEmpty() || story->toPlainText().trimmed().isEmpty())
        {
            status->setText("请填写书名和核心故事。");
            return;
        }
        pages->setCurrentIndex(1);
        references->setPlainText(story->toPlainText());
        status->clear();
    });
    auto *plan = page();
    plan->addWidget(new QLabel("选择要生成的资料"));
    auto *grid = new QGridLayout;
    plan->addLayout(grid);
    for (const auto &kind : studioKinds())
    {
        auto *box = new QCheckBox(kind);
        int n = categories.size();
        grid->addWidget(box, n / 5, n % 5);
        box->setChecked(kind == "人物" || kind == "大纲");
        categories << box;
    }
    plan->addWidget(new QLabel("创作要求"));
    request->setPlaceholderText("例如：设计三个昆仑门派，写出各派与主角的关系。");
    plan->addWidget(request, 1);
    plan->addWidget(new QLabel("随本次生成发送的参考资料（可编辑）"));
    plan->addWidget(references, 1);
    auto *commands = new QHBoxLayout;
    plan->addLayout(commands);
    if (!book)
    {
        action(commands, "返回编辑故事", [this] { pages->setCurrentIndex(0); });
        action(commands, "仅用故事建书", [this] {
            try
            {
                this->create(title->text().trimmed(), story->toPlainText(), {});
            }
            catch (const std::exception &e)
            {
                status->setText(QString::fromUtf8(e.what()));
            }
        });
    }
    action(commands, "生成所选资料", [this] { generate(false); });
    auto *resume = action(commands, "查看待写入资料", [this] { pages->setCurrentIndex(2); });
    resume->hide();
    connect(table->model(), &QAbstractItemModel::rowsInserted, resume, [resume] { resume->show(); });
    connect(table->model(), &QAbstractItemModel::rowsRemoved, resume,
            [this, resume] { resume->setVisible(table->rowCount() > 0); });
    auto *preview = page();
    preview->addWidget(new QLabel("双击可编辑，勾选要写入的条目。关系两端使用人物档案中的姓名。"));
    table->setHorizontalHeaderLabels({"写入", "类型", "名称／人物", "关系另一方", "内容"});
    table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    table->horizontalHeader()->setSectionResizeMode(4, QHeaderView::Stretch);
    table->setAlternatingRowColors(true);
    table->setObjectName("studioItems");
    preview->addWidget(table, 1);
    auto *confirm = new QHBoxLayout;
    preview->addLayout(confirm);
    action(confirm, "返回调整要求", [this] { pages->setCurrentIndex(1); });
    action(confirm, book ? "写入所选资料" : "创建书籍并写入", [this] { acceptRows(); });
    auto *footer = new QHBoxLayout;
    root->addLayout(footer);
    status->setWordWrap(true);
    status->setTextFormat(Qt::PlainText);
    footer->addWidget(status, 1);
    footer->addWidget(cancel);
    cancel->hide();
    connect(cancel, &QPushButton::clicked, this, [this] {
        if (pending)
            pending->abort();
    });
    idle.setSingleShot(true);
    idle.setTimerType(Qt::PreciseTimer);
    connect(&idle, &QTimer::timeout, this, [this] {
        if (pending)
        {
            pending->setProperty("idleExpired", true);
            pending->abort();
        }
    });
    if (book)
    {
        references->setPlainText(studioBookContext(*book));
        pages->setCurrentIndex(1);
    }
}

CreationStudio::~CreationStudio()
{
    if (pending)
    {
        disconnect(pending, nullptr, this, nullptr);
        pending->abort();
    }
}

void CreationStudio::loadModel()
{
    const auto &s = sites.at(provider->currentIndex());
    model->clear();
    model->addItems(s.models);
    int row = model->findText(QSettings().value("writing/model/" + s.id).toString());
    if (row >= 0)
        model->setCurrentIndex(row);
    key->setText(keys.contains(s.credentialId()) ? keys.value(s.credentialId()) : storedAiKey(s.credentialId()));
}

QStringList CreationStudio::selectedKinds() const
{
    QStringList kinds;
    for (auto *c : categories)
        if (c->isChecked())
            kinds << c->text();
    return kinds;
}

void CreationStudio::setBusy(bool busy)
{
    pages->setEnabled(!busy);
    settings->setEnabled(!busy);
    cancel->setVisible(busy);
}

void CreationStudio::generate(bool core)
{
    if (pending)
        return;
    const auto s = sites.at(provider->currentIndex());
    const auto token = key->text().trimmed();
    const auto kinds = selectedKinds();
    if (model->currentText().isEmpty() || (!s.loopback() && token.isEmpty()))
    {
        status->setText("请选择已配置的模型，并填写该站点的密钥。");
        return;
    }
    if (core && idea->toPlainText().trimmed().isEmpty())
    {
        status->setText("请先填写一句灵感。");
        return;
    }
    if (!core && kinds.isEmpty())
    {
        status->setText("请至少选择一种资料。");
        return;
    }
    QString rules = core ? QStringLiteral("用中文把作者的灵感发展成为400至700字的核心故事。只"
                                          "返回可编辑的故事简介。")
                         : QStringLiteral("为小说作者草拟资料。只输出所选类型；每条以单独一行 "
                                          "@@类型|名称 开头，后续行是内容。"
                                          "关系以 @@关系|人物姓名|另一人物姓名 "
                                          "开头，下一行只写关系名称。人物必须已存在或同时生成。"
                                          "章节目录只给标题和梗概。每类按作者要求生成，未指定时1至3条。"
                                          "不要重复已有条目，不使用代码围栏。");
    QString input = core ? title->text() + "\n" + idea->toPlainText()
                         : "所选类型：" + kinds.join("、") + "\n创作要求：" + request->toPlainText() +
                               "\n参考资料：\n" + references->toPlainText();
    QJsonArray turns;
    QJsonObject payload{{"model", model->currentText()}, {"stream", true}};
    if (s.messagesApi())
        payload["system"] = rules;
    else
        turns.append(QJsonObject{{"role", "system"}, {"content", rules}});
    turns.append(QJsonObject{{"role", "user"}, {"content", input}});
    payload["messages"] = turns;
    int limit = s.outputLimit(model->currentText());
    if (limit > 0)
        payload["max_tokens"] = limit;
    QNetworkRequest req(s.endpoint());
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    s.setHeaders(req, token);
    keys[s.credentialId()] = token;
    response = {};
    response.messagesApi = s.messagesApi();
    auto *reply = network.post(req, QJsonDocument(payload).toJson(QJsonDocument::Compact));
    pending = reply;
    idle.start(300000);
    setBusy(true);
    status->setText("正在等待模型…");
    auto stream = [reply] {
        return reply->header(QNetworkRequest::ContentTypeHeader)
            .toString()
            .startsWith("text/event-stream", Qt::CaseInsensitive);
    };
    connect(reply, &QIODevice::readyRead, this, [this, reply, stream] {
        if (reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() != 200)
            return;
        if (!stream())
        {
            idle.start();
            return;
        }
        auto part = response.read(reply->readAll());
        if (!part.isEmpty() || response.receivedThinking)
            idle.start();
        if (!part.isEmpty())
            status->setText("正在生成，已收到 " + QString::number(response.text.size()) + " 个字符");
        else if (response.receivedThinking)
            status->setText("模型正在思考…");
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply, stream, core, kinds, limit, token] {
        idle.stop();
        pending = nullptr;
        reply->deleteLater();
        setBusy(false);
        if (reply->error() != QNetworkReply::NoError)
        {
            auto reason = ChatResponse::errorMessage(QJsonDocument::fromJson(reply->readAll()).object());
            if (reason.isEmpty())
                reason = reply->errorString();
            if (!token.isEmpty())
                reason.replace(token, "***");
            status->setText(reply->property("idleExpired").toBool()                   ? "连续 5 分钟没有新内容。"
                            : reply->error() == QNetworkReply::OperationCanceledError ? "已取消生成。"
                                                                                      : "请求失败：" + reason);
            return;
        }
        if (stream())
            response.read(reply->readAll(), true);
        else
            response.readBufferedReply(reply->readAll(), true);
        if (!response.error.isEmpty())
        {
            auto error = response.error;
            if (!token.isEmpty())
                error.replace(token, "***");
            status->setText(error);
            return;
        }
        if (stream() && !response.finished)
        {
            status->setText("生成中断，请重试。");
            return;
        }
        if (response.text.trimmed().isEmpty())
        {
            status->setText(response.emptyReplyMessage(limit));
            return;
        }
        if (!core && response.truncated())
        {
            status->setText("资料生成达到上限。请减少生成条数后重试。");
            return;
        }
        try
        {
            if (core)
                story->setPlainText(response.text);
            else
                review(parseStudioItems(response.text, kinds));
            status->setText(core ? (response.truncated() ? "故事达到长度上限，请检查结尾。" : "请核对并编辑核心故事。")
                                 : "已生成草稿，请逐条核对。");
        }
        catch (const std::exception &e)
        {
            status->setText(QString::fromUtf8(e.what()));
        }
    });
}

void CreationStudio::review(const QList<StudioItem> &rows)
{
    table->setRowCount(0);
    for (const auto &r : rows)
    {
        int i = table->rowCount();
        table->insertRow(i);
        auto *check = new QTableWidgetItem;
        check->setFlags(Qt::ItemIsEnabled | Qt::ItemIsUserCheckable | Qt::ItemIsSelectable);
        check->setCheckState(Qt::Checked);
        table->setItem(i, 0, check);
        auto *kind = new QTableWidgetItem(r.kind);
        kind->setFlags(kind->flags() & ~Qt::ItemIsEditable);
        table->setItem(i, 1, kind);
        table->setItem(i, 2, new QTableWidgetItem(r.name));
        table->setItem(i, 3, new QTableWidgetItem(r.target));
        auto *body = new QPlainTextEdit;
        body->setPlainText(r.body);
        table->setCellWidget(i, 4, body);
        table->setRowHeight(i, 78);
    }
    pages->setCurrentIndex(2);
}

void CreationStudio::acceptRows()
{
    QList<StudioItem> rows;
    for (int i = 0; i < table->rowCount(); ++i)
        if (table->item(i, 0)->checkState() == Qt::Checked)
            rows.append({table->item(i, 1)->text(), table->item(i, 2)->text().trimmed(),
                         table->item(i, 3)->text().trimmed(),
                         qobject_cast<QPlainTextEdit *>(table->cellWidget(i, 4))->toPlainText().trimmed()});
    if (rows.isEmpty())
    {
        status->setText("请勾选要写入的资料。");
        return;
    }
    try
    {
        if (!book)
        {
            create(title->text().trimmed(), story->toPlainText(), rows);
            return;
        }
        applyStudioItems(*book, rows);
        table->setRowCount(0);
        pages->setCurrentIndex(1);
        references->setPlainText(studioBookContext(*book));
        status->setText(QString("已写入 %1 条资料。").arg(rows.size()));
    }
    catch (const std::exception &e)
    {
        status->setText(QString::fromUtf8(e.what()));
    }
}
