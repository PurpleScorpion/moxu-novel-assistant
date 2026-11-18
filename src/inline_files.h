#pragma once
#include <QtWidgets>
#include <functional>
#include <stdexcept>

// Replace a section's contents while keeping its editor and unsaved fields alive.
class InlineActions : public QStackedWidget
{
    QWidget *home;
    QVBoxLayout *layout = nullptr;

  public:
    explicit InlineActions(QWidget *home) : home(home)
    {
        addWidget(home);
    }
    void finish()
    {
        auto *old = currentWidget();
        if (old == home)
            return;
        setCurrentWidget(home);
        removeWidget(old);
        old->deleteLater();
        layout = nullptr;
    }
    QVBoxLayout *begin(const QString &title)
    {
        finish();
        auto *page = new QWidget;
        layout = new QVBoxLayout(page);
        layout->setSpacing(10);
        auto *heading = new QLabel(title);
        heading->setTextFormat(Qt::PlainText);
        heading->setWordWrap(true);
        heading->setObjectName("sectionTitle");
        layout->addWidget(heading);
        addWidget(page);
        setCurrentWidget(page);
        return layout;
    }
    QPushButton *buttons(const QString &label, std::function<void()> apply)
    {
        auto *page = currentWidget();
        auto *message = new QLabel;
        message->setObjectName("actionError");
        message->setWordWrap(true);
        message->setTextFormat(Qt::PlainText);
        layout->addWidget(message);
        auto *row = new QHBoxLayout;
        layout->addLayout(row);
        row->addStretch();
        auto *cancel = new QPushButton("取消");
        auto *confirm = new QPushButton(label);
        confirm->setObjectName("primary");
        row->addWidget(cancel);
        row->addWidget(confirm);
        connect(cancel, &QPushButton::clicked, page, [this] { finish(); });
        connect(confirm, &QPushButton::clicked, page, [this, page, message, apply] {
            try
            {
                apply();
                if (currentWidget() == page)
                    finish();
            }
            catch (const std::exception &error)
            {
                message->setText(QString::fromUtf8(error.what()));
            }
        });
        return confirm;
    }
};

enum class PathUse
{
    OpenFile,
    SaveFile,
    Directory
};
class InlinePathPicker : public QWidget
{
    PathUse use;
    QString extension;
    QLineEdit *input = new QLineEdit;

  public:
    InlinePathPicker(PathUse use, const QString &folder, const QString &extension = {}, const QString &filename = {},
                     QStringList nameFilters = {})
        : use(use), extension(extension)
    {
        auto *layout = new QVBoxLayout(this);
        layout->setContentsMargins(0, 0, 0, 0);
        auto *model = new QFileSystemModel(this);
        QDir::Filters filters = QDir::AllDirs | QDir::NoDotAndDotDot | QDir::Drives;
        if (use != PathUse::Directory)
            filters |= QDir::Files;
        model->setFilter(filters);
        if (!extension.isEmpty())
            nameFilters.append("*" + extension);
        if (!nameFilters.isEmpty())
        {
            model->setNameFilters(nameFilters);
            model->setNameFilterDisables(false);
        }
        model->setRootPath("");
        auto *tree = new QTreeView;
        tree->setModel(model);
        tree->setHeaderHidden(true);
        tree->setEditTriggers(QAbstractItemView::NoEditTriggers);
        for (int column = 1; column < model->columnCount(); ++column)
            tree->hideColumn(column);
        layout->addWidget(tree, 1);
        layout->addWidget(new QLabel(use == PathUse::Directory  ? "目录路径"
                                     : use == PathUse::OpenFile ? "文件路径"
                                                                : "保存路径"));
        input->setObjectName("filePath");
        layout->addWidget(input);
        QDir dir(folder);
        if (use == PathUse::Directory)
            setPath(dir.absolutePath());
        else if (!filename.isEmpty())
            setPath(dir.absoluteFilePath(filename));
        else
            input->setPlaceholderText("在上方选择文件，或粘贴完整路径");
        auto initial = model->index(dir.absolutePath());
        tree->setCurrentIndex(initial);
        tree->scrollTo(initial);
        connect(tree->selectionModel(), &QItemSelectionModel::currentChanged, this,
                [this, model, filename](const QModelIndex &index) {
                    if (!index.isValid())
                        return;
                    auto chosen = model->fileInfo(index);
                    if (!chosen.isDir() || this->use == PathUse::Directory)
                        setPath(chosen.absoluteFilePath());
                    else if (this->use == PathUse::OpenFile)
                        input->clear();
                    else if (this->use == PathUse::SaveFile)
                    {
                        auto name = QFileInfo(input->text()).fileName();
                        setPath(QDir(chosen.absoluteFilePath()).filePath(name.isEmpty() ? filename : name));
                    }
                });
    }
    QLineEdit *field() const
    {
        return input;
    }
    void setPath(const QString &value)
    {
        input->setText(QDir::toNativeSeparators(value));
    }
    QString path() const
    {
        QString value = QDir::fromNativeSeparators(input->text().trimmed());
        if (use == PathUse::SaveFile && !value.isEmpty() && !extension.isEmpty() &&
            QFileInfo(value).suffix().isEmpty() && !value.endsWith('/'))
            value += extension;
        return value.isEmpty() ? value : QDir::cleanPath(value);
    }
    QString checkedPath() const
    {
        auto value = path();
        if (value.isEmpty() || !QDir::isAbsolutePath(value))
            throw std::runtime_error("请选择位置或填写完整路径。");
        if (use != PathUse::Directory && !extension.isEmpty() && !value.endsWith(extension, Qt::CaseInsensitive))
            throw std::runtime_error(("请使用 " + extension + " 文件。").toStdString());
        if (use == PathUse::OpenFile && !QFileInfo(value).isFile())
            throw std::runtime_error("找不到文件，请重新选择。");
        if (use == PathUse::SaveFile && !QFileInfo(value).dir().exists())
            throw std::runtime_error("保存目录不存在，请选择已有目录。");
        if (use == PathUse::SaveFile && QFileInfo(value).isDir())
            throw std::runtime_error("请填写文件名。");
        return value;
    }
};
