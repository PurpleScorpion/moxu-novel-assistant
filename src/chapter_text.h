#pragma once
#include <QList>
#include <QString>

inline qsizetype chapterWordCount(const QString &text)
{
    qsizetype count = 0;
    for (auto ch : text.toUcs4())
        if (QChar::isLetterOrNumber(ch))
            ++count;
    return count;
}
