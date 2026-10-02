#include "filereader.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>

namespace gw {

FileReader::FileReader(QString root) : m_root(std::move(root)) {}

QString FileReader::resolve(const QString &absolutePath) const
{
    // Clean the path on its own first, so ".." cannot climb above the root.
    QString path = QDir::cleanPath(QLatin1Char('/') + absolutePath);
    while (path.startsWith(QLatin1String("/..")))
        path = QDir::cleanPath(QLatin1Char('/') + path.mid(3));
    return QDir::cleanPath(m_root + path);
}

std::optional<QByteArray> FileReader::read(const QString &absolutePath) const
{
    QFile file(resolve(absolutePath));
    if (!file.open(QIODevice::ReadOnly))
        return std::nullopt;
    return file.readAll();
}

bool FileReader::exists(const QString &absolutePath) const
{
    return QFileInfo::exists(resolve(absolutePath));
}

QStringList FileReader::entries(const QString &absoluteDir) const
{
    return QDir(resolve(absoluteDir))
        .entryList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::System, QDir::Name);
}

QString FileReader::linkTargetName(const QString &absolutePath) const
{
    const QFileInfo info(resolve(absolutePath));
    return info.isSymLink() ? QFileInfo(info.symLinkTarget()).fileName() : QString();
}

} // namespace gw
