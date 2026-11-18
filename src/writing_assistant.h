#pragma once
#include "downward_combo.h"
#include "ai_sites.h"
#include "chat_response.h"
#include "writing_notes.h"
#include <QtNetwork>
#include <QtWidgets>

class WritingAssistant : public QWidget
{
    QComboBox *service = new DownwardCombo;
    QComboBox *model = new DownwardCombo;
    QLineEdit *key = new QLineEdit;
    QPushButton *config = new QPushButton("AI 配置"), *test = new QPushButton("测试连接");
    QLabel *referenceCount = new QLabel;
    QList<AiSite> sites;
    QTimer progress, deadline;
    QElapsedTimer elapsed;
    QString phase;
    bool timedOut = false;
    ChatResponse response;
    QTreeWidget *sources = new QTreeWidget;
    QPlainTextEdit *task = new QPlainTextEdit, *context = new QPlainTextEdit, *preview = new QPlainTextEdit,
                   *result = new QPlainTextEdit;
    QTabWidget *tabs = new QTabWidget;
    QPushButton *send = new QPushButton("发送给 AI"), *apply = new QPushButton, *stop = new QPushButton("取消请求");
    QLabel *status = new QLabel;
    QNetworkAccessManager network;
    QPointer<QNetworkReply> pending;
    QHash<QString, QString> &sessionKeys;

