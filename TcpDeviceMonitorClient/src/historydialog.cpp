#include "historydialog.h"

#include <QComboBox>
#include <QDateTimeEdit>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QTabWidget>
#include <QTableWidget>
#include <QVBoxLayout>

HistoryDialog::HistoryDialog(const QString &databasePath, QWidget *parent) : QDialog(parent)
{
    setWindowTitle(QStringLiteral("历史数据与报警记录"));
    resize(1000, 620);
    auto *layout = new QVBoxLayout(this);
    auto *filters = new QHBoxLayout;
    m_start = new QDateTimeEdit(QDateTime::currentDateTime().addDays(-1));
    m_end = new QDateTimeEdit(QDateTime::currentDateTime());
    m_start->setObjectName("historyStart");
    m_end->setObjectName("historyEnd");
    for (QDateTimeEdit *edit : {m_start, m_end}) {
        edit->setCalendarPopup(true);
        edit->setDisplayFormat("yyyy-MM-dd HH:mm:ss");
    }
    m_device = new QComboBox;
    m_device->setObjectName("historyDevice");
    m_device->addItem(QStringLiteral("全部设备"), 0);
    for (int i = 1; i <= 4; ++i)
        m_device->addItem(QStringLiteral("设备%1").arg(i), i);
    auto *query = new QPushButton(QStringLiteral("查询"));
    query->setObjectName("historyQuery");
    filters->addWidget(new QLabel(QStringLiteral("开始：")));
    filters->addWidget(m_start);
    filters->addWidget(new QLabel(QStringLiteral("结束：")));
    filters->addWidget(m_end);
    filters->addWidget(m_device);
    filters->addWidget(query);
    layout->addLayout(filters);

    auto *tabs = new QTabWidget;
    for (int kind = 0; kind < 2; ++kind) {
        auto &page = m_pages[kind];
        const QString prefix = kind == 0 ? QStringLiteral("telemetry") : QStringLiteral("alarms");
        auto *tab = new QWidget;
        auto *tabLayout = new QVBoxLayout(tab);
        tabLayout->setContentsMargins(0, 0, 0, 0);
        page.table = new QTableWidget;
        page.table->setObjectName(prefix + "Table");
        if (kind == 0) {
            page.table->setColumnCount(7);
            page.table->setHorizontalHeaderLabels({QStringLiteral("时间"), QStringLiteral("设备"),
                QStringLiteral("温度"), QStringLiteral("压力"), QStringLiteral("转速"),
                QStringLiteral("状态"), QStringLiteral("故障码")});
        } else {
            page.table->setColumnCount(4);
            page.table->setHorizontalHeaderLabels({QStringLiteral("时间"), QStringLiteral("设备"),
                QStringLiteral("类型"), QStringLiteral("内容")});
        }
        page.table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
        page.table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
        page.table->setEditTriggers(QAbstractItemView::NoEditTriggers);
        page.table->setSelectionBehavior(QAbstractItemView::SelectRows);
        tabLayout->addWidget(page.table);
        auto *paging = new QHBoxLayout;
        page.previous = new QPushButton(QStringLiteral("上一页"));
        page.next = new QPushButton(QStringLiteral("下一页"));
        page.status = new QLabel;
        page.previous->setObjectName(prefix + "Previous");
        page.next->setObjectName(prefix + "Next");
        page.status->setObjectName(prefix + "Status");
        paging->addWidget(page.previous);
        paging->addWidget(page.next);
        paging->addWidget(page.status, 1);
        tabLayout->addLayout(paging);
        tabs->addTab(tab, kind == 0 ? QStringLiteral("遥测历史") : QStringLiteral("报警记录"));
        connect(page.previous, &QPushButton::clicked, this, [this, kind] { turnPage(kind, -1); });
        connect(page.next, &QPushButton::clicked, this, [this, kind] { turnPage(kind, 1); });
    }
    layout->addWidget(tabs);
    connect(query, &QPushButton::clicked, this, &HistoryDialog::beginQuery);

    m_control = std::make_shared<HistoryQueryControl>();
    auto *worker = new HistoryQueryWorker(databasePath, m_control);
    worker->moveToThread(&m_queryThread);
    connect(this, &HistoryDialog::queryRequested, worker, &HistoryQueryWorker::query,
            Qt::QueuedConnection);
    connect(worker, &HistoryQueryWorker::completed, this, &HistoryDialog::showResult,
            Qt::QueuedConnection);
    connect(&m_queryThread, &QThread::finished, worker, &QObject::deleteLater);
    m_queryThread.start();
    beginQuery();
}

HistoryDialog::~HistoryDialog()
{
    m_control->stopping.store(true);
    m_queryThread.requestInterruption();
    m_queryThread.quit();
    m_queryThread.wait();
}

void HistoryDialog::beginQuery()
{
    if (m_start->dateTime() > m_end->dateTime()) {
        QMessageBox::warning(this, QStringLiteral("查询条件错误"),
                             QStringLiteral("开始时间不能晚于结束时间"));
        return;
    }
    m_queryStart = m_start->dateTime();
    m_queryEnd = m_end->dateTime();
    m_queryDevice = m_device->currentData().toInt();
    for (int kind = 0; kind < 2; ++kind) {
        auto &page = m_pages[kind];
        page.cursors = {HistoryCursor{}};
        page.index = 0;
        page.nextCursor = {};
        page.table->setRowCount(0);
        requestPage(kind);
    }
}

void HistoryDialog::requestPage(int kind)
{
    auto &page = m_pages[kind];
    page.requestId = ++m_requestSequence;
    page.pending = true;
    page.hasMore = false;
    page.previous->setEnabled(false);
    page.next->setEnabled(false);
    page.status->setText(QStringLiteral("正在查询第 %1 页…").arg(page.index + 1));
    m_control->latestRequest[kind].store(page.requestId);
    emit queryRequested({page.requestId, kind, m_queryStart, m_queryEnd,
                         m_queryDevice, page.cursors.at(page.index)});
}

void HistoryDialog::turnPage(int kind, int direction)
{
    auto &page = m_pages[kind];
    if (page.pending || (direction < 0 && page.index == 0)
        || (direction > 0 && !page.hasMore)) return;
    if (direction > 0) {
        // Reuse a cursor for previous pages; replace only the following page.
        page.cursors = page.cursors.mid(0, page.index + 1);
        page.cursors.append(page.nextCursor);
    }
    page.index += direction;
    requestPage(kind);
}

void HistoryDialog::showResult(const HistoryQueryResult &result)
{
    if (result.kind < 0 || result.kind > 1) return;
    auto &page = m_pages[result.kind];
    if (result.requestId != page.requestId) return;
    page.pending = false;
    page.hasMore = result.hasMore;
    page.nextCursor = result.nextCursor;
    page.table->setUpdatesEnabled(false);
    page.table->setRowCount(result.rows.size());
    for (int row = 0; row < result.rows.size(); ++row) {
        const auto &values = result.rows.at(row);
        for (int column = 0; column < values.size(); ++column)
            page.table->setItem(row, column, new QTableWidgetItem(values.at(column)));
    }
    page.table->setUpdatesEnabled(true);
    page.previous->setEnabled(page.index > 0);
    page.next->setEnabled(page.hasMore);
    page.status->setText(result.error.isEmpty()
        ? QStringLiteral("第 %1 页，%2 条（每页最多 %3 条）%4").arg(page.index + 1)
              .arg(result.rows.size()).arg(HistoryQueryWorker::PageSize)
              .arg(page.hasMore ? QString() : QStringLiteral("，已到末页"))
        : QStringLiteral("查询失败：%1").arg(result.error));
}
