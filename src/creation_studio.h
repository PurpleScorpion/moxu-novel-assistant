#pragma once
#include "ai_sites.h"
#include "chat_response.h"
#include "studio_data.h"
#include <QtNetwork>
#include <QtWidgets>
#include <functional>

class CreationStudio : public QWidget
{
  public:
    using Create = std::function<void(const QString &, const QString &, const QList<StudioItem> &)>;
    CreationStudio(QHash<QString, QString> &keys, Book *book, Create create = {}, std::function<void()> back = {});
    ~CreationStudio() override;

  private:
    Book *book;
    Create create;
    QHash<QString, QString> &keys;
    QList<AiSite> sites;
    QStackedWidget *pages = new QStackedWidget;
    QComboBox *provider = new DownwardCombo, *model = new DownwardCombo;
    QLineEdit *key = new QLineEdit, *title = new QLineEdit;
    QPlainTextEdit *idea = new QPlainTextEdit, *story = new QPlainTextEdit, *request = new QPlainTextEdit;
    QPlainTextEdit *references = new QPlainTextEdit;
    QTableWidget *table = new QTableWidget(0, 5);
    QList<QCheckBox *> categories;
    QLabel *status = new QLabel;
    QNetworkAccessManager network;
    QPointer<QNetworkReply> pending;
    QPushButton *cancel = new QPushButton("取消请求");
    QWidget *settings;
    QTimer idle;
    ChatResponse response;
    QStringList selectedKinds() const;
    void loadModel();
    void generate(bool core);
    void review(const QList<StudioItem> &rows);
    void acceptRows();
    void setBusy(bool busy);
};