    void setBusy(bool busy)
    {
        for (QWidget *control : QList<QWidget *>{service, model, key, config, test, task, context, sources, send})
            control->setEnabled(!busy);
        stop->setEnabled(busy);
        stop->setVisible(busy);
    }
    const AiSite &site() const
    {
        return sites.at(service->currentIndex());
    }
    QUrl endpoint() const
    {
        return site().endpoint();
    }
    void loadService()
    {
        const auto &selected = site();
        model->clear();
        model->addItems(selected.models);
        int index = model->findText(QSettings().value("writing/model/" + selected.id).toString());
        if (index >= 0)
            model->setCurrentIndex(index);
        const auto cred = selected.credentialId();
        key->setText(sessionKeys.contains(cred) ? sessionKeys.value(cred) : storedAiKey(cred));
        key->setPlaceholderText(selected.loopback() ? "本机密钥（如服务需要）" : "本站点 API 密钥");
        updatePreview();
    }
    void reloadSites(const QString &selected)
    {
        QSignalBlocker block(service);
        sites = aiSites();
        const auto glm = activeGlmSite();
        sites.removeIf([&](const AiSite &entry) { return isGlmSite(entry.id) && entry.id != glm; });
        service->clear();
        for (const auto &entry : sites)
            service->addItem(isGlmSite(entry.id) ? QStringLiteral("智谱 GLM") : entry.name, entry.id);
        service->setCurrentIndex(qMax(0, service->findData(isGlmSite(selected) ? glm : selected)));
        loadService();
    }
    QPair<QString, QString> messages() const
    {
        QString rules = "你是中文小说写作助手。遵循作者的写作要求，只返回可直接使用的小说正文。若选用的写作规则冲突，优"
                        "先遵循本书规则。";
        QStringList outlines;
        for (int group = 0; group < sources->topLevelItemCount(); ++group)
        {
            auto *section = sources->topLevelItem(group);
            for (int i = 0; i < section->childCount(); ++i)
            {
                auto *item = section->child(i);
                if (item->checkState(0) != Qt::Checked)
                    continue;
                QString part =
                    "【" + section->text(0) + " · " + item->text(0) + "】\n" + item->data(0, Qt::UserRole).toString();
                if (group == 2)
                    outlines.append(part);
                else
                    rules += "\n\n" + part;
            }
        }
        QString input = "写作任务\n" + task->toPlainText();
        if (!outlines.isEmpty())
            input += "\n\n剧情参考\n" + outlines.join("\n\n");
        if (!context->toPlainText().isEmpty())
            input += "\n\n正文上下文\n" + context->toPlainText();
        return {rules, input};
    }
    void updatePreview()
    {
        auto [rules, input] = messages();
        int count = 0;
        for (QTreeWidgetItemIterator it(sources, QTreeWidgetItemIterator::Checked); *it; ++it)
            ++count;
        referenceCount->setText(QString("已选 %1 项").arg(count));
        preview->setPlainText("目标地址  " + endpoint().toString() + "\n模型  " + model->currentText() +
                              "\n\n写作规则\n" + rules + "\n\n" + input);
    }
    void updateProgress()
    {
        status->setText(QString("%1 · %2 秒").arg(phase).arg(elapsed.elapsed() / 1000));
    }
    bool isStream(QNetworkReply *reply) const
    {
        return reply->header(QNetworkRequest::ContentTypeHeader)
            .toString()
            .startsWith("text/event-stream", Qt::CaseInsensitive);
    }
    void readStream(QNetworkReply *reply, bool end)
    {
        const auto added = response.read(reply->readAll(), end);
        if (!added.isEmpty())
        {
            QTextCursor tail(result->document());
            tail.movePosition(QTextCursor::End);
            tail.insertText(added);
            phase = "正在接收候选正文";
        }
        else if (response.thinking && response.text.isEmpty())
            phase = "模型正在思考";
        updateProgress();
    }
    QString failure(QNetworkReply *reply, int code, const QString &detail) const
    {
        QString reason = detail;
        if (reason.isEmpty())
        {
            switch (reply->error())
            {
            case QNetworkReply::HostNotFoundError:
                reason = "无法解析站点域名";
                break;
            case QNetworkReply::SslHandshakeFailedError:
                reason = "TLS 证书验证或握手失败";
                break;
            case QNetworkReply::ConnectionRefusedError:
                reason = "连接被拒绝，请检查地址和端口";
                break;
            case QNetworkReply::ProxyConnectionRefusedError:
                reason = "代理拒绝连接";
                break;
            default:
                reason = code == 401 || code == 403 ? "请核对密钥所属站点及模型权限" : reply->errorString();
                break;
            }
        }
        if (!key->text().isEmpty())
            reason.replace(key->text(), "***");
        return QString("请求失败%1，%2").arg(code ? QString("（HTTP %1）").arg(code) : QString(), reason.left(260));
    }
    void submit(bool probe = false)
    {
        if (pending)
            return;
        if (model->currentText().isEmpty())
        {
            status->setText("请先在「AI 配置」中为本站点添加模型。");
            return;
        }
        if (!site().loopback() && key->text().trimmed().isEmpty())
        {
            status->setText("请填写本站点的 API 密钥。");
            return;
        }
        if (!probe && task->toPlainText().trimmed().isEmpty())
        {
            status->setText("请填写写作任务。");
            return;
        }
        QSettings settings;
        settings.setValue("writing/provider", site().id);
        settings.setValue("writing/model/" + site().id, model->currentText());
        QJsonArray turns;
        QJsonObject payload{{"model", model->currentText()}, {"stream", !probe}};
        if (probe)
            turns.append(QJsonObject{{"role", "user"}, {"content", "请只回复 OK。"}});
        else
        {
            auto [rules, input] = messages();
            if (site().messagesApi())
                payload["system"] = rules;
            else
                turns.append(QJsonObject{{"role", "system"}, {"content", rules}});
            turns.append(QJsonObject{{"role", "user"}, {"content", input}});
        }
        payload["messages"] = turns;
        if (probe || site().messagesApi())
            payload["max_tokens"] = probe ? 64 : 4096;
        QNetworkRequest request(endpoint());
        request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
        request.setTransferTimeout(60000);
        const auto token = key->text().trimmed();
        site().setHeaders(request, token);
        sessionKeys[site().credentialId()] = token;
        bool canApply = probe && apply->isEnabled();
        if (!probe)
        {
            result->clear();
            result->setReadOnly(true);
            tabs->setCurrentWidget(result);
        }
        apply->setEnabled(false);
        apply->hide();
        response = {};
        response.messagesApi = site().messagesApi();
        timedOut = false;
        auto *reply = network.post(request, QJsonDocument(payload).toJson(QJsonDocument::Compact));
        pending = reply;
        elapsed.start();
        phase = probe ? "正在测试站点和模型" : "正在连接 " + service->currentText();
        updateProgress();
        setBusy(true);
        progress.start(1000);
        deadline.start(probe ? 30000 : 180000);
        connect(reply, &QNetworkReply::requestSent, this, [this, probe] {
            phase = probe ? "测试请求已发送" : "已发送，等待模型输出";
            updateProgress();
        });
        connect(reply, &QIODevice::readyRead, this, [this, reply, probe] {
            if (!probe && isStream(reply) && reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() == 200)
                readStream(reply, false);
        });
        connect(reply, &QNetworkReply::finished, this, [this, reply, probe, canApply] {
            progress.stop();
            deadline.stop();
            pending = nullptr;
            reply->deleteLater();
            setBusy(false);
            apply->setEnabled(canApply);
            apply->setVisible(canApply);
            int code = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            if (timedOut || reply->error() == QNetworkReply::TimeoutError)
            {
                status->setText("请求超时，请检查站点、网络或稍后重试。");
                return;
            }
            if (reply->error() == QNetworkReply::OperationCanceledError)
            {
                status->setText("请求已取消");
                return;
            }
            if (reply->error() != QNetworkReply::NoError || code < 200 || code >= 300)
            {
                auto error = ChatResponse::errorMessage(QJsonDocument::fromJson(reply->readAll()).object());
                status->setText(failure(reply, code, error));
                return;
            }
            QString text;
            bool reasoningOnly = false;
            if (!probe && isStream(reply))
            {
                readStream(reply, true);
                if (!response.error.isEmpty() || !response.finished)
                {
                    status->setText(
                        failure(reply, code, response.error.isEmpty() ? "生成中断，请重新发送。" : response.error));
                    return;
                }
                text = response.text;
            }
            else
            {
                auto obj = QJsonDocument::fromJson(reply->readAll()).object();
                auto choices = obj.value("choices").toArray();
                if (obj.contains("error"))
                {
                    status->setText(failure(reply, code, ChatResponse::errorMessage(obj)));
                    return;
                }
                if (site().messagesApi() && obj["type"] == "message")
                {
                    bool thought = false;
                    for (auto part : obj["content"].toArray())
                    {
                        auto block = part.toObject();
                        if (block["type"] == "text")
                            text += block["text"].toString();
                        else if (block["type"] == "thinking" || block["type"] == "redacted_thinking")
                            thought = true;
                    }
                    const auto finish = obj["stop_reason"].toString();
                    reasoningOnly = probe && text.trimmed().isEmpty() && thought &&
                                    (finish == "max_tokens" || finish == "end_turn");
                }
                else if (!site().messagesApi() && !choices.isEmpty())
                {
                    const auto choice = choices.first().toObject();
                    const auto message = choice["message"].toObject();
                    const auto finish = choice["finish_reason"].toString();
                    text = message["content"].toString();
                    reasoningOnly = probe && text.trimmed().isEmpty() && (finish == "length" || finish == "stop") &&
                                    !message["reasoning_content"].toString().trimmed().isEmpty();
                }
            }
            if (text.trimmed().isEmpty() && !reasoningOnly)
            {
                status->setText("服务未返回正文，请核对模型名称及接口类型。");
                return;
            }
            if (probe)
            {
                status->setText(QString("连接成功 · %1 / %2 · %3 毫秒")
                                    .arg(service->currentText(), model->currentText())
                                    .arg(elapsed.elapsed()) +
                                (reasoningOnly ? "（本次短测试仅返回思考内容）" : ""));
                return;
            }
            if (!isStream(reply))
                result->setPlainText(text);
            result->setReadOnly(false);
            apply->setEnabled(true);
            apply->show();
            tabs->setCurrentWidget(result);
            status->setText("候选正文已就绪，可编辑后采纳。");
        });
    }

