#include "ui_zlogpanel.h"
#include "zlogpanel.h"
#include "zenoapplication.h"
#include <zenomodel/include/modelrole.h>
#include <zenomodel/include/uihelper.h>
#include <zenoui/style/zenostyle.h>
#include <zenoui/comctrl/ztoolbutton.h>


LogItemDelegate::LogItemDelegate(QObject* parent)
    : _base(parent)
{

}

void LogItemDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const
{
    painter->save();

    QStyleOptionViewItem opt = option;
    initStyleOption(&opt, index);

    QtMsgType type = (QtMsgType)index.data(ROLE_LOGTYPE).toInt();
    QColor clr;
    if (type == QtFatalMsg) {
        clr = QColor("#C8544F");
    } else if (type == QtInfoMsg) {
        clr = QColor("#507CC8");
    } else if (type == QtWarningMsg) {
        clr = QColor("#C89A50");
    }
    else if (type == QtCriticalMsg) {
        clr = QColor("#339455");
    }
    else if (type == QtDebugMsg) {
        clr = QColor("#A3B1C0");
    }
    else {
        clr = QColor("#A3B1C0");
    }

    QRect rc = opt.rect;

    if (QListView* pView = qobject_cast<QListView*>(parent()))
    {
        if (pView->currentIndex() == index)
        {
            painter->fillRect(rc, QColor("#3B546D"));
        }
    }

    QPen pen = painter->pen();
    pen.setColor(clr);

    

    QFont font("Consolas", 10);
    font.setBold(true);
    painter->setFont(font);

    painter->setPen(pen);
    painter->drawText(rc.adjusted(4,0,0,0), opt.text);

    painter->setPen(QColor("#24282E"));
    painter->drawLine(rc.bottomLeft(), rc.bottomRight());

    painter->restore();
}


LogListView::LogListView(QWidget* parent)
    : _base(parent)
{
}

void LogListView::rowsInserted(const QModelIndex& parent, int start, int end)
{
    _base::rowsInserted(parent, start, end);

    connect(&m_timer, &QTimer::timeout, this, [=]() {
        scrollToBottom();
        m_timer.stop();
    });
    m_timer.start(50);
}


ZlogPanel::ZlogPanel(QWidget* parent)
    : QWidget(parent)
    , m_pFilterModel(nullptr)
{
    m_ui = new Ui::LogPanel;
    m_ui->setupUi(this);

    m_ui->listView->setItemDelegate(new LogItemDelegate(m_ui->listView));

    m_ui->btnDebug->setButtonOptions(ZToolButton::Opt_Checkable | ZToolButton::Opt_HasIcon | ZToolButton::Opt_NoBackground);
    m_ui->btnDebug->setIcon(ZenoStyle::dpiScaledSize(QSize(20, 20)),
        ":/icons/logger_debug_unchecked.svg",
        ":/icons/logger_debug_unchecked.svg",
        ":/icons/logger_debug_checked.svg",
        ":/icons/logger_debug_checked.svg");
    m_ui->btnDebug->setChecked(true);

    m_ui->btnInfo->setButtonOptions(ZToolButton::Opt_Checkable | ZToolButton::Opt_HasIcon | ZToolButton::Opt_NoBackground);
    m_ui->btnInfo->setIcon(ZenoStyle::dpiScaledSize(QSize(20, 20)),
        ":/icons/logger_info_unchecked.svg",
        ":/icons/logger_info_unchecked.svg",
        ":/icons/logger_info_checked.svg",
        ":/icons/logger_info_checked.svg");
    m_ui->btnInfo->setChecked(true);

    m_ui->btnWarn->setButtonOptions(ZToolButton::Opt_Checkable | ZToolButton::Opt_HasIcon | ZToolButton::Opt_NoBackground);
    m_ui->btnWarn->setIcon(ZenoStyle::dpiScaledSize(QSize(20, 20)),
        ":/icons/logger_warning_unchecked.svg",
        ":/icons/logger_warning_unchecked.svg",
        ":/icons/logger_warning_checked.svg",
        ":/icons/logger_warning_checked.svg");
    m_ui->btnWarn->setChecked(true);

    m_ui->btnError->setButtonOptions(ZToolButton::Opt_Checkable | ZToolButton::Opt_HasIcon | ZToolButton::Opt_NoBackground);
    m_ui->btnError->setIcon(ZenoStyle::dpiScaledSize(QSize(20, 20)),
        ":/icons/logger_error_unchecked.svg",
        ":/icons/logger_error_unchecked.svg",
        ":/icons/logger_error_checked.svg",
        ":/icons/logger_error_checked.svg");
    m_ui->btnError->setChecked(true);

    m_ui->btnKey->setButtonOptions(ZToolButton::Opt_Checkable | ZToolButton::Opt_HasIcon | ZToolButton::Opt_NoBackground);
    m_ui->btnKey->setIcon(ZenoStyle::dpiScaledSize(QSize(20, 20)),
        ":/icons/logger-key-unchecked.svg",
        ":/icons/logger-key-unchecked.svg",
        ":/icons/logger-key-checked.svg",
        ":/icons/logger-key-checked.svg");
    m_ui->btnKey->setChecked(true);

    m_ui->editSearch->setProperty("cssClass", "zeno2_2_lineedit");
    m_ui->editSearch->setPlaceholderText(tr("Search"));

    m_ui->btnDelete->setButtonOptions(ZToolButton::Opt_HasIcon | ZToolButton::Opt_NoBackground);
    m_ui->btnDelete->setIcon(ZenoStyle::dpiScaledSize(QSize(20, 20)),
        ":/icons/toolbar_delete_idle.svg",
        ":/icons/toolbar_delete_light.svg",
        "",
        "");

    m_ui->btnSetting->setButtonOptions(ZToolButton::Opt_HasIcon | ZToolButton::Opt_NoBackground);
    m_ui->btnSetting->setIcon(ZenoStyle::dpiScaledSize(QSize(20, 20)),
        ":/icons/settings.svg",
        ":/icons/settings-on.svg",
        "",
        "");

    initSignals();
    initModel();
    onFilterChanged();
}

