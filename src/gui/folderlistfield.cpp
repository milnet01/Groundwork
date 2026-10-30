#include "folderlistfield.h"

#include "core/translations.h"
#include "switch.h"

#include <QDir>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

#include <algorithm>

namespace gw {

FolderListField::FolderListField(const Item &item, const CheckResult &result, QWidget *parent)
    : QWidget(parent), m_item(item), m_held(result.heldFolders), m_list(new QVBoxLayout),
      m_add(new QPushButton(tr("Add a folder…"), this)), m_refusal(new QLabel(this))
{
    m_refusal->setTextFormat(Qt::PlainText);
    m_refusal->setWordWrap(true);
    m_refusal->hide();
    m_list->setContentsMargins(0, 0, 0, 0);
    connect(m_add, &QPushButton::clicked, this, &FolderListField::chooseFolder);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addLayout(m_list);
    layout->addWidget(m_add, 0, Qt::AlignLeft);
    layout->addWidget(m_refusal);

    const QList<ListedFolder> opened = parseFolderList(result.currentValue).value_or(QList<ListedFolder>{});
    m_openedWithFolders = !opened.isEmpty();
    for (const ListedFolder &folder : opened)
        addLine(folder);
    updateAddButton();
}

QString FolderListField::value() const
{
    if (m_lines.isEmpty() && !m_openedWithFolders)
        return {};
    QList<ListedFolder> folders;
    for (const Line &line : m_lines)
        folders.append({line.path, line.write->isChecked()});
    return folderListValue(folders);
}

void FolderListField::addFolder(const QString &path, bool write)
{
    QString why = m_item.whyRefused(path);
    if (why.isEmpty() && m_held.contains(path))
        why = tr("This folder is already shared as %1.").arg(isolated(m_held.value(path)));
    if (why.isEmpty()
        && std::any_of(m_lines.cbegin(), m_lines.cend(), [&path](const Line &line) { return line.path == path; }))
        why = tr("This folder is already on the list.");
    m_refusal->setText(why);
    m_refusal->setVisible(!why.isEmpty());
    if (why.isEmpty() && m_lines.size() < kMaxListedFolders) {
        addLine({path, write});
        updateAddButton();
    }
}

QString FolderListField::refusal() const { return m_refusal->text(); }

void FolderListField::addLine(const ListedFolder &folder)
{
    auto *widget = new QWidget(this);
    auto *name = new QLabel(isolated(folder.path), widget);
    name->setTextFormat(Qt::PlainText);
    name->setWordWrap(true);
    auto *write = new Switch(tr("Let people add, change and delete files"), widget);
    write->setChecked(folder.write);
    write->setAccessibleDescription(folder.path);
    auto *remove = new QPushButton(tr("Remove"), widget);
    remove->setAccessibleName(tr("Remove %1").arg(folder.path));
    connect(remove, &QPushButton::clicked, this, [this, widget] {
        m_lines.removeIf([widget](const Line &line) { return line.widget == widget; });
        widget->deleteLater();
        updateAddButton();
    });

    auto *controls = new QHBoxLayout;
    controls->addWidget(write, 1);
    controls->addWidget(remove);
    auto *layout = new QVBoxLayout(widget);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(name);
    layout->addLayout(controls);
    m_list->addWidget(widget);
    m_lines.append({folder.path, write, widget});
}

void FolderListField::chooseFolder()
{
    const QString dir = QFileDialog::getExistingDirectory(this, tr("Choose a folder to share"), QDir::homePath());
    if (!dir.isEmpty())
        addFolder(dir);
}

void FolderListField::updateAddButton() { m_add->setEnabled(m_lines.size() < kMaxListedFolders); }

} // namespace gw
