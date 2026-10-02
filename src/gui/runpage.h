// The run page: starts the Worker as a child process and shows its
// progress from the markers it prints (docs/reference/worker-interface.md).
#pragma once

#include <QHash>
#include <QProcess>
#include <QWizardPage>

class QLabel;
class QPlainTextEdit;
class QVBoxLayout;

namespace gw {

class RunPage : public QWizardPage
{
    Q_OBJECT
public:
    explicit RunPage(QWidget *parent = nullptr);

    // Starts the Worker; titles name the rows, in run order.
    void start(const QString &program, const QStringList &arguments,
               const QList<QPair<QString, QString>> &titles);
    bool isRunning() const { return m_process.state() != QProcess::NotRunning; }
    bool isComplete() const override;

    QString statusOf(const QString &id) const;
    QString summary() const;
    QStringList hints() const { return m_hints; }

signals:
    void runFinished();

private:
    void readOutput();
    void handleLine(const QString &line);

    QProcess m_process;
    QString m_pending;
    bool m_started = false;
    QHash<QString, QLabel *> m_status;
    QStringList m_hints;
    QVBoxLayout *m_rows;
    QLabel *m_hintLabel;
    QLabel *m_summary;
    QPlainTextEdit *m_details;
};

} // namespace gw
