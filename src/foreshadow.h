#pragma once
#include "book.h"
#include "downward_combo.h"
#include <QtWidgets>

namespace Clue
{
inline int locate(const QString &body, qint64 version, qint64 recorded, int offset, const QString &quote)
{
    if (quote.isEmpty())
        return -1;
    if (recorded == version && offset >= 0 && body.mid(offset, quote.size()) == quote)
        return offset;
    const int found = body.indexOf(quote);
    return found >= 0 && body.indexOf(quote, found + 1) < 0 ? found : -1;
}

inline void mark(Book &book, int clue, const QString &title, bool resolution, int chapter, qint64 version, int offset,
                 const QString &quote)
{
    book.atomic([&] {
        auto q = book.run("SELECT body_version,body FROM chapters WHERE id=?", {chapter});
        if (!q.next() || q.value(0).toLongLong() != version || quote.trimmed().isEmpty() || offset < 0 ||
            q.value(1).toString().mid(offset, quote.size()) != quote)
            throw std::runtime_error("选区原文已变化，请重新选择。");
        const QString status = resolution ? "已回收" : "已埋设";
        if (!clue)
        {
            if (resolution || title.trimmed().isEmpty())
                throw std::runtime_error("请选择伏笔或填写新伏笔名称。");
            clue = book.run("INSERT INTO foreshadows(title,status) VALUES(?,?)", {title.trimmed(), status})
                       .lastInsertId()
                       .toInt();
        }
        else
        {
            const bool hasResolution =
                book.value("SELECT 1 FROM clue_quotes WHERE clue=? AND role='回收'", {clue}).isValid();
            book.run("UPDATE foreshadows SET status=? WHERE id=?",
                     {hasResolution ? QStringLiteral("已回收") : status, clue});
        }
        book.run("INSERT OR REPLACE INTO clue_quotes(clue,role,chapter,version,offset,quote) VALUES(?,?,?,?,?,?)",
                 {clue, resolution ? "回收" : "埋设", chapter, version, offset, quote});
    });
}
} // namespace Clue

class Foreshadows : public QWidget
{
    Book *book = nullptr;
    QListWidget *list = new QListWidget;
    QLineEdit *title = new QLineEdit;
    DownwardCombo *state = new DownwardCombo, *expected = new DownwardCombo;
    QPlainTextEdit *note = new QPlainTextEdit;
    QFormLayout *form = new QFormLayout;
    QLabel *notice = new QLabel;
    QPushButton *anchors[2];
    int selected = 0;
    void showEntry(int id)
    {
        selected = id;
        auto q = book->run("SELECT title,status,expected,note FROM foreshadows WHERE id=?", {id});
        q.next();
        title->setText(id ? q.value(0).toString() : QString());
        state->setCurrentIndex(id ? state->findText(q.value(1).toString()) : 0);
        expected->setCurrentIndex(id ? qMax(0, expected->findData(q.value(2))) : 0);
        note->setPlainText(id ? q.value(3).toString() : QString());
        for (int i = 0; i < 2; ++i)
        {
            const QString role = i ? "回收" : "埋设";
            auto source = book->run(
                "SELECT c.position,c.title,c.body,c.body_version,q.version,q.offset,q.quote FROM clue_quotes q "
                "JOIN chapters c ON c.id=q.chapter WHERE clue=? AND role=?",
                {id, role});
            bool present = source.next();
            QString label = role + "原文";
            if (present)
            {
                const auto quote = source.value(6).toString();
                const int pos = Clue::locate(source.value(2).toString(), source.value(3).toLongLong(),
                                             source.value(4).toLongLong(), source.value(5).toInt(), quote);
                label += " · " + numberedChapter(source.value(0).toInt(), source.value(1).toString()) +
                         (pos < 0 ? " · 原文待核对" : " · 定位");
                anchors[i]->setToolTip(quote);
            }
            else
                anchors[i]->setToolTip({});
            anchors[i]->setText(label);
            anchors[i]->setEnabled(present);
        }
    }

