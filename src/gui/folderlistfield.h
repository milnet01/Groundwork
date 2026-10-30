// The value of an item that takes a list of folders (GRND-0066 spec 4.5): one
// line per folder with a switch and a Remove button, and an add button. It
// reads and writes the list as core/folderlist.h's value.
#pragma once

#include "core/folderlist.h"
#include "core/item.h"

#include <QWidget>

class QLabel;
class QPushButton;
class QVBoxLayout;

namespace gw {

class Switch;

class FolderListField : public QWidget
{
    Q_OBJECT
public:
    FolderListField(const Item &item, const CheckResult &result, QWidget *parent = nullptr);

    // The list as a value. An empty list is no value, so a new user cannot
    // apply nothing, unless the row opened with folders, so that a user can
    // remove the last one.
    QString value() const;
    // Adds a folder, or says why not beneath the list.
    void addFolder(const QString &path, bool write = false);
    // Why the last folder was refused; empty if it was not.
    QString refusal() const;

private:
    struct Line
    {
        QString path;
        Switch *write;
        QWidget *widget;
    };
    void addLine(const ListedFolder &folder);
    void chooseFolder();
    void updateAddButton();

    const Item &m_item;
    QMap<QString, QString> m_held;
    bool m_openedWithFolders = false;
    QList<Line> m_lines;
    QVBoxLayout *m_list;
    QPushButton *m_add;
    QLabel *m_refusal;
};

} // namespace gw
