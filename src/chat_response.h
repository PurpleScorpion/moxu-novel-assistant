#pragma once
#include <QtCore>

// Decode complete SSE records. Keep partial UTF-8 bytes between network reads.
struct ChatResponse
{
    QByteArray buffer, event;
    QString text, error, stopReason;
    bool receivedThinking = false, sawThinking = false, finished = false;
    bool messagesApi = false;

    QString read(const QByteArray &bytes, bool end = false)
    {
        receivedThinking = false;
        buffer += bytes;
        QString added;
        for (;;)
        {
            auto pos = buffer.indexOf('\n');
            if (pos < 0 && (!end || buffer.isEmpty()))
                break;
            if (pos < 0)
                pos = buffer.size();
            auto line = buffer.first(pos);
            buffer.remove(0, qMin(pos + 1, buffer.size()));
            if (line.endsWith('\r'))
                line.chop(1);
            if (line.isEmpty())
                added += consume();
            else if (line.startsWith("data:"))
            {
                if (!event.isEmpty())
                    event += '\n';
                event += line.mid(5).trimmed();
            }
        }
        if (end)
            added += consume();
        sawThinking |= receivedThinking;
        text += added;
        return added;
    }
    static QString errorMessage(const QJsonObject &object)
    {
        auto value = object.value("error");
        if (value.isString())
            return value.toString().left(240);
        auto details = value.toObject();
        return (details.value("code").toVariant().toString() + " " + details.value("message").toString())
            .trimmed()
            .left(240);
    }

    bool truncated() const { return stopReason == "max_tokens" || stopReason == "length"; }

    QString emptyReplyMessage(int outputLimit) const
    {
        if (truncated())
        {
            const auto limit = outputLimit > 0 ? QString("（本次设置 %1 token）").arg(outputLimit) : QString();
            return (sawThinking ? QStringLiteral("只收到思考内容，") : QStringLiteral("未收到正文，")) +
                   "服务报告已达到生成长度上限" + limit + "。可缩短本次写作任务，或在 AI 配置里调高输出档位后重试。";
        }
        if (stopReason == "content_filter" || stopReason == "sensitive" || stopReason == "refusal")
            return QStringLiteral("服务端拦截或拒绝了本次生成，未返回正文。可调整写作要求后重试。");
        if (stopReason == "model_context_window_exceeded")
            return QStringLiteral("服务报告上下文已满，未返回正文。请减少引用资料后重试。");
        const QString reason = stopReason.isEmpty() ? QStringLiteral("未提供结束原因") : stopReason.left(48);
        return (sawThinking ? QStringLiteral("模型只返回了思考内容") : QStringLiteral("服务结束了本次生成，未返回正文")) +
               "（" + reason + "）。可在写作要求中指定要写的段落后重试。";
    }

    void readJson(const QByteArray &bytes)
    {
        const auto doc = QJsonDocument::fromJson(bytes);
        const auto obj = doc.object();
        if (!doc.isObject())
            error = "服务未返回有效的 JSON，请检查站点地址或网关响应。";
        else if (obj.contains("error"))
        {
            error = errorMessage(obj);
            if (error.isEmpty()) error = "服务报告生成失败。";
        }
        else if (messagesApi && obj["type"] == "message")
        {
            stopReason = obj["stop_reason"].toString();
            text = textContent(obj["content"]);
        }
        else if (!messagesApi && !obj["choices"].toArray().isEmpty())
        {
            const auto choice = obj["choices"].toArray().first().toObject();
            const auto message = choice["message"].toObject();
            stopReason = choice["finish_reason"].toString();
            sawThinking = !message["reasoning_content"].toString().isEmpty();
            text = textContent(message["content"]);
        }
        else
            error = "站点返回的 JSON 与已选接口协议不符，请核对 AI 配置中的协议。";
    }

  private:
    QString textContent(const QJsonValue &content)
    {
        if (content.isString()) return content.toString();
        QString value;
        for (const auto &part : content.toArray())
        {
            const auto block = part.toObject();
            if (block["type"] == "text") value += block["text"].toString();
            else if (block["type"] == "thinking" || block["type"] == "redacted_thinking") sawThinking = true;
        }
        return value;
    }

    QString consume()
    {
        auto data = std::exchange(event, {});
        if (data.isEmpty())
            return {};
        if (!messagesApi && data.trimmed() == "[DONE]")
        {
            finished = true;
            return {};
        }
        auto doc = QJsonDocument::fromJson(data);
        if (!doc.isObject())
        {
            error = "服务返回了无法解析的流式数据。";
            return {};
        }
        auto obj = doc.object();
        if (obj.contains("error"))
        {
            error = errorMessage(obj);
            if (error.isEmpty())
                error = "服务报告输出失败。";
            return {};
        }
        if (messagesApi)
        {
            auto type = obj["type"].toString();
            const auto reason = obj["delta"].toObject()["stop_reason"].toString();
            if (type == "message_delta" && !reason.isEmpty())
                stopReason = reason;
            if (type == "message_stop")
                finished = true;
            else if (type == "content_block_start")
            {
                auto block = obj["content_block"].toObject();
                if (block["type"] == "thinking" || block["type"] == "redacted_thinking")
                    sawThinking = true;
                if (block["type"] == "text")
                    return block["text"].toString();
                if (block["type"] == "thinking" && !block["thinking"].toString().isEmpty())
                    receivedThinking = true;
            }
            else if (type == "content_block_delta")
            {
                auto delta = obj["delta"].toObject();
                if (delta["type"] == "text_delta")
                    return delta["text"].toString();
                if (delta["type"] == "thinking_delta" && !delta["thinking"].toString().isEmpty())
                    receivedThinking = true;
            }
            return {};
        }
        auto choices = obj["choices"].toArray();
        if (choices.isEmpty())
            return {};
        auto choice = choices.first().toObject(), delta = choice["delta"].toObject();
        const auto reason = choice["finish_reason"].toString();
        if (!reason.isEmpty())
        {
            stopReason = reason;
            finished = true;
        }
        if (!delta["reasoning_content"].toString().isEmpty())
            receivedThinking = true;
        return textContent(delta["content"]);
    }
};
