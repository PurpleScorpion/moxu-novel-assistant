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
    QString resourceKind;
};

struct CatalogEntry
{
    QString kind, name, level, cooldown;
    int uses = -1;
};

enum class StoryKind
{
    Note,
    Acquire,
    Transfer
};

enum class SkillAction
{
    Learn,
    Advance,
    Use
};
enum class SupplyAction
{
    Receive,
    Consume,
    Give
};
struct CharacterSkill
{
    QString name, level, cooldown;
    int remaining = -1; // -1 means unlimited.
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
    int transactionDepth = 0;
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
    void refreshWordCounts();
    int insertChapter(const QString &title, int anchor = 0, bool after = false);
    void moveChapter(int id, int direction);
    HistoryLimits historyLimits() const;
    HistoryUsage historyUsage() const;
    qint64 historyRemovalCount(HistoryLimits limits) const;
    void setHistoryLimits(HistoryLimits limits, qint64 expectedRemoval);
    qint64 historyCleanupCount() const;
    void includeLegacyInHistoryLimit(qint64 expectedRemoval);
    qint64 event(int chapter, int person, const QString &resource, int delta, const QString &description,
               const QString *body = nullptr);
    qint64 balance(int person, const QString &resource, int chapter) const;
    QMap<QString, QString> resourceBalances(int person, int chapter) const;
    QStringList ownedItems(int person, int chapter) const;
    QMap<QString, qint64> supplyCounts(int person, int chapter) const;
    qint64 supplyEvent(int chapter, int person, SupplyAction action, const QString &name, int amount, int recipient,
                     const QString &description, const QString *body = nullptr);
    qint64 storyEvent(int chapter, int person, StoryKind kind, const QString &item, int recipient,
                    const QString &description, const QString *body = nullptr);
    QList<CharacterSkill> characterSkills(int person, int chapter) const;
    qint64 skillEvent(int chapter, int person, SkillAction action, const CharacterSkill &skill,
                    const QString &description, const QString *body = nullptr);
    QStringList resourceNames() const;
    QList<CatalogEntry> catalog(const QString &kind = {}, bool includeHistory = false) const;
    void saveCatalogEntry(const CatalogEntry &entry, const CatalogEntry &previous = {});
    void removeCatalogEntry(const CatalogEntry &entry);
    void createResource(const QString &name);
    LedgerPage ledgerPage(const LedgerFilter &filter, qint64 page) const;
    void plotStep(const QString &arc, const QString &strand, int chapter, const QString &phase, const QString &note);
};
