#pragma once
#include <QtCore>

// Estimate text in tenths of a token and round only for display.
inline qint64 tokenWeight(QStringView text)
{
    qint64 weight = 0;
    for (qsizetype i = 0; i < text.size(); ++i)
    {
        char32_t c = text[i].unicode();
        if (QChar::isHighSurrogate(c) && i + 1 < text.size() && text[i + 1].isLowSurrogate())
        {
            c = QChar::surrogateToUcs4(text[i], text[i + 1]);
            ++i;
        }
        if (c < 128) weight += 3;
        else if (QChar::script(c) == QChar::Script_Han) weight += 6;
        else weight += c > 0xffff ? 20 : 10;
    }
    return weight;
}

struct TokenEstimate
{
    qint64 weight = 0;
    QStringList rows;

    void addWeight(const QString &name, qint64 amount)
    {
        weight += amount;
        rows << QString("%1　≈ %2 token").arg(name, QLocale(QLocale::English).toString((amount + 9) / 10));
    }
    void add(const QString &name, const QString &text) { addWeight(name, tokenWeight(text)); }
};
