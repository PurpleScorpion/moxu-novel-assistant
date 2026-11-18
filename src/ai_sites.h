#pragma once
#include <QtNetwork>
#include <QtWidgets>
#include <functional>
#include <stdexcept>
#ifdef Q_OS_WIN
#include <windows.h>

#include <wincred.h>
#endif

struct AiSite
{
    QString id, name, address;
    QStringList models;
    QString protocol = "chat";
    bool messagesApi() const
    {
        return protocol == "messages" || protocol == "messages-bearer";
    }
    QUrl endpoint() const
    {
        QUrl url(address.trimmed(), QUrl::StrictMode);
        auto path = url.path();
        while (path.endsWith('/'))
            path.chop(1);
        if (messagesApi())
        {
            if (!path.endsWith("/v1/messages"))
                path += path.endsWith("/v1") ? "/messages" : "/v1/messages";
        }
        else if (!path.endsWith("/chat/completions"))
            path += "/chat/completions";
        url.setPath(path);
        return url;
    }
    void setHeaders(QNetworkRequest &request, const QString &key) const
    {
        request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
        if (messagesApi())
            request.setRawHeader("anthropic-version", "2023-06-01");
        if (key.isEmpty())
            return;
        if (protocol == "messages")
            request.setRawHeader("x-api-key", key.toUtf8());
        else
            request.setRawHeader("Authorization", "Bearer " + key.toUtf8());
    }
    bool loopback() const
    {
        const auto host = endpoint().host();
        return host == "127.0.0.1" || host == "localhost" || host == "::1";
    }
    QString credentialId() const
    {
        return "Moxu/AI/" + id + "/" +
               QString::fromLatin1(
                   QCryptographicHash::hash(endpoint().toEncoded(), QCryptographicHash::Sha256).toHex());
    }
    bool validAddress() const
    {
        auto url = endpoint();
        return url.isValid() && !url.host().isEmpty() && url.userInfo().isEmpty() && !url.hasQuery() &&
               !url.hasFragment() && (url.scheme() == "https" || (url.scheme() == "http" && loopback()));
    }
};

inline QList<AiSite> aiSites()
{
    QSettings prefs;
    QList<AiSite> sites{
        {"deepseek", "DeepSeek", "https://api.deepseek.com", {"deepseek-flash", "deepseek-v4-pro"}},
        {"glm", "智谱 GLM（国内）", "https://open.bigmodel.cn/api/paas/v4", {"glm-5.3", "glm-5.2", "glm-5-turbo"}},
        {"zai", "Z.AI GLM（国际）", "https://api.z.ai/api/paas/v4", {"glm-5.2", "glm-5-turbo", "glm-5.3"}},
        {"zai-messages",
         "Z.AI（ZCode 接口）",
         "https://api.z.ai/api/anthropic",
         {"GLM-5.2", "GLM-5-Turbo"},
         "messages-bearer"},
        {"local",
         "本机兼容服务",
         QString("http://127.0.0.1:%1/v1").arg(prefs.value("writing/port", 8080).toInt()),
         {}}};
    for (auto &site : sites)
    {
        const auto old = prefs.value("writing/model/" + site.id).toString();
        if (!old.isEmpty() && !site.models.contains(old))
            site.models.prepend(old);
    }
    prefs.beginGroup("aiSites");
    for (auto id : prefs.childKeys())
    {
        auto obj = QJsonDocument::fromJson(prefs.value(id).toByteArray()).object();
        AiSite site{id, obj["name"].toString(), obj["address"].toString(), obj["models"].toVariant().toStringList()};
        site.protocol = obj["protocol"].toString("chat");
        auto it = std::find_if(sites.begin(), sites.end(), [&](const AiSite &s) { return s.id == id; });
        if (it == sites.end())
            sites.append(site);
        else
            *it = site;
    }
    return sites;
}
inline QString storedAiKey(const QString &id)
{
#ifdef Q_OS_WIN
    PCREDENTIALW credential = nullptr;
    if (CredReadW(id.toStdWString().c_str(), CRED_TYPE_GENERIC, 0, &credential))
    {
        auto key = QString::fromUtf8(reinterpret_cast<const char *>(credential->CredentialBlob),
                                     credential->CredentialBlobSize);
        CredFree(credential);
        return key;
    }
#endif
    return {};
}
inline void storeAiKey(const QString &id, const QString &key)
{
#ifdef Q_OS_WIN
    auto name = id.toStdWString();
    if (key.isEmpty())
    {
        if (!CredDeleteW(name.c_str(), CRED_TYPE_GENERIC, 0) && GetLastError() != ERROR_NOT_FOUND)
            throw std::runtime_error("无法清除 Windows 凭据。");
        return;
    }
    auto bytes = key.toUtf8();
    CREDENTIALW c{};
    c.Type = CRED_TYPE_GENERIC;
    c.TargetName = name.data();
    c.Persist = CRED_PERSIST_LOCAL_MACHINE;
    c.CredentialBlob = reinterpret_cast<LPBYTE>(bytes.data());
    c.CredentialBlobSize = bytes.size();
    if (!CredWriteW(&c, 0))
        throw std::runtime_error("Windows 凭据保存失败。");
#endif
}

