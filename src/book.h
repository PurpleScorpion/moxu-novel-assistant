#pragma once
#include <QtSql>
#include <functional>
#include <stdexcept>

QSqlQuery executeSql(const QSqlDatabase &db, const QString &sql, const QVariantList &args = {});
void ensureSkillHeaderColumn(const QSqlDatabase &db);

struct HistoryLimits
{
    int perChapter = 10;
    int perBook = 100;
};

struct HistoryUsage
{
    qint64 count, bytes, legacyCount;
};

struct LedgerFilter
{
    QString person, resource, description;
    bool newestFirst = true;
};

struct LedgerPage
{
    static constexpr int pageSize = 30;
    qint64 number = 1, pages = 1, total = 0;
    QList<QStringList> rows;
};

inline QString numberedChapter(qint64 number, const QString &title)
{
    return QString("第 %1 章 · %2").arg(number).arg(title);
}

class Book
{
    QString connection;
    QSqlDatabase db;
    QList<qint64> historiesToKeep(bool includeLegacy, HistoryLimits limits) const;
    void trimHistory(bool includeLegacy, const QList<qint64> &keep);

  public:
    enum class SaveMode
    {
        Checkpoint,
        Automatic
    };
    static constexpr int historyBudgetMiB = 32;
    static constexpr qint64 historyBudget = qint64(historyBudgetMiB) * 1024 * 1024;
    Book(const QString &path, const QString &newTitle = {});
    ~Book();
    Book(const Book &) = delete;
    Book &operator=(const Book &) = delete;
    QString filePath() const
    {
        return db.databaseName();
    }
    QSqlQuery run(const QString &sql, const QVariantList &args = {}) const;
    QVariant value(const QString &sql, const QVariantList &args = {}) const;
    void atomic(const std::function<void()> &operation);
    void save(int chapter, const QString &body, SaveMode mode = SaveMode::Checkpoint);
    HistoryLimits historyLimits() const;
    HistoryUsage historyUsage() const;
    qint64 historyRemovalCount(HistoryLimits limits) const;
    void setHistoryLimits(HistoryLimits limits, qint64 expectedRemoval);
    qint64 historyCleanupCount() const;
    void includeLegacyInHistoryLimit(qint64 expectedRemoval);
    void event(int chapter, int person, const QString &resource, int delta, const QString &description,
               const QString *body = nullptr);
    qint64 balance(int person, const QString &resource, int chapter) const;
    QStringList resourceNames() const;
    void createResource(const QString &name);
    LedgerPage ledgerPage(const LedgerFilter &filter, qint64 page) const;
};
