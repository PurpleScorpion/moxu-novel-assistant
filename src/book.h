#pragma once
#include <QtSql>
#include <functional>
#include <stdexcept>

QSqlQuery executeSql(const QSqlDatabase &db, const QString &sql, const QVariantList &args = {});
void ensureSkillHeaderColumn(const QSqlDatabase &db);

class Book
{
    QString connection;
    QSqlDatabase db;
    QList<qint64> historiesToKeep(bool includeLegacy) const;
    void trimHistory(bool includeLegacy, const QList<qint64> &keep);

  public:
    enum class SaveMode
    {
        Checkpoint,
        Automatic
    };
    static constexpr int chapterHistoryLimit = 10;
    static constexpr int bookHistoryLimit = 100;
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
    qint64 historyCleanupCount() const;
    void includeLegacyInHistoryLimit(qint64 expectedRemoval);
    void event(int chapter, int person, const QString &resource, int delta, const QString &description,
               const QString *body = nullptr);
    qint64 balance(int person, const QString &resource, int chapter) const;
};