void ZlogPanel::initModel()
{
    m_pFilterModel = new CustomFilterProxyModel(this);
    m_pFilterModel->setSourceModel(zenoApp->logModel());
    m_pFilterModel->setFilterRole(ROLE_LOGTYPE);
    m_ui->listView->setModel(m_pFilterModel);
}

void ZlogPanel::initSignals()
{
    connect(m_ui->btnKey, &ZToolButton::toggled, this, [=](bool bOn) {
        onFilterChanged();
    });

    connect(m_ui->btnDebug, &ZToolButton::toggled, this, [=](bool bOn) {
        onFilterChanged();
    });

    connect(m_ui->btnError, &ZToolButton::toggled, this, [=](bool bOn) {
        onFilterChanged();
    });

    connect(m_ui->btnInfo, &ZToolButton::toggled, this, [=](bool bOn) {
        onFilterChanged();
    });

    connect(m_ui->btnWarn, &ZToolButton::toggled, this, [=](bool bOn) {
        onFilterChanged();
    });

    connect(m_ui->btnDelete, &ZToolButton::clicked, this, [=]() {
        zenoApp->logModel()->clear();
    });
}

void ZlogPanel::onFilterChanged()
{
    QVector<QtMsgType> filters;
    if (m_ui->btnWarn->isChecked())
        filters.append(QtWarningMsg);
    if (m_ui->btnKey->isChecked())
        filters.append(QtCriticalMsg);
    if (m_ui->btnDebug->isChecked())
        filters.append(QtDebugMsg);
    if (m_ui->btnError->isChecked())
        filters.append(QtFatalMsg);
    if (m_ui->btnInfo->isChecked())
        filters.append(QtInfoMsg);
    m_pFilterModel->setFilters(filters);
}


//////////////////////////////////////////////////////////////////////////
CustomFilterProxyModel::CustomFilterProxyModel(QObject *parent)
    : QSortFilterProxyModel(parent)
    , m_filters(0)
{
}

void CustomFilterProxyModel::setFilters(const QVector<QtMsgType>& filters)
{
    m_filters = filters;
    invalidate();
}

bool CustomFilterProxyModel::filterAcceptsRow(int source_row, const QModelIndex &source_parent) const
{
    QModelIndex index = sourceModel()->index(source_row, 0, source_parent);
    int role = filterRole();
    QtMsgType type = (QtMsgType)index.data(ROLE_LOGTYPE).toInt();
    if (m_filters.contains(type))
        return true;
    return false;
}