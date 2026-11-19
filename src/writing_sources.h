#pragma once
#include <QtWidgets>
#include <array>

class WritingSources : public QListWidget
{
  public:
    enum Group { GlobalSkill, BookSkill, Outline, Full, Summary, GroupCount };
    enum Role { Body = Qt::UserRole, ChapterId, GroupId, Available };
    struct Section
    {
        QString name;
        QListWidgetItem *heading = nullptr;
        QList<QListWidgetItem *> entries;
        bool expanded = true;
    };
    std::array<Section, GroupCount> sections;

    WritingSources()
    {
        setObjectName("writingSources");
        setUniformItemSizes(true);
        connect(this, &QListWidget::itemClicked, this, [this](QListWidgetItem *item) {
            auto id = groupOf(item);
            if (item == sections[id].heading)
                setExpanded(id, !sections[id].expanded);
        });
    }
    static Group groupOf(const QListWidgetItem *item) { return Group(item->data(GroupId).toInt()); }
    void addHeading(Group id, const QString &name)
    {
        auto &s = sections[id];
        s.name = name;
        s.heading = new QListWidgetItem("▾  " + name, this);
        s.heading->setData(GroupId, int(id));
        auto font = s.heading->font();
        font.setBold(true);
        s.heading->setFont(font);
    }
    QListWidgetItem *addEntry(Group id, const QString &name, const QVariant &body = {}, int chapter = 0,
                              bool available = true)
    {
        auto &s = sections[id];
        auto *item = new QListWidgetItem(name);
        item->setData(GroupId, int(id));
        item->setData(ChapterId, chapter);
        item->setData(Body, body);
        item->setData(Available, available);
        item->setCheckState(Qt::Unchecked);
        insertItem(row(s.heading) + 1 + s.entries.size(), item);
        s.entries.append(item);
        item->setHidden(!s.expanded || !available);
        return item;
    }
    void setExpanded(Group id, bool expanded)
    {
        auto &s = sections[id];
        QSignalBlocker block(this);
        s.expanded = expanded;
        s.heading->setText((expanded ? "▾  " : "▸  ") + s.name);
        for (auto *entry : s.entries)
            entry->setHidden(!expanded || !entry->data(Available).toBool());
    }

  protected:
    void keyPressEvent(QKeyEvent *event) override
    {
        if (auto *item = currentItem())
        {
            auto id = groupOf(item);
            if (item == sections[id].heading)
            {
                switch (event->key())
                {
                case Qt::Key_Left: setExpanded(id, false); return;
                case Qt::Key_Right: setExpanded(id, true); return;
                case Qt::Key_Return:
                case Qt::Key_Enter:
                case Qt::Key_Space: setExpanded(id, !sections[id].expanded); return;
                }
            }
        }
        QListWidget::keyPressEvent(event);
    }
};