  public:
    std::function<void(int, int, int)> jump;
    Foreshadows()
    {
        auto *layout = new QVBoxLayout(this);
        layout->addWidget(new QLabel("在这里规划伏笔。在专注写作中选中原文，可标记埋设或回收的位置。"));
        auto *columns = new QHBoxLayout;
        layout->addLayout(columns, 1);
        columns->addWidget(list, 1);
        columns->addLayout(form, 2);
        state->addItems({"待埋设", "已埋设", "已回收", "已放弃"});
        form->addRow("伏笔名称", title);
        form->addRow("状态", state);
        form->addRow("预计回收章节", expected);
        form->addRow("备注", note);
        for (int i = 0; i < 2; ++i)
        {
            anchors[i] = new QPushButton;
            form->addRow(anchors[i]);
            connect(anchors[i], &QPushButton::clicked, this, [this, i] {
                auto q = book->run("SELECT chapter,body,body_version,version,offset,quote FROM clue_quotes JOIN "
                                   "chapters ON chapters.id=chapter "
                                   "WHERE clue=? AND role=?",
                                   {selected, i ? "回收" : "埋设"});
                if (!q.next())
                    return;
                const auto quote = q.value(5).toString();
                const int pos = Clue::locate(q.value(1).toString(), q.value(2).toLongLong(), q.value(3).toLongLong(),
                                             q.value(4).toInt(), quote);
                if (pos < 0)
                    notice->setText("原文已修改或存在多处匹配，请在正文中重新标记。原摘录：" + quote);
                else if (jump)
                    jump(q.value(0).toInt(), pos, quote.size());
            });
        }
        auto *actions = new QHBoxLayout;
        auto *create = new QPushButton("新建伏笔"), *save = new QPushButton("保存伏笔");
        actions->addWidget(create);
        actions->addWidget(save);
        form->addRow(actions);
        notice->setWordWrap(true);
        notice->setTextFormat(Qt::PlainText);
        layout->addWidget(notice);
        connect(create, &QPushButton::clicked, this, [this] {
            list->setCurrentRow(-1);
            showEntry(0);
            title->setFocus();
        });
        connect(list, &QListWidget::currentRowChanged, this, [this](int row) {
            if (book)
            {
                notice->clear();
                showEntry(row < 0 ? 0 : list->item(row)->data(Qt::UserRole).toInt());
            }
        });
        connect(save, &QPushButton::clicked, this, [this] {
            if (!book || title->text().trimmed().isEmpty())
            {
                notice->setText("请填写伏笔名称。");
                return;
            }
            try
            {
                QVariantList args{title->text().trimmed(), state->currentText(), expected->currentData(),
                                  note->toPlainText()};
                if (selected)
                {
                    args << selected;
                    book->run("UPDATE foreshadows SET title=?,status=?,expected=?,note=? WHERE id=?", args);
                }
                else
                    selected = book->run("INSERT INTO foreshadows(title,status,expected,note) VALUES(?,?,?,?)", args)
                                   .lastInsertId()
                                   .toInt();
                load(*book, selected);
                notice->setText("伏笔已保存");
            }
            catch (const std::exception &e)
            {
                notice->setText(QString::fromUtf8(e.what()));
            }
        });
    }
    void load(Book &source, int preferred = 0)
    {
        book = &source;
        QSignalBlocker block(list);
        list->clear();
        expected->clear();
        expected->addItem("未指定");
        auto chapters = book->run("SELECT id,position,title FROM chapters ORDER BY position");
        while (chapters.next())
            expected->addItem(numberedChapter(chapters.value(1).toInt(), chapters.value(2).toString()),
                              chapters.value(0));
        auto q = book->run("SELECT id,title,status FROM foreshadows ORDER BY id");
        int row = 0;
        while (q.next())
        {
            auto *item = new QListWidgetItem(q.value(2).toString() + " · " + q.value(1).toString(), list);
            item->setData(Qt::UserRole, q.value(0));
            if (preferred == q.value(0).toInt())
                row = list->count() - 1;
        }
        list->setCurrentRow(row);
        showEntry(list->currentItem() ? list->currentItem()->data(Qt::UserRole).toInt() : 0);
        notice->clear();
    }
};
