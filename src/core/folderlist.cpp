#include "folderlist.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace gw {

QString folderListValue(const QList<ListedFolder> &folders)
{
    QJsonArray list;
    for (const ListedFolder &folder : folders)
        list.append(QJsonObject{{QStringLiteral("path"), folder.path}, {QStringLiteral("write"), folder.write}});
    return QString::fromUtf8(QJsonDocument(list).toJson(QJsonDocument::Compact));
}

std::optional<QList<ListedFolder>> parseFolderList(const QString &value)
{
    const QJsonDocument document = QJsonDocument::fromJson(value.toUtf8());
    if (!document.isArray())
        return std::nullopt;
    QList<ListedFolder> folders;
    for (const QJsonValue &entry : document.array()) {
        const QJsonObject object = entry.toObject();
        const QJsonValue path = object.value(QStringLiteral("path"));
        const QJsonValue write = object.value(QStringLiteral("write"));
        if (object.size() != 2 || !path.isString() || !write.isBool())
            return std::nullopt;
        folders.append({path.toString(), write.toBool()});
    }
    return folders;
}

} // namespace gw