  public:
    ~WritingAssistant() override
    {
        if (pending)
        {
            disconnect(pending, nullptr, this, nullptr);
            pending->abort();
        }
    }
    WritingAssistant(Book &book, int currentChapter, QString excerpt, bool replacing, QHash<QString, QString> &keys,
                     std::function<void()> back, std::function<void(QString)> accept)
        : sessionKeys(keys)
    {
        auto *stack = new QStackedLayout(this);
        auto *home = new QWidget;
        home->setObjectName("writingWorkspace");
        home->setStyleSheet(R"(
            QWidget#writingWorkspace { background:#0b1511; }
            QFrame#writingRail { background:#102019; border:1px solid #2b4737; border-radius:8px; }
            QLabel#railHeading { color:#c8f0d2; font-weight:bold; font-size:16px; }
            QLabel#mutedLabel { color:#9bb6a7; }
            QFrame#railRule { background:#304638; }
            QTreeWidget { background:transparent; border:none; padding:0; }
            QTabWidget::pane { background:#12231b; border:1px solid #324b3c; border-radius:5px; }
            QTabBar::tab { background:transparent; color:#a2b7aa; border-bottom:2px solid transparent; padding:9px 16px; }
            QTabBar::tab:selected { color:#d5f9dd; background:#183223; border-bottom-color:#92ddaa; }
            QPlainTextEdit { background:#0c1913; border:1px solid #34513e; border-radius:5px; padding:6px; }
            QPlainTextEdit:focus { border-color:#80be92; }
        )");
        stack->addWidget(home);
        auto *layout = new QVBoxLayout(home);
        layout->setContentsMargins(22, 16, 22, 16);
        layout->setSpacing(14);
        auto *top = new QHBoxLayout;
        layout->addLayout(top);
        auto *heading = new QLabel("AI 写作");
        heading->setObjectName("title");
        top->addWidget(heading);
        auto chapterTitle =
            new QLabel(book.value("SELECT title FROM chapters WHERE id=?", {currentChapter}).toString());
        chapterTitle->setTextFormat(Qt::PlainText);
        chapterTitle->setObjectName("mutedLabel");
        chapterTitle->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        top->addSpacing(10);
        top->addWidget(chapterTitle, 1);
        top->addWidget(config);
        auto *close = new QPushButton("返回正文");
        top->addWidget(close);
        service->setObjectName("aiService");
        model->setObjectName("aiModel");
        model->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
        model->setMinimumContentsLength(10);
        key->setEchoMode(QLineEdit::Password);
        test->setToolTip("仅发送简短测试消息，可能产生少量 API 用量。");
        connect(config, &QPushButton::clicked, this, [this, stack, home] {
            const auto current = site().id;
            auto *page = new AiConfig(sessionKeys, current, [this, stack, home, current](QString selected) {
                auto *old = stack->currentWidget();
                stack->setCurrentWidget(home);
                stack->removeWidget(old);
                old->deleteLater();
                reloadSites(selected.isEmpty() ? current : selected);
            });
            stack->addWidget(page);
            stack->setCurrentWidget(page);
        });
        auto *panes = new QSplitter;
        layout->addWidget(panes, 1);
        panes->setChildrenCollapsible(false);
        panes->setHandleWidth(12);
        auto *library = new QFrame;
        library->setObjectName("writingRail");
        library->setMinimumWidth(230);
        auto *left = new QVBoxLayout(library);
        left->setContentsMargins(16, 16, 16, 12);
        left->setSpacing(8);
        auto *settingsHeading = new QLabel("生成设置");
        settingsHeading->setObjectName("railHeading");
        left->addWidget(settingsHeading);
        auto *fields = new QFormLayout;
        fields->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
        fields->addRow("服务", service);
        fields->addRow("模型", model);
        fields->addRow("密钥", key);
        left->addLayout(fields);
        left->addWidget(test);
        left->addSpacing(6);
        auto *rule = new QFrame;
        rule->setObjectName("railRule");
        rule->setFixedHeight(1);
        left->addWidget(rule);
        left->addSpacing(4);
        auto *referenceHeader = new QHBoxLayout;
        auto *refHeading = new QLabel("写作资料");
        refHeading->setObjectName("railHeading");
        referenceHeader->addWidget(refHeading);
        referenceHeader->addStretch();
        referenceCount->setObjectName("mutedLabel");
        referenceHeader->addWidget(referenceCount);
        left->addLayout(referenceHeader);
        auto *help = new QLabel("勾选随本次请求发送的条目。");
        help->setObjectName("mutedLabel");
        help->setWordWrap(true);
        left->addWidget(help);
        sources->setObjectName("writingSources");
        sources->setHeaderHidden(true);
        left->addWidget(sources, 1);
        panes->addWidget(library);
        auto *workspace = new QWidget;
        auto *workLayout = new QVBoxLayout(workspace);
        workLayout->setContentsMargins(0, 0, 0, 0);
        workLayout->setSpacing(12);
        workLayout->addWidget(tabs, 1);
        panes->addWidget(workspace);
        panes->setSizes({258, 790});
        panes->setStretchFactor(1, 1);
        QMap<int, QString> chapters;
        auto q = book.run("SELECT id,title FROM chapters");
        while (q.next())
            chapters[q.value(0).toInt()] = q.value(1).toString();
        for (auto scope : {NoteScope::Global, NoteScope::Book, NoteScope::Outline})
        {
            auto *group = new QTreeWidgetItem(sources, {scopeName(scope)});
            for (const auto &note : WritingNotes(book, scope).list())
            {
                QString label = note.title;
                if (scope == NoteScope::Outline)
                {
                    QString where = note.chapter ? chapters.value(note.chapter) : QString("全书");
                    if (note.chapter == currentChapter)
                        where += " · 当前章";
                    label = where + " / " + label;
                }
                auto *item = new QTreeWidgetItem(group, {label});
                item->setCheckState(0, Qt::Unchecked);
                item->setData(0, Qt::UserRole, note.body);
                item->setToolTip(0, note.body);
            }
            group->setExpanded(true);
        }
        auto *compose = new QWidget;
        auto *inputs = new QVBoxLayout(compose);
        inputs->setContentsMargins(18, 16, 18, 16);
        inputs->setSpacing(8);
        inputs->addWidget(new QLabel("写作任务"));
        inputs->addWidget(task, 1);
        task->setPlainText(replacing ? "润色所选段落，保留原意和情节。" : "承接上下文，续写下一段。");
        inputs->addSpacing(6);
        inputs->addWidget(new QLabel("正文上下文"));
        context->setPlaceholderText("输入需要参考的正文，或从编辑器选中一段文字后进入。");
        inputs->addWidget(context, 3);
        context->setPlainText(excerpt);
        tabs->addTab(compose, "写作要求");
        tabs->addTab(preview, "发送预览");
        tabs->addTab(result, "候选正文");
        preview->setReadOnly(true);
        result->setReadOnly(true);
        context->setFont(QFont("Microsoft YaHei UI", 12));
        result->setFont(context->font());
        for (auto *field : {task, context, preview, result})
            field->document()->setDocumentMargin(10);
        auto *actions = new QHBoxLayout;
        workLayout->addLayout(actions);
        status->setTextFormat(Qt::PlainText);
        status->setWordWrap(true);
        status->setObjectName("mutedLabel");
        status->setText("准备就绪");
        actions->addWidget(status, 1);
        actions->addWidget(stop);
        actions->addWidget(send);
        actions->addWidget(apply);
        stop->setEnabled(false);
        stop->hide();
        apply->setEnabled(false);
        apply->hide();
        send->setObjectName("primary");
        apply->setText(replacing ? "采纳并替换选区" : "采纳并插入光标处");
        reloadSites(QSettings().value("writing/provider", "deepseek").toString());
        connect(service, &QComboBox::currentIndexChanged, this, [this] { loadService(); });
        connect(model, &QComboBox::currentTextChanged, this, [this] { updatePreview(); });
        connect(key, &QLineEdit::textEdited, this,
                [this] { sessionKeys[site().credentialId()] = key->text().trimmed(); });
        deadline.setSingleShot(true);
        deadline.setParent(this);
        deadline.setObjectName("requestDeadline");
        connect(&deadline, &QTimer::timeout, this, [this] {
            if (pending)
            {
                timedOut = true;
                pending->abort();
            }
        });
        connect(&progress, &QTimer::timeout, this, [this] { updateProgress(); });
        connect(test, &QPushButton::clicked, this, [this] { submit(true); });
        for (auto *field : {task, context})
            connect(field, &QPlainTextEdit::textChanged, this, [this] { updatePreview(); });
        connect(sources, &QTreeWidget::itemChanged, this, [this] { updatePreview(); });
        connect(send, &QPushButton::clicked, this, [this] { submit(); });
        connect(stop, &QPushButton::clicked, this, [this] {
            if (pending)
                pending->abort();
        });
        connect(close, &QPushButton::clicked, this, [this, back] {
            if (pending)
                pending->abort();
            back();
        });
        connect(apply, &QPushButton::clicked, this, [this, accept] {
            if (!result->toPlainText().trimmed().isEmpty())
                accept(result->toPlainText());
        });
    }
};
