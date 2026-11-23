#pragma once
#include "book.h"

struct TaskReward
{
    CatalogEntry resource;
    int quantity = 1;
};

struct StoryTask
{
    int id = 0, revision = 0, person = 0, published = 0, completed = 0;
    QString title, instructions;
    QList<TaskReward> rewards;
};

namespace StoryTasks
{
StoryTask get(Book &book, int id);
int save(Book &book, const StoryTask &task);
void complete(Book &book, int id, int revision, int chapter);
void remove(Book &book, int id, int revision);
} // namespace StoryTasks
