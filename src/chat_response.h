#pragma once
#include <QtCore>

// Decode complete SSE records. Keep partial UTF-8 bytes between network reads.
struct ChatResponse
{
    QByteArray buffer, event;
    QString text, error;
    bool thinking = false, finished = false;
    bool messagesApi = false;

    QString read(const QByteArray &bytes, bool end = false)
    {
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

  private:
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
            if (type == "message_stop")
                finished = true;
            else if (type == "content_block_start")
            {
                auto block = obj["content_block"].toObject();
                if (block["type"] == "text")
                    return block["text"].toString();
                if (block["type"] == "thinking")
                    thinking = true;
            }
            else if (type == "content_block_delta")
            {
                auto delta = obj["delta"].toObject();
                if (delta["type"] == "text_delta")
                    return delta["text"].toString();
                if (delta["type"] == "thinking_delta")
                    thinking = true;
            }
            return {};
        }
        auto choices = obj["choices"].toArray();
        if (choices.isEmpty())
            return {};
        auto choice = choices.first().toObject(), delta = choice["delta"].toObject();
        if (!choice["finish_reason"].toString().isEmpty())
            finished = true;
        if (!delta["reasoning_content"].toString().isEmpty())
            thinking = true;
        return delta["content"].toString();
    }
};
