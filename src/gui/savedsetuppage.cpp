#include "savedsetuppage.h"

#include "itemrow.h"
#include "switch.h"
#include "wizard.h"

#include "core/translations.h"

#include <QDateTime>
#include <QFileInfo>
#include <QLabel>
#include <QLocale>
#include <QScrollArea>
#include <QVBoxLayout>

namespace gw {
namespace {

QLabel *wrapped(QWidget *parent, const QString &text)
{
    auto *label = new QLabel(text, parent);
    label->setWordWrap(true);
    label->setTextFormat(Qt::PlainText);
    return label;
}

QString joined(const QStringList &names)
{
    QStringList shown;
    for (const QString &name : names)
        shown << isolated(name);
    return shown.join(QStringLiteral(", "));
}

} // namespace

SavedSetupPage::SavedSetupPage(Wizard *wizard) : QWizardPage(wizard), m_wizard(wizard), m_outer(new QVBoxLayout(this))
{
    setTitle(tr("Your saved setup"));
    setSubTitle(tr("What you kept starts switched on. Switch off anything you do not want back."));
    m_outer->setContentsMargins(0, 0, 0, 0);
}

void SavedSetupPage::initializePage()
{
    SavedSetup *saved = m_wizard->savedSetup();
    if (saved && !saved->rowsMade)
        makeRows();
}

void SavedSetupPage::makeRows()
{
    SavedSetup &saved = *m_wizard->savedSetup();
    saved.rowsMade = true;
    // Another file read since: its rows go, and the page is made afresh.
    delete findChild<QScrollArea *>();
    m_inner = new QWidget;
    auto *rows = new QVBoxLayout(m_inner);
    auto *scroll = new QScrollArea(this);
    scroll->setObjectName(QStringLiteral("carryPage")); // grown to fit, as the carry pages are
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidgetResizable(true);
    scroll->setWidget(m_inner);
    m_outer->addWidget(scroll);

    const Manifest &m = saved.manifest;
    const QDateTime created = QDateTime::fromString(m.created, Qt::ISODate);
    rows->addWidget(wrapped(m_inner, created.isValid()
                                         ? tr("File: %1, saved %2")
                                               .arg(isolated(QFileInfo(saved.file).fileName()),
                                                    isolated(QLocale().toString(created.toLocalTime(), QLocale::ShortFormat)))
                                         : tr("File: %1").arg(isolated(QFileInfo(saved.file).fileName()))));
    if (m.systemId != m_wizard->systemId())
        rows->addWidget(wrapped(m_inner, tr("Saved on a different system: %1").arg(isolated(m.systemId))));
    if (!saved.carried.unknownItems.isEmpty())
        rows->addWidget(wrapped(m_inner, tr("Not offered by this version: %1").arg(joined(saved.carried.unknownItems))));
    QStringList notNeeded;
    for (const QString &id : std::as_const(saved.carried.keptItems)) {
        if (m_wizard->results().value(id).state == CheckState::NotNeeded)
            notNeeded << m_wizard->catalogue().find(id)->title();
    }
    if (!notNeeded.isEmpty())
        rows->addWidget(wrapped(m_inner, tr("Kept, but not needed here: %1").arg(joined(notNeeded))));
    for (const QString &line : std::as_const(saved.skipped))
        rows->addWidget(wrapped(m_inner, line));
    // A Flatpak source cannot be added without its key (measured 2026-10-20),
    // so it is named for the user to add (user, 2026-10-20).
    if (!m.flatpakRemotes.isEmpty()) {
        QStringList remotes;
        for (const FlatpakRemote &r : m.flatpakRemotes)
            remotes << QStringLiteral("%1 — %2").arg(isolated(r.name), isolated(r.url));
        rows->addWidget(wrapped(m_inner, tr("Groundwork cannot add these Flatpak sources without their keys. Add "
                                            "them yourself if you need them: %1")
                                             .arg(remotes.join(QStringLiteral("; ")))));
    }

    for (const std::unique_ptr<Item> &item : saved.carried.owned) {
        auto *row = new ItemRow(*item, m_wizard->results().value(item->id()), m_inner);
        rows->addWidget(row);
        m_wizard->addCarriedRow(row);
        const QString id = item->id();
        auto entry = [&](const QString &text, std::function<void(Manifest &)> keep) {
            auto *on = new Switch(text, m_inner);
            on->setChecked(true);
            on->setObjectName(id + QLatin1Char('/') + text);
            rows->addWidget(on);
            saved.entries.append({id, on, std::move(keep)});
            return on;
        };
        if (id == kCarriedPackagesId) {
            for (const QString &name : m.packages)
                entry(isolated(name), [name](Manifest &k) { k.packages << name; });
        } else if (id == kCarriedFlatpaksId) {
            for (const FlatpakApp &app : m.flatpaks)
                entry(isolated(app.app), [app](Manifest &k) { k.flatpaks << app; });
        } else if (id == kCarriedSettingsId) {
            for (const SettingsRow &setting : m.settings) {
                entry(isolated(setting.name), [setting](Manifest &k) { k.settings << setting; });
                if (!setting.secret)
                    continue;
                QLabel *mark = wrapped(m_inner, tr("Holds saved passwords or keys."));
                mark->setObjectName(QStringLiteral("secretMark:") + setting.name);
                rows->addWidget(mark);
            }
        } else {
            for (const ZypperSource &source : m.zypperSources) {
                if (kCarriedSourcePrefix + source.alias == id)
                    saved.entries.append({id, nullptr, [source](Manifest &k) { k.zypperSources << source; }});
            }
        }
    }
    rows->addStretch();
    m_wizard->startCarried();
}

bool SavedSetupPage::validatePage()
{
    m_wizard->switchOnKept();
    return true;
}

} // namespace gw
