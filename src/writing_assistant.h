#pragma once
#include "writing_notes.h"
#include <QtNetwork>
#include <QtWidgets>

class WritingAssistant : public QWidget
{
    QComboBox *service = new QComboBox;
    QLineEdit *model = new QLineEdit, *key = new QLineEdit;
    QSpinBox *port = new QSpinBox;
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
        for (QWidget *control : QList<QWidget *>{service, model, key, port, task, context, sources, send})
            control->setEnabled(!busy);
        stop->setEnabled(busy);
    }

    QString provider() const
    {
        return service->currentData().toString();
    }
    bool local() const
    {
        return provider() == "local";
    }
    QUrl endpoint() const
    {
        if (local())
            return QUrl(QString("http://127.0.0.1:%1/v1/chat/completions").arg(port->value()));
        if (provider() == "deepseek")
            return QUrl("https://api.deepseek.com/chat/completions");
        return QUrl("https://open.bigmodel.cn/api/paas/v4/chat/completions");
    }
    void loadService()
    {
        auto p = provider();
        QString preset = local() ? QString() : p == "deepseek" ? "deepseek-flash" : "glm-5.3";
        model->setText(QSettings().value("writing/model/" + p, preset).toString());
        key->setText(sessionKeys.value(p));
        key->setVisible(!local());
        port->setVisible(local());
        updatePreview();
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
        preview->setPlainText("目标地址  " + endpoint().toString() + "\n模型  " + model->text() + "\n\n写作规则\n" +
                              rules + "\n\n" + input);
    }
    void submit()
    {
        if (pending)
            return;
        if (model->text().trimmed().isEmpty() || (!local() && key->text().trimmed().isEmpty()) ||
            task->toPlainText().trimmed().isEmpty())
        {
            status->setText("请填写模型名称、写作任务及所选云服务的 API 密钥。");
            return;
        }
        QSettings settings;
        settings.setValue("writing/provider", provider());
        settings.setValue("writing/model/" + provider(), model->text().trimmed());
        if (local())
            settings.setValue("writing/port", port->value());
        auto [rules, input] = messages();
        QJsonArray turns{QJsonObject{{"role", "system"}, {"content", rules}},
                         QJsonObject{{"role", "user"}, {"content", input}}};
        QJsonObject payload{{"model", model->text().trimmed()}, {"messages", turns}, {"stream", false}};
        QNetworkRequest request(endpoint());
        request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
        request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
        request.setTransferTimeout(120000);
        if (!local())
        {
            sessionKeys[provider()] = key->text().trimmed();
            request.setRawHeader("Authorization", "Bearer " + key->text().trimmed().toUtf8());
        }
        pending = network.post(request, QJsonDocument(payload).toJson(QJsonDocument::Compact));
        result->clear();
        result->setReadOnly(true);
        apply->setEnabled(false);
        setBusy(true);
        tabs->setCurrentWidget(result);
        status->setText("正在等待" + service->currentText() + "返回候选正文…");
        connect(pending, &QNetworkReply::finished, this, [this] {
            auto *reply = pending.data();
            pending = nullptr;
            reply->deleteLater();
            setBusy(false);
            int code = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            if (reply->error() == QNetworkReply::OperationCanceledError)
            {
                status->setText("请求已取消");
                return;
            }
            if (reply->error() != QNetworkReply::NoError || code < 200 || code >= 300)
            {
                QString reason;
                if (code == 401 || code == 403)
                    reason = "请检查 API 密钥和模型权限";
                else if (code == 402)
                    reason = "请检查账户余额";
                else if (code == 429)
                    reason = "请求过于频繁，请稍后再试";
                else if (code == 400 || code == 404)
                    reason = "请检查模型名称及接口配置";
                else
                    reason = "请检查服务连接后重试";
                status->setText(
                    QString("请求失败%1，%2。").arg(code ? QString("（HTTP %1）").arg(code) : QString(), reason));
                return;
            }
            auto choices = QJsonDocument::fromJson(reply->readAll()).object().value("choices").toArray();
            auto text = choices.isEmpty()
                            ? QString()
                            : choices.first().toObject().value("message").toObject().value("content").toString();
            if (text.trimmed().isEmpty())
            {
                status->setText("服务未返回候选正文，请检查模型是否支持对话接口。");
                return;
            }
            result->setPlainText(text);
            result->setReadOnly(false);
            apply->setEnabled(true);
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
        auto *layout = new QVBoxLayout(this);
        layout->setContentsMargins(22, 14, 22, 14);
        auto *top = new QHBoxLayout;
        layout->addLayout(top);
        auto *heading = new QLabel("AI 写作");
        heading->setObjectName("title");
        top->addWidget(heading);
        top->addStretch();
        auto *close = new QPushButton("返回正文");
        top->addWidget(close);
        auto *providerRow = new QHBoxLayout;
        layout->addLayout(providerRow);
        service->addItem("DeepSeek", "deepseek");
        service->addItem("智谱 GLM", "glm");
        service->addItem("本机兼容服务", "local");
        providerRow->addWidget(new QLabel("服务"));
        providerRow->addWidget(service);
        providerRow->addWidget(new QLabel("模型"));
        providerRow->addWidget(model, 1);
        model->setPlaceholderText("模型标识");
        key->setPlaceholderText("API 密钥（仅本次运行使用）");
        key->setEchoMode(QLineEdit::Password);
        providerRow->addWidget(key, 1);
        port->setPrefix("本机端口  ");
        port->setRange(1, 65535);
        port->setValue(QSettings().value("writing/port", 8080).toInt());
        providerRow->addWidget(port);
        auto *panes = new QSplitter;
        layout->addWidget(panes, 1);
        sources->setHeaderLabel("本次使用的资料");
        panes->addWidget(sources);
        panes->addWidget(tabs);
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
        inputs->addWidget(new QLabel("写作任务"));
        inputs->addWidget(task, 1);
        task->setPlainText(replacing ? "润色所选段落，保留原意和情节。" : "承接上下文，续写下一段。");
        inputs->addWidget(new QLabel("正文上下文（可修改发送范围）"));
        inputs->addWidget(context, 3);
        context->setPlainText(excerpt);
        tabs->addTab(compose, "写作要求");
        tabs->addTab(preview, "发送预览");
        tabs->addTab(result, "候选正文");
        preview->setReadOnly(true);
        result->setReadOnly(true);
        auto *actions = new QHBoxLayout;
        layout->addLayout(actions);
        status->setWordWrap(true);
        status->setText("在资料工作区管理 Skill 和大纲。在此勾选后，可通过「发送预览」核对本次内容。");
        actions->addWidget(status, 1);
        actions->addWidget(stop);
        actions->addWidget(send);
        actions->addWidget(apply);
        stop->setEnabled(false);
        apply->setEnabled(false);
        send->setObjectName("primary");
        apply->setText(replacing ? "采纳并替换选区" : "采纳并插入光标处");
        service->setCurrentIndex(qMax(0, service->findData(QSettings().value("writing/provider", "deepseek"))));
        loadService();
        connect(service, &QComboBox::currentIndexChanged, this, [this] { loadService(); });
        connect(port, &QSpinBox::valueChanged, this, [this] { updatePreview(); });
        connect(model, &QLineEdit::textChanged, this, [this] { updatePreview(); });
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
