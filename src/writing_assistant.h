#pragma once
#include "token_estimate.h"
#include "revision_context.h"
#include "downward_combo.h"
#include "chapter_diff.h"
#include "chapter_summary.h"
#include "chapter_check.h"
#include "chapter_text.h"
#include "ai_sites.h"
#include "chat_response.h"
#include "writing_notes.h"
#include "writing_sources.h"
#include "studio_data.h"
#include <QScopeGuard>
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
    static constexpr int generationIdleMs = 5 * 60 * 1000;
    QElapsedTimer elapsed;
    QString phase;
    bool timedOut = false;
    ChatResponse response;
    WritingSources *sources = new WritingSources;
    QMap<int, QListWidgetItem *> fullEntries, summaryEntries;
    ChapterCheck *check = nullptr;
    ChapterSummary *summaries = nullptr;
    QString targetChapter, originalBody;
    QPlainTextEdit *task = new QPlainTextEdit, *context = new QPlainTextEdit, *preview = new QPlainTextEdit,
                   *result = new QPlainTextEdit;
    QTabWidget *tabs = new QTabWidget;
    QPlainTextEdit *feedback = new QPlainTextEdit;
    QPlainTextEdit *revisionLog = new QPlainTextEdit;
    QPushButton *revise = new QPushButton("发送修改意见");
    QPushButton *applyChapter = new QPushButton("覆盖本章");
    QTextBrowser *comparison = new QTextBrowser;
    QLabel *draftLength = new QLabel;
    QLabel *draftWarning = new QLabel("本次输出已达上限，候选正文可能没有写完。请核对结尾后再采纳。");
    bool wholeSelection;
    QPushButton *useChapter = new QPushButton("使用本章全文");
    QPushButton *usePrevious = new QPushButton("引用上一章");
    QPushButton *prepare = new QPushButton("准备本章资料");
    QJsonArray revisionHistory;
    QCheckBox *compressHistory = new QCheckBox("自动压缩上下文");
    bool hasDraft = false, previewRevision = false, draftTruncated = false;
    QPushButton *send = new QPushButton("发送给 AI"), *apply = new QPushButton, *stop = new QPushButton("取消请求");
    QLabel *status = new QLabel;
    QNetworkAccessManager network;
    QPointer<QNetworkReply> pending;
    QHash<QString, QString> &sessionKeys;

    void setBusy(bool busy)
    {
        for (QWidget *control : QList<QWidget *>{service, model, key, config, test, compressHistory, task, context, sources, send})
            control->setEnabled(!busy);
        stop->setEnabled(busy);
        stop->setVisible(busy);
        useChapter->setEnabled(!busy);
        usePrevious->setEnabled(!busy);
        prepare->setEnabled(!busy);
        summaries->setEnabled(!busy);
        if (check) check->setEnabled(!busy);
    }
    void updateActions()
    {
        const bool reviewing = tabs->currentWidget() == check || tabs->currentWidget() == revisionLog || tabs->currentWidget() == summaries;
        const bool comparing = tabs->currentWidget() == comparison;
        const bool candidatePage = tabs->currentWidget() == result;
        const bool ready = hasDraft && !pending;
        const bool revision = hasDraft && previewRevision && !reviewing && !comparing;
        feedback->setVisible(revision);
        feedback->setEnabled(!pending);
        revise->setVisible(revision && !pending);
        revise->setEnabled(!feedback->toPlainText().trimmed().isEmpty() && !result->toPlainText().trimmed().isEmpty());
        send->setVisible(!revision && !reviewing && !comparing && !pending);
        apply->setVisible(ready && !comparing && !wholeSelection && tabs->currentWidget() != summaries);
        apply->setEnabled(hasDraft && !pending && !result->toPlainText().trimmed().isEmpty());
        tabs->setTabVisible(tabs->indexOf(comparison), ready);
        applyChapter->setVisible(ready && candidatePage);
        applyChapter->setEnabled(apply->isEnabled());
        draftWarning->setVisible(ready && candidatePage && draftTruncated);
        draftLength->setVisible(ready && (candidatePage || comparing));
        if (ready && (candidatePage || comparing))
            draftLength->setText(QString("候选稿 %1 字").arg(chapterWordCount(result->toPlainText())));
    }
    QJsonArray revisionTurns() const
    {
        auto turns = revisionHistory;
        turns.append(QJsonObject{{"role", "assistant"}, {"content", result->toPlainText()}});
        turns.append(QJsonObject{{"role", "user"}, {"content", feedback->toPlainText()}});
        return turns;
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
    QPair<QString, QString> messages(TokenEstimate *estimate = nullptr) const
    {
        QString rules = "你是中文小说写作助手。遵循作者的写作要求，只返回可直接使用的小说正文，勿附字数估算。若选用的写作规则冲突，优"
                        "先遵循本书规则。按作者给定的时间、地点和设定行文，核对年份和时间跨度。"
                        "收到修改意见时，在上一版正文上修改，保留此前已确认的修正和无关段落，返回完整修订稿。";
        QStringList outlines;
        QMap<int, QString> preceding;
        for (const auto &section : sources->sections)
        {
            for (const auto *item : section.entries)
            {
                if (item->checkState() != Qt::Checked)
                    continue;
                auto group = WritingSources::groupOf(item);
                const auto body = item->data(WritingSources::Body).toString();
                if (estimate) estimate->add(section.name + " · " + item->text(), body);
                QString part =
                    "【" + section.name + " · " + item->text() + "】\n" + body;
                if (group == WritingSources::Full || group == WritingSources::Summary || group == WritingSources::Tail)
                    preceding.insert(item->data(WritingSources::ChapterOrder).toInt(), part);
                else if (group == WritingSources::Outline || group == WritingSources::Snapshot)
                    outlines.append(part);
                else
                    rules += "\n\n" + part;
            }
        }
        QString input = "写作任务\n" + task->toPlainText();
        if (!preceding.isEmpty())
        {
            rules += "\n所引前文是已发生的剧情，大纲是创作计划。续写时核对章末的人物位置、时间、"
                     "已知信息和未完成的行动，从当前章向前推进；润色选段时仅用前文核对连续性。";
            input += "\n\n当前写作章节\n" + targetChapter +
                     "\n\n前文章节（按原书顺序）\n" + preceding.values().join("\n\n");
        }
        if (!outlines.isEmpty())
            input += "\n\n剧情参考\n" + outlines.join("\n\n");
        const auto excerpt = context->toPlainText();
        if (!excerpt.isEmpty())
            input += "\n\n正文上下文\n" + excerpt;
        if (estimate)
        {
            estimate->add("写作任务", task->toPlainText());
            if (!excerpt.isEmpty())
                estimate->add((excerpt == originalBody ? "本章全文 · " : "正文上下文 · ") + targetChapter, excerpt);
            estimate->addWeight("系统规则与资料标题", tokenWeight(rules) + tokenWeight(input) - estimate->weight);
        }
        return {rules, input};
    }
    void prepareMaterials(Book &book, int chapter)
    {
        using S = WritingSources;
        auto recommend = [](QListWidgetItem *item, const QString &reason) {
            item->setData(S::Recommendation, reason);
            item->setCheckState(Qt::Checked);
        };
        QString mentions = task->toPlainText() + "\n" + context->toPlainText() + "\n" + originalBody;
        for (auto *entry : sources->sections[S::Outline].entries)
            if (entry->data(S::ChapterId).toInt() == chapter)
            {
                recommend(entry, "这条大纲明确关联当前章。");
                mentions += "\n" + entry->data(S::Body).toString();
            }
        for (auto *entry : sources->sections[S::BookSkill].entries)
            if (entry->text().contains("世界观") || entry->text().contains("写作风格") ||
                entry->text().contains("创作规范") || mentions.contains(entry->text()))
                recommend(entry, "本书写作约定或本章提及的 Skill。");

        const auto &prior = sources->sections[S::Full].entries;
        if (prior.isEmpty()) return;
        auto *previous = prior.first();
        int previousId = previous->data(S::ChapterId).toInt();
        auto tail = book.value("SELECT substr(body,-1200) FROM chapters WHERE id=?", {previousId}).toString();
        mentions += "\n" + tail;
        auto &tails = sources->sections[S::Tail];
        if (!tail.trimmed().isEmpty())
        {
            auto *entry = tails.entries.isEmpty() ? sources->addEntry(S::Tail,previous->text(),tail,previousId) : tails.entries.first();
            entry->setData(S::ChapterOrder, previous->data(S::ChapterOrder));
            tails.heading->setHidden(false);
            if (previous->checkState() != Qt::Checked && summaryEntries.value(previousId)->checkState() != Qt::Checked)
                recommend(entry, "上一章最后 1200 个字符，用于衔接当前场景。");
        }
        int recent = 0;
        for (auto *full : prior)
        {
            auto id = full->data(S::ChapterId).toInt();
            auto *summary = summaryEntries.value(id);
            if (id == previousId || full->checkState()==Qt::Checked || !summary->data(S::Available).toBool()) continue;
            recommend(summary, "较早章节已有剧情摘要，可用来核对前情。");
            if (++recent == 2) break;
        }
        auto &snapshots = sources->sections[S::Snapshot];
        auto people = book.run("SELECT id,name FROM people ORDER BY id");
        while (people.next())
        {
            int id = people.value(0).toInt();
            auto name = people.value(1).toString();
            if (name.isEmpty() || !mentions.contains(name)) continue;
            auto found = std::find_if(snapshots.entries.begin(),snapshots.entries.end(),[id](auto *entry){
                return entry->data(S::ChapterId).toInt()==id;
            });
            if (found != snapshots.entries.end()) { recommend(*found, "本章或前文章末提及此人，快照取上一章末的账本记录。"); continue; }
            auto body = ledgerSnapshot(book,id,previousId);
            if (body.isEmpty()) continue;
            auto *entry = sources->addEntry(S::Snapshot,name + " · 截至" + previous->text(),body,id);
            recommend(entry, "本章或前文章末提及此人，快照取上一章末的账本记录。");
            snapshots.heading->setHidden(false);
        }
    }
    void updatePreview()
    {
        int count = 0;
        for (const auto &section : sources->sections)
            for (const auto *item : section.entries)
                if (item->checkState() == Qt::Checked) ++count;
        referenceCount->setText(QString("已选 %1 项").arg(count));
        if (tabs->currentWidget() != preview)
            return;
        TokenEstimate estimate;
        auto [rules, input] = messages(&estimate);
        QString transcript = "\n\n写作规则\n" + rules + "\n\n" + input;
        if (hasDraft && previewRevision && !pending)
        {
            const auto revision = RevisionContext::prepare(revisionTurns(), compressHistory->isChecked());
            if (!revision.summary.isEmpty())
            {
                transcript += revision.summary;
                estimate.add("早期修订摘要", revision.summary);
            }
            for (qsizetype i = 0; i < revision.turns.size(); ++i)
            {
                auto message = revision.turns.at(i).toObject();
                const bool current = i >= revision.turns.size() - 2;
                const QString name = message["role"] == "assistant" ? (current ? "最新候选稿" : "历轮候选稿")
                                                                    : (current ? "本轮修改意见" : "历轮修改意见");
                const auto text = message["content"].toString();
                estimate.add(name, text);
                transcript += "\n\n" + name + "\n" + text;
            }
        }
        const auto selected = model->currentText();
        const auto limit = site().outputLimit(selected);
        QString tier = limit ? QLocale(QLocale::English).toString(limit) + " token" : QStringLiteral("由站点决定");
        if (!site().outputLimits.value(selected).toInt()) tier = "默认档（" + tier + "）";
        preview->setPlainText("模型　" + selected + "　|　当前输出档位　" + tier + "\nAI 思考强度　" + AiSite::effortLabel(site().effort(selected)) + "\n" + modelOutputGuide(selected) +
            "\n\n本次输入约 " + QLocale(QLocale::English).toString((estimate.weight + 9) / 10) +
            " token（按文本估算，实际以模型分词为准）\n" + estimate.rows.join('\n') +
            "\n\n目标地址  " + endpoint().toString() + transcript);
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
    void readStream(QNetworkReply *reply, bool end, bool summarizing)
    {
        const auto added = response.read(reply->readAll(), end);
        if (!end && (response.receivedThinking || !added.isEmpty()))
            deadline.start();
        if (check)
            phase = "正在核对本章事件";
        else if (summarizing)
            phase = "正在生成剧情摘要";
        else if (!added.isEmpty())
        {
            QTextCursor tail(result->document());
            tail.movePosition(QTextCursor::End);
            tail.insertText(added);
            phase = "正在接收候选正文";
        }
        else if (response.receivedThinking && response.text.isEmpty())
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
    enum class Request { Write, Probe, Revise, Summary, Check };
    void submit(Request kind = Request::Write, const QString &summarySource = {})
    {
        const bool probe = kind == Request::Probe, revision = kind == Request::Revise;
        const bool summarizing = kind == Request::Summary;
        const bool checking = kind == Request::Check;
        const bool writing = !probe && !summarizing && !checking;
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
        if (writing && task->toPlainText().trimmed().isEmpty())
        {
            status->setText("请填写写作任务。");
            return;
        }
        if (revision && (!hasDraft || feedback->toPlainText().trimmed().isEmpty() ||
                         result->toPlainText().trimmed().isEmpty()))
            return;
        QSettings settings;
        settings.setValue("writing/provider", site().id);
        settings.setValue("writing/model/" + site().id, model->currentText());
        QJsonArray turns;
        const auto conversation = revision ? revisionTurns() : QJsonArray{};
        QJsonObject payload{{"model", model->currentText()}, {"stream", !probe}};
        if (probe)
            turns.append(QJsonObject{{"role", "user"}, {"content", "请只回复 OK。"}});
        else
        {
            auto [rules, input] = summarizing
                ? qMakePair(QStringLiteral("为小说作者整理本章剧情摘要。用约 150 至 300 个中文字，记录已经发生的关键事件、"
                                           "人物关系与资源变化、未解线索以及章末状态。忠于正文，只返回摘要内容，章节标题由软件添加。"),
                            summarySource)
                : checking ? qMakePair(ChapterCheck::rules(), summarySource) : messages();
            if (site().messagesApi())
                payload["system"] = rules;
            else
                turns.append(QJsonObject{{"role", "system"}, {"content", rules}});
            const auto revisionContext = RevisionContext::prepare(conversation, compressHistory->isChecked());
            turns.append(QJsonObject{{"role", "user"}, {"content", input + revisionContext.summary}});
            for (auto turn : revisionContext.turns)
                turns.append(turn);
        }
        if (!probe) site().applyEffort(payload, model->currentText());
        payload["messages"] = turns;
        const int outputLimit = probe ? 64 : site().outputLimit(model->currentText());
        if (outputLimit > 0)
            payload["max_tokens"] = outputLimit;
        QNetworkRequest request(endpoint());
        request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
        request.setTransferTimeout(0); // The request timer below handles probes and model inactivity.
        const auto token = key->text().trimmed();
        site().setHeaders(request, token);
        sessionKeys[site().credentialId()] = token;
        const auto previousDraft = result->toPlainText();
        if (writing)
        {
            result->setReadOnly(true);
            tabs->setCurrentWidget(result);
            result->clear();
        }
        apply->setEnabled(false);
        apply->hide();
        response = {};
        response.messagesApi = site().messagesApi();
        timedOut = false;
        auto *reply = network.post(request, QJsonDocument(payload).toJson(QJsonDocument::Compact));
        pending = reply;
        if (revision)
            feedback->clear();
        elapsed.start();
        phase = probe ? "正在测试站点和模型" : "正在连接 " + service->currentText();
        updateProgress();
        setBusy(true);
        updateActions();
        progress.start(1000);
        deadline.start(probe ? 30000 : generationIdleMs);
        connect(reply, &QNetworkReply::requestSent, this, [this, probe] {
            phase = probe ? "测试请求已发送" : "已发送，等待模型输出";
            updateProgress();
        });
        connect(reply, &QIODevice::readyRead, this, [this, reply, probe, summarizing] {
            if (probe || reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() != 200)
                return;
            if (isStream(reply))
                readStream(reply, false, summarizing);
            else if (reply->bytesAvailable() > 0)
                deadline.start(); // Some compatible sites return one JSON body instead of SSE.
        });
        connect(reply, &QNetworkReply::finished, this, [this, reply, probe, writing, checking, summarizing, previousDraft, conversation, outputLimit] {
            progress.stop();
            deadline.stop();
            pending = nullptr;
            reply->deleteLater();
            setBusy(summaries->batchActive());
            bool completed = false;
            auto restoreEditor = qScopeGuard([&] {
                if (summarizing && !completed)
                    summaries->stopBatch();
                if (writing && !completed)
                {
                    result->setPlainText(previousDraft);
                    if (!conversation.isEmpty())
                        feedback->setPlainText(conversation.last().toObject()["content"].toString());
                }
                result->setReadOnly(!hasDraft);
                updateActions();
                updatePreview();
            });
            int code = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            if (timedOut)
            {
                status->setText(probe ? "测试连接请求超时（30 秒）。"
                                      : "请求超时：已连续 5 分钟未收到新的思考内容或正文。");
                return;
            }
            if (reply->error() == QNetworkReply::TimeoutError)
            {
                status->setText("网络连接超时，请检查站点或网络后重试。");
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
            if (!probe && isStream(reply))
            {
                readStream(reply, true, summarizing);
                if (response.error.isEmpty() && !response.finished)
                    response.error = "生成中断，请重新发送。";
            }
            else
                response.readBufferedReply(reply->readAll(), !probe);
            if (!response.error.isEmpty())
            {
                status->setText(failure(reply, code, response.error));
                return;
            }
            const auto &text = response.text;
            const bool truncated = response.truncated();
            const bool reasoningOnly = probe && text.trimmed().isEmpty() && response.sawThinking &&
                (truncated || response.stopReason == "end_turn" || response.stopReason == "stop");
            if (text.trimmed().isEmpty() && !reasoningOnly)
            {
                status->setText(response.emptyReplyMessage(outputLimit));
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
            if (checking)
            {
                status->setText(truncated ? QStringLiteral("检查结果已截断，请调整输出档位后重试。") : check->review(text));
                return;
            }
            if (summarizing)
            {
                if (truncated) status->setText("摘要输出中断，原摘要已保留。");
                else
                {
                    completed = summaries->generated(text);
                    status->setText(completed ? "剧情摘要已生成，可继续编辑。" : "摘要保存失败，请查看提示。");
                }
                return;
            }
            if (!isStream(reply))
                result->setPlainText(text);
            completed = true;
            revisionHistory = conversation;
            if (conversation.isEmpty())
                revisionLog->clear();
            else
                revisionLog->appendPlainText(QString("第 %1 轮\n%2\n")
                                                 .arg(conversation.size() / 2)
                                                 .arg(conversation.last().toObject()["content"].toString()));
            tabs->setTabVisible(tabs->indexOf(revisionLog), !conversation.isEmpty());
            hasDraft = true;
            draftTruncated = truncated;
            previewRevision = true;
            feedback->clear();
            tabs->setCurrentWidget(result);
            status->setText(revisionHistory.isEmpty() ? "候选正文已就绪，可提出修改意见或采纳。"
                                                      : QString("第 %1 轮修改已完成。").arg(revisionHistory.size() / 2));
        });
    }

  public:
    qint64 reviewedVersion = -1;
    void showConflict(const QString &latest, qint64 version)
    {
        originalBody = latest;
        reviewedVersion = version;
        wholeSelection = true;
        tabs->setCurrentWidget(comparison);
        status->setText("原章已被修改。返回正文可保留最新版；核对差异后可从候选正文重新确认覆盖。");
    }
    void flushSummary() { summaries->flush(); }
    ~WritingAssistant() override
    {
        if (pending)
        {
            disconnect(pending, nullptr, this, nullptr);
            pending->abort();
        }
    }
    WritingAssistant(Book &book, int currentChapter, QString excerpt, bool replacing, bool entireSelection, QHash<QString, QString> &keys,
                     std::function<void()> back, std::function<void(QString, bool)> accept, bool summaryOnly = false, bool checkOnly = false)
        : wholeSelection(entireSelection), sessionKeys(keys)
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
            QListWidget#writingSources { background:transparent; border:none; padding:0; }
            QListWidget#writingSources::item { padding:4px; }
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
        draftLength->setObjectName("candidateWordCount");
        draftLength->setToolTip("与章节字数相同，每个汉字、字母和数字各计一字。");
        top->addWidget(draftLength);
        draftLength->hide();
        top->addWidget(config);
        auto *close = new QPushButton("返回正文");
        top->addWidget(close);
        service->setObjectName("aiService");
        model->setObjectName("aiModel");
        model->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
        model->setMinimumContentsLength(10);
        key->setEchoMode(QLineEdit::Password);
        key->setToolTip("在这里输入的密钥仅在本次运行中使用。要在更新后继续使用，请在 AI 配置中勾选记住密钥并保存。");
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
        compressHistory->setObjectName("compressRevisionHistory");
        compressHistory->setToolTip("连续修改时，将早期修订整理成摘要，完整发送最新候选稿和本轮意见。");
        compressHistory->setChecked(QSettings().value("writing/compressRevisionHistory", true).toBool());
        left->addWidget(compressHistory);
        compressHistory->setVisible(!summaryOnly);
        connect(compressHistory, &QCheckBox::toggled, this, [this](bool checked) {
            QSettings().setValue("writing/compressRevisionHistory", checked);
            updatePreview();
        });
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
        connect(sources, &QListWidget::currentItemChanged, this, [help](QListWidgetItem *entry) {
            const auto reason = entry ? entry->data(WritingSources::Recommendation).toString() : QString();
            help->setText(reason.isEmpty() ? QStringLiteral("勾选随本次请求发送的条目。") : reason);
        });
        left->addWidget(prepare);
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
        QHash<int, QString> chapters;
        QList<int> chapterOrder;
        auto q = book.run("SELECT id,title FROM chapters ORDER BY position");
        while (q.next())
        {
            const int id = q.value(0).toInt();
            chapters[id] = q.value(1).toString();
            chapterOrder.append(id);
        }
        for (auto scope : {NoteScope::Global, NoteScope::Book, NoteScope::Outline})
        {
            const auto group = scope == NoteScope::Global ? WritingSources::GlobalSkill :
                               scope == NoteScope::Book ? WritingSources::BookSkill : WritingSources::Outline;
            sources->addHeading(group, scopeName(scope));
            if (summaryOnly || checkOnly) continue;
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
                sources->addEntry(group, label, note.body, note.chapter);
            }
        }
        sources->addHeading(WritingSources::Full, "前章全文");
        sources->addHeading(WritingSources::Summary, "前章摘要");
        sources->addHeading(WritingSources::Tail, "上一章结尾");
        sources->addHeading(WritingSources::Snapshot, "人物账本快照");
        sources->sections[WritingSources::Tail].heading->setHidden(true);
        sources->sections[WritingSources::Snapshot].heading->setHidden(true);
        QSet<int> availableSummaries;
        QMap<int, QString> summaryStatus;
        auto stored = book.run("SELECT c.id,s.source_version,c.body_version "
                               "FROM chapter_summaries s JOIN chapters c ON c.id=s.chapter");
        while (stored.next())
        {
            availableSummaries.insert(stored.value(0).toInt());
            if (stored.value(1).isNull()) summaryStatus.insert(stored.value(0).toInt(),"（旧摘要 · 待核对）");
            else if (stored.value(1) != stored.value(2)) summaryStatus.insert(stored.value(0).toInt(),"（正文已修改）");
        }
        const int currentIndex = chapterOrder.indexOf(currentChapter);
        targetChapter = numberedChapter(currentIndex + 1, chapters.value(currentChapter));
        for (int i = currentIndex - 1; !summaryOnly && !checkOnly && i >= 0; --i)
        {
            const int id = chapterOrder.at(i);
            const auto label = numberedChapter(i + 1, chapters.value(id));
            auto *full = sources->addEntry(WritingSources::Full, label, {}, id);
            auto *summary = sources->addEntry(WritingSources::Summary, label + summaryStatus.value(id), {}, id, availableSummaries.contains(id));
            full->setData(WritingSources::ChapterOrder, i);
            summary->setData(WritingSources::ChapterOrder, i);
            fullEntries.insert(id, full);
            summaryEntries.insert(id, summary);
        }
        sources->sections[WritingSources::Full].heading->setHidden(fullEntries.isEmpty());
        sources->restoreGroups(fullEntries.size() <= 3);
        sources->sections[WritingSources::Summary].heading->setHidden(std::none_of(summaryEntries.cbegin(), summaryEntries.cend(),
            [](const auto *item) { return item->data(WritingSources::Available).toBool(); }));
        chapterTitle->setText(targetChapter);
        connect(prepare, &QPushButton::clicked, this, [this,&book,currentChapter,help] {
            try { prepareMaterials(book,currentChapter); help->setText("已按本章大纲、人物和前文勾选。章末节选可切换为全文。"); }
            catch(const std::exception &e){ status->setText(QString::fromUtf8(e.what())); }
        });
        auto *compose = new QWidget;
        auto *inputs = new QVBoxLayout(compose);
        inputs->setContentsMargins(18, 16, 18, 16);
        inputs->setSpacing(8);
        inputs->addWidget(new QLabel("写作任务"));
        inputs->addWidget(task, 1);
        const QString initialTask = replacing ? "润色所选段落，保留原意和情节。" : "按写作要求创作一段小说正文。";
        task->setPlainText(initialTask);
        inputs->addSpacing(6);
        auto *contextHeader = new QHBoxLayout;
        inputs->addLayout(contextHeader);
        contextHeader->addWidget(new QLabel("正文上下文"), 1);
        contextHeader->addWidget(usePrevious);
        usePrevious->setVisible(!fullEntries.isEmpty());
        usePrevious->setToolTip("勾选上一章全文。更早的章节可以在左侧勾选已保存的摘要。");
        connect(usePrevious, &QPushButton::clicked, this, [this] {
            auto *item = sources->sections[WritingSources::Full].entries.first();
            item->setCheckState(Qt::Checked);
            sources->setExpanded(WritingSources::Full, true);
            sources->scrollToItem(item);
        });
        contextHeader->addWidget(useChapter);
        originalBody = book.value("SELECT body FROM chapters WHERE id=?", {currentChapter}).toString();
        connect(useChapter, &QPushButton::clicked, this, [this, initialTask] {
            context->setPlainText(originalBody);
            if (task->toPlainText() == initialTask)
                task->setPlainText("修改本章全文，保留原意和情节，返回完整正文。");
        });
        context->setPlaceholderText("可选。未选中正文时此处留空，也可以手动填写或使用本章全文。");
        inputs->addWidget(context, 3);
        context->setPlainText(excerpt);
        tabs->addTab(compose, "写作要求");
        tabs->addTab(preview, "发送预览");
        tabs->addTab(result, "候选正文");
        result->setObjectName("candidateDraft");
        revisionLog->setReadOnly(true);
        tabs->setTabVisible(tabs->addTab(revisionLog, "修改记录"), false);
        comparison->setOpenLinks(false);
        comparison->setObjectName("chapterComparison");
        tabs->setTabVisible(tabs->addTab(comparison, "整章对比"), false);
        summaries = new ChapterSummary(book, currentChapter, [this,&book](int id, const QString &text) {
            if (auto *item = summaryEntries.value(id))
            {
                QSignalBlocker block(sources);
                auto label = fullEntries.value(id)->text();
                if (!text.isEmpty())
                {
                    auto state = book.run("SELECT s.source_version,c.body_version FROM chapter_summaries s "
                                                  "JOIN chapters c ON c.id=s.chapter WHERE c.id=?", {id});
                    if (state.next())
                        label += state.value(0).isNull() ? "（旧摘要 · 待核对）" :
                                 state.value(0) != state.value(1) ? "（正文已修改）" : "";
                }
                item->setText(label);
                item->setData(WritingSources::Available, !text.isEmpty());
                item->setHidden(text.isEmpty() || !sources->sections[WritingSources::Summary].expanded);
                if (text.isEmpty()) item->setCheckState(Qt::Unchecked);
                item->setData(WritingSources::Body, item->checkState() == Qt::Checked ? text : QString());
                bool any = false;
                for (const auto *entry : std::as_const(summaryEntries))
                    if (entry->data(WritingSources::Available).toBool()) { any = true; break; }
                sources->sections[WritingSources::Summary].heading->setHidden(!any);
            }
            updatePreview();
        }, [this](QString body) {
            submit(Request::Summary, body);
            return !pending.isNull();
        }, summaryOnly, [this](bool busy) { setBusy(busy); });
        tabs->addTab(summaries, "剧情摘要");
        preview->setReadOnly(true);
        result->setReadOnly(true);
        context->setFont(QFont("Microsoft YaHei UI", 12));
        result->setFont(context->font());
        for (auto *field : {task, context, preview, result})
            field->document()->setDocumentMargin(10);
        feedback->setObjectName("revisionFeedback");
        draftWarning->setWordWrap(true);
        draftWarning->setStyleSheet("color: #f0c879;");
        workLayout->addWidget(draftWarning);
        draftWarning->hide();
        feedback->setPlaceholderText("输入本轮修改意见…");
        feedback->setAccessibleName("修改意见");
        feedback->setFixedHeight(74);
        workLayout->addWidget(feedback);
        feedback->hide();
        auto *actions = new QHBoxLayout;
        workLayout->addLayout(actions);
        status->setTextFormat(Qt::PlainText);
        status->setWordWrap(true);
        status->setObjectName("mutedLabel");
        status->setText("准备就绪");
        actions->addWidget(status, 1);
        actions->addWidget(stop);
        actions->addWidget(send);
        actions->addWidget(revise);
        revise->hide();
        actions->addWidget(apply);
        actions->addWidget(applyChapter);
        applyChapter->setToolTip("用当前候选稿替换本章所有正文，可在编辑器中按 Ctrl+Z 撤销。");
        applyChapter->hide();
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
        deadline.setTimerType(Qt::PreciseTimer);
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
        connect(test, &QPushButton::clicked, this, [this] { submit(Request::Probe); });
        for (auto *field : {task, context})
            connect(field, &QPlainTextEdit::textChanged, this, [this] { updatePreview(); });
        connect(sources, &QListWidget::itemChanged, this, [this, &book](QListWidgetItem *item) {
            const auto group = WritingSources::groupOf(item);
            if (sources->sections[group].heading == item) return;
            const bool summary = group == WritingSources::Summary;
            if (group == WritingSources::Tail && item->checkState() == Qt::Checked)
            {
                const auto id = item->data(WritingSources::ChapterId).toInt();
                for (auto *other : {fullEntries.value(id),summaryEntries.value(id)})
                    if (other) other->setCheckState(Qt::Unchecked);
            }
            if (group == WritingSources::Full || summary)
            {
                QSignalBlocker block(sources);
                QString body;
                if (item->checkState() == Qt::Checked)
                {
                    try
                    {
                        const auto id = item->data(WritingSources::ChapterId).toInt();
                        body = book.value(summary ? "SELECT body FROM chapter_summaries WHERE chapter=?"
                                                  : "SELECT body FROM chapters WHERE id=?", {id}).toString();
                        if (body.trimmed().isEmpty())
                            throw std::runtime_error(summary ? "尚无摘要，可在「剧情摘要」页生成或填写。" : "这一章尚无正文。");
                        auto *other = summary ? fullEntries.value(id) : summaryEntries.value(id);
                        other->setCheckState(Qt::Unchecked);
                        other->setData(WritingSources::Body, QString());
                        for (auto *tail : sources->sections[WritingSources::Tail].entries)
                            if (tail->data(WritingSources::ChapterId).toInt() == id)
                                tail->setCheckState(Qt::Unchecked);
                    }
                    catch (const std::exception &e)
                    {
                        item->setCheckState(Qt::Unchecked);
                        status->setText(item->text() + " · " + QString::fromUtf8(e.what()));
                    }
                }
                item->setData(WritingSources::Body, body);
            }
            updatePreview();
        });
        connect(send, &QPushButton::clicked, this, [this] { submit(); });
        connect(revise, &QPushButton::clicked, this, [this] { submit(Request::Revise); });
        connect(tabs, &QTabWidget::currentChanged, this, [this] {
            if (tabs->currentWidget() != summaries && !summaries->saveEdits())
            {
                tabs->setCurrentWidget(summaries);
                return;
            }
            if (tabs->currentWidget() == comparison)
            {
                const auto draft = result->toPlainText();
                comparison->setHtml(QString("<p><b>原文 %1 字　→　新稿 %2 字</b></p>")
                                        .arg(chapterWordCount(originalBody)).arg(chapterWordCount(draft)) +
                                    ChapterDiff::render(originalBody, draft));
                comparison->verticalScrollBar()->setValue(0);
            }
            else if (tabs->currentWidget() != preview && tabs->currentWidget() != revisionLog)
                previewRevision = tabs->currentWidget() == result;
            updateActions();
            if (tabs->currentWidget() == preview)
                updatePreview();
        });
        connect(feedback, &QPlainTextEdit::textChanged, this, [this] {
            updateActions();
            if (tabs->currentWidget() == preview)
                updatePreview();
        });
        connect(result, &QPlainTextEdit::textChanged, this, [this] {
            if (hasDraft && !pending)
                updateActions();
        });
        connect(stop, &QPushButton::clicked, this, [this] {
            if (pending)
                pending->abort();
            else if (summaries->batchActive())
            {
                summaries->stopBatch();
                status->setText("已停止批量生成，已保存的摘要可继续使用。");
            }
        });
        connect(close, &QPushButton::clicked, this, [this, back] {
            if (!summaries->saveEdits()) return;
            if (pending)
                pending->abort();
            back();
        });
        auto adopt = [this, accept, replacing](bool wholeChapter) {
            const auto text = result->toPlainText();
            if (text.trimmed().isEmpty()) return;
            if (!wholeChapter && !replacing)
            {
                accept(text, false);
                return;
            }
            const QString action = wholeChapter ? "确认覆盖" : "确认替换";
            auto *dialog = new QMessageBox(QMessageBox::Question, wholeChapter ? "覆盖本章" : "替换选区",
                wholeChapter ? "用候选稿覆盖本章全部正文？" : "用候选稿替换进入 AI 写作时选中的文字？",
                QMessageBox::Ok | QMessageBox::Cancel, this);
            dialog->setOption(QMessageBox::Option::DontUseNativeDialog);
            dialog->setAttribute(Qt::WA_DeleteOnClose);
            dialog->setTextFormat(Qt::PlainText);
            dialog->setInformativeText(targetChapter + "\n确认后将保存并返回正文，可按 Ctrl+Z 撤销。");
            dialog->button(QMessageBox::Ok)->setText(action);
            dialog->button(QMessageBox::Cancel)->setText("取消");
            dialog->setDefaultButton(QMessageBox::Cancel);
            dialog->setEscapeButton(QMessageBox::Cancel);
            connect(dialog, &QDialog::finished, this, [accept, text, wholeChapter](int choice) {
                if (choice == QMessageBox::Ok) accept(text, wholeChapter);
            });
            dialog->open();
        };
        connect(apply, &QPushButton::clicked, this, [adopt] { adopt(false); });
        connect(applyChapter, &QPushButton::clicked, this, [adopt] { adopt(true); });
        if (checkOnly)
        {
            check = new ChapterCheck(book,currentChapter,[this](const QString &input) { submit(Request::Check,input); });
            tabs->addTab(check,"检查本章");
            heading->setText("章后漏记检查");
            library->hide();
            tabs->setStyleSheet("QTabWidget::pane { background:transparent; border:none; }");
            auto *connection = new QFrame;
            connection->setObjectName("contentPanel");
            auto *bar = new QHBoxLayout(connection);
            bar->setContentsMargins(14, 9, 14, 9);
            bar->setSpacing(10);
            for (const auto &[label, widget] : QList<QPair<QString,QWidget *>>{{"服务",service},{"模型",model},{"密钥",key}})
            {
                auto *caption = new QLabel(label);
                caption->setObjectName("mutedLabel");
                bar->addWidget(caption);
                bar->addWidget(widget, 1);
            }
            bar->addWidget(test);
            layout->insertWidget(1,connection);
            tabs->setCurrentWidget(check);
            tabs->tabBar()->hide();
        }
        else if (summaryOnly)
        {
            layout->setContentsMargins(0, 0, 0, 0);
            heading->hide();
            chapterTitle->hide();
            close->hide();
            library->hide();
            top->insertWidget(0, new QLabel("服务"));
            top->insertWidget(1, service);
            top->insertWidget(2, new QLabel("模型"));
            top->insertWidget(3, model, 1);
            top->insertWidget(4, key, 1);
            top->insertWidget(5, test);
            tabs->setCurrentWidget(summaries);
            tabs->tabBar()->hide();
        }
    }
};
