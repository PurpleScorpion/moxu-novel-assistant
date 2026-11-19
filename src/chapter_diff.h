#pragma once
#include <QHash>
#include <QStringList>
#include <algorithm>

namespace ChapterDiff
{
using Match = QPair<qsizetype, qsizetype>;

// Find unchanged paragraphs. Limit the LCS table for unusually long manuscripts.
inline QList<Match> matchingLines(const QStringList &oldLines, const QStringList &newLines)
{
    const qsizetype n = oldLines.size(), m = newLines.size();
    QList<Match> matches;
    qsizetype start = 0, endOld = n, endNew = m;
    while (start < n && start < m && oldLines[start] == newLines[start])
    {
        matches.append({start, start});
        ++start;
    }
    while (endOld > start && endNew > start && oldLines[endOld - 1] == newLines[endNew - 1])
    {
        --endOld;
        --endNew;
    }
    const auto height = endOld - start, width = endNew - start;
    if (height && width && height + 1 <= 1000000 / (width + 1))
    {
        QList<int> table((height + 1) * (width + 1), 0);
        auto cell = [&](qsizetype i, qsizetype j) -> int & { return table[i * (width + 1) + j]; };
        for (qsizetype i = height; i-- > 0;)
            for (qsizetype j = width; j-- > 0;)
                cell(i, j) = oldLines[start + i] == newLines[start + j] ? 1 + cell(i + 1, j + 1)
                                                                        : qMax(cell(i + 1, j), cell(i, j + 1));
        qsizetype i = 0, j = 0;
        while (i < height && j < width)
        {
            if (oldLines[start + i] == newLines[start + j])
            {
                matches.append({start + i, start + j});
                ++i;
                ++j;
            }
            else if (cell(i + 1, j) >= cell(i, j + 1))
                ++i;
            else
                ++j;
        }
    }
    else if (height && width)
    {
        QHash<QString, QList<qsizetype>> positions;
        for (auto j = start; j < endNew; ++j)
            positions[newLines[j]].append(j);
        auto next = start;
        for (auto i = start; i < endOld && next < endNew; ++i)
        {
            auto found = positions.constFind(oldLines[i]);
            if (found == positions.cend())
                continue;
            auto it = std::lower_bound(found->cbegin(), found->cend(), next);
            if (it != found->cend())
            {
                matches.append({i, *it});
                next = *it + 1;
            }
        }
    }
    for (auto i = endOld, j = endNew; i < n; ++i, ++j)
        matches.append({i, j});
    matches.append({n, m});
    return matches;
}

inline QString paragraph(const QString &line, qsizetype index, bool added, qsizetype prefix, qsizetype suffix)
{
    const auto chars = line.toStdU32String();
    const auto size = qsizetype(chars.size());
    QString marked;
    if (line.isEmpty())
        marked = QStringLiteral("（空行）");
    else
    {
        auto part = [&](qsizetype pos, qsizetype len) {
            return QString::fromUcs4(chars.data() + pos, len).toHtmlEscaped();
        };
        const auto style = added ? "background-color:#245435;color:#c2ffd2" :
                                   "background-color:#613138;color:#ffdad6;text-decoration:line-through";
        marked = part(0, prefix) + "<span style=\"" + style + "\">" + part(prefix, size - prefix - suffix) +
                 "</span>" + part(size - suffix, suffix);
    }
    return QString("<p style='white-space:pre-wrap'><b style='color:%1'>%2 · 第 %3 行</b><br/>")
               .arg(added ? "#96e6ad" : "#ffaaa4", added ? "＋ 新稿" : "－ 原文").arg(index + 1) + marked + "</p>";
}

inline QString render(const QString &original, const QString &candidate)
{
    if (original == candidate)
        return QStringLiteral("两版正文完全一致。");
    const auto lines = [](const QString &s) { return s.isEmpty() ? QStringList{} : s.split('\n'); };
    const auto before = lines(original), after = lines(candidate);
    QString html;
    qsizetype i = 0, j = 0;
    for (auto [a, b] : matchingLines(before, after))
    {
        if (a > i || b > j)
        {
            qsizetype prefix = 0, suffix = 0;
            if (a - i == 1 && b - j == 1)
            {
                const auto x = before[i].toUcs4(), y = after[j].toUcs4();
                const auto limit = qMin(x.size(), y.size());
                while (prefix < limit && x[prefix] == y[prefix])
                    ++prefix;
                while (prefix + suffix < limit && x[x.size() - 1 - suffix] == y[y.size() - 1 - suffix])
                    ++suffix;
            }
            for (; i < a; ++i)
                html += paragraph(before[i], i, false, prefix, suffix);
            for (; j < b; ++j)
                html += paragraph(after[j], j, true, prefix, suffix);
        }
        i = a + 1;
        j = b + 1;
    }
    return html;
}
}
