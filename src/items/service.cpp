#include "service.h"

namespace gw {

ServiceState readService(const CheckContext &context, const QString &unit)
{
    const CommandResult enabled = context.run({QStringLiteral("systemctl"), QStringLiteral("is-enabled"), unit});
    const CommandResult active = context.run({QStringLiteral("systemctl"), QStringLiteral("is-active"), unit});
    ServiceState state;
    if (!enabled.started || !active.started || enabled.timedOut || active.timedOut)
        return state;
    state.known = true;
    const QByteArray answer = enabled.out.trimmed();
    state.found = answer != "not-found";
    state.enabled = answer == "enabled";
    state.active = active.out.trimmed() == "active";
    return state;
}

} // namespace gw
