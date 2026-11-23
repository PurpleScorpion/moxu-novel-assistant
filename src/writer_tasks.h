#pragma once
#include "story_tasks.h"
#include <QWidget>

// A new task is published; a saved task is completed at the supplied revision.
using CommitWriterTask = std::function<void(const StoryTask &, bool insertInManuscript, const QString &preview)>;
QWidget *writerTaskForm(Book &book, int chapter, CommitWriterTask commit);
