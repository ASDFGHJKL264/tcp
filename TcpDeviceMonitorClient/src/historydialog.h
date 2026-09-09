#ifndef HISTORYDIALOG_H
#define HISTORYDIALOG_H

#include <QDialog>

class QComboBox;
class QDateTimeEdit;
class QTableWidget;

class HistoryDialog final : public QDialog
{
    Q_OBJECT
public:
    explicit HistoryDialog(const QString &databasePath, QWidget *parent = nullptr);
    ~HistoryDialog() override;

private:
    void queryTelemetry();
    void queryAlarms();
    QString m_connectionName;
    QDateTimeEdit *m_start;
    QDateTimeEdit *m_end;
    QComboBox *m_device;
    QTableWidget *m_telemetry;
    QTableWidget *m_alarms;
};

#endif
