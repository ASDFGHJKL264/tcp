#include "historydialog.h"

#include <QComboBox>
#include <QDateTimeEdit>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QTabWidget>
#include <QTableWidget>
#include <QVBoxLayout>

HistoryDialog::HistoryDialog(const QString &databasePath, QWidget *parent) : QDialog(parent)
{
    setWindowTitle(QStringLiteral("历史数据与报警记录"));
    resize(1000, 620);
    m_connectionName = QStringLiteral("history-%1").arg(reinterpret_cast<quintptr>(this));
    QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", m_connectionName);
    db.setDatabaseName(databasePath);
    if (!db.open())
        QMessageBox::critical(this, QStringLiteral("数据库错误"), db.lastError().text());

    auto *layout = new QVBoxLayout(this);
    auto *filters = new QHBoxLayout;
    m_start = new QDateTimeEdit(QDateTime::currentDateTime().addDays(-1));
    m_end = new QDateTimeEdit(QDateTime::currentDateTime());
    for (QDateTimeEdit *edit : {m_start, m_end}) {
        edit->setCalendarPopup(true);
        edit->setDisplayFormat("yyyy-MM-dd HH:mm:ss");
    }
    m_device = new QComboBox;
    m_device->addItem(QStringLiteral("全部设备"), 0);
    for (int i = 1; i <= 4; ++i)
        m_device->addItem(QStringLiteral("设备%1").arg(i), i);
    auto *query = new QPushButton(QStringLiteral("查询"));
    filters->addWidget(new QLabel(QStringLiteral("开始：")));
    filters->addWidget(m_start);
    filters->addWidget(new QLabel(QStringLiteral("结束：")));
    filters->addWidget(m_end);
    filters->addWidget(m_device);
    filters->addWidget(query);
    layout->addLayout(filters);

    auto *tabs = new QTabWidget;
    m_telemetry = new QTableWidget;
    m_telemetry->setColumnCount(7);
    m_telemetry->setHorizontalHeaderLabels({QStringLiteral("时间"), QStringLiteral("设备"),
        QStringLiteral("温度"), QStringLiteral("压力"), QStringLiteral("转速"),
        QStringLiteral("状态"), QStringLiteral("故障码")});
    m_alarms = new QTableWidget;
    m_alarms->setColumnCount(4);
    m_alarms->setHorizontalHeaderLabels({QStringLiteral("时间"), QStringLiteral("设备"),
        QStringLiteral("类型"), QStringLiteral("内容")});
    for (QTableWidget *table : {m_telemetry, m_alarms}) {
        table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
        table->setEditTriggers(QAbstractItemView::NoEditTriggers);
        table->setSelectionBehavior(QAbstractItemView::SelectRows);
    }
    tabs->addTab(m_telemetry, QStringLiteral("遥测历史"));
    tabs->addTab(m_alarms, QStringLiteral("报警记录"));
    layout->addWidget(tabs);
    connect(query, &QPushButton::clicked, this, [this] { queryTelemetry(); queryAlarms(); });
    queryTelemetry();
    queryAlarms();
}

HistoryDialog::~HistoryDialog()
{
    {
        QSqlDatabase db = QSqlDatabase::database(m_connectionName, false);
        if (db.isValid()) db.close();
    }
    QSqlDatabase::removeDatabase(m_connectionName);
}

static void fillTable(QTableWidget *table, QSqlQuery &query, int columns)
{
    table->setRowCount(0);
    while (query.next()) {
        const int row = table->rowCount();
        table->insertRow(row);
        for (int column = 0; column < columns; ++column)
            table->setItem(row, column, new QTableWidgetItem(query.value(column).toString()));
    }
}

void HistoryDialog::queryTelemetry()
{
    QString sql = "SELECT sample_time,device_id,printf('%.1f',temperature),"
                  "printf('%.2f',pressure),rpm,CASE state WHEN 1 THEN '运行' WHEN 2 THEN '故障' "
                  "ELSE '停止' END,fault_code FROM telemetry WHERE sample_time>=? AND sample_time<=?";
    const int id = m_device->currentData().toInt();
    if (id > 0) sql += " AND device_id=?";
    sql += " ORDER BY sample_time DESC LIMIT 5000";
    QSqlQuery query(QSqlDatabase::database(m_connectionName));
    query.prepare(sql);
    query.addBindValue(m_start->dateTime().toString(Qt::ISODateWithMs));
    query.addBindValue(m_end->dateTime().toString(Qt::ISODateWithMs));
    if (id > 0) query.addBindValue(id);
    if (query.exec()) fillTable(m_telemetry, query, 7);
}

void HistoryDialog::queryAlarms()
{
    QString sql = "SELECT alarm_time,device_id,alarm_type,message FROM alarms "
                  "WHERE alarm_time>=? AND alarm_time<=?";
    const int id = m_device->currentData().toInt();
    if (id > 0) sql += " AND device_id=?";
    sql += " ORDER BY alarm_time DESC LIMIT 5000";
    QSqlQuery query(QSqlDatabase::database(m_connectionName));
    query.prepare(sql);
    query.addBindValue(m_start->dateTime().toString(Qt::ISODateWithMs));
    query.addBindValue(m_end->dateTime().toString(Qt::ISODateWithMs));
    if (id > 0) query.addBindValue(id);
    if (query.exec()) fillTable(m_alarms, query, 4);
}
