// The run page: starts the Worker as a child process and shows its
// progress from the markers it prints (docs/reference/worker-interface.md).
#pragma once

#include <QHash>
#include <QPointer>
#include <QProcess>
#include <QWizardPage>

class QLabel;
class QPushButton;
class QPlainTextEdit;
class QVBoxLayout;

namespace gw {

class AskpassDialog;
class PasswordChannel;

class RunPage : public QWizardPage
{
    Q_OBJECT
public:
    explicit RunPage(QWidget *parent = nullptr);
    ~RunPage() override;

    // Starts the Worker; titles name the rows, in run order.
    void start(const QString &program, const QStringList &arguments,
               const QList<QPair<QString, QString>> &titles);
    bool isRunning() const { return m_process.state() != QProcess::NotRunning; }
    bool isComplete() const override;
    // The Worker has finished, and exited 0.
    bool endedOk() const;
    // Going back past the page empties it for the next run (GRND-0105).
    void cleanupPage() override;

    QString statusOf(const QString &id) const;
    QString summary() const;
    QStringList hints() const { return m_hints; }
    // Offered when a run ends and OneUp is installed (GRND-0024).
    QPushButton *oneUpButton() const { return m_oneUp; }
    // Off where nothing was set up to keep up to date, as after preparing a drive.
    void setOffersOneUp(bool offers) { m_offersOneUp = offers; }

signals:
    void runFinished();

private:
    void readOutput();
    void handleLine(const QString &line);
    void finish();
    void askPassword(const QString &sudoPrompt);

    QProcess m_process;
    PasswordChannel *m_passwords;
    QPointer<AskpassDialog> m_passwordForm;
    QString m_pending;
    bool m_started = false;
    QHash<QString, QLabel *> m_status;
    QStringList m_hints;
    QVBoxLayout *m_rows;
    QLabel *m_hintLabel;
    QLabel *m_summary;
    QPlainTextEdit *m_details;
    QPushButton *m_oneUp;
    bool m_offersOneUp = true;
};

} // namespace gw
