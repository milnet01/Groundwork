// Reads system files for checks. Every path is resolved under a root
// directory, which is "/" in use and a temporary tree in tests, so no test
// reads the real system (docs/design.md, What every part does the same way).
#pragma once

#include <QByteArray>
#include <QString>
#include <QStringList>

#include <optional>

namespace gw {

class FileReader
{
public:
    explicit FileReader(QString root = QStringLiteral("/"));

    // The file's bytes, or nothing if it is absent or unreadable.
    std::optional<QByteArray> read(const QString &absolutePath) const;
    bool exists(const QString &absolutePath) const;
    // Names of the entries directly inside a directory, sorted; empty if
    // the directory is absent.
    QStringList entries(const QString &absoluteDir) const;

    const QString &root() const { return m_root; }

private:
    QString resolve(const QString &absolutePath) const;
    QString m_root;
};

} // namespace gw
