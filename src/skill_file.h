#pragma once
#include "writing_notes.h"

WritingNote readSkillFile(const QString &path);
void writeSkillFile(const QString &path, const WritingNote &note, bool overwrite);
