#pragma once
#include <QtCore>

struct RevisionContext
{
    QString summary;
    QJsonArray turns;

    static QString excerpt(QStringView text, qsizetype limit)
    {
        text = text.trimmed();
        if (text.size() <= limit)
            return text.toString().simplified();
        if (text[limit - 1].isHighSurrogate()) --limit;
        return text.first(limit).toString().simplified() + QStringLiteral("…");
    }

    static QString changeExcerpt(QStringView before, QStringView after)
    {
        qsizetype start = 0;
        while (start < before.size() && start < after.size() && before[start] == after[start])
            ++start;
        if (start == before.size() && start == after.size()) return {};
        if (start > 0 && after[start - 1].isHighSurrogate()) --start;
        auto end = after.size();
        auto oldEnd = before.size();
        while (end > start && oldEnd > start && after[end - 1] == before[oldEnd - 1])
        {
            --end;
            --oldEnd;
        }
        if (end < after.size() && after[end].isLowSurrogate()) ++end;
        const auto changed = after.sliced(start, end - start).trimmed();
        if (changed.isEmpty()) return QStringLiteral("（删减或调整留白）");
        return QStringLiteral("（改后片段：") + excerpt(changed, 60) + QStringLiteral("）");
    }

    // Each pair contains the candidate as edited by the author, then the requested revision.
    static RevisionContext prepare(const QJsonArray &history, bool compress)
    {
        if (!compress || history.size() <= 2) return {{}, history};
        const auto priorRounds = history.size() / 2 - 1;
        QString summary = QStringLiteral("\n\n早期修订摘要（以最新候选稿和本轮意见为准）");
        for (qsizetype round = 0; round < priorRounds; ++round)
        {
            const auto content = [&](qsizetype offset) {
                return history.at(round * 2 + offset).toObject()["content"].toString();
            };
            const bool recent = priorRounds - round <= 6;
            summary += QStringLiteral("\n第 %1 轮：").arg(round + 1) + excerpt(content(1), recent ? 160 : 36);
            if (recent) summary += changeExcerpt(content(0), content(2));
        }
        return {summary, {history.at(history.size() - 2), history.last()}};
    }
};
