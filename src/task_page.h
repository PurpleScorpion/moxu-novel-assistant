#pragma once
#include "story_tasks.h"
#include <QtWidgets>

class TaskPage : public QWidget
{
    Book *book = nullptr;
    StoryTask task;
    QList<CatalogEntry> catalog;
    int preferredChapter = 0;
    QStackedWidget *pages = new QStackedWidget;
    QWidget *listPage = new QWidget, *formPage = new QWidget;
    QTableWidget *list = new QTableWidget, *rewards = new QTableWidget;
    QLineEdit *title = new QLineEdit;
    QPlainTextEdit *description = new QPlainTextEdit;
    QComboBox *person, *published, *completed, *resource;
    QSpinBox *quantity = new QSpinBox;
    QWidget *rewardTools = new QWidget, *settlement = new QWidget;
    QPushButton *open, *commit, *discard, *addReward, *dropReward;
    QLabel *heading = new QLabel, *summary = new QLabel, *message = new QLabel;
    bool confirmDelete = false;
    void refreshList(int select = 0);
    void reloadChoices(int actor, int start, int end);
    void showForm(int id);
    void showRewards();
    void save(bool finish);
    void invoke(const std::function<void()> &action);

  public:
    TaskPage();
    void load(Book &source, int currentChapter);
    void resetBook();
    void saveNow();
};
