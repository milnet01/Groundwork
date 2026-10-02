#include "markers.h"

#include <QRegularExpression>

namespace gw {

QString formatMarker(const QString &name, const QStringList &fields)
{
    QString line = QStringLiteral("@@") + name + QStringLiteral("@@");
    for (QString field : fields) {
        field.replace(QLatin1Char('|'), QLatin1Char('/'));
        field.replace(QLatin1Char('\r'), QLatin1Char(' '));
        field.replace(QLatin1Char('\n'), QLatin1Char(' '));
        line += QLatin1Char('|') + field;
    }
    return line;
}

std::optional<Marker> parseMarker(const QString &line)
{
    static const QRegularExpression shape(QStringLiteral("^@@([A-Z_]+)@@((?:\\|[^|]*)*)$"));
    const QRegularExpressionMatch m = shape.match(line);
    if (!m.hasMatch())
        return std::nullopt;
    Marker marker{m.captured(1), {}};
    const QString rest = m.captured(2);
    if (!rest.isEmpty())
        marker.fields = rest.mid(1).split(QLatin1Char('|'));
    return marker;
}

} // namespace gw
