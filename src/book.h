#pragma once
#include <QtSql>
#include <functional>
#include <stdexcept>

QSqlQuery executeSql(const QSqlDatabase &db, const QString &sql, const QVariantList &args = {});

class Book
{
    QString connection;
    QSqlDatabase db;

  public:
    Book(const QString &path, const QString &newTitle = {});
    ~Book();
    Book(const Book &) = delete;
    Book &operator=(const Book &) = delete;
    QSqlQuery run(const QString &sql, const QVariantList &args = {}) const;
    QVariant value(const QString &sql, const QVariantList &args = {}) const;
    void atomic(const std::function<void()> &operation);
    void save(int chapter, const QString &body);
    void event(int chapter, int person, const QString &resource, int delta, const QString &description,
               const QString *body = nullptr);
    qint64 balance(int person, const QString &resource, int chapter) const;
};
