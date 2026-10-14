#ifndef HISTORYDIALOG_H
#define HISTORYDIALOG_H

#include "historyqueryworker.h"

#include <QDialog>
#include <QThread>

class QComboBox;
class QDateTimeEdit;
class QLabel;
class QPushButton;
class QTableWidget;

class HistoryDialog final : public QDialog
{
    Q_OBJECT
public:
    explicit HistoryDialog(const QString &databasePath, QWidget *parent = nullptr);
    ~HistoryDialog() override;

signals:
    void queryRequested(const HistoryQueryRequest &request);

private:
    struct PageState {
        QList<HistoryCursor> cursors {HistoryCursor{}};
        HistoryCursor nextCursor;
        int index = 0;
        quint64 requestId = 0;
        bool hasMore = false;
        bool pending = false;
        QTableWidget *table = nullptr;
        QPushButton *previous = nullptr;
        QPushButton *next = nullptr;
        QLabel *status = nullptr;
    };
    void beginQuery();
    void requestPage(int kind);
    void turnPage(int kind, int direction);
    void showResult(const HistoryQueryResult &result);

    QDateTimeEdit *m_start;
    QDateTimeEdit *m_end;
    QComboBox *m_device;
    PageState m_pages[2];
    QDateTime m_queryStart;
    QDateTime m_queryEnd;
    int m_queryDevice = 0;
    quint64 m_requestSequence = 0;
    QThread m_queryThread;
    std::shared_ptr<HistoryQueryControl> m_control;
};

#endif
