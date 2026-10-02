// The marker line format, OneUp's: @@NAME@@|field|field, one per line, on
// standard output (docs/design.md; docs/reference/worker-interface.md lists
// Groundwork's markers).
#pragma once

#include <QString>
#include <QStringList>

#include <optional>

namespace gw {

struct Marker
{
    QString name;
    QStringList fields;
};

// A field may not hold '|' or a line break: '|' becomes '/', and line breaks
// become spaces, so a field never splits the line.
QString formatMarker(const QString &name, const QStringList &fields = {});

// Nothing for a line that is not a marker; such lines are log text.
std::optional<Marker> parseMarker(const QString &line);

} // namespace gw
