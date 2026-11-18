#include "skill_file.h"
#include <yaml-cpp/yaml.h>

namespace
{
YAML::Node parseHeader(const QString &header)
{
    try
    {
        auto node = YAML::Load(header.toStdString());
        if (!node.IsMap())
            throw std::runtime_error("SKILL.md 的 YAML 文件头应为字段列表。");
        return node;
    }
    catch (const YAML::Exception &error)
    {
        throw std::runtime_error(
            QString("SKILL.md 文件头格式有误，请检查第 %1 行附近的 YAML。").arg(error.mark.line + 2).toStdString());
    }
}
QString scalar(const YAML::Node &node)
{
    return node.IsDefined() && node.IsScalar() ? QString::fromStdString(node.Scalar()) : QString();
}
QString displayTitle(const YAML::Node &header)
{
    auto metadata = header["metadata"];
    if (metadata.IsDefined() && metadata.IsMap())
    {
        auto title = scalar(metadata["moxu-title"]).trimmed();
        if (!title.isEmpty())
            return title;
    }
    return scalar(header["name"]).trimmed();
}
QString portableName(const QString &title)
{
    auto slug = title.toLower().replace(QRegularExpression("[^a-z0-9]+"), "-").left(64);
    slug.remove(QRegularExpression("^-|-$"));
    if (!slug.isEmpty())
        return slug;
    return "writing-" +
           QString::fromLatin1(QCryptographicHash::hash(title.toUtf8(), QCryptographicHash::Sha256).toHex().first(12));
}
QString exportHeader(const WritingNote &note)
{
    YAML::Node header = note.skillHeader.isEmpty() ? YAML::Node(YAML::NodeType::Map) : parseHeader(note.skillHeader);
    const auto &fields = std::as_const(header);
    QString name = scalar(fields["name"]), description = scalar(fields["description"]);
    if (!name.isEmpty() && !description.trimmed().isEmpty() && displayTitle(header) == note.title)
        return note.skillHeader; // Keep comments, block scalars and extension fields verbatim.
    if (name.isEmpty())
        header["name"] = portableName(note.title).toStdString();
    if (description.trimmed().isEmpty())
        header["description"] = QString("在小说创作中应用「%1」写作要求。").arg(note.title.left(500)).toStdString();
    if (displayTitle(header) != note.title)
    {
        if (fields["metadata"].IsDefined() && !fields["metadata"].IsMap())
            throw std::runtime_error("文件头的 metadata 应为字段列表，无法写入显示标题。");
        header["metadata"]["moxu-title"] = note.title.toStdString();
    }
    return QString::fromStdString(YAML::Dump(header)) + '\n';
}
} // namespace
WritingNote readSkillFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        throw std::runtime_error("无法读取所选 Skill 文件。");
    auto bytes = file.readAll();
    if (file.error() != QFileDevice::NoError)
        throw std::runtime_error("Skill 文件读取失败。");
    if (!bytes.isValidUtf8() || bytes.contains('\0'))
        throw std::runtime_error("请选择 UTF-8 编码的 Markdown 文本。");
    QString text = QString::fromUtf8(bytes);
    text.replace("\r\n", "\n").replace('\r', '\n');
    if (text.trimmed().isEmpty())
        throw std::runtime_error("Skill 文件为空。");
    WritingNote note;
    note.body = text;
    static const QRegularExpression start("\\A---[ \\t]*(?:\\n|\\z)");
    auto open = start.match(text);
    if (open.hasMatch())
    {
        static const QRegularExpression end("^---[ \\t]*(?:\\n|\\z)", QRegularExpression::MultilineOption);
        auto close = end.match(text, open.capturedEnd());
        if (!close.hasMatch())
            throw std::runtime_error("SKILL.md 文件头缺少结束标记 ---。");
        note.skillHeader = text.mid(open.capturedEnd(), close.capturedStart() - open.capturedEnd());
        note.title = displayTitle(parseHeader(note.skillHeader));
        note.body = text.mid(close.capturedEnd());
    }
    if (note.title.isEmpty())
    {
        static const QRegularExpression heading("^# [ \\t]*(.+)$", QRegularExpression::MultilineOption);
        note.title = heading.match(note.body).captured(1).trimmed();
        if (note.title.isEmpty())
            note.title = QFileInfo(path).completeBaseName();
        if (note.title.isEmpty())
            note.title = "写作 Skill";
    }
    return note;
}

void writeSkillFile(const QString &path, const WritingNote &note, bool overwrite)
{
    if (!path.endsWith(".md", Qt::CaseInsensitive))
        throw std::runtime_error("请将 Skill 保存为 .md 文件。");
    auto header = exportHeader(note);
    if (!header.endsWith('\n'))
        header += '\n';
    const auto bytes = ("---\n" + header + "---\n" + note.body).toUtf8();
    if (!overwrite)
    {
        QTemporaryFile pending(QFileInfo(path).dir().filePath(".moxu-skill-XXXXXX"));
        if (!pending.open() || pending.write(bytes) != bytes.size() || !pending.flush())
            throw std::runtime_error("写入 Skill 文件失败，请检查目标位置是否可写。");
        if (!pending.rename(path))
            throw std::runtime_error("无法创建目标文件，请检查文件名或确认覆盖已有文件。");
        pending.setAutoRemove(false);
        return;
    }
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit())
        throw std::runtime_error("写入 Skill 文件失败，请检查目标位置是否可写。");
}
