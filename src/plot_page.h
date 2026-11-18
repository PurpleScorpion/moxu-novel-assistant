#pragma once
#include "downward_combo.h"
#include "book.h"
#include <QtWidgets>

class PlotPage : public QWidget
{
    Book *book = nullptr;
    QString file;
    QComboBox *arcs = new DownwardCombo, *strands = new DownwardCombo, *chapters = new DownwardCombo,
              *phases = new DownwardCombo;
    QLineEdit *arcName = new QLineEdit, *strandName = new QLineEdit, *note = new QLineEdit;
    QTableWidget *history = new QTableWidget;
    QLabel *notice = new QLabel;
    QFormLayout *form = new QFormLayout;
    bool newArc() const
    {
        return arcs->currentIndex() == 0;
    }
    bool newStrand() const
    {
        return !newArc() && strands->currentIndex() == 1;
    }

    void updateFields()
    {
        form->setRowVisible(arcName, newArc());
        form->setRowVisible(strands, !newArc());
        form->setRowVisible(strandName, newStrand());
        phases->clear();
        if (newArc() || newStrand())
            phases->addItem("开始");
        else
            phases->addItems({"进展", "结束"});
        notice->clear();
    }
    void showArc(const QString &selectedLine = {})
    {
        QSignalBlocker block(strands);
        strands->clear();
        strands->addItem("主线", QStringLiteral(""));
        strands->addItem("＋ 新建支线");
        history->setRowCount(0);
        if (!newArc())
        {
            auto rows = book->run(
                "WITH numbered AS (SELECT id,title,row_number() OVER(ORDER BY id) AS seq FROM chapters) "
                "SELECT c.seq,c.title,s.strand,s.phase,s.note FROM plot_steps s JOIN numbered c ON c.id=s.chapter "
                "WHERE arc=? ORDER BY s.chapter,s.id",
                {arcs->currentData()});
            QMap<QString, bool> closed;
            while (rows.next())
            {
                const auto line = rows.value(2).toString(), phase = rows.value(3).toString();
                if (!line.isEmpty())
                    closed[line] = (phase == "结束");
                const QStringList cells = {numberedChapter(rows.value(0).toLongLong(), rows.value(1).toString()),
                                           line.isEmpty() ? QString("主线") : line, phase, rows.value(4).toString()};
                int r = history->rowCount();
                history->insertRow(r);
                for (int c = 0; c < cells.size(); ++c)
                {
                    auto *item = new QTableWidgetItem(cells[c]);
                    item->setToolTip(Qt::convertFromPlainText(cells[c]));
                    history->setItem(r, c, item);
                }
            }
            for (auto it = closed.cbegin(); it != closed.cend(); ++it)
                strands->addItem(it.key() + (it.value() ? " · 已结束" : " · 进行中"), it.key());
        }
        strands->setCurrentIndex(selectedLine.isEmpty() ? 0 : qMax(0, strands->findData(selectedLine)));
        updateFields();
    }
    void loadArcs(const QString &selected, const QString &line = {})
    {
        QSignalBlocker block(arcs);
        arcs->clear();
        arcs->addItem("＋ 新建大事");
        auto rows = book->run(
            "SELECT arc,max(phase='结束') FROM plot_steps WHERE strand='' GROUP BY arc ORDER BY min(id) DESC");
        while (rows.next())
        {
            const auto name = rows.value(0).toString();
            arcs->addItem(name + (rows.value(1).toBool() ? " · 已结束" : " · 进行中"), name);
        }
        arcs->setCurrentIndex(selected.isEmpty() ? 0 : qMax(0, arcs->findData(selected)));
        showArc(line);
    }

  public:
    PlotPage()
    {
        auto *layout = new QVBoxLayout(this);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->addLayout(form);
        arcs->setObjectName("plotArc");
        strands->setObjectName("plotStrand");
        chapters->setObjectName("plotChapter");
        phases->setObjectName("plotPhase");
        arcName->setObjectName("plotArcName");
        strandName->setObjectName("plotStrandName");
        note->setObjectName("plotNote");
        arcName->setPlaceholderText("例如：玄武门之变");
        strandName->setPlaceholderText("例如：宫门部署");
        note->setPlaceholderText("这一章发生了什么？");
        form->addRow("剧情大事", arcs);
        form->addRow("大事名称", arcName);
        form->addRow("剧情线", strands);
        form->addRow("支线名称", strandName);
        auto *when = new QHBoxLayout;
        when->addWidget(chapters, 2);
        when->addWidget(phases, 1);
        form->addRow("章节与节点", when);
        form->addRow("经过", note);
        auto *actions = new QHBoxLayout;
        layout->addLayout(actions);
        notice->setTextFormat(Qt::PlainText);
        notice->setWordWrap(true);
        actions->addWidget(notice, 1);
        auto *save = new QPushButton("保存节点");
        save->setObjectName("primary");
        actions->addWidget(save);
        history->setColumnCount(4);
        history->setHorizontalHeaderLabels({"章节", "剧情线", "节点", "经过"});
        history->verticalHeader()->hide();
        history->setEditTriggers(QAbstractItemView::NoEditTriggers);
        history->setSelectionBehavior(QAbstractItemView::SelectRows);
        history->horizontalHeader()->setStretchLastSection(true);
        history->setColumnWidth(0, 175);
        history->setColumnWidth(2, 65);
        layout->addWidget(history, 1);
        connect(arcs, &QComboBox::currentIndexChanged, this, [this] { showArc(); });
        connect(strands, &QComboBox::currentIndexChanged, this, &PlotPage::updateFields);
        connect(save, &QPushButton::clicked, this, &PlotPage::saveNow);
    }
    void saveNow()
    {
        try
        {
            auto arc = newArc() ? arcName->text().trimmed() : arcs->currentData().toString();
            auto line = newStrand() ? strandName->text().trimmed() : strands->currentData().toString();
            if (newStrand() && line.isEmpty())
                throw std::runtime_error("请填写支线名称。");
            book->plotStep(arc, line, chapters->currentData().toInt(), phases->currentText(), note->text());
            arcName->clear();
            strandName->clear();
            note->clear();
            loadArcs(arc, line);
            notice->setText("已保存剧情节点");
        }
        catch (const std::exception &e)
        {
            notice->setText(QString::fromUtf8(e.what()));
        }
    }
    void load(Book &source, int preferredChapter = 0)
    {
        book = &source;
        bool changed = file != source.filePath();
        file = source.filePath();
        if (changed)
        {
            arcName->clear();
            strandName->clear();
            note->clear();
            loadArcs({});
            if (arcs->count() > 1)
                arcs->setCurrentIndex(1);
        }
        else
        {
            const bool draftingStrand = newStrand();
            const auto phase = phases->currentText();
            loadArcs(arcs->currentData().toString(), strands->currentData().toString());
            if (draftingStrand)
                strands->setCurrentIndex(1);
            phases->setCurrentText(phase);
        }
        int selected = preferredChapter ? preferredChapter : changed ? 0 : chapters->currentData().toInt();
        chapters->clear();
        auto rows = book->run("SELECT id,title FROM chapters ORDER BY id");
        while (rows.next())
            chapters->addItem(numberedChapter(chapters->count() + 1, rows.value(1).toString()), rows.value(0));
        chapters->setCurrentIndex(qMax(0, chapters->findData(selected)));
    }
};
