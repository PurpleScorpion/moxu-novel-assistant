#include "dialog_watch.h"
#include "window.h"
#include <QTest>

namespace
{
void check(bool ok, const char *message)
{
    if (!ok)
        throw std::runtime_error(message);
}
template <class T> QList<T *> visible(QWidget &parent)
{
    QList<T *> result;
    for (auto *widget : parent.findChildren<T *>())
        if (widget->isVisible())
            result.append(widget);
    return result;
}
void click(QWidget &parent, QString label)
{
    for (auto *button : visible<QPushButton>(parent))
        if (button->text() == label)
        {
            button->click();
            return;
        }
    throw std::runtime_error(("未找到按钮 " + label).toStdString());
}
} // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    applyTheme(app);
    app.setOrganizationName("MoxuTests");
    app.setApplicationName("Focus");
    QTemporaryDir dir;
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, dir.path());
    try
    {
        auto path = dir.filePath("甲.moxu");
        {
            Book book(path, "甲");
            book.run("INSERT INTO people(name,attributes) VALUES('林澈','境界：筑基'),('苏禾','境界：金丹')");
            book.run("INSERT INTO templates(title,pattern) VALUES('简表','{{姓名}} / {{境界}} / {{积分}}')");
            book.save(1, "开头原文结尾");
        }
        QSettings().setValue("recent", QStringList{path});
        QSettings().setValue("booksDir", dir.path());
        Window window;
        window.show();
        auto *recent = visible<QListWidget>(window).value(0);
        check(recent, "缺少书架");
        recent->setCurrentRow(0);
        QTest::keyClick(recent, Qt::Key_Return);
        click(window, "进入专注写作");
        DialogWatch watch;
        app.installEventFilter(&watch);
        auto *editor = window.findChild<QPlainTextEdit *>("manuscript");
        auto action = [&](QString title) {
            for (auto *a : window.findChildren<QAction *>())
                if (a->text() == title)
                {
                    a->trigger();
                    return;
                }
            throw std::runtime_error(("未找到工具 " + title).toStdString());
        };
        auto sheet = [&]() -> QWidget & {
            QApplication::processEvents();
            for (auto *w : window.findChildren<QWidget *>("writerSheet"))
                if (w->isVisible())
                    return *w;
            throw std::runtime_error("未显示写作侧栏");
        };
        auto preview = [&] { return visible<QPlainTextEdit>(sheet()).at(0); };
        auto cursor = editor->textCursor();
        cursor.setPosition(2);
        cursor.setPosition(4, QTextCursor::KeepAnchor);
        editor->setTextCursor(cursor);
        action("属性面板");
        auto options = visible<QComboBox>(sheet());
        check(options.size() == 2, "人物与模板应在同一页");
        options.at(0)->setCurrentIndex(1);
        options.at(1)->setCurrentIndex(1);
        check(preview()->toPlainText() == "苏禾 / 金丹 / 0", "切换后预览错误");
        preview()->setPlainText("取消草稿");
        click(sheet(), "收起");
        check(editor->toPlainText() == "开头原文结尾", "取消后正文变更");
        action("属性面板");
        preview()->setPlainText("面板");
        click(sheet(), "插入属性面板");
        check(editor->toPlainText() == "开头面板结尾", "选区插入错误");
        action("记录事件");
        auto *desc = sheet().findChild<QLineEdit *>("eventDescription");
        desc->setText("完成任务");
        const auto editorWidth = editor->width();
        window.findChild<QPushButton *>("collapseChapters")->click();
        QApplication::processEvents();
        check(!window.findChild<QListWidget *>("chapterList")->isVisible() && editor->width() > editorWidth &&
                  desc->isVisible() && desc->text() == "完成任务",
              "收起目录时未扩宽正文或丢失了事件草稿");
        click(window, "展开目录");
        check(desc->isVisible() && desc->text() == "完成任务", "展开章节目录关闭了事件侧栏");
        click(sheet(), "新建资源");
        check(!desc->isVisible(), "创建资源未进入独立页面");
        click(window, "返回");
        check(desc->isVisible() && desc->text() == "完成任务", "返回后事件草稿丢失");
        click(sheet(), "新建资源");
        auto *newResource = visible<QLineEdit>(window).first();
        newResource->setText("积分");
        click(window, "创建");
        check(newResource->isVisible(), "重名时未停留在创建页");
        newResource->setText("灵石");
        click(window, "创建");
        auto *resource = sheet().findChild<QComboBox *>("resourceChoice");
        check(!resource->isEditable() && resource->currentText() == "灵石", "新建资源后没有自动选中");
        resource->setCurrentIndex(resource->findText("积分"));
        auto *amount = visible<QSpinBox>(sheet()).first();
        amount->setValue(-5);
        click(sheet(), "保存事件");
        Book verify(path);
        check(verify.value("SELECT COUNT(*) FROM events").toInt() == 0 && editor->toPlainText() == "开头面板结尾",
              "失败事件未回滚");
        check(sheet().findChild<QLabel *>("sheetMessage")->isVisible() && desc->text() == "完成任务", "失败未保留表单");
        amount->setValue(300);
        check(preview()->toPlainText().contains("0 → 300"), "余额预览错误");
        if (argc == 2)
            check(window.grab().save(QString::fromLocal8Bit(argv[1])), "截图失败");
        click(sheet(), "保存事件");
        check(verify.balance(1, "积分", 1) == 300 && verify.value("SELECT body FROM chapters WHERE id=1").toString() ==
                                                         "开头面板完成任务（积分+300，剩余300）结尾",
              "事件及正文未一起保存");
        auto before = editor->toPlainText();
        action("记录事件");
        sheet().findChild<QLineEdit *>("eventDescription")->setText("购买丹药");
        visible<QSpinBox>(sheet()).first()->setValue(-60);
        visible<QCheckBox>(sheet()).first()->setChecked(false);
        click(sheet(), "保存事件");
        check(verify.balance(1, "积分", 1) == 240 && editor->toPlainText() == before, "仅记账改变了正文");
        action("记录事件");
        resource = sheet().findChild<QComboBox *>("resourceChoice");
        resource->setCurrentIndex(resource->findText("灵石"));
        sheet().findChild<QLineEdit *>("eventDescription")->setText("获得灵石");
        visible<QSpinBox>(sheet()).first()->setValue(12);
        visible<QCheckBox>(sheet()).first()->setChecked(false);
        click(sheet(), "保存事件");
        check(verify.balance(1, "灵石", 1) == 12, "所选资源与实际入账的资源不同");
        action("历史版本");
        auto *versions = sheet().findChild<QListWidget *>("historyVersions");
        check(versions && versions->isVisible() && versions->count() >= 2, "历史版本列表未显示");
        versions->setCurrentRow(1);
        check(preview()->toPlainText() ==
                  verify.value("SELECT body FROM revisions WHERE id=?", {versions->currentItem()->data(Qt::UserRole)})
                      .toString(),
              "切换版本后预览错误");
        versions->setCurrentRow(0);
        check(preview()->toPlainText() == "开头面板结尾", "历史预览错误");
        click(sheet(), "用此版本恢复本章");
        check(editor->toPlainText() == "开头面板结尾", "历史恢复错误");
        verify.run("WITH RECURSIVE n(i) AS (SELECT 1 UNION ALL SELECT i+1 FROM n WHERE i<14) "
                   "INSERT INTO revisions(chapter,body) SELECT 1,'旧稿' FROM n");
        action("历史版本");
        click(sheet(), "整理全书旧版历史…");
        const auto toRemove = verify.historyCleanupCount();
        check(toRemove > 0, "整理应预告淘汰的旧稿");
        click(sheet(), "返回历史版本");
        check(verify.value("SELECT count(*) FROM revisions WHERE rolling=0").toInt() == 14, "取消整理丢失旧历史");
        click(sheet(), "整理全书旧版历史…");
        click(sheet(), "确认整理");
        check(verify.value("SELECT count(*) FROM revisions").toInt() == HistoryLimits{}.perChapter &&
                  verify.value("SELECT body FROM chapters WHERE id=1").toString() == editor->toPlainText() &&
                  sheet().findChild<QLabel *>("sheetMessage")->text().contains(QString::number(toRemove)),
              "整理后数量或正文有误");
        action("返回资料工作区");
        auto *usage = window.findChild<QLabel *>("historyUsage");
        check(usage->isVisible() && usage->text().contains("共 10 份"), "书籍概览的历史统计错误");
        click(window, "设置历史上限");
        auto *chapterLimit = window.findChild<QSpinBox *>("chapterHistoryLimit");
        chapterLimit->setValue(2);
        click(window, "取消");
        check(verify.historyUsage().count == 10, "取消变更删掉了历史");
        click(window, "设置历史上限");
        check(chapterLimit->value() == 10, "取消后仍显示草稿");
        chapterLimit->setValue(2);
        window.findChild<QSpinBox *>("bookHistoryLimit")->setValue(7);
        click(window, "应用并移除 8 份");
        check(usage->text().contains("共 2 份") && verify.historyLimits().perBook == 7, "未应用本书上限或未刷新用量");
        window.findChild<QListWidget *>("navigation")->setCurrentRow(3);
        auto *ledger = window.findChild<QTableWidget *>("eventTable");
        check(ledger->isVisible() && ledger->rowCount() == 3 && ledger->item(0, 5)->text() == "获得灵石" &&
                  ledger->horizontalHeaderItem(4)->text() == "结余",
              "账本列顺序或默认排序错误");
        for (int col = 0; col < 6; ++col)
            check((ledger->item(0, col)->textAlignment() == Qt::AlignCenter) == (col >= 1 && col <= 4),
                  "账本列对齐错误");
        window.findChild<QLineEdit *>("ledgerPerson")->setText("澈");
        window.findChild<QLineEdit *>("ledgerDescription")->setText("丹药");
        for (auto *combo : visible<QComboBox>(window))
            if (combo->accessibleName() == "筛选资源")
                combo->setCurrentIndex(combo->findText("积分"));
        click(window, "搜索");
        check(ledger->rowCount() == 1 && ledger->item(0, 4)->text() == "240", "组合检索未保留事件发生后的真实结余");
        click(window, "重置");
        for (auto *combo : visible<QComboBox>(window))
            if (combo->accessibleName() == "事件排序")
                combo->setCurrentIndex(1);
        check(ledger->rowCount() == 3 && ledger->item(0, 5)->text() == "完成任务", "账本正序或重置错误");
        verify.run(
            "WITH RECURSIVE n(i) AS (SELECT 1 UNION ALL SELECT i+1 FROM n WHERE i<31) "
            "INSERT INTO events(chapter,person,resource,delta,description) SELECT 1,2,'灵石',1,'采集'||i FROM n");
        click(window, "搜索");
        check(ledger->rowCount() == 30, "账本第一页条数错误");
        click(window, "下一页");
        check(ledger->rowCount() == 4 && ledger->item(3, 4)->text() == "31", "翻页行数或跨页余额错误");
        click(window, "上一页");
        check(ledger->rowCount() == 30 && ledger->item(0, 5)->text() == "完成任务", "上一页未恢复结果");
        click(window, "进入专注写作");
        action("历史版本");
        bool currentLimits = false;
        for (auto *label : visible<QLabel>(sheet()))
            currentLimits |= label->text().contains("每章最多 2 份，全书共用 7 份");
        check(currentLimits, "历史侧栏未显示本书设置");
        action("时间轴");
        check(visible<QTextEdit>(sheet()).first()->toPlainText().contains("240"), "时间轴错误");
        action("关系图");
        check(!visible<QGraphicsView>(sheet()).isEmpty(), "关系图未嵌入");
        action("资料与导出");
        click(sheet(), "新建输出模板");
        visible<QLineEdit>(sheet()).first()->setText("门派面板");
        click(sheet(), "保存模板");
        check(verify.value("SELECT count(*) FROM templates WHERE title='门派面板'").toInt() == 1, "模板未保存");
        action("资料与导出");
        for (auto *button : visible<QPushButton>(sheet()))
            check(button->text() != "整书基础校对", "资料与导出中仍有已移除的校对入口");
        click(sheet(), "导出全书 TXT");
        auto out = dir.filePath("导出.txt");
        {
            QFile file(out);
            check(file.open(QIODevice::WriteOnly), "无法建立导出样本");
            file.write("keep");
        }
        visible<QLineEdit>(sheet()).first()->setText(out);
        click(sheet(), "导出文本");
        {
            QFile file(out);
            file.open(QIODevice::ReadOnly);
            check(file.readAll() == "keep", "已有文件被静默覆盖");
        }
        visible<QCheckBox>(sheet()).first()->setChecked(true);
        click(sheet(), "导出文本");
        {
            QFile file(out);
            file.open(QIODevice::ReadOnly);
            check(file.readAll().contains("开头面板结尾"), "TXT 导出失败");
        }
        action("资料与导出");
        click(sheet(), "备份整本书…");
        auto copy = dir.filePath("备份.moxu");
        visible<QLineEdit>(sheet()).first()->setText(copy);
        click(sheet(), "保存备份");
        {
            Book backup(copy);
            check(backup.balance(1, "积分", 1) == 240, "备份缺少事件");
        }
        click(window, "＋ 新章节");
        visible<QLineEdit>(sheet()).first()->setText("第二章");
        click(sheet(), "创建章节");
        action("人物名字");
        click(sheet(), "插入姓名");
        check(editor->toPlainText() == "林澈", "新章节人物名插入失败");
        click(window, "改章名");
        auto *newName = visible<QLineEdit>(sheet()).first();
        newName->clear();
        click(sheet(), "保存章节名");
        check(newName->isVisible(), "空名校验未保留编辑表单");
        newName->setText("重逢");
        click(sheet(), "保存章节名");
        check(verify.value("SELECT title FROM chapters WHERE id=2").toString() == "重逢" &&
                  verify.value("SELECT body FROM chapters WHERE id=2").toString() == "林澈",
              "改名未保留原章节");
        auto *chapterList = visible<QListWidget>(window).first();
        check(chapterList->item(1)->text() == "第 2 章 · 重逢", "目录没有显示序号及新章名");
        auto titleStart = [&] {
            QApplication::processEvents();
            const auto bounds = chapterList->visualItemRect(chapterList->item(1));
            auto image = chapterList->viewport()->grab(bounds).toImage();
            for (int x = 2; x < image.width() - 2; ++x)
                for (int y = image.height() / 4; y < 3 * image.height() / 4; ++y)
                {
                    auto pixel = image.pixelColor(x, y);
                    if (pixel.red() > 135 && pixel.green() > 165)
                        return x;
                }
            return -1;
        };
        auto selectedStart = titleStart();
        chapterList->setCurrentRow(0);
        check(selectedStart >= 0 && titleStart() == selectedStart, "目录选中前后的文字位置变动");
        auto setMode = [&](QString label) {
            sheet().findChild<QComboBox *>("eventKind")->setCurrentText(label);
            visible<QCheckBox>(sheet()).first()->setChecked(false);
        };
        action("返回资料工作区");
        window.findChild<QListWidget *>("navigation")->setCurrentRow(3);
        click(window, "记录事件");
        setMode("获得道具");
        sheet().findChild<QLineEdit *>("eventSubject")->setText("盘古幡");
        click(sheet(), "保存事件");
        action("记录事件");
        setMode("转赠道具");
        check(sheet().findChild<QComboBox *>("eventKnown")->currentText() == "盘古幡", "道具无法转赠");
        click(sheet(), "保存事件");
        action("记录事件");
        setMode("习得技能／功法");
        sheet().findChild<QLineEdit *>("eventSubject")->setText("八九玄功");
        visible<QSpinBox>(sheet()).first()->setValue(1);
        click(sheet(), "保存事件");
        action("记录事件");
        setMode("施展技能／功法");
        click(sheet(), "保存事件");
        verify.event(2, 1, "灵石", 10, "第二章收入");
        verify.run(
            "INSERT INTO templates(title,pattern) VALUES('旅途','{{灵石}} / {{持有道具}} / {{八九玄功.剩余次数}}')");
        action("属性面板");
        options = visible<QComboBox>(sheet());
        options.last()->setCurrentText("旅途");
        check(preview()->toPlainText() == "12 / 无 / 0", "资源、技能状态或截章错误");
        options.first()->setCurrentIndex(1);
        check(preview()->toPlainText() == "31 / 盘古幡 / 未记录", "人物资产串用");
        click(sheet(), "插入属性面板");
        check(editor->toPlainText().contains("31 / 盘古幡 / 未记录"), "资产面板没有写入正文");
        action("记录事件");
        setMode("获得消耗品");
        sheet().findChild<QComboBox *>("supplyName")->setEditText("回城卷轴");
        click(sheet(), "保存事件");
        action("记录事件");
        setMode("使用消耗品");
        check(sheet().findChild<QComboBox *>("supplyName")->currentText() == "回城卷轴", "消耗品库存未加载");
        sheet().findChild<QSpinBox *>("supplyQuantity")->setValue(2);
        click(sheet(), "保存事件");
        check(sheet().findChild<QLabel *>("sheetMessage")->text().contains("数量不足"), "超量使用未提示");
        sheet().findChild<QSpinBox *>("supplyQuantity")->setValue(1);
        click(sheet(), "保存事件");
        verify.run("INSERT INTO templates(title,pattern) VALUES('随身物品','{{回城卷轴.数量}} / {{消耗品}}')");
        action("属性面板");
        visible<QComboBox>(sheet()).last()->setCurrentText("随身物品");
        check(preview()->toPlainText() == "0 / 无", "一次性卷轴用后仍出现在库存");
        click(sheet(), "收起");
        action("剧情大事");
        window.findChild<QLineEdit *>("plotArcName")->setText("玄武门之变");
        click(window, "保存节点");
        auto *strand = window.findChild<QComboBox *>("plotStrand");
        strand->setCurrentIndex(1);
        window.findChild<QLineEdit *>("plotStrandName")->setText("宫门部署");
        click(window, "保存节点");
        check(strand->currentData().toString() == "宫门部署", "支线未保存或未选中");
        click(window, "进入专注写作");
        action("时间轴");
        check(visible<QTextEdit>(sheet()).first()->toPlainText().contains("玄武门之变 · 宫门部署 · 开始"),
              "时间轴缺少并行剧情");
        action("返回资料工作区");
        auto *nav = window.findChild<QListWidget *>("navigation");
        nav->setCurrentItem(nav->findItems("资源管理", Qt::MatchExactly).first());
        auto *catalog = window.findChild<QTableWidget *>("catalogTable");
        auto pickCatalog = [&](QString text) {
            auto matches = catalog->findItems(text, Qt::MatchExactly);
            check(!matches.isEmpty(), "资源目录缺少条目");
            catalog->setCurrentItem(matches.first());
        };
        pickCatalog("盘古幡");
        click(window, "编辑");
        visible<QLineEdit>(window).first()->setText("混元幡");
        click(window, "保存修改");
        pickCatalog("混元幡");
        click(window, "删除");
        check(catalog->findItems("盘古幡", Qt::MatchExactly).isEmpty() &&
                  catalog->findItems("混元幡", Qt::MatchExactly).isEmpty(),
              "目录改删后旧条目重新出现");
        click(window, "新建资源");
        auto *kind = visible<QComboBox>(window).first();
        kind->setCurrentText("技能／功法");
        auto texts = visible<QLineEdit>(window);
        texts[0]->setText("上清仙法");
        texts[1]->setText("第二重");
        texts[2]->setText("一日");
        visible<QSpinBox>(window).first()->setValue(3);
        click(window, "创建");
        pickCatalog("上清仙法");
        nav->setCurrentRow(3);
        click(window, "重置");
        auto *filter = visible<QComboBox>(window).first();
        filter->setCurrentText("盘古幡");
        check(ledger->rowCount() == 2 && ledger->item(0, 2)->text() == "盘古幡", "旧道具名筛选失效");
        click(window, "进入专注写作");
        action("记录事件");
        setMode("习得技能／功法");
        sheet().findChild<QComboBox *>("eventSubjectChoice")->setCurrentText("上清仙法");
        check(visible<QSpinBox>(sheet()).first()->value() == 3, "技能目录未带入初始次数");
        click(sheet(), "保存事件");
        verify.supplyEvent(2, 1, SupplyAction::Receive, "蓝瓶", 3, 0, "买药");
        action("返回资料工作区");
        nav->setCurrentRow(1);
        auto *snapshotChapter = window.findChild<QComboBox *>("snapshotChapter");
        auto *tabs = window.findChild<QTabWidget *>("personTabs");
        check(tabs, "缺少人物档案标签页");
        tabs->setCurrentIndex(1);
        auto *report = window.findChild<QTextBrowser *>("snapshotReport");
        check(report->isVisible(), "未显示章节快照");
        snapshotChapter->setCurrentIndex(0);
        auto first = report->toPlainText();
        check(first.contains("上清仙法") && first.contains("第二重") && first.contains("一日") &&
                  first.contains("12") && !first.contains("蓝瓶") && !first.contains("盘古幡"),
              "第一章人物快照不符");
        snapshotChapter->setCurrentIndex(1);
        check(report->toPlainText().contains("蓝瓶") && report->toPlainText().contains("22"), "第二章库存或收支未更新");
        window.findChild<QListWidget *>("personList")->setCurrentRow(1);
        check(report->toPlainText().contains("盘古幡") && !report->toPlainText().contains("上清仙法"), "快照串用人物");
        check(watch.shown.isEmpty(), "专注写作中出现了对话框");
    }
    catch (const std::exception &e)
    {
        fprintf(stderr, "%s\n", e.what());
        return 1;
    }
    return 0;
}