class AiConfig : public QWidget
{
    QList<AiSite> sites = aiSites();
    QListWidget *list = new QListWidget;
    QLineEdit *name = new QLineEdit, *address = new QLineEdit, *key = new QLineEdit;
    QPlainTextEdit *models = new QPlainTextEdit;
    QComboBox *protocol = new QComboBox;
    QCheckBox *remember = new QCheckBox("在此电脑记住密钥（Windows 凭据管理器）");
    QLabel *notice = new QLabel;
    QHash<QString, QString> &keys;
    QString id;
    QNetworkAccessManager network;
    QPointer<QNetworkReply> catalog;
    QPushButton *fetch = new QPushButton("从站点读取模型");
    void cancelCatalog()
    {
        if (catalog)
        {
            auto *reply = catalog.data();
            catalog.clear();
            reply->abort();
            fetch->setEnabled(true);
        }
    }
    void fetchModels()
    {
        AiSite site{id, name->text(), address->text(), {}, protocol->currentData().toString()};
        if (!site.validAddress())
        {
            notice->setText("请先填写有效的站点地址。");
            return;
        }
        auto url = site.endpoint().resolved(QUrl(site.messagesApi() ? "models" : "../models"));
        QNetworkRequest request(url);
        site.setHeaders(request, key->text().trimmed());
        auto *reply = network.get(request);
        catalog = reply;
        fetch->setEnabled(false);
        notice->setText("正在读取此站点的模型列表…");
        QTimer::singleShot(15000, reply, [reply] {
            if (!reply->isFinished())
                reply->abort();
        });
        connect(reply, &QNetworkReply::finished, this, [this, reply] {
            reply->deleteLater();
            if (catalog != reply)
                return;
            catalog.clear();
            fetch->setEnabled(true);
            const auto http = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            if (reply->error() != QNetworkReply::NoError || http != 200)
            {
                notice->setText(http ? QString("读取模型失败（HTTP %1）。请核对地址、密钥，或手动添加模型。").arg(http)
                                     : "读取模型失败，请检查网络，或手动添加模型。");
                return;
            }
            auto data = QJsonDocument::fromJson(reply->readAll()).object()["data"].toArray();
            QStringList names = models->toPlainText().split('\n', Qt::SkipEmptyParts);
            int found = 0;
            for (auto item : data)
            {
                const auto model = item.toObject()["id"].toString().trimmed();
                if (model.isEmpty() || model.contains('\n') || model.contains('\r'))
                    continue;
                ++found;
                if (!names.contains(model))
                    names.append(model);
            }
            if (found)
            {
                models->setPlainText(names.join('\n'));
                notice->setText(QString("站点返回了 %1 个模型。可编辑列表，再点「保存配置」。").arg(found));
            }
            else
                notice->setText("站点未返回模型列表，可手动添加模型。");
        });
    }
    void load(int row)
    {
        cancelCatalog();
        AiSite site = sites.value(row);
        QSignalBlocker block(address), blockProtocol(protocol);
        id = site.id;
        name->setText(site.name);
        protocol->setCurrentIndex(qMax(0, protocol->findData(site.protocol)));
        address->setText(site.address);
        models->setPlainText(site.models.join('\n'));
        const auto boundCredential = site.id.isEmpty() ? QString() : site.credentialId();
        auto stored = boundCredential.isEmpty() ? QString() : storedAiKey(boundCredential);
        key->setText(keys.value(boundCredential, stored));
        remember->setChecked(!stored.isEmpty());
        notice->clear();
    }
    void save()
    {
        AiSite site{id, name->text().trimmed(), address->text().trimmed(), {}, protocol->currentData().toString()};
        for (auto line : models->toPlainText().split('\n'))
        {
            line = line.trimmed();
            if (!line.isEmpty() && !site.models.contains(line))
                site.models.append(line);
        }
        if (site.name.isEmpty() || site.models.isEmpty())
            throw std::runtime_error("请填写站点名称，并至少配置一个模型。");
        if (!site.validAddress())
            throw std::runtime_error("请填写兼容对话 API 的 HTTPS 基础地址。本机服务也可使用 HTTP。");
        if (site.id.isEmpty())
            site.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        const auto credential = site.credentialId();
        if (remember->isChecked() || !storedAiKey(credential).isEmpty())
            storeAiKey(credential, remember->isChecked() ? key->text().trimmed() : QString());
        QSettings settings;
        settings.setValue("aiSites/" + site.id,
                          QJsonDocument(QJsonObject{{"name", site.name},
                                                    {"protocol", site.protocol},
                                                    {"address", site.address},
                                                    {"models", QJsonArray::fromStringList(site.models)}})
                              .toJson(QJsonDocument::Compact));
        settings.sync();
        if (settings.status() != QSettings::NoError)
            throw std::runtime_error("无法写入站点设置。");
        auto row = list->currentRow();
        if (row >= 0 && sites[row].credentialId() != credential)
        {
            auto old = sites[row].credentialId();
            keys.remove(old);
            if (!storedAiKey(old).isEmpty())
                storeAiKey(old, {});
        }
        keys[credential] = key->text().trimmed();
        if (row < 0)
        {
            sites.append(site);
            list->addItem(site.name);
            list->setCurrentRow(sites.size() - 1);
        }
        else
        {
            sites[row] = site;
            list->item(row)->setText(site.name);
            load(row);
        }
        notice->setText("站点和模型已保存");
    }

