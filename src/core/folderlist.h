// A list of folders as one item's value (GRND-0066 spec 4.2): compact JSON,
// which reaches the Worker in one --set argument, whatever a path holds.
#pragma once

#include <QList>
#include <QString>

#include <optional>

namespace gw {

// The most folders one list may hold (spec 4.3).
inline constexpr int kMaxListedFolders = 20;

struct ListedFolder
{
    QString path;
    bool write = false; // others may add, change and delete files
};

// [{"path":"/mnt/Games/PC Games","write":false}]
QString folderListValue(const QList<ListedFolder> &folders);
// Nothing unless value is exactly that shape.
std::optional<QList<ListedFolder>> parseFolderList(const QString &value);

} // namespace gw
