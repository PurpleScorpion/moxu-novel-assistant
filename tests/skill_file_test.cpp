#include "dialog_watch.h"
#include "dossier.h"

namespace
{
void check(bool ok, const char *message)
{
    if (!ok)
        throw std::runtime_error(message);
}
void put(const QString &path, const QByteArray &data)
{
    QFile file(path);
    check(file.open(QIODevice::WriteOnly) && file.write(data) == data.size(), "无法建立测试文件");
}
void click(QWidget &page, const QString &text)
{
    QApplication::processEvents();
    for (auto *button : page.findChildren<QPushButton *>())
        if (button->isVisible() && button->text() == text)
        {
            button->click();
            return;
        }
    throw std::runtime_error(("未找到按钮 " + text).toStdString());
}
void fileAction(QWidget &page, QString button, QString path, int scope = -1, bool cancel = false)
{
    click(page, button);
    visibleControl<QLineEdit>(page, "filePath")->setText(path);
    if (scope >= 0)
        visibleControl<QComboBox>(page, "importScope")->setCurrentIndex(scope);
    click(page, cancel ? "取消" : scope >= 0 ? "确认导入" : "确认导出");
}
} // namespace

int main(int argc, char **argv)
{
    QCoreApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
    QApplication app(argc, argv);
    DialogWatch watch;
    app.installEventFilter(&watch);
    QTemporaryDir dir;
    app.setOrganizationName("MoxuTests");
    app.setApplicationName("SkillFiles");
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, dir.path());
    QSettings().setValue("booksDir", dir.path());
    try
    {
        auto db = QSqlDatabase::addDatabase("QSQLITE", "moxu-personal-writing-library");
        db.setDatabaseName(dir.filePath("skills.sqlite"));
        check(db.open(), "无法建立全局库");
        const QString oldSchema = "CREATE TABLE writing_notes(id INTEGER PRIMARY KEY AUTOINCREMENT,kind TEXT,title "
                                  "TEXT,body TEXT,chapter INTEGER,revision INTEGER DEFAULT 1)";
        executeSql(db, oldSchema);
        const QString path = dir.filePath("甲.moxu");
        {
            Book old(path, "甲");
            old.run("DROP TABLE writing_notes");
            old.run(oldSchema);
            old.run("INSERT INTO writing_notes(kind,title,body) VALUES('skill','我的规则','使用短句。')");
        }
        Book book(path);
        QHash<QString, QString> sessionKeys;
        Dossier page(sessionKeys, [] {}, [] {}, [] {}, [](QString) {});
        page.enter(&book);
        page.show();
        auto *nav = page.findChild<QListWidget *>("navigation");
        selectDossierPage(nav, 5);
        const auto shared = dir.filePath("SKILL.md");
        for (auto *editor : page.findChildren<QPlainTextEdit *>())
            if (editor->isVisible())
                editor->appendPlainText("保留人物口吻。");
        fileAction(page, "导出 SKILL.md…", shared);
        auto authored = readSkillFile(shared);
        check(authored.title == "我的规则" && authored.body == "使用短句。\n保留人物口吻。", "自写 Skill 导出丢失修改");
        check(authored.skillHeader.contains("name:") && authored.skillHeader.contains("description:"),
              "缺少标准文件头");
        fileAction(page, "导入 SKILL.md…", shared, 0, true);
        check(WritingNotes(book, NoteScope::Global).list().isEmpty(), "取消导入留下了条目");
        fileAction(page, "导入 SKILL.md…", shared, 0);
        check(nav->currentItem()->data(Qt::UserRole).toInt() == 4 && WritingNotes(book, NoteScope::Global).list().first().title == authored.title,
              "导入全局失败");
        const QString external = dir.filePath("人物对话.md");
        QString header = "name: dialogue\ndescription: >-\n  对话练习，\n  保留人物口吻。\nmetadata:\n  author: "
                         "'示例作者'\nlicense: MIT\n";
        QString instructions = "## 人物\n\n遵循  人物口吻。\n\n---\n结尾。\n";
        put(external, QByteArray::fromHex("efbbbf") +
                          QString("---\n" + header + "---\n" + instructions).replace("\n", "\r\n").toUtf8());
        fileAction(page, "导入 SKILL.md…", external, 1);
        auto notes = WritingNotes(book, NoteScope::Book).list();
        check(nav->currentItem()->data(Qt::UserRole).toInt() == 5 && notes.size() == 2 && notes.last().body == instructions &&
                  notes.last().skillHeader == header,
              "外部 Skill 正文或元数据有误");
        auto copy = dir.filePath("分享.md");
        fileAction(page, "导出 SKILL.md…", copy);
        put(copy, "保留原文件");
        fileAction(page, "导出 SKILL.md…", copy);
        {
            QFile kept(copy);
            kept.open(QIODevice::ReadOnly);
            check(kept.readAll() == "保留原文件", "原 Skill 被静默覆盖");
        }
        auto *destination = visibleControl<QLineEdit>(page, "filePath");
        auto *replace = visibleControl<QCheckBox>(page);
        replace->setChecked(true);
        destination->setText(shared);
        check(!replace->isChecked(), "修改路径没有重置覆盖许可");
        destination->setText(copy);
        click(page, "取消");
        {
            QFile kept(copy);
            kept.open(QIODevice::ReadOnly);
            check(kept.readAll() == "保留原文件", "取消覆盖写入了文件");
        }
        fileAction(page, "导出 SKILL.md…", copy);
        visibleControl<QCheckBox>(page)->setChecked(true);
        click(page, "确认导出");
        auto exported = readSkillFile(copy);
        check(exported.title == "dialogue" && exported.skillHeader == header && exported.body == instructions,
              "外部文件再次导出丢失内容");
        fileAction(page, "导入 SKILL.md…", copy, 1);
        check(WritingNotes(book, NoteScope::Book).list().size() == 3, "同名导入覆盖了原 Skill");
        put(external, "---\nname: [\n---\n无效");
        fileAction(page, "导入 SKILL.md…", external, 1);
        check(WritingNotes(book, NoteScope::Book).list().size() == 3, "无效文件产生了条目");
        put(external, "# 自定义标题\n\n正文");
        check(readSkillFile(external).title == "自定义标题", "普通 Markdown 标题识别失败");
        check(!visibleControl<QLabel>(page, "actionError")->text().isEmpty(), "无效文件缺少页内提示");
        click(page, "确认导入"); // Retry the repaired file on the same form.
        check(WritingNotes(book, NoteScope::Book).list().size() == 4, "修正文件后导入失败");
        click(page, "删除");
        click(page, "取消");
        check(WritingNotes(book, NoteScope::Book).list().size() == 4, "取消后条目被删除");
        click(page, "删除");
        click(page, "确认删除");
        check(WritingNotes(book, NoteScope::Book).list().size() == 3, "页内删除失败");
        check(watch.shown.isEmpty(), "资料操作中弹出了对话框");
        Book other(dir.filePath("乙.moxu"), "乙");
        check(WritingNotes(other, NoteScope::Global).list().size() == 1 &&
                  WritingNotes(other, NoteScope::Book).list().isEmpty(),
              "目标范围错误");
    }
    catch (const std::exception &e)
    {
        fprintf(stderr, "%s\n", e.what());
        return 1;
    }
    QSqlDatabase::removeDatabase("moxu-personal-writing-library");
    return 0;
}
