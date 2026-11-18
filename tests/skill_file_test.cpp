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
void fileAction(QWidget &page, QString button, QString path, int scope = -1, bool cancel = false)
{
    bool found = false;
    QTimer::singleShot(80, &page, [&] {
        auto *dialog = qobject_cast<QFileDialog *>(QApplication::activeModalWidget());
        if (!dialog)
            return;
        auto *target = dialog->findChild<QComboBox *>("importScope");
        found = scope < 0 || (target && target->isVisible());
        if (cancel || !found)
        {
            dialog->reject();
            return;
        }
        if (scope >= 0)
            target->setCurrentIndex(scope);
        auto *name = dialog->findChild<QLineEdit *>("fileNameEdit");
        if (!name)
        {
            found = false;
            dialog->reject();
            return;
        }
        name->setText(QDir::toNativeSeparators(path));
        QTimer::singleShot(3500, dialog, [&, dialog] {
            qCritical() << "文件选择未完成" << button << dialog->selectedFiles();
            found = false;
            if (auto *modal = qobject_cast<QDialog *>(QApplication::activeModalWidget()))
                modal->reject();
            dialog->reject();
        });
        QMetaObject::invokeMethod(dialog, "accept");
    });
    for (auto *b : page.findChildren<QPushButton *>())
    {
        if (b->isVisible() && b->text() == button)
        {
            b->click();
            check(found, "未打开文件或范围选择器");
            return;
        }
    }
    throw std::runtime_error("未找到导入导出入口");
}
} // namespace

int main(int argc, char **argv)
{
    QCoreApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
    QApplication app(argc, argv);
    QTemporaryDir dir;
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
        Dossier page([] {}, [] {}, [] {}, [](QString) {});
        page.enter(&book);
        page.show();
        auto *nav = page.findChild<QListWidget *>("navigation");
        nav->setCurrentRow(5);
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
        check(nav->currentRow() == 4 && WritingNotes(book, NoteScope::Global).list().first().title == authored.title,
              "导入全局失败");
        const QString external = dir.filePath("人物对话.md");
        QString header = "name: dialogue\ndescription: >-\n  对话练习，\n  保留人物口吻。\nmetadata:\n  author: "
                         "'示例作者'\nlicense: MIT\n";
        QString instructions = "## 人物\n\n遵循  人物口吻。\n\n---\n结尾。\n";
        put(external, QByteArray::fromHex("efbbbf") +
                          QString("---\n" + header + "---\n" + instructions).replace("\n", "\r\n").toUtf8());
        fileAction(page, "导入 SKILL.md…", external, 1);
        auto notes = WritingNotes(book, NoteScope::Book).list();
        check(nav->currentRow() == 5 && notes.size() == 2 && notes.last().body == instructions &&
                  notes.last().skillHeader == header,
              "外部 Skill 正文或元数据有误");
        auto copy = dir.filePath("分享.md");
        fileAction(page, "导出 SKILL.md…", copy);
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
        Book other(dir.filePath("乙.moxu"), "乙");
        check(WritingNotes(other, NoteScope::Global).list().size() == 1 &&
                  WritingNotes(other, NoteScope::Book).list().isEmpty(),
              "目标范围错误");
    }
    catch (const std::exception &e)
    {
        qCritical("%s", e.what());
        return 1;
    }
    QSqlDatabase::removeDatabase("moxu-personal-writing-library");
    return 0;
}