  public:
    AiConfig(QHash<QString, QString> &keys, QString selected, std::function<void(QString)> back) : keys(keys)
    {
        name->setObjectName("siteName");
        address->setObjectName("siteAddress");
        models->setObjectName("siteModels");
        list->setObjectName("siteList");
        key->setObjectName("siteKey");
        notice->setObjectName("siteNotice");
        notice->setWordWrap(true);
        notice->setTextFormat(Qt::PlainText);
        auto *root = new QVBoxLayout(this);
        root->setContentsMargins(24, 16, 24, 16);
        auto *heading = new QLabel("AI 配置");
        heading->setObjectName("title");
        root->addWidget(heading);
        auto *columns = new QHBoxLayout;
        root->addLayout(columns, 1);
        auto *left = new QVBoxLayout;
        columns->addLayout(left, 1);
        left->addWidget(new QLabel("服务站点"));
        left->addWidget(list);
        auto *add = new QPushButton("新增站点");
        left->addWidget(add);
        auto *form = new QVBoxLayout;
        columns->addLayout(form, 3);
        form->addWidget(new QLabel("站点名称"));
        form->addWidget(name);
        form->addWidget(new QLabel("接口协议"));
        protocol->setObjectName("siteProtocol");
        protocol->addItem("OpenAI 兼容（Chat Completions）", "chat");
        protocol->addItem("Anthropic Messages（API Key）", "messages");
        protocol->addItem("Anthropic Messages（Bearer，ZCode 接口）", "messages-bearer");
        form->addWidget(protocol);
        form->addWidget(new QLabel("接口基础地址"));
        form->addWidget(address);
        auto *modelHeader = new QHBoxLayout;
        form->addLayout(modelHeader);
        modelHeader->addWidget(new QLabel("可选模型 · 每行一个模型标识"), 1);
        modelHeader->addWidget(fetch);
        connect(fetch, &QPushButton::clicked, this, [this] { fetchModels(); });
        form->addWidget(models, 1);
        form->addWidget(new QLabel("API 密钥"));
        key->setEchoMode(QLineEdit::Password);
        form->addWidget(key);
        form->addWidget(remember);
        form->addWidget(notice);
        auto *buttons = new QHBoxLayout;
        form->addLayout(buttons);
        auto *done = new QPushButton("返回");
        auto *commit = new QPushButton("保存配置");
        buttons->addWidget(done);
        buttons->addWidget(commit);
        for (const auto &site : sites)
            list->addItem(site.name);
        connect(list, &QListWidget::currentRowChanged, this, [this](int row) { load(row); });
        connect(protocol, &QComboBox::currentIndexChanged, this, [this] {
            cancelCatalog();
            key->clear();
            remember->setChecked(false);
            notice->setText("协议已更改，请核对基础地址并填写对应的密钥。");
        });
        connect(address, &QLineEdit::textChanged, this, [this] {
            cancelCatalog();
            key->clear();
            remember->setChecked(false);
            notice->setText("地址已更改，请填写此站点的密钥。");
        });
        connect(add, &QPushButton::clicked, this, [this] {
            list->setCurrentRow(-1);
            load(-1);
            name->setFocus();
        });
        connect(done, &QPushButton::clicked, this, [this, back] { back(id); });
        connect(commit, &QPushButton::clicked, this, [this] {
            try
            {
                save();
            }
            catch (const std::exception &e)
            {
                notice->setText(QString::fromUtf8(e.what()));
            }
        });
        int row = 0;
        for (int i = 0; i < sites.size(); ++i)
            if (sites[i].id == selected)
                row = i;
        list->setCurrentRow(row);
    }
};
