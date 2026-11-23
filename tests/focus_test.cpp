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
QComboBox *ledgerChapterChoice(QWidget &window)
{
    for (auto *combo : visible<QComboBox>(window))
        if (combo->objectName() == "ledgerChapter")
            return combo;
    throw std::runtime_error("账本缺少目标章节");
}
} // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    applyTheme(app);
    app.setOrganizationName("MoxuTests");
    app.setApplicationName("Focus");
    app.setApplicationVersion("1.2.3");
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
        check(window.windowTitle() == "墨序 v1.2.3 · 小说辅助系统", "书架标题未显示运行版本");
        auto *recent = visible<QListWidget>(window).value(0);
        check(recent, "缺少书架");
        recent->setCurrentRow(0);
        QTest::keyClick(recent, Qt::Key_Return);
        check(window.windowTitle() == "墨序 v1.2.3 · 甲", "资料页标题缺少版本或书名");
        click(window, "进入专注写作");
        check(window.windowTitle() == "墨序 v1.2.3 · 甲", "专注写作标题缺少版本或书名");
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
        click(sheet(), "查看章节快照");
        auto *sideBalances = sheet().findChild<QTableWidget *>("snapshotBalances");
        check(editor->isVisible() && sideBalances && sideBalances->isVisible() &&
                  sideBalances->horizontalHeaderItem(1)->text() == "余额" && sideBalances->showGrid(),
              "查阅人物快照离开了正文");
        click(sheet(), "返回属性面板");
        check(preview()->toPlainText() == "取消草稿", "查看快照后丢失面板草稿");
        click(sheet(), "收起");
        check(editor->toPlainText() == "开头原文结尾", "取消后正文变更");
        action("属性面板");
        preview()->setPlainText("面板");
        click(sheet(), "插入属性面板");
        check(editor->toPlainText() == "开头面板结尾", "选区插入错误");
        action("记录事件");
        auto *desc = sheet().findChild<QLineEdit *>("eventDescription");
        desc->setText("完成任务");
        action("记录事件");
        check(desc->isVisible() && desc->text() == "完成任务", "重复点击工具清空了事件草稿");
        action("返回资料工作区");
        click(window, "进入专注写作");
        check(desc->isVisible() && desc->text() == "完成任务", "切换模式后丢失了事件草稿");
        const auto editorWidth = editor->width();
        window.findChild<QPushButton *>("collapseChapters")->click();
        QApplication::processEvents();
        check(!window.findChild<QListWidget *>("chapterList")->isVisible() && editor->width() > editorWidth &&
                  desc->isVisible() && desc->text() == "完成任务",
              "收起目录时未扩宽正文或丢失了事件草稿");
        click(window, "展开目录");
        check(desc->isVisible() && desc->text() == "完成任务", "展开章节目录关闭了事件侧栏");
        click(sheet(), "新建资源");
        check(!desc->isVisible() && editor->isVisible(), "新建资源未保留正文工作区");
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
        selectDossierPage(window.findChild<QListWidget *>("navigation"), 3);
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
        click(window, "记录事件");
        click(window, "返回账本");
        check(ledger->isVisible() && ledger->rowCount() == 4 && ledger->item(3, 4)->text() == "31",
              "取消记事后账本翻页位置丢失");
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
        auto *presetPicker = sheet().findChild<QComboBox *>("templatePreset");
        presetPicker->setCurrentText("冒险者背包");
        auto *panelFormat = visible<QPlainTextEdit>(sheet()).first();
        check(panelFormat->toPlainText().contains("{{血瓶.数量}}"), "属性预设未填入模板编辑框");
        panelFormat->appendPlainText("门派专用");
        visible<QLineEdit>(sheet()).first()->setText("门派面板");
        click(sheet(), "保存模板");
        check(verify.value("SELECT pattern FROM templates WHERE title='门派面板'").toString().endsWith("门派专用"),
              "属性预设自定义修改未保存");
        action("属性面板");
        auto panelChoices = visible<QComboBox>(sheet());
        panelChoices.first()->setCurrentIndex(1);
        const auto actorName = panelChoices.first()->currentText();
        panelChoices.last()->setCurrentText("预设 · 系统状态栏");
        check(preview()->toPlainText().startsWith("宿主：" + actorName), "直接使用预设未带入人物");
        click(sheet(), "以此格式新建模板");
        check(visible<QPlainTextEdit>(sheet()).first()->toPlainText().startsWith("宿主：{{姓名}}"), "另存格式丢失字段");
        visible<QLineEdit>(sheet()).first()->setText("我的系统");
        click(sheet(), "保存模板");
        panelChoices = visible<QComboBox>(sheet());
        check(panelChoices.first()->currentText() == actorName && panelChoices.last()->currentText() == "我的系统",
              "保存自定义格式后未返回原人物和新模板");
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
        selectDossierPage(window.findChild<QListWidget *>("navigation"), 3);
        click(window, "记录事件");
        check(!editor->isVisible() && window.findChild<QListWidget *>("navigation")->isVisible(),
              "账本记事跳到了写作模式");
        auto *ledgerForm = ledgerChapterChoice(window)->parentWidget();
        ledgerForm->findChild<QComboBox *>("eventKind")->setCurrentText("获得道具");
        ledgerForm->findChild<QLineEdit *>("eventSubject")->setText("盘古幡");
        ledgerForm->findChild<QCheckBox *>()->setChecked(true);
        click(window, "保存事件");
        check(ledger->isVisible() && editor->toPlainText().endsWith("获得了盘古幡") &&
                  verify.value("SELECT body FROM chapters WHERE id=1").toString() == editor->toPlainText(),
              "保存后未回到账本或正文不同步");
        click(window, "进入专注写作");
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
        check(editor->isVisible(), "记录剧情大事离开了正文");
        sheet().findChild<QLineEdit *>("plotArcName")->setText("玄武门之变");
        click(window, "保存节点");
        auto *strand = sheet().findChild<QComboBox *>("plotStrand");
        strand->setCurrentIndex(1);
        sheet().findChild<QLineEdit *>("plotStrandName")->setText("宫门部署");
        click(window, "保存节点");
        check(strand->currentData().toString() == "宫门部署", "支线未保存或未选中");
        click(sheet(), "收起");
        action("时间轴");
        check(visible<QTextEdit>(sheet()).first()->toPlainText().contains("玄武门之变 · 宫门部署 · 开始"),
              "时间轴缺少并行剧情");
        action("返回资料工作区");
        auto *nav = window.findChild<QListWidget *>("navigation");
        check(nav->currentItem()->data(Qt::UserRole).toInt() == 3 && ledger->isVisible(), "返回资料工作区未回到原先的账本");
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
        selectDossierPage(nav, 3);
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
        selectDossierPage(nav, 1);
        auto *tabs = window.findChild<QTabWidget *>("personTabs");
        check(tabs, "缺少人物档案标签页");
        tabs->setCurrentIndex(1);
        auto *snapshotChapter = tabs->findChild<QComboBox *>("snapshotChapter");
        auto *balances = tabs->findChild<QTableWidget *>("snapshotBalances");
        auto *items = tabs->findChild<QTableWidget *>("snapshotItems");
        auto *supplies = tabs->findChild<QTableWidget *>("snapshotSupplies");
        auto *skills = tabs->findChild<QTableWidget *>("snapshotSkills");
        check(balances && items && supplies && skills && skills->isVisible() &&
                  skills->horizontalHeaderItem(3)->text() == "剩余次数",
              "章节快照表头缺失");
        auto tableRow = [](QTableWidget *table, const QString &name) {
            QStringList cells;
            for (int r = 0; r < table->rowCount(); ++r)
                if (table->item(r, 0) && table->item(r, 0)->text() == name)
                {
                    for (int c = 0; c < table->columnCount(); ++c)
                        if (auto *cell = table->item(r, c))
                            cells << cell->text();
                    break;
                }
            return cells;
        };
        snapshotChapter->setCurrentIndex(0);
        check(tableRow(skills, "上清仙法") == QStringList{"上清仙法", "第二重", "一日", "3"} &&
                  tableRow(balances, "灵石") == QStringList{"灵石", "12"} &&
                  tableRow(supplies, "无") == QStringList{"无"} && supplies->columnSpan(0, 0) == 2 &&
                  tableRow(items, "盘古幡").isEmpty(),
              "第一章人物快照不符");
        snapshotChapter->setCurrentIndex(1);
        check(tableRow(supplies, "蓝瓶") == QStringList{"蓝瓶", "3"} && supplies->columnSpan(0, 0) == 1 &&
                  tableRow(balances, "灵石") == QStringList{"灵石", "22"},
              "第二章库存或收支未更新");
        window.findChild<QListWidget *>("personList")->setCurrentRow(1);
        check(tableRow(items, "盘古幡") == QStringList{"盘古幡"} && tableRow(skills, "上清仙法").isEmpty(),
              "快照串用人物");
        auto originalRelation = verify.run("INSERT INTO relations(a,b,label) VALUES(1,2,'师傅')").lastInsertId();
        selectDossierPage(nav, 2);
        auto *relations = window.findChild<QListWidget *>("relationList");
        relations->setCurrentRow(0);
        auto *label = visible<QLineEdit>(window).first();
        check(label->text() == "师傅", "旧关系未载入编辑栏");
        label->setText("师徒");
        auto pair = visible<QComboBox>(window);
        pair[0]->setCurrentIndex(1);
        click(window, "保存修改");
        check(verify.value("SELECT label FROM relations WHERE id=?", {originalRelation}).toString() == "师傅",
              "相同人物的无效关系覆盖了旧记录");
        pair[1]->setCurrentIndex(0);
        click(window, "保存修改");
        check(verify.value("SELECT count(*) FROM relations WHERE id=? AND a=2 AND b=1 AND label='师徒'",
                           {originalRelation}).toInt() == 1 && relations->count() == 1, "修改关系未更新原记录");
        click(window, "新建关系");
        label->setText("同门");
        click(window, "保存关系");
        check(relations->count() == 2 && relations->currentItem()->text().contains("同门"), "无法继续创建关系");
        click(window, "查看关系图");
        check(nav->isVisible() && !visible<QGraphicsView>(window).isEmpty(), "资料关系图跳出了侧栏");
        auto graphLabels = [&](QWidget &parent) {
            QStringList labels;
            for (auto *item : visible<QGraphicsView>(parent).first()->scene()->items())
                if (auto *text = qgraphicsitem_cast<QGraphicsTextItem *>(item))
                    labels << text->toPlainText();
            return labels;
        };
        check(graphLabels(window).contains("师徒") && !graphLabels(window).contains("师傅"), "关系图还在显示旧名称");
        click(window, "返回关系列表");
        click(window, "进入专注写作");
        action("关系图");
        action("返回资料工作区");
        relations->setCurrentRow(0);
        click(window, "删除关系");
        click(window, "取消");
        check(relations->count() == 2, "取消后关系仍被删除");
        click(window, "删除关系");
        click(window, "确认删除");
        check(verify.value("SELECT count(*) FROM relations WHERE id=?", {originalRelation}).toInt() == 0 &&
                  relations->count() == 1 && relations->item(0)->text().contains("同门"), "删除关系影响了其他记录");
        click(window, "进入专注写作");
        check(!graphLabels(sheet()).contains("师徒") && graphLabels(sheet()).contains("同门"), "写作关系图没有刷新");
        action("返回资料工作区");
        selectDossierPage(nav, 3);
        click(window, "记录事件");
        auto *which = ledgerChapterChoice(window);
        which->setCurrentIndex(1);
        auto *entry = which->parentWidget();
        entry->findChild<QComboBox *>("eventKind")->setCurrentText("普通事件");
        entry->findChild<QLineEdit *>("eventDescription")->setText("另章记事");
        entry->findChild<QCheckBox *>()->setChecked(true);
        auto currentText = editor->toPlainText();
        click(window, "保存事件");
        check(ledger->isVisible() && editor->toPlainText() == currentText &&
                  verify.value("SELECT body FROM chapters WHERE id=2").toString().endsWith("另章记事"),
              "追加正文未写入所选章节");
        click(window, "进入专注写作");
        click(sheet(), "收起");
        chapterList->setCurrentRow(0);
        const QString original = "上文。下文。";
        editor->setPlainText(original);
        auto taskCursor = editor->textCursor();
        taskCursor.setPosition(1);
        taskCursor.setPosition(3, QTextCursor::KeepAnchor);
        editor->setTextCursor(taskCursor);
        StoryTask future;
        future.person = 1;
        future.published = 2;
        future.title = "后章任务";
        int futureId = StoryTasks::save(verify, future);
        action("任务");
        auto *taskTabs = sheet().findChild<QTabWidget *>();
        taskTabs->setCurrentIndex(1);
        check(sheet().findChild<QComboBox *>("writerTaskPending")->findData(futureId) < 0, "当前章节出现未来任务");
        taskTabs->setCurrentIndex(0);
        sheet().findChild<QLineEdit *>("writerTaskTitle")->setText("寻回玉简");
        sheet().findChild<QPlainTextEdit *>("writerTaskConditions")->setPlainText("将玉简带回山门。");
        auto *taskActor = sheet().findChild<QComboBox *>("writerTaskActor");
        taskActor->setCurrentIndex(taskActor->findData(1));
        auto *taskResource = sheet().findChild<QComboBox *>("writerTaskResource");
        taskResource->setCurrentText("数值资源 · 积分");
        sheet().findChild<QSpinBox *>("writerTaskQuantity")->setValue(42);
        click(sheet(), "添加奖励");
        auto previewText = [&] { return sheet().findChild<QPlainTextEdit *>("taskMessagePreview")->toPlainText(); };
        check(previewText().contains("任务奖励：积分 × 42"), "添加奖励未更新发布预览");
        click(sheet(), "编辑模板");
        auto *pattern = sheet().findChild<QPlainTextEdit *>("taskMessagePattern");
        pattern->setPlainText("  ");
        click(sheet(), "保存模板");
        check(pattern->isVisible() && StoryTasks::messagePattern(verify, false) == StoryTasks::defaultMessagePattern(false),
              "空白模板覆盖了已保存的格式");
        pattern->setPlainText("【悬赏】{{承接人}} · {{任务名}}\n{{完成条件}}\n酬劳：{{奖励}}");
        const QString publishNotice = "【悬赏】林澈 · 寻回玉简\n将玉简带回山门。\n酬劳：积分 × 42";
        check(previewText() == publishNotice, "编辑发布模板未实时预览");
        click(sheet(), "保存模板");
        check(previewText() == publishNotice, "保存模板丢失发布草稿");
        auto *insertTask = sheet().findChild<QCheckBox *>("writerTaskInsert");
        check(!insertTask->isChecked(), "发布任务默认插入正文");
        insertTask->setChecked(true);
        const auto beforePoints = verify.balance(1, "积分", 2);
        const auto savedFormat = StoryTasks::messagePattern(verify, false);
        StoryTasks::saveMessagePattern(verify, false, "临时修改");
        click(sheet(), "在本章发布任务");
        check(editor->toPlainText() == original &&
                  !verify.value("SELECT 1 FROM story_tasks WHERE title='寻回玉简'").isValid(),
              "预览过期后仍发布任务或插入正文");
        StoryTasks::saveMessagePattern(verify, false, savedFormat);
        click(sheet(), "在本章发布任务");
        const int publishedId =
            verify.value("SELECT id FROM story_tasks WHERE title='寻回玉简' AND published=1").toInt();
        check(editor->toPlainText() == original.left(3) + "\n" + publishNotice + "\n" + original.mid(3) &&
                  verify.value("SELECT body FROM chapters WHERE id=1") == editor->toPlainText() &&
                  publishNotice.contains("将玉简带回山门。") && publishNotice.contains("积分 × 42") &&
                  verify.balance(1, "积分", 2) == beforePoints,
              "发布任务未保留选区或提前发奖");

        chapterList->setCurrentRow(1);
        action("任务");
        sheet().findChild<QTabWidget *>()->setCurrentIndex(1);
        auto *choices = sheet().findChild<QComboBox *>("writerTaskPending");
        choices->setCurrentIndex(choices->findData(publishedId));
        check(previewText().contains("【系统任务完成】寻回玉简"), "发布模板影响完成模板");
        click(sheet(), "编辑模板");
        pattern = sheet().findChild<QPlainTextEdit *>("taskMessagePattern");
        pattern->setPlainText("「{{任务名}}」已交付。\n{{承接人}}获得{{奖励}}。");
        click(sheet(), "保存模板");
        const QString finishNotice = "「寻回玉简」已交付。\n林澈获得积分 × 42。";
        check(previewText() == finishNotice, "完成预览未填入当前任务");
        click(sheet(), "编辑模板");
        click(sheet(), "使用默认格式");
        click(sheet(), "取消");
        check(previewText() == finishNotice, "取消编辑影响已保存的完成模板");
        auto secondBody = editor->toPlainText();
        insertTask = sheet().findChild<QCheckBox *>("writerTaskInsert");
        check(!insertTask->isChecked(), "结算默认插入正文");
        insertTask->setChecked(true);
        verify.run("CREATE TRIGGER refuse_task_text BEFORE UPDATE OF body ON chapters BEGIN SELECT "
                   "RAISE(ABORT,'写入失败'); END");
        click(sheet(), "完成任务并发奖");
        check(sheet().isVisible() && editor->toPlainText() == secondBody &&
                  !StoryTasks::get(verify, publishedId).completed && verify.balance(1, "积分", 2) == beforePoints,
              "正文写入失败未回滚任务和奖励");
        verify.run("DROP TRIGGER refuse_task_text");
        click(sheet(), "完成任务并发奖");
        check(editor->toPlainText().contains(finishNotice) &&
                  verify.value("SELECT body FROM chapters WHERE id=2") == editor->toPlainText() &&
                  verify.balance(1, "积分", 2) == beforePoints + 42 &&
                  StoryTasks::get(verify, publishedId).completed == 2,
              "当前章节结算或正文通知错误");
        auto afterFinish = editor->toPlainText();
        action("任务");
        sheet().findChild<QTabWidget *>()->setCurrentIndex(1);
        choices = sheet().findChild<QComboBox *>("writerTaskPending");
        check(choices->findData(publishedId) < 0 && choices->findData(futureId) >= 0, "可结算列表未刷新");
        choices->setCurrentIndex(choices->findData(futureId));
        click(sheet(), "完成任务并发奖");
        check(StoryTasks::get(verify, futureId).completed == 2 && editor->toPlainText() == afterFinish,
              "未勾选插入仍改动了正文");
        action("任务");
        sheet().findChild<QLineEdit *>("writerTaskTitle")->setText("无正文通知");
        click(sheet(), "在本章发布任务");
        check(verify.value("SELECT count(*) FROM story_tasks WHERE title='无正文通知' AND published=2").toInt() == 1 &&
                  editor->toPlainText() == afterFinish,
              "只发布任务失败");
        check(watch.shown.isEmpty(), "专注写作中出现了对话框");
    }
    catch (const std::exception &e)
    {
        qCritical("%s", e.what());
        return 1;
    }
    return 0;
}
