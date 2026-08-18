#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "ui/strategypanel.h"
#include "ui/legalconsentdialog.h"
#include "indicator/indicators.h"
#include "strategy/movingaveragestrategy.h"
#include "strategy/prosperitygrowthstrategy.h"

#include <QAbstractItemView>
#include <QAction>
#include <QDateTime>
#include <QDialog>
#include <QDoubleSpinBox>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QHeaderView>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QScrollArea>
#include <QSet>
#include <QSizePolicy>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTabWidget>
#include <QTextEdit>
#include <QTimer>
#include <QVBoxLayout>

#include <cmath>
#include <vector>

namespace {
constexpr int MaxStoredTradeRecords = 50;
constexpr int ProsperityGrowthRequiredSamples = 60;
constexpr double BoardLotSize = 100.0;
constexpr double CommissionRate = 0.0002;
constexpr double MinCommission = 5.0;
constexpr double StampDutyRate = 0.001;
constexpr double TransferFeeRate = 0.00001;

struct TradeFeeBreakdown
{
    double commission = 0.0;
    double stampDuty = 0.0;
    double transferFee = 0.0;
    double total = 0.0;
};

double roundDownToBoardLot(double shares)
{
    if (shares <= 0.0) {
        return 0.0;
    }
    return std::floor(shares / BoardLotSize) * BoardLotSize;
}

TradeFeeBreakdown calculateStockFees(double amount, bool sell)
{
    TradeFeeBreakdown fees;
    if (amount <= 0.0) {
        return fees;
    }

    fees.commission = qMax(amount * CommissionRate, MinCommission);
    fees.stampDuty = sell ? amount * StampDutyRate : 0.0;
    fees.transferFee = amount * TransferFeeRate;
    fees.total = fees.commission + fees.stampDuty + fees.transferFee;
    return fees;
}

QString feeSummary(const TradeFeeBreakdown& fees)
{
    return QStringLiteral("费用 %1（佣金 %2，印花税 %3，过户费 %4）")
        .arg(fees.total, 0, 'f', 2)
        .arg(fees.commission, 0, 'f', 2)
        .arg(fees.stampDuty, 0, 'f', 2)
        .arg(fees.transferFee, 0, 'f', 2);
}

double affordableBoardLot(double price, double budget)
{
    double lotSize = roundDownToBoardLot(budget / price);
    while (lotSize >= BoardLotSize) {
        const double amount = price * lotSize;
        if (amount + calculateStockFees(amount, false).total <= budget) {
            return lotSize;
        }
        lotSize -= BoardLotSize;
    }
    return 0.0;
}
bool shouldLogStrategyProgressSample(int sampleCount, int requiredSamples)
{
    if (sampleCount <= 0 || requiredSamples <= 0) {
        return false;
    }

    return sampleCount == 1
        || sampleCount == requiredSamples
        || sampleCount == requiredSamples - 1
        || sampleCount % 5 == 0;
}

double simpleAverageOfLast(const QVector<double>& values, int period)
{
    if (period <= 0 || values.size() < period) {
        return 0.0;
    }

    double sum = 0.0;
    for (int i = values.size() - period; i < values.size(); ++i) {
        sum += values.at(i);
    }
    return sum / period;
}

void showThemedWarning(QWidget* parent, const QString& title, const QString& message)
{
    QDialog dialog(parent);
    dialog.setModal(true);
    dialog.setWindowTitle(title);
    dialog.setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);
    dialog.setObjectName(QStringLiteral("themedWarningDialog"));
    dialog.setMinimumWidth(560);
    dialog.setStyleSheet(QStringLiteral(R"(
        QDialog#themedWarningDialog {
            background-color: #1a1a2e;
            border: 1px solid #4a5568;
            border-radius: 8px;
        }
        QLabel#dialogTitle {
            color: #f7fafc;
            font-size: 15px;
            font-weight: bold;
        }
        QLabel#dialogIcon {
            background-color: #f6ad55;
            color: #1a1a2e;
            border-radius: 14px;
            min-width: 28px;
            max-width: 28px;
            min-height: 28px;
            max-height: 28px;
            font-size: 18px;
            font-weight: bold;
        }
        QLabel#dialogMessage {
            color: #e2e8f0;
            font-size: 13px;
        }
        QPushButton#dialogCloseButton {
            background-color: transparent;
            color: #a0aec0;
            border: none;
            border-radius: 4px;
            min-width: 28px;
            max-width: 28px;
            min-height: 28px;
            max-height: 28px;
            padding: 0;
            font-size: 16px;
        }
        QPushButton#dialogCloseButton:hover {
            background-color: #2d3748;
            color: white;
        }
        QPushButton#dialogOkButton {
            background-color: #2b6cb0;
            color: white;
            border: none;
            border-radius: 6px;
            padding: 8px 24px;
            min-width: 72px;
            min-height: 32px;
            font-size: 13px;
        }
        QPushButton#dialogOkButton:hover {
            background-color: #3182ce;
        }
        QPushButton#dialogOkButton:pressed {
            background-color: #2c5282;
        }
    )"));

    auto* rootLayout = new QVBoxLayout(&dialog);
    rootLayout->setContentsMargins(18, 16, 18, 16);
    rootLayout->setSpacing(14);

    auto* titleLayout = new QHBoxLayout();
    titleLayout->setContentsMargins(0, 0, 0, 0);
    titleLayout->setSpacing(10);

    auto* titleLabel = new QLabel(title, &dialog);
    titleLabel->setObjectName(QStringLiteral("dialogTitle"));

    auto* closeButton = new QPushButton(QStringLiteral("X"), &dialog);
    closeButton->setObjectName(QStringLiteral("dialogCloseButton"));
    closeButton->setCursor(Qt::PointingHandCursor);

    titleLayout->addWidget(titleLabel, 1);
    titleLayout->addWidget(closeButton, 0, Qt::AlignTop);
    rootLayout->addLayout(titleLayout);

    auto* contentLayout = new QHBoxLayout();
    contentLayout->setContentsMargins(0, 2, 0, 2);
    contentLayout->setSpacing(12);

    auto* iconLabel = new QLabel(QStringLiteral("!"), &dialog);
    iconLabel->setObjectName(QStringLiteral("dialogIcon"));
    iconLabel->setAlignment(Qt::AlignCenter);

    auto* messageLabel = new QLabel(message, &dialog);
    messageLabel->setObjectName(QStringLiteral("dialogMessage"));
    messageLabel->setWordWrap(true);
    messageLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    messageLabel->setMinimumWidth(430);

    contentLayout->addWidget(iconLabel, 0, Qt::AlignTop);
    contentLayout->addWidget(messageLabel, 1);
    rootLayout->addLayout(contentLayout);

    auto* buttonLayout = new QHBoxLayout();
    buttonLayout->setContentsMargins(0, 4, 0, 0);
    buttonLayout->addStretch();

    auto* okButton = new QPushButton(QStringLiteral("确定"), &dialog);
    okButton->setObjectName(QStringLiteral("dialogOkButton"));
    okButton->setCursor(Qt::PointingHandCursor);
    okButton->setDefault(true);
    buttonLayout->addWidget(okButton);
    rootLayout->addLayout(buttonLayout);

    QObject::connect(closeButton, &QPushButton::clicked, &dialog, &QDialog::accept);
    QObject::connect(okButton, &QPushButton::clicked, &dialog, &QDialog::accept);

    dialog.exec();
}

bool showThemedQuestion(QWidget* parent, const QString& title, const QString& message,
                        const QString& acceptText, const QString& rejectText)
{
    QDialog dialog(parent);
    dialog.setModal(true);
    dialog.setWindowTitle(title);
    dialog.setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);
    dialog.setObjectName(QStringLiteral("themedQuestionDialog"));
    dialog.setMinimumWidth(560);
    dialog.setStyleSheet(QStringLiteral(R"(
        QDialog#themedQuestionDialog {
            background-color: #1a1a2e;
            border: 1px solid #4a5568;
            border-radius: 8px;
        }
        QLabel#questionTitle {
            color: #f7fafc;
            font-size: 15px;
            font-weight: bold;
        }
        QLabel#questionIcon {
            background-color: #2b6cb0;
            color: white;
            border-radius: 14px;
            min-width: 28px;
            max-width: 28px;
            min-height: 28px;
            max-height: 28px;
            font-size: 18px;
            font-weight: bold;
        }
        QLabel#questionMessage {
            color: #e2e8f0;
            font-size: 13px;
        }
        QPushButton#questionCloseButton {
            background-color: transparent;
            color: #a0aec0;
            border: none;
            border-radius: 4px;
            min-width: 28px;
            max-width: 28px;
            min-height: 28px;
            max-height: 28px;
            padding: 0;
            font-size: 16px;
        }
        QPushButton#questionCloseButton:hover {
            background-color: #2d3748;
            color: white;
        }
        QPushButton#questionAcceptButton {
            background-color: #2b6cb0;
            color: white;
            border: none;
            border-radius: 6px;
            padding: 8px 22px;
            min-width: 88px;
            min-height: 32px;
            font-size: 13px;
            font-weight: 600;
        }
        QPushButton#questionAcceptButton:hover {
            background-color: #3182ce;
        }
        QPushButton#questionAcceptButton:pressed {
            background-color: #2c5282;
        }
        QPushButton#questionRejectButton {
            background-color: #718096;
            color: white;
            border: none;
            border-radius: 6px;
            padding: 8px 22px;
            min-width: 88px;
            min-height: 32px;
            font-size: 13px;
        }
        QPushButton#questionRejectButton:hover {
            background-color: #5a6678;
        }
    )"));

    auto* rootLayout = new QVBoxLayout(&dialog);
    rootLayout->setContentsMargins(18, 16, 18, 16);
    rootLayout->setSpacing(14);

    auto* titleLayout = new QHBoxLayout();
    titleLayout->setContentsMargins(0, 0, 0, 0);
    titleLayout->setSpacing(10);

    auto* titleLabel = new QLabel(title, &dialog);
    titleLabel->setObjectName(QStringLiteral("questionTitle"));

    auto* closeButton = new QPushButton(QStringLiteral("X"), &dialog);
    closeButton->setObjectName(QStringLiteral("questionCloseButton"));
    closeButton->setCursor(Qt::PointingHandCursor);

    titleLayout->addWidget(titleLabel, 1);
    titleLayout->addWidget(closeButton, 0, Qt::AlignTop);
    rootLayout->addLayout(titleLayout);

    auto* contentLayout = new QHBoxLayout();
    contentLayout->setContentsMargins(0, 2, 0, 2);
    contentLayout->setSpacing(12);

    auto* iconLabel = new QLabel(QStringLiteral("?"), &dialog);
    iconLabel->setObjectName(QStringLiteral("questionIcon"));
    iconLabel->setAlignment(Qt::AlignCenter);

    auto* messageLabel = new QLabel(message, &dialog);
    messageLabel->setObjectName(QStringLiteral("questionMessage"));
    messageLabel->setWordWrap(true);
    messageLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    messageLabel->setMinimumWidth(430);

    contentLayout->addWidget(iconLabel, 0, Qt::AlignTop);
    contentLayout->addWidget(messageLabel, 1);
    rootLayout->addLayout(contentLayout);

    auto* buttonLayout = new QHBoxLayout();
    buttonLayout->setContentsMargins(0, 4, 0, 0);
    buttonLayout->addStretch();

    auto* rejectButton = new QPushButton(rejectText, &dialog);
    rejectButton->setObjectName(QStringLiteral("questionRejectButton"));
    rejectButton->setCursor(Qt::PointingHandCursor);

    auto* acceptButton = new QPushButton(acceptText, &dialog);
    acceptButton->setObjectName(QStringLiteral("questionAcceptButton"));
    acceptButton->setCursor(Qt::PointingHandCursor);
    acceptButton->setDefault(true);

    buttonLayout->addWidget(rejectButton);
    buttonLayout->addWidget(acceptButton);
    rootLayout->addLayout(buttonLayout);

    QObject::connect(closeButton, &QPushButton::clicked, &dialog, &QDialog::reject);
    QObject::connect(rejectButton, &QPushButton::clicked, &dialog, &QDialog::reject);
    QObject::connect(acceptButton, &QPushButton::clicked, &dialog, &QDialog::accept);

    return dialog.exec() == QDialog::Accepted;
}

bool showThemedAmountInput(QWidget* parent, const QString& title, const QString& label,
                           double currentValue, double minimum, double maximum,
                           int decimals, double* value)
{
    if (!value) {
        return false;
    }

    QDialog dialog(parent);
    dialog.setModal(true);
    dialog.setWindowTitle(title);
    dialog.setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);
    dialog.setObjectName(QStringLiteral("themedAmountDialog"));
    dialog.setMinimumWidth(440);
    dialog.setStyleSheet(QStringLiteral(R"(
        QDialog#themedAmountDialog {
            background-color: #1a1a2e;
            border: 1px solid #4a5568;
            border-radius: 8px;
        }
        QLabel#amountTitle {
            color: #f7fafc;
            font-size: 15px;
            font-weight: bold;
        }
        QLabel#amountLabel {
            color: #e2e8f0;
            font-size: 13px;
            min-width: 48px;
        }
        QDoubleSpinBox#amountSpin {
            background-color: #4a5568;
            color: white;
            border: 1px solid #718096;
            border-radius: 6px;
            padding: 6px 10px;
            font-size: 14px;
            min-height: 34px;
        }
        QDoubleSpinBox#amountSpin:focus {
            border-color: #63b3ed;
        }
        QPushButton#amountCloseButton {
            background-color: transparent;
            color: #a0aec0;
            border: none;
            border-radius: 4px;
            min-width: 28px;
            max-width: 28px;
            min-height: 28px;
            max-height: 28px;
            padding: 0;
            font-size: 16px;
        }
        QPushButton#amountCloseButton:hover {
            background-color: #2d3748;
            color: white;
        }
        QPushButton#amountAcceptButton {
            background-color: #2b6cb0;
            color: white;
            border: none;
            border-radius: 6px;
            padding: 8px 22px;
            min-width: 88px;
            min-height: 32px;
            font-size: 13px;
            font-weight: 600;
        }
        QPushButton#amountAcceptButton:hover {
            background-color: #3182ce;
        }
        QPushButton#amountAcceptButton:pressed {
            background-color: #2c5282;
        }
        QPushButton#amountRejectButton {
            background-color: #718096;
            color: white;
            border: none;
            border-radius: 6px;
            padding: 8px 22px;
            min-width: 88px;
            min-height: 32px;
            font-size: 13px;
        }
        QPushButton#amountRejectButton:hover {
            background-color: #5a6678;
        }
    )"));

    auto* rootLayout = new QVBoxLayout(&dialog);
    rootLayout->setContentsMargins(18, 16, 18, 16);
    rootLayout->setSpacing(14);

    auto* titleLayout = new QHBoxLayout();
    titleLayout->setContentsMargins(0, 0, 0, 0);
    titleLayout->setSpacing(10);

    auto* titleLabel = new QLabel(title, &dialog);
    titleLabel->setObjectName(QStringLiteral("amountTitle"));

    auto* closeButton = new QPushButton(QStringLiteral("X"), &dialog);
    closeButton->setObjectName(QStringLiteral("amountCloseButton"));
    closeButton->setCursor(Qt::PointingHandCursor);

    titleLayout->addWidget(titleLabel, 1);
    titleLayout->addWidget(closeButton, 0, Qt::AlignTop);
    rootLayout->addLayout(titleLayout);

    auto* inputLayout = new QHBoxLayout();
    inputLayout->setContentsMargins(0, 4, 0, 2);
    inputLayout->setSpacing(10);

    auto* labelWidget = new QLabel(label, &dialog);
    labelWidget->setObjectName(QStringLiteral("amountLabel"));

    auto* amountSpin = new QDoubleSpinBox(&dialog);
    amountSpin->setObjectName(QStringLiteral("amountSpin"));
    amountSpin->setRange(minimum, maximum);
    amountSpin->setDecimals(decimals);
    amountSpin->setSingleStep(1000.0);
    amountSpin->setValue(qBound(minimum, currentValue, maximum));
    amountSpin->setMinimumWidth(280);

    inputLayout->addWidget(labelWidget);
    inputLayout->addWidget(amountSpin, 1);
    rootLayout->addLayout(inputLayout);

    auto* buttonLayout = new QHBoxLayout();
    buttonLayout->setContentsMargins(0, 4, 0, 0);
    buttonLayout->addStretch();

    auto* rejectButton = new QPushButton(QStringLiteral("取消"), &dialog);
    rejectButton->setObjectName(QStringLiteral("amountRejectButton"));
    rejectButton->setCursor(Qt::PointingHandCursor);

    auto* acceptButton = new QPushButton(QStringLiteral("确定"), &dialog);
    acceptButton->setObjectName(QStringLiteral("amountAcceptButton"));
    acceptButton->setCursor(Qt::PointingHandCursor);
    acceptButton->setDefault(true);

    buttonLayout->addWidget(rejectButton);
    buttonLayout->addWidget(acceptButton);
    rootLayout->addLayout(buttonLayout);

    QObject::connect(closeButton, &QPushButton::clicked, &dialog, &QDialog::reject);
    QObject::connect(rejectButton, &QPushButton::clicked, &dialog, &QDialog::reject);
    QObject::connect(acceptButton, &QPushButton::clicked, &dialog, &QDialog::accept);

    amountSpin->setFocus(Qt::PopupFocusReason);
    amountSpin->selectAll();

    if (dialog.exec() != QDialog::Accepted) {
        return false;
    }

    *value = amountSpin->value();
    return true;
}
}

MainWindow::MainWindow(bool guestMode, const QString& accountName, QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
    , m_marketData(nullptr)
    , m_manualTradeMarketData(nullptr)
    , m_accountPanel(nullptr)
    , m_statisticsPanel(nullptr)
    , m_signalPanel(nullptr)
    , m_newsPanel(nullptr)
    , m_screenerPanel(nullptr)
    , m_mainTabs(nullptr)
    , m_accountTabs(nullptr)
    , m_strategyStartTimer(nullptr)
    , m_isRunning(false)
    , m_activeAccountIndex(0)
    , m_activeRuntimeId(0)
    , m_currentPrice(10.78)
    , m_hasLastMarketData(false)
    , m_hasLastStrategyMarketData(false)
    , m_lastMarketDataErrorLoggedAt()
    , m_pendingManualTradeType(SignalType::NONE)
    , m_pendingManualTradeVolume(0.0)
    , m_guestMode(guestMode)
    , m_accountName(accountName)
{
    ui->setupUi(this);
    setWindowTitle(QStringLiteral("星策 StarQuant - 先以模拟谋方略，再持真仓逐行情"));

    QMenu* complianceMenu = menuBar()->addMenu(QStringLiteral("帮助与合规"));
    QAction* legalAction = complianceMenu->addAction(QStringLiteral("法律、隐私与风险说明"));
    connect(legalAction, &QAction::triggered, this, &MainWindow::showLegalInformation);

    const QString darkTheme =
        "QMainWindow { background-color: #253b6e; }"
        "QWidget#centralwidget { background-color: #253b6e; }"
        "QGroupBox { background-color: #1a1a2e; border: 1px solid #4a5568; border-radius: 8px; margin-top: 10px; padding-top: 10px; }"
        "QGroupBox::title { color: #63b3ed; font-weight: bold; left: 10px; }"
        "QLabel { color: #e0e0e0; font-size: 13px; }"
        "QLineEdit { background-color: #4a5568; color: white; border: 1px solid #718096; border-radius: 6px; padding: 8px 10px; font-size: 13px; min-height: 32px; }"
        "QLineEdit:focus { border-color: #63b3ed; }"
        "QComboBox { background-color: #4a5568; color: white; border: 1px solid #718096; border-radius: 6px; padding: 6px 10px; font-size: 13px; min-height: 30px; }"
        "QComboBox:disabled { background-color: #3b4556; color: #8ea0b8; }"
        "QComboBox QAbstractItemView { background-color: #1a1a2e; color: #e0e0e0; border: 1px solid #4a5568; selection-background-color: #2b6cb0; selection-color: white; }"
        "QCheckBox { color: #e0e0e0; spacing: 8px; font-size: 13px; }"
        "QCheckBox:disabled { color: #8ea0b8; }"
        "QCheckBox::indicator { width: 14px; height: 14px; }"
        "QCheckBox::indicator:unchecked { background-color: #253b6e; border: 1px solid #718096; border-radius: 3px; }"
        "QCheckBox::indicator:checked { background-color: #63b3ed; border: 1px solid #90cdf4; border-radius: 3px; image: url(:/icons/checkbox_check.png); }"
        "QCheckBox::indicator:unchecked:disabled { background-color: #2d3748; border-color: #4a5568; }"
        "QCheckBox::indicator:checked:disabled { background-color: #4a6b8f; border-color: #5f7894; image: url(:/icons/checkbox_check.png); }"
        "QPushButton { background-color: #718096; color: white; border: none; border-radius: 6px; padding: 8px 20px; font-size: 13px; min-height: 32px; }"
        "QPushButton:hover { background-color: #5a6678; }"
        "QPushButton:disabled { background-color: #3b4556; color: #8ea0b8; }"
        "QPushButton#startBtn { background-color: #2196f3; }"
        "QPushButton#startBtn:hover { background-color: #1976d2; }"
        "QPushButton#stopBtn { background-color: #e53e3e; }"
        "QPushButton#stopBtn:hover { background-color: #c53030; }"
        "QPushButton#favoriteAddBtn { background-color: #38a169; }"
        "QPushButton#favoriteAddBtn:hover { background-color: #2f855a; }"
        "QPushButton#favoriteRemoveBtn { background-color: #718096; }"
        "QPushButton#favoriteBuyBtn { background-color: #38a169; }"
        "QPushButton#favoriteBuyBtn:hover { background-color: #2f855a; }"
        "QPushButton#favoriteSellBtn { background-color: #e53e3e; }"
        "QPushButton#favoriteSellBtn:hover { background-color: #c53030; }"
        "QTableWidget { background-color: #1a1a2e; border: 1px solid #4a5568; border-radius: 6px; gridline-color: #4a5568; }"
        "QTableWidget::item { color: #e0e0e0; padding: 4px 8px; }"
        "QTableWidget::item:selected { background-color: #4299e1; }"
        "QTableWidget::horizontalHeader { background-color: #2d3748; }"
        "QTableWidget::horizontalHeader::section { color: #63b3ed; background-color: #2d3748; padding: 6px 8px; border: 1px solid #4a5568; }"
        "QTextEdit { background-color: #1a1a2e; color: #e0e0e0; border: 1px solid #4a5568; border-radius: 6px; font-family: Consolas,Monaco,monospace; font-size: 12px; }"
        "QTextBrowser { background-color: #1a1a2e; color: #e0e0e0; border: 1px solid #4a5568; border-radius: 6px; font-size: 13px; }"
        "QListWidget { background-color: #1a1a2e; color: #e0e0e0; border: 1px solid #4a5568; border-radius: 6px; padding: 4px; }"
        "QListWidget::item { padding: 6px 8px; border-radius: 4px; }"
        "QListWidget::item:selected { background-color: #2b6cb0; color: white; }"
        "QSpinBox { background-color: #4a5568; color: white; border: 1px solid #718096; border-radius: 6px; padding: 4px 8px; font-size: 13px; }"
        "QDoubleSpinBox { background-color: #4a5568; color: white; border: 1px solid #718096; border-radius: 6px; padding: 4px 8px; font-size: 13px; }"
        "QTabWidget#mainTabs::pane { border: 1px solid #4a5568; border-radius: 8px; background-color: #253b6e; top: -1px; }"
        "QTabBar::tab { background-color: #2d3748; color: #cbd5e0; padding: 10px 24px; min-width: 72px; border: 1px solid #4a5568; border-bottom: none; border-top-left-radius: 8px; border-top-right-radius: 8px; margin-right: 4px; }"
        "QTabBar::tab:selected { background-color: #1a1a2e; color: #63b3ed; font-weight: bold; }"
        "QTabBar::tab:!selected { margin-top: 4px; }"
        "QTabBar::tab:hover:!selected { background-color: #3b4a68; color: white; }"
        "QMessageBox { background-color: #1a1a2e; }"
        "QMessageBox QLabel { color: #e2e8f0; font-size: 13px; }"
        "QMessageBox QPushButton { background-color: #718096; color: white; border: none; border-radius: 6px; padding: 8px 20px; min-width: 78px; min-height: 32px; }"
        "QMessageBox QPushButton:hover { background-color: #5a6678; }"
        "QStatusBar { background-color: #2d3748; color: #e0e0e0; }";
    setStyleSheet(darkTheme);

    m_marketData = new MarketDataSimulator(this);
    m_manualTradeMarketData = new MarketDataSimulator(this);
    m_marketData->setFeedMode(MarketDataFeedMode::QuoteWhenOpenTrendWhenClosed);
    m_manualTradeMarketData->setFeedMode(MarketDataFeedMode::QuoteOnly);
    m_strategyStartTimer = new QTimer(this);
    m_strategyStartTimer->setInterval(30000);
    connect(m_strategyStartTimer, &QTimer::timeout, this, &MainWindow::startWaitingStrategiesIfReady);

    connect(m_marketData, &MarketDataSimulator::dataUpdated, this, &MainWindow::onMarketDataUpdated);
    connect(m_marketData, &MarketDataSimulator::intradayDataUpdated, this, &MainWindow::onIntradayDataUpdated);
    connect(m_marketData, &MarketDataSimulator::fallbackIntradayDataUsed, this, &MainWindow::onFallbackIntradayDataUsed);
    connect(m_marketData, &MarketDataSimulator::errorOccurred, this, &MainWindow::onMarketDataError);
    connect(ui->strategyPanel, &StrategyPanel::startClicked, this, &MainWindow::onStartStrategy);
    connect(ui->strategyPanel, &StrategyPanel::stopClicked, this, &MainWindow::onStopStrategy);
    connect(ui->strategyPanel, &StrategyPanel::strategyInstanceStartRequested, this, &MainWindow::onStartStrategyInstance);
    connect(ui->strategyPanel, &StrategyPanel::strategyInstanceStopRequested, this, &MainWindow::onStopStrategyInstance);
    connect(ui->strategyPanel, &StrategyPanel::strategyInstanceSelectionChanged, this, &MainWindow::onStrategyInstanceSelectionChanged);
    connect(ui->strategyPanel, &StrategyPanel::parametersChanged, this, &MainWindow::onUpdateParameters);
    connect(ui->strategyPanel, &StrategyPanel::viewSymbolChanged, this, &MainWindow::onViewSymbolChanged);
    connect(ui->strategyPanel, &StrategyPanel::favoriteSelected, this, &MainWindow::onFavoriteSelected);
    connect(ui->strategyPanel, &StrategyPanel::favoriteBuyRequested, this, &MainWindow::onFavoriteBuyRequested);
    connect(ui->strategyPanel, &StrategyPanel::favoriteSellRequested, this, &MainWindow::onFavoriteSellRequested);
    connect(m_manualTradeMarketData, &MarketDataSimulator::dataUpdated, this, &MainWindow::onManualTradeQuoteUpdated);
    connect(m_manualTradeMarketData, &MarketDataSimulator::errorOccurred, this, &MainWindow::onManualTradeQuoteError);

    m_accountPanel = new AccountPanel(this);
    ui->verticalLayout_3->replaceWidget(ui->accountPanel, m_accountPanel);
    delete ui->accountPanel;
    ui->accountPanel = m_accountPanel;
    m_accountPanels.append(m_accountPanel);
    for (int i = 1; i < 6; ++i) {
        m_accountPanels.append(new AccountPanel(this));
    }

    m_statisticsPanel = new StatisticsPanel(this);
    ui->horizontalLayout_2->replaceWidget(ui->statisticsPanel, m_statisticsPanel);
    delete ui->statisticsPanel;
    ui->statisticsPanel = m_statisticsPanel;

    m_signalPanel = new SignalPanel(this);
    ui->horizontalLayout_2->replaceWidget(ui->signalPanel, m_signalPanel);
    delete ui->signalPanel;
    ui->signalPanel = m_signalPanel;

    m_newsPanel = new NewsPanel(this);
    m_screenerPanel = new ScreenerPanel(this);
    connect(m_screenerPanel, &ScreenerPanel::viewStockRequested, this, &MainWindow::onScreenerViewStock);
    connect(m_screenerPanel, &ScreenerPanel::addFavoriteRequested, this, &MainWindow::onScreenerAddFavorite);
    loadAccountState();
    ui->strategyPanel->setAccountNames(accountNames());

    buildTabbedLayout();

    ui->strategyPanel->setRunningState(false);
    m_marketData->setSymbol(ui->strategyPanel->getViewSymbol());
    updateAccountInfo();

    m_marketData->startSimulation();
    ui->statusbar->showMessage(m_guestMode
        ? QStringLiteral("游客看盘：实时行情已启动")
        : QStringLiteral("客户端已登录：%1，实时行情已启动").arg(m_accountName), 4000);
}

MainWindow::~MainWindow()
{
    m_marketData->stopSimulation();
    for (StrategyRuntime* runtime : m_strategyRuntimes) {
        if (runtime && runtime->marketData) {
            runtime->marketData->stopSimulation();
        }
    }
    if (m_manualTradeMarketData) {
        m_manualTradeMarketData->stopSimulation();
    }
    saveAccountState();
    for (StrategyRuntime* runtime : m_strategyRuntimes) {
        if (!runtime) {
            continue;
        }
        delete runtime->marketData;
        delete runtime->strategy;
        delete runtime;
    }
    m_strategyRuntimes.clear();
    delete ui;
}

void MainWindow::buildTabbedLayout()
{
    ui->verticalLayout_3->removeWidget(ui->strategyPanel);
    ui->verticalLayout_3->removeWidget(ui->marketPanel);
    ui->verticalLayout_3->removeWidget(m_accountPanel);
    ui->verticalLayout_4->removeWidget(ui->chartPanel);
    ui->horizontalLayout_2->removeWidget(m_statisticsPanel);
    ui->horizontalLayout_2->removeWidget(m_signalPanel);

    ui->strategyPanel->setParent(nullptr);
    ui->marketPanel->setParent(nullptr);
    m_accountPanel->setParent(nullptr);
    ui->chartPanel->setParent(nullptr);
    m_statisticsPanel->setParent(nullptr);
    m_signalPanel->setParent(nullptr);

    while (QLayoutItem* item = ui->horizontalLayout->takeAt(0)) {
        if (QWidget* widget = item->widget()) {
            widget->hide();
        }
        delete item;
    }

    m_mainTabs = new QTabWidget(ui->centralwidget);
    m_mainTabs->setObjectName(QStringLiteral("mainTabs"));
    m_mainTabs->setDocumentMode(false);
    m_mainTabs->setTabPosition(QTabWidget::North);
    m_mainTabs->setElideMode(Qt::ElideNone);
    m_mainTabs->setUsesScrollButtons(false);
    m_mainTabs->addTab(createMainTab(), QStringLiteral("主页"));
    m_mainTabs->addTab(createScreenerTab(), QStringLiteral("选股"));
    m_mainTabs->addTab(createStrategyTab(), QStringLiteral("策略"));
    m_mainTabs->addTab(createPersonalTab(), QStringLiteral("账户"));
    m_mainTabs->addTab(createNewsTab(), QStringLiteral("新闻"));

    ui->horizontalLayout->setContentsMargins(8, 8, 8, 8);
    ui->horizontalLayout->addWidget(m_mainTabs);
}

QWidget* MainWindow::createMainTab()
{
    auto* tab = new QWidget(m_mainTabs);
    auto* rootLayout = new QHBoxLayout(tab);
    rootLayout->setContentsMargins(10, 10, 10, 10);
    rootLayout->setSpacing(12);

    auto* leftLayout = new QVBoxLayout();
    leftLayout->setContentsMargins(0, 0, 0, 0);
    leftLayout->setSpacing(12);

    QWidget* strategyControls = ui->strategyPanel->takeStrategyControlWidget(tab);
    QWidget* tradeLog = ui->strategyPanel->takeTradeLogWidget(tab);
    ui->marketPanel->setSymbolSelector(ui->strategyPanel->takeSymbolSelectorWidget(ui->marketPanel));

    ui->marketPanel->setMinimumHeight(200);
    m_statisticsPanel->setMinimumHeight(160);
    if (strategyControls) {
        strategyControls->setMinimumHeight(74);
    }
    if (tradeLog) {
        tradeLog->setMinimumHeight(190);
    }

    leftLayout->addWidget(ui->marketPanel);
    if (strategyControls) {
        leftLayout->addWidget(strategyControls);
    }
    leftLayout->addWidget(m_statisticsPanel);
    if (tradeLog) {
        leftLayout->addWidget(tradeLog, 1);
    }

    ui->chartPanel->setMinimumWidth(520);
    rootLayout->addLayout(leftLayout, 1);
    rootLayout->addWidget(ui->chartPanel, 3);
    return tab;
}

QWidget* MainWindow::createStrategyTab()
{
    auto* tab = new QWidget(m_mainTabs);
    auto* rootLayout = new QVBoxLayout(tab);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(0);

    auto* scrollArea = new QScrollArea(tab);
    scrollArea->setWidgetResizable(true);
    scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scrollArea->setStyleSheet(QStringLiteral("QScrollArea { border: none; background: transparent; }"));

    auto* content = new QWidget(scrollArea);
    auto* contentLayout = new QHBoxLayout(content);
    contentLayout->setSizeConstraint(QLayout::SetMinimumSize);
    contentLayout->setContentsMargins(10, 10, 10, 10);
    contentLayout->setSpacing(12);

    ui->strategyPanel->setMinimumWidth(420);
    m_signalPanel->setMinimumWidth(360);
    contentLayout->addWidget(ui->strategyPanel, 2);
    contentLayout->addWidget(m_signalPanel, 1);

    scrollArea->setWidget(content);
    rootLayout->addWidget(scrollArea);
    return tab;
}

QWidget* MainWindow::createScreenerTab()
{
    auto* tab = new QWidget(m_mainTabs);
    auto* rootLayout = new QVBoxLayout(tab);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    m_screenerPanel->setParent(tab);
    rootLayout->addWidget(m_screenerPanel);
    return tab;
}

QWidget* MainWindow::createPersonalTab()
{
    auto* tab = new QWidget(m_mainTabs);
    auto* rootLayout = new QHBoxLayout(tab);
    rootLayout->setContentsMargins(10, 10, 10, 10);
    rootLayout->setSpacing(12);

    QWidget* watchlist = ui->strategyPanel->takeWatchlistWidget(tab);
    if (watchlist) {
        watchlist->setMinimumWidth(300);
        watchlist->setMaximumWidth(420);
        watchlist->setMaximumHeight(QWIDGETSIZE_MAX);
        watchlist->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
        rootLayout->addWidget(watchlist, 1);
    }

    m_accountTabs = new QTabWidget(tab);
    m_accountTabs->setObjectName(QStringLiteral("accountTabs"));
    m_accountTabs->setMinimumWidth(620);
    m_accountTabs->setUsesScrollButtons(false);

    for (int i = 0; i < m_accountPanels.size(); ++i) {
        auto* accountPage = new QWidget(m_accountTabs);
        auto* pageLayout = new QVBoxLayout(accountPage);
        pageLayout->setContentsMargins(0, 0, 0, 0);
        pageLayout->setSpacing(8);

        AccountPanel* accountPanel = m_accountPanels.at(i);
        accountPanel->setParent(accountPage);

        auto* controls = new QGroupBox(QStringLiteral("账户资金转入转出"), accountPanel);
        auto* controlsLayout = new QHBoxLayout(controls);
        controlsLayout->setContentsMargins(10, 12, 10, 10);
        controlsLayout->setSpacing(8);

        auto* amountSpin = new QDoubleSpinBox(controls);
        amountSpin->setRange(0.0, 100000000.0);
        amountSpin->setDecimals(2);
        amountSpin->setSingleStep(1000.0);
        amountSpin->setValue(10000.0);
        amountSpin->setMinimumWidth(130);
        auto* transferInBtn = new QPushButton(QStringLiteral("转入"), controls);
        auto* transferOutBtn = new QPushButton(QStringLiteral("转出"), controls);
        auto* quick10kBtn = new QPushButton(QStringLiteral("1万"), controls);
        auto* quick50kBtn = new QPushButton(QStringLiteral("5万"), controls);
        auto* quick100kBtn = new QPushButton(QStringLiteral("10万"), controls);
        auto* customBtn = new QPushButton(QStringLiteral("自定义"), controls);
        auto* resetBtn = new QPushButton(QStringLiteral("归零"), controls);

        controlsLayout->addWidget(amountSpin);
        controlsLayout->addWidget(transferInBtn);
        controlsLayout->addWidget(transferOutBtn);
        controlsLayout->addWidget(quick10kBtn);
        controlsLayout->addWidget(quick50kBtn);
        controlsLayout->addWidget(quick100kBtn);
        controlsLayout->addWidget(customBtn);
        controlsLayout->addWidget(resetBtn);
        controlsLayout->addStretch();

        connect(quick10kBtn, &QPushButton::clicked, amountSpin, [amountSpin]() { amountSpin->setValue(10000.0); });
        connect(quick50kBtn, &QPushButton::clicked, amountSpin, [amountSpin]() { amountSpin->setValue(50000.0); });
        connect(quick100kBtn, &QPushButton::clicked, amountSpin, [amountSpin]() { amountSpin->setValue(100000.0); });
        connect(customBtn, &QPushButton::clicked, this, [this, amountSpin]() {
            double value = amountSpin->value();
            if (showThemedAmountInput(this, QStringLiteral("自定义金额"), QStringLiteral("金额"),
                                      amountSpin->value(), 0.0, 100000000.0, 2, &value)) {
                amountSpin->setValue(value);
            }
        });
        connect(transferInBtn, &QPushButton::clicked, this, [this, i, amountSpin]() {
            transferAccountCash(i, amountSpin->value());
        });
        connect(transferOutBtn, &QPushButton::clicked, this, [this, i, amountSpin]() {
            transferAccountCash(i, -amountSpin->value());
        });
        connect(resetBtn, &QPushButton::clicked, this, [this, i]() {
            resetAccountAssets(i);
        });

        accountPanel->insertFundsControlWidget(controls);
        pageLayout->addWidget(accountPanel, 1);
        m_accountTabs->addTab(accountPage, i < m_accounts.size() ? m_accounts.at(i).name : QStringLiteral("账户 %1").arg(i + 1));
    }

    connect(m_accountTabs, &QTabWidget::currentChanged, this, [this](int index) {
        if (index >= 0 && index < m_accounts.size()) {
            m_activeAccountIndex = index;
        }
    });

    rootLayout->addWidget(m_accountTabs, 3);
    return tab;
}

QWidget* MainWindow::createNewsTab()
{
    auto* tab = new QWidget(m_mainTabs);
    auto* rootLayout = new QVBoxLayout(tab);
    rootLayout->setContentsMargins(10, 10, 10, 10);
    rootLayout->setSpacing(12);

    m_newsPanel->setParent(tab);
    rootLayout->addWidget(m_newsPanel);
    return tab;
}

void MainWindow::onMarketDataUpdated(const MarketData &data)
{
    m_currentPrice = data.close;
    m_lastMarketData = data;
    m_hasLastMarketData = true;

    ui->marketPanel->updateMarketData(data);
    ui->strategyPanel->rememberStockName(data.symbol, data.name);
    if (MarketDataSimulator::isAShareContinuousTradingTime(data.timestamp)) {
        ui->chartPanel->updateChartData(data);
    }
    m_statisticsPanel->setData(data.turnover, data.volume, data.volume, 0.0, 0.0, 0);

    updatePositionsPrice(data.symbol, data.close);

    updateAccountInfo();
}

void MainWindow::onIntradayDataUpdated(const QVector<MarketData>& data)
{
    ui->chartPanel->updateIntradayData(data);
}

void MainWindow::onFallbackIntradayDataUsed(const QString& symbol, const QString& reason)
{
    const QString detail = reason.isEmpty() ? QStringLiteral("分时接口未返回可用数据") : reason;
    ui->strategyPanel->addSystemLog(QStringLiteral("分时行情获取失败，已使用 %1 的摘要数据兜底。原因：%2").arg(symbol, detail));
}

void MainWindow::onStrategyMarketDataUpdated(int strategyId, const MarketData& data)
{
    StrategyRuntime* runtime = runtimeForStrategy(strategyId);
    if (!runtime) {
        return;
    }

    runtime->lastMarketData = data;
    runtime->hasLastMarketData = true;
    m_lastStrategyMarketData = data;
    m_hasLastStrategyMarketData = true;

    ui->strategyPanel->rememberStockName(data.symbol, data.name);
    updatePositionsPrice(data.symbol, data.close);
    if (m_activeRuntimeId == strategyId) {
        updateSignalIndicators();
    }
    updateAccountInfo();

    if (!runtime->running || !runtime->strategy) {
        return;
    }

    if (!runtime->firstQuoteLogged) {
        runtime->firstQuoteLogged = true;
        ui->strategyPanel->addStrategyLog(strategyRuntimeLogLabel(runtime), QStringLiteral("行情就绪：%1 @ %2")
            .arg(ui->strategyPanel->stockDisplayText(data.symbol))
            .arg(data.close, 0, 'f', 2));
    }

    runtime->closeSamples.append(data.close);
    while (runtime->closeSamples.size() > 240) {
        runtime->closeSamples.removeFirst();
    }

    bool hasAccountPosition = false;
    double avgCost = 0.0;
    if (runtime->accountIndex >= 0 && runtime->accountIndex < m_accounts.size()) {
        const AccountState& account = m_accounts.at(runtime->accountIndex);
        const auto posIt = account.positions.constFind(data.symbol);
        hasAccountPosition = posIt != account.positions.cend() && posIt.value().quantity > 0.0;
        avgCost = hasAccountPosition ? posIt.value().avgCost : 0.0;
    }
    runtime->strategy->syncPositionState(hasAccountPosition, avgCost);

    runtime->tradeTriggeredOnTick = false;
    runtime->strategy->processMarketData(data);
    appendStrategyProgressLog(runtime);
    runtime->tradeTriggeredOnTick = false;
}
void MainWindow::onStrategyIntradayDataUpdated(int strategyId, const QVector<MarketData>& data)
{
    StrategyRuntime* runtime = runtimeForStrategy(strategyId);
    if (!runtime) {
        return;
    }
    runtime->indicatorHistory = data;
    if (m_activeRuntimeId == strategyId) {
        updateSignalIndicators();
    }
}

void MainWindow::resetStrategyProgressTracking(StrategyRuntime* runtime, const StrategyConfig& config)
{
    if (!runtime) {
        return;
    }
    runtime->config = config;
    runtime->closeSamples.clear();
    runtime->lastProgressSampleLogged = 0;
    runtime->lastMonitorSampleLogged = 0;
    runtime->firstQuoteLogged = false;
    runtime->monitorEntered = false;
    runtime->tradeTriggeredOnTick = false;
}

void MainWindow::appendStrategyProgressLog(StrategyRuntime* runtime)
{
    if (!runtime || !runtime->running || runtime->closeSamples.isEmpty()) {
        return;
    }

    const StrategyConfig& config = runtime->config;
    const bool doubleMA = config.strategyType == StrategyType::DoubleMA;
    const QString label = strategyRuntimeLogLabel(runtime);
    const int requiredSamples = doubleMA
        ? qMax(config.doubleMAConfig.fastMA, config.doubleMAConfig.slowMA)
        : ProsperityGrowthRequiredSamples;
    auto* movingAverage = doubleMA ? qobject_cast<MovingAverageStrategy*>(runtime->strategy) : nullptr;
    const int sampleCount = movingAverage
        ? movingAverage->closedBarCount()
        : runtime->closeSamples.size();
    const QString sampleName = doubleMA
        ? QStringLiteral("%1分钟K线").arg(config.doubleMAConfig.barPeriodMinutes)
        : QStringLiteral("样本");

    if (sampleCount < requiredSamples) {
        if (sampleCount != runtime->lastProgressSampleLogged
            && shouldLogStrategyProgressSample(sampleCount, requiredSamples)) {
            ui->strategyPanel->addStrategyLog(label, QStringLiteral("%1收集中：%2/%3")
                .arg(sampleName)
                .arg(sampleCount)
                .arg(requiredSamples));
            runtime->lastProgressSampleLogged = sampleCount;
        }
        return;
    }

    if (!runtime->monitorEntered) {
        ui->strategyPanel->addStrategyLog(label, QStringLiteral("进入监控：%1/%2 个%3已就绪")
            .arg(sampleCount)
            .arg(requiredSamples)
            .arg(sampleName));
        runtime->monitorEntered = true;
        runtime->lastProgressSampleLogged = sampleCount;
    }

    if (runtime->tradeTriggeredOnTick) {
        return;
    }

    if (doubleMA) {
        if (sampleCount != runtime->lastMonitorSampleLogged
            && (sampleCount == requiredSamples || sampleCount % 5 == 0)) {
            const double fastMA = movingAverage
                ? movingAverage->fastMA()
                : simpleAverageOfLast(runtime->closeSamples, config.doubleMAConfig.fastMA);
            const double slowMA = movingAverage
                ? movingAverage->slowMA()
                : simpleAverageOfLast(runtime->closeSamples, config.doubleMAConfig.slowMA);
            ui->strategyPanel->addStrategyLog(label, QStringLiteral("当前快MA:%1，慢MA:%2，暂无交易")
                .arg(fastMA, 0, 'f', 3)
                .arg(slowMA, 0, 'f', 3));
            runtime->lastMonitorSampleLogged = sampleCount;
        }
        return;
    }

    if (sampleCount != runtime->lastMonitorSampleLogged
        && (sampleCount == requiredSamples || sampleCount % 5 == 0)) {
        ui->strategyPanel->addStrategyLog(label, QStringLiteral("成长监控：%1/%2 个样本，暂无交易")
            .arg(sampleCount)
            .arg(requiredSamples));
        runtime->lastMonitorSampleLogged = sampleCount;
    }
}

void MainWindow::updateSignalIndicators()
{
    if (!m_signalPanel) {
        return;
    }

    StrategyRuntime* runtime = runtimeForStrategy(m_activeRuntimeId);
    if (!runtime && !m_strategyRuntimes.isEmpty()) {
        runtime = m_strategyRuntimes.first();
    }
    if (!runtime) {
        m_signalPanel->clearIndicators();
        return;
    }

    QVector<MarketData> points = runtime->indicatorHistory;
    if (runtime->hasLastMarketData && runtime->lastMarketData.close > 0.0) {
        if (!points.isEmpty() && points.last().symbol != runtime->lastMarketData.symbol) {
            points.clear();
        }

        if (points.isEmpty()) {
            points.append(runtime->lastMarketData);
        } else {
            const QDateTime lastTime = points.last().timestamp;
            const QDateTime quoteTime = runtime->lastMarketData.timestamp;
            const bool sameMinute = lastTime.isValid() && quoteTime.isValid()
                && lastTime.date() == quoteTime.date()
                && lastTime.time().hour() == quoteTime.time().hour()
                && lastTime.time().minute() == quoteTime.time().minute();

            if (sameMinute) {
                points.last() = runtime->lastMarketData;
            } else if (!quoteTime.isValid() || !lastTime.isValid() || quoteTime > lastTime) {
                points.append(runtime->lastMarketData);
            }
        }
    }

    while (points.size() > 240) {
        points.removeFirst();
    }
    runtime->indicatorHistory = points;

    std::vector<double> closes;
    closes.reserve(static_cast<size_t>(points.size()));
    for (const MarketData& point : points) {
        if (point.close > 0.0) {
            closes.push_back(point.close);
        }
    }

    constexpr int RequiredPeriods = 26;
    if (closes.empty()) {
        m_signalPanel->clearIndicators();
        return;
    }
    if (static_cast<int>(closes.size()) < RequiredPeriods) {
        m_signalPanel->showIndicatorWarmup(static_cast<int>(closes.size()), RequiredPeriods);
        return;
    }

    const double rsi = TechnicalIndicators::calculateRSI(closes, 14);
    const auto macdValues = TechnicalIndicators::calculateMACD(closes, 12, 26, 9);
    const auto bollValues = TechnicalIndicators::calculateBollingerBands(closes, 20, 2.0);
    const double currentPrice = runtime->hasLastMarketData && runtime->lastMarketData.close > 0.0
        ? runtime->lastMarketData.close
        : closes.back();

    m_signalPanel->updateIndicators(rsi, macdValues.first, bollValues.second, bollValues.first, currentPrice);
}
void MainWindow::onMarketDataError(const QString &message)
{
    ui->statusbar->showMessage(message, 5000);
    ui->chartPanel->setEmptyMessage(QStringLiteral("行情接口暂不可用，正在重试..."));

    const QDateTime now = QDateTime::currentDateTime();
    const bool shouldLog = !m_lastMarketDataErrorLoggedAt.isValid()
        || m_lastMarketDataErrorLoggedAt.secsTo(now) >= 30
        || m_lastMarketDataErrorMessage != message;
    if (!shouldLog) {
        return;
    }

    m_lastMarketDataErrorLoggedAt = now;
    m_lastMarketDataErrorMessage = message;
    ui->strategyPanel->addSystemLog(QStringLiteral("行情错误：%1").arg(message));
}

void MainWindow::onStrategyMarketDataError(int strategyId, const QString& message)
{
    ui->statusbar->showMessage(message, 5000);
    StrategyRuntime* runtime = runtimeForStrategy(strategyId);
    if (!runtime || !runtime->running) {
        return;
    }

    const QString label = strategyRuntimeLogLabel(runtime);
    if (!MarketDataSimulator::isAShareContinuousTradingTime()) {
        runtime->running = false;
        runtime->waiting = true;
        runtime->hasLastMarketData = false;
        runtime->indicatorHistory.clear();
        if (runtime->marketData) {
            runtime->marketData->stopSimulation();
            runtime->marketData->setSymbol(runtime->config.symbol);
        }
        ui->strategyPanel->setStrategyInstanceRunning(strategyId, false, true);
        refreshStrategyRunningState();
        ui->strategyPanel->addStrategyLog(label, QStringLiteral("已收盘，切换为等待；连续竞价时自动启动。"));
        ui->statusbar->showMessage(QStringLiteral("策略 %1 已切换为等待，连续竞价时自动启动").arg(strategyDisplayIndex(strategyId)), 3000);
        return;
    }

    ui->strategyPanel->addStrategyLog(label, QStringLiteral("行情错误：%1").arg(message));
    showThemedWarning(this,
                      QStringLiteral("策略已停止"),
                      QStringLiteral("%1 因实时行情不可用已停止。\n\n%2")
                          .arg(label, message));
    stopStrategyRuntime(strategyId);
}

void MainWindow::onStrategySignal(int strategyId, const StrategySignal &signal)
{
    StrategyRuntime* runtime = runtimeForStrategy(strategyId);
    if (runtime) {
        runtime->tradeTriggeredOnTick = true;
    }
    const QString label = runtime ? strategyRuntimeLogLabel(runtime) : QStringLiteral("策略 %1").arg(strategyDisplayIndex(strategyId));
    ui->strategyPanel->addSignalLog(signal, label);

    const QString signalSymbolText = ui->strategyPanel->stockDisplayText(signal.symbol);
    QString signalText;
    switch (signal.type) {
    case SignalType::BUY:
        signalText = QStringLiteral("[买入信号] %1 @ %2").arg(signalSymbolText).arg(signal.price, 0, 'f', 2);
        break;
    case SignalType::SELL:
        signalText = QStringLiteral("[卖出信号] %1 @ %2").arg(signalSymbolText).arg(signal.price, 0, 'f', 2);
        break;
    case SignalType::STOP_LOSS:
        signalText = QStringLiteral("[止损信号] %1 @ %2").arg(signalSymbolText).arg(signal.price, 0, 'f', 2);
        break;
    case SignalType::TAKE_PROFIT:
        signalText = QStringLiteral("[止盈信号] %1 @ %2").arg(signalSymbolText).arg(signal.price, 0, 'f', 2);
        break;
    default:
        signalText = QStringLiteral("[策略信号] %1 @ %2").arg(signalSymbolText).arg(signal.price, 0, 'f', 2);
        break;
    }

    ui->statusbar->showMessage(signalText, 3000);
    executeOrder(strategyId, signal);
}
void MainWindow::loadAccountState()
{
    QSettings settings(QStringLiteral("StarQuant"), QStringLiteral("StarQuant"));
    m_accounts.clear();
    for (int i = 0; i < 6; ++i) {
        AccountState account;
        account.name = QStringLiteral("账户 %1").arg(i + 1);
        account.initialCapital = 100000.0;
        account.currentCash = 100000.0;
        m_accounts.append(account);
    }

    auto loadPositions = [](const QJsonArray& positions, AccountState* account) {
        if (!account) {
            return;
        }
        account->positions.clear();
        for (const QJsonValue& value : positions) {
            if (!value.isObject()) {
                continue;
            }
            const QJsonObject object = value.toObject();
            const QString symbol = MarketDataSimulator::normalizeSymbol(object.value(QStringLiteral("symbol")).toString());
            const double quantity = object.value(QStringLiteral("quantity")).toDouble();
            if (symbol.isEmpty() || quantity <= 0.0) {
                continue;
            }

            PositionInfo position;
            position.symbol = symbol;
            position.quantity = quantity;
            position.avgCost = object.value(QStringLiteral("avgCost")).toDouble();
            position.currentPrice = object.value(QStringLiteral("currentPrice")).toDouble(position.avgCost);
            position.marketValue = position.quantity * position.currentPrice;
            position.profit = (position.currentPrice - position.avgCost) * position.quantity;
            position.profitPercent = position.avgCost > 0.0 ? (position.currentPrice / position.avgCost - 1.0) * 100.0 : 0.0;
            account->positions[symbol] = position;
        }
    };

    auto loadTrades = [](const QJsonArray& trades, AccountState* account) {
        if (!account) {
            return;
        }
        account->tradeRecords.clear();
        for (const QJsonValue& value : trades) {
            if (!value.isObject()) {
                continue;
            }
            const QJsonObject object = value.toObject();
            TradeRecordInfo record;
            record.time = object.value(QStringLiteral("time")).toString();
            record.symbol = MarketDataSimulator::normalizeSymbol(object.value(QStringLiteral("symbol")).toString());
            if (record.symbol.isEmpty()) {
                record.symbol = object.value(QStringLiteral("symbol")).toString();
            }
            record.type = object.value(QStringLiteral("type")).toString();
            record.price = object.value(QStringLiteral("price")).toDouble();
            record.volume = object.value(QStringLiteral("volume")).toDouble();
            record.amount = object.value(QStringLiteral("amount")).toDouble();
            record.fee = object.value(QStringLiteral("fee")).toDouble();
            if (record.symbol.isEmpty() || record.type.isEmpty()) {
                continue;
            }
            account->tradeRecords.append(record);
            while (account->tradeRecords.size() > MaxStoredTradeRecords) {
                account->tradeRecords.removeFirst();
            }
        }
    };

    bool loadedNewAccounts = false;
    const QByteArray accountsPayload = settings.value(QStringLiteral("accounts/v1")).toString().toUtf8();
    if (!accountsPayload.isEmpty()) {
        QJsonParseError error;
        const QJsonDocument document = QJsonDocument::fromJson(accountsPayload, &error);
        if (error.error == QJsonParseError::NoError && document.isArray()) {
            const QJsonArray accounts = document.array();
            for (int i = 0; i < qMin(6, accounts.size()); ++i) {
                if (!accounts.at(i).isObject()) {
                    continue;
                }
                const QJsonObject object = accounts.at(i).toObject();
                AccountState& account = m_accounts[i];
                account.name = object.value(QStringLiteral("name")).toString(account.name);
                if (account.name == QStringLiteral("Account %1").arg(i + 1) || account.name.trimmed().isEmpty()) {
                    account.name = QStringLiteral("账户 %1").arg(i + 1);
                }
                account.initialCapital = object.value(QStringLiteral("initialCapital")).toDouble(account.initialCapital);
                if (account.initialCapital < 0.0) {
                    account.initialCapital = 100000.0;
                }
                account.currentCash = object.value(QStringLiteral("currentCash")).toDouble(account.initialCapital);
                loadPositions(object.value(QStringLiteral("positions")).toArray(), &account);
                loadTrades(object.value(QStringLiteral("trades")).toArray(), &account);
                loadedNewAccounts = true;
            }
        }
    }

    if (!loadedNewAccounts) {
        AccountState& account = m_accounts[0];
        account.initialCapital = settings.value(QStringLiteral("account/initialCapital"), 100000.0).toDouble();
        if (account.initialCapital < 0.0) {
            account.initialCapital = 100000.0;
        }
        account.currentCash = settings.value(QStringLiteral("account/currentCash"), account.initialCapital).toDouble();

        const QByteArray positionsPayload = settings.value(QStringLiteral("account/positions")).toString().toUtf8();
        if (!positionsPayload.isEmpty()) {
            QJsonParseError error;
            const QJsonDocument document = QJsonDocument::fromJson(positionsPayload, &error);
            if (error.error == QJsonParseError::NoError && document.isArray()) {
                loadPositions(document.array(), &account);
            }
        }

        const QByteArray tradesPayload = settings.value(QStringLiteral("account/trades")).toString().toUtf8();
        if (!tradesPayload.isEmpty()) {
            QJsonParseError error;
            const QJsonDocument document = QJsonDocument::fromJson(tradesPayload, &error);
            if (error.error == QJsonParseError::NoError && document.isArray()) {
                loadTrades(document.array(), &account);
            }
        }
    }

    for (int accountIndex = 0; accountIndex < m_accounts.size() && accountIndex < m_accountPanels.size(); ++accountIndex) {
        for (const TradeRecordInfo& record : m_accounts.at(accountIndex).tradeRecords) {
            m_accountPanels.at(accountIndex)->addTradeRecord(record.symbol, record.type, record.price, record.volume, record.amount, record.fee, record.time);
        }
    }
}

void MainWindow::saveAccountState() const
{
    QSettings settings(QStringLiteral("StarQuant"), QStringLiteral("StarQuant"));
    settings.setValue(QStringLiteral("account/guestMode"), m_guestMode);
    settings.setValue(QStringLiteral("account/accountName"), m_accountName);

    auto positionsToJson = [](const QMap<QString, PositionInfo>& positions) {
        QJsonArray array;
        for (auto it = positions.cbegin(); it != positions.cend(); ++it) {
            const PositionInfo& position = it.value();
            if (position.quantity <= 0.0) {
                continue;
            }
            QJsonObject object;
            object.insert(QStringLiteral("symbol"), position.symbol.isEmpty() ? it.key() : position.symbol);
            object.insert(QStringLiteral("quantity"), position.quantity);
            object.insert(QStringLiteral("avgCost"), position.avgCost);
            object.insert(QStringLiteral("currentPrice"), position.currentPrice);
            array.append(object);
        }
        return array;
    };

    auto tradesToJson = [](const QVector<TradeRecordInfo>& trades) {
        QJsonArray array;
        for (const TradeRecordInfo& record : trades) {
            QJsonObject object;
            object.insert(QStringLiteral("time"), record.time);
            object.insert(QStringLiteral("symbol"), record.symbol);
            object.insert(QStringLiteral("type"), record.type);
            object.insert(QStringLiteral("price"), record.price);
            object.insert(QStringLiteral("volume"), record.volume);
            object.insert(QStringLiteral("amount"), record.amount);
            object.insert(QStringLiteral("fee"), record.fee);
            array.append(object);
        }
        return array;
    };

    QJsonArray accounts;
    for (const AccountState& account : m_accounts) {
        QJsonObject object;
        object.insert(QStringLiteral("name"), account.name);
        object.insert(QStringLiteral("initialCapital"), account.initialCapital);
        object.insert(QStringLiteral("currentCash"), account.currentCash);
        object.insert(QStringLiteral("positions"), positionsToJson(account.positions));
        object.insert(QStringLiteral("trades"), tradesToJson(account.tradeRecords));
        accounts.append(object);
    }
    settings.setValue(QStringLiteral("accounts/v1"), QString::fromUtf8(QJsonDocument(accounts).toJson(QJsonDocument::Compact)));

    if (!m_accounts.isEmpty()) {
        const AccountState& account = m_accounts.first();
        settings.setValue(QStringLiteral("account/initialCapital"), account.initialCapital);
        settings.setValue(QStringLiteral("account/currentCash"), account.currentCash);
        settings.setValue(QStringLiteral("account/positions"), QString::fromUtf8(QJsonDocument(positionsToJson(account.positions)).toJson(QJsonDocument::Compact)));
        settings.setValue(QStringLiteral("account/trades"), QString::fromUtf8(QJsonDocument(tradesToJson(account.tradeRecords)).toJson(QJsonDocument::Compact)));
    }
    settings.sync();
}

void MainWindow::appendTradeRecord(int accountIndex, const QString& symbol, const QString& type, double price, double volume, double amount, double fee, const QString& time)
{
    if (accountIndex < 0 || accountIndex >= m_accounts.size()) {
        return;
    }

    TradeRecordInfo record;
    record.time = time;
    record.symbol = symbol;
    record.type = type;
    record.price = price;
    record.volume = volume;
    record.amount = amount;
    record.fee = fee;

    AccountState& account = m_accounts[accountIndex];
    account.tradeRecords.append(record);
    while (account.tradeRecords.size() > MaxStoredTradeRecords) {
        account.tradeRecords.removeFirst();
    }

    if (accountIndex < m_accountPanels.size() && m_accountPanels.at(accountIndex)) {
        m_accountPanels.at(accountIndex)->addTradeRecord(symbol, type, price, volume, amount, fee, time);
    }
}

void MainWindow::executeOrder(int strategyId, const StrategySignal& signal)
{
    StrategyRuntime* runtime = runtimeForStrategy(strategyId);
    const QString label = runtime ? strategyRuntimeLogLabel(runtime) : QStringLiteral("策略 %1").arg(strategyDisplayIndex(strategyId));
    if (!runtime || runtime->accountIndex < 0 || runtime->accountIndex >= m_accounts.size()) {
        ui->strategyPanel->addStrategyLog(label, QStringLiteral("订单跳过：未绑定账户。"));
        return;
    }

    const double price = signal.price;
    const QString symbol = MarketDataSimulator::normalizeSymbol(signal.symbol);
    const bool isBuy = signal.type == SignalType::BUY;
    const QString typeStr = isBuy ? QStringLiteral("买入") : QStringLiteral("卖出");

    if (price <= 0.0 || symbol.isEmpty()) {
        return;
    }

    AccountState& account = m_accounts[runtime->accountIndex];
    const StrategyConfig& config = runtime->config;

    if (isBuy) {
        double marketValue = 0.0;
        for (const auto& position : account.positions) {
            const double markPrice = position.currentPrice > 0.0 ? position.currentPrice : price;
            marketValue += position.quantity * markPrice;
        }

        const double totalAssets = account.currentCash + marketValue;
        const double maxPercent = qBound(0.0, config.positionConfig.maxSingleTrackPercent, 100.0);
        const double targetValue = totalAssets * maxPercent / 100.0;
        const double currentPositionValue = account.positions.contains(symbol)
            ? account.positions.value(symbol).quantity * price
            : 0.0;
        const double budget = qMin(account.currentCash, qMax(0.0, targetValue - currentPositionValue));
        const double lotSize = affordableBoardLot(price, budget);

        if (lotSize < BoardLotSize) {
            const double minTradeAmount = price * BoardLotSize;
            const double minTradeCashOut = minTradeAmount + calculateStockFees(minTradeAmount, false).total;
            const QString reason = budget < minTradeCashOut
                ? QStringLiteral("买入跳过：现金或目标仓位不足一手（含费用）。")
                : QStringLiteral("买入跳过：当前持仓已达到或超过目标仓位。");
            ui->statusbar->showMessage(reason, 5000);
            ui->strategyPanel->addStrategyLog(label, reason);
            return;
        }

        const double tradeAmount = price * lotSize;
        const TradeFeeBreakdown fees = calculateStockFees(tradeAmount, false);
        const double cashOut = tradeAmount + fees.total;
        if (cashOut > account.currentCash) {
            const QString reason = QStringLiteral("买入跳过：可用资金不足。");
            ui->statusbar->showMessage(reason, 5000);
            ui->strategyPanel->addStrategyLog(label, reason);
            return;
        }

        account.currentCash -= cashOut;
        if (account.positions.contains(symbol)) {
            PositionInfo& pos = account.positions[symbol];
            const double totalQty = pos.quantity + lotSize;
            pos.avgCost = (pos.avgCost * pos.quantity + cashOut) / totalQty;
            pos.quantity = totalQty;
            pos.currentPrice = price;
        } else {
            PositionInfo pos;
            pos.symbol = symbol;
            pos.quantity = lotSize;
            pos.avgCost = cashOut / lotSize;
            pos.currentPrice = price;
            account.positions[symbol] = pos;
        }

        appendTradeRecord(runtime->accountIndex, symbol, typeStr, price, lotSize, tradeAmount, fees.total,
                          QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss"));
        ui->strategyPanel->addStrategyLog(label, QStringLiteral("已成交：买入 %1 股 @ %2，%3")
            .arg(lotSize, 0, 'f', 0)
            .arg(price, 0, 'f', 2)
            .arg(feeSummary(fees)));
        updateAccountInfo(runtime->accountIndex);
        saveAccountState();
        return;
    }

    if (!account.positions.contains(symbol) || account.positions.value(symbol).quantity <= 0.0) {
        const QString reason = QStringLiteral("卖出跳过：没有可用持仓。");
        ui->statusbar->showMessage(reason, 5000);
        ui->strategyPanel->addStrategyLog(label, reason);
        return;
    }

    PositionInfo& pos = account.positions[symbol];
    pos.currentPrice = price;

    double lotSize = pos.quantity;
    if (signal.type == SignalType::TAKE_PROFIT
        && config.strategyType == StrategyType::ProsperityGrowth
        && config.riskConfig.partialTakeProfitEnabled
        && pos.quantity > BoardLotSize) {
        lotSize = qMax(BoardLotSize, roundDownToBoardLot(pos.quantity / 2.0));
    }
    lotSize = qMin(lotSize, pos.quantity);

    if (lotSize <= 0.0) {
        const QString reason = QStringLiteral("卖出跳过：计算数量为 0。");
        ui->statusbar->showMessage(reason, 5000);
        ui->strategyPanel->addStrategyLog(label, reason);
        return;
    }

    const double tradeAmount = price * lotSize;
    const TradeFeeBreakdown fees = calculateStockFees(tradeAmount, true);
    const double cashIn = tradeAmount - fees.total;
    account.currentCash += cashIn;
    pos.quantity -= lotSize;

    if (pos.quantity <= 0) {
        account.positions.remove(symbol);
    }

    appendTradeRecord(runtime->accountIndex, symbol, typeStr, price, lotSize, tradeAmount, fees.total,
                      QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss"));
    ui->strategyPanel->addStrategyLog(label, QStringLiteral("已成交：卖出 %1 股 @ %2，%3")
        .arg(lotSize, 0, 'f', 0)
        .arg(price, 0, 'f', 2)
        .arg(feeSummary(fees)));
    updateAccountInfo(runtime->accountIndex);
    saveAccountState();
}
double MainWindow::latestPriceForSymbol(const QString& symbol) const
{
    const QString normalized = MarketDataSimulator::normalizeSymbol(symbol);
    if (m_hasLastMarketData && MarketDataSimulator::normalizeSymbol(m_lastMarketData.symbol) == normalized) {
        if (m_lastMarketData.close > 0.0) {
            return m_lastMarketData.close;
        }
        if (m_lastMarketData.previousClose > 0.0) {
            return m_lastMarketData.previousClose;
        }
    }
    if (m_hasLastStrategyMarketData && MarketDataSimulator::normalizeSymbol(m_lastStrategyMarketData.symbol) == normalized) {
        if (m_lastStrategyMarketData.close > 0.0) {
            return m_lastStrategyMarketData.close;
        }
        if (m_lastStrategyMarketData.previousClose > 0.0) {
            return m_lastStrategyMarketData.previousClose;
        }
    }
    for (const StrategyRuntime* runtime : m_strategyRuntimes) {
        if (!runtime || !runtime->hasLastMarketData || MarketDataSimulator::normalizeSymbol(runtime->lastMarketData.symbol) != normalized) {
            continue;
        }
        if (runtime->lastMarketData.close > 0.0) {
            return runtime->lastMarketData.close;
        }
        if (runtime->lastMarketData.previousClose > 0.0) {
            return runtime->lastMarketData.previousClose;
        }
    }
    for (const AccountState& account : m_accounts) {
        if (account.positions.contains(normalized) && account.positions.value(normalized).currentPrice > 0.0) {
            return account.positions.value(normalized).currentPrice;
        }
    }
    return 0.0;
}

void MainWindow::onFavoriteSelected(const QString& symbol)
{
    const QString normalized = MarketDataSimulator::normalizeSymbol(symbol);
    const double price = latestPriceForSymbol(normalized);
    if (price > 0.0) {
        ui->strategyPanel->setManualTradePrice(normalized, price);
        return;
    }

    m_manualPriceQuoteSymbol = normalized;
    m_manualTradeMarketData->setSymbol(normalized);
    m_manualTradeMarketData->startSimulation();
}

void MainWindow::onScreenerViewStock(const QString& symbol, const QString& name)
{
    ui->strategyPanel->selectViewSymbol(symbol, name);
    if (m_mainTabs) {
        m_mainTabs->setCurrentIndex(0);
    }
    ui->statusbar->showMessage(QStringLiteral("已切换至 %1 行情。").arg(name.isEmpty() ? symbol : name), 3000);
}

void MainWindow::onScreenerAddFavorite(const QString& symbol, const QString& name)
{
    ui->strategyPanel->addFavorite(symbol, name);
    ui->statusbar->showMessage(QStringLiteral("已加入自选：%1").arg(name.isEmpty() ? symbol : name), 3000);
}

void MainWindow::showLegalInformation()
{
    LegalConsentDialog::showInformation(this);
}

void MainWindow::onFavoriteBuyRequested(const QString& symbol, double price, double volume)
{
    requestManualTrade(symbol, SignalType::BUY, price, volume);
}

void MainWindow::onFavoriteSellRequested(const QString& symbol, double price, double volume)
{
    requestManualTrade(symbol, SignalType::SELL, price, volume);
}

void MainWindow::requestManualTrade(const QString& symbol, SignalType type, double price, double volume)
{
    const QString normalized = MarketDataSimulator::normalizeSymbol(symbol);
    const double tradePrice = price > 0.0 ? price : latestPriceForSymbol(normalized);
    if (tradePrice > 0.0) {
        executeManualTrade(normalized, type, tradePrice, volume);
        return;
    }

    m_pendingManualTradeSymbol = normalized;
    m_pendingManualTradeType = type;
    m_pendingManualTradeVolume = volume;
    m_manualTradeMarketData->setSymbol(normalized);
    m_manualTradeMarketData->startSimulation();
    ui->statusbar->showMessage(QStringLiteral("正在获取手动委托最新行情..."), 3000);
}

void MainWindow::onManualTradeQuoteUpdated(const MarketData& data)
{
    const QString normalized = MarketDataSimulator::normalizeSymbol(data.symbol);
    const double quotePrice = data.close > 0.0 ? data.close : data.previousClose;
    ui->strategyPanel->rememberStockName(normalized, data.name);

    if (quotePrice > 0.0 && normalized == m_manualPriceQuoteSymbol) {
        ui->strategyPanel->setManualTradePrice(normalized, quotePrice);
        m_manualPriceQuoteSymbol.clear();
    }

    if (m_pendingManualTradeType == SignalType::NONE || normalized != m_pendingManualTradeSymbol || quotePrice <= 0.0) {
        if (m_pendingManualTradeType == SignalType::NONE && m_manualPriceQuoteSymbol.isEmpty()) {
            m_manualTradeMarketData->stopSimulation();
        }
        return;
    }

    const SignalType type = m_pendingManualTradeType;
    const double volume = m_pendingManualTradeVolume;
    m_pendingManualTradeSymbol.clear();
    m_pendingManualTradeType = SignalType::NONE;
    m_pendingManualTradeVolume = 0.0;
    m_manualTradeMarketData->stopSimulation();
    executeManualTrade(normalized, type, quotePrice, volume);
}

void MainWindow::onManualTradeQuoteError(const QString& message)
{
    if (m_pendingManualTradeType == SignalType::NONE && m_manualPriceQuoteSymbol.isEmpty()) {
        return;
    }

    m_pendingManualTradeSymbol.clear();
    m_manualPriceQuoteSymbol.clear();
    m_pendingManualTradeType = SignalType::NONE;
    m_pendingManualTradeVolume = 0.0;
    m_manualTradeMarketData->stopSimulation();
    ui->statusbar->showMessage(QStringLiteral("手动委托行情失败：%1").arg(message), 5000);
}

void MainWindow::executeManualTrade(const QString& symbol, SignalType type, double price, double volume)
{
    const QString normalized = MarketDataSimulator::normalizeSymbol(symbol);
    if (volume <= 0.0 || price <= 0.0 || m_activeAccountIndex < 0 || m_activeAccountIndex >= m_accounts.size()) {
        return;
    }

    AccountState& account = m_accounts[m_activeAccountIndex];
    const bool isBuy = type == SignalType::BUY;
    const QString typeStr = isBuy ? QStringLiteral("买入") : QStringLiteral("卖出");

    if (isBuy) {
        const double amount = price * volume;
        const TradeFeeBreakdown fees = calculateStockFees(amount, false);
        const double cashOut = amount + fees.total;
        if (cashOut > account.currentCash) {
            ui->statusbar->showMessage(QStringLiteral("手动买入跳过：可用资金不足。"), 4000);
            return;
        }

        account.currentCash -= cashOut;
        if (account.positions.contains(normalized)) {
            PositionInfo& pos = account.positions[normalized];
            const double totalQty = pos.quantity + volume;
            pos.avgCost = (pos.avgCost * pos.quantity + cashOut) / totalQty;
            pos.quantity = totalQty;
            pos.currentPrice = price;
        } else {
            PositionInfo pos;
            pos.symbol = normalized;
            pos.quantity = volume;
            pos.avgCost = cashOut / volume;
            pos.currentPrice = price;
            account.positions[normalized] = pos;
        }

        appendTradeRecord(m_activeAccountIndex, normalized, typeStr, price, volume, amount, fees.total,
                          QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss"));
    } else if (type == SignalType::SELL) {
        if (!account.positions.contains(normalized) || account.positions.value(normalized).quantity <= 0.0) {
            ui->statusbar->showMessage(QStringLiteral("手动卖出跳过：没有可用持仓。"), 4000);
            return;
        }

        PositionInfo& pos = account.positions[normalized];
        volume = qMin(volume, pos.quantity);
        pos.currentPrice = price;
        const double amount = price * volume;
        const TradeFeeBreakdown fees = calculateStockFees(amount, true);
        account.currentCash += amount - fees.total;
        pos.quantity -= volume;
        if (pos.quantity <= 0.0) {
            account.positions.remove(normalized);
        }

        appendTradeRecord(m_activeAccountIndex, normalized, typeStr, price, volume, amount, fees.total,
                          QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss"));
    } else {
        return;
    }

    updateAccountInfo(m_activeAccountIndex);
    saveAccountState();
    ui->statusbar->showMessage(QStringLiteral("手动委托已成交：%1 %2 股 @ %3，账户 %4")
        .arg(isBuy ? QStringLiteral("买入") : QStringLiteral("卖出"))
        .arg(volume, 0, 'f', 0)
        .arg(price, 0, 'f', 2)
        .arg(m_activeAccountIndex + 1), 4000);
}

void MainWindow::updateAccountInfo()
{
    for (int i = 0; i < m_accounts.size(); ++i) {
        updateAccountInfo(i);
    }
}

void MainWindow::updateAccountInfo(int accountIndex)
{
    if (accountIndex < 0 || accountIndex >= m_accounts.size() || accountIndex >= m_accountPanels.size()) {
        return;
    }

    AccountState& account = m_accounts[accountIndex];
    double marketValue = 0.0;
    for (auto it = account.positions.begin(); it != account.positions.end(); ++it) {
        PositionInfo& pos = it.value();
        pos.marketValue = pos.quantity * pos.currentPrice;
        pos.profit = (pos.currentPrice - pos.avgCost) * pos.quantity;
        pos.profitPercent = pos.avgCost > 0.0 ? (pos.currentPrice / pos.avgCost - 1) * 100.0 : 0.0;
        marketValue += pos.marketValue;
    }

    const double totalAssets = account.currentCash + marketValue;
    const double totalProfit = totalAssets - account.initialCapital;
    const double profitPercent = account.initialCapital > 0.0
        ? (totalAssets / account.initialCapital - 1.0) * 100.0
        : 0.0;

    m_accountPanels.at(accountIndex)->updateAccount(totalAssets, account.currentCash, marketValue, totalProfit, profitPercent);

    QMap<QString, ::PositionInfo> accountPositions;
    for (auto it = account.positions.cbegin(); it != account.positions.cend(); ++it) {
        const PositionInfo& pos = it.value();
        ::PositionInfo info;
        info.symbol = pos.symbol;
        info.quantity = pos.quantity;
        info.avgCost = pos.avgCost;
        info.currentPrice = pos.currentPrice;
        info.marketValue = pos.quantity * pos.currentPrice;
        info.profit = (pos.currentPrice - pos.avgCost) * pos.quantity;
        info.profitPercent = pos.avgCost > 0.0 ? (pos.currentPrice / pos.avgCost - 1) * 100.0 : 0.0;
        accountPositions[it.key()] = info;
    }
    m_accountPanels.at(accountIndex)->updatePositions(accountPositions);
}

void MainWindow::updatePositionsPrice(const QString& symbol, double price)
{
    const QString normalized = MarketDataSimulator::normalizeSymbol(symbol);
    if (normalized.isEmpty() || price <= 0.0) {
        return;
    }
    for (AccountState& account : m_accounts) {
        if (account.positions.contains(normalized)) {
            account.positions[normalized].currentPrice = price;
        }
    }
}

void MainWindow::transferAccountCash(int accountIndex, double amount)
{
    if (accountIndex < 0 || accountIndex >= m_accounts.size() || qFuzzyIsNull(amount)) {
        return;
    }
    AccountState& account = m_accounts[accountIndex];
    const double absAmount = std::abs(amount);
    if (absAmount <= 0.0) {
        return;
    }

    const bool transferIn = amount > 0.0;
    if (!transferIn && absAmount > account.currentCash) {
        ui->statusbar->showMessage(QStringLiteral("转出失败：可用资金不足。"), 4000);
        return;
    }

    if (transferIn) {
        account.currentCash += absAmount;
        account.initialCapital += absAmount;
    } else {
        account.currentCash -= absAmount;
        account.initialCapital = qMax(0.0, account.initialCapital - absAmount);
    }

    appendTradeRecord(accountIndex,
                      QStringLiteral("资金"),
                      transferIn ? QStringLiteral("转入") : QStringLiteral("转出"),
                      0.0,
                      0.0,
                      absAmount,
                      0.0,
                      QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss"));
    updateAccountInfo(accountIndex);
    saveAccountState();
    ui->statusbar->showMessage(QStringLiteral("账户 %1 %2 %3")
        .arg(accountIndex + 1)
        .arg(transferIn ? QStringLiteral("转入") : QStringLiteral("转出"))
        .arg(absAmount, 0, 'f', 2), 3000);
}

void MainWindow::resetAccountAssets(int accountIndex)
{
    if (accountIndex < 0 || accountIndex >= m_accounts.size()) {
        return;
    }

    AccountState& account = m_accounts[accountIndex];
    double marketValue = 0.0;
    for (const auto& position : account.positions) {
        marketValue += position.quantity * position.currentPrice;
    }
    const double totalAssets = account.currentCash + marketValue;

    const bool confirmed = showThemedQuestion(
        this,
        QStringLiteral("账户资产归零"),
        QStringLiteral("确认将账户 %1 的现金和持仓归零？成交记录会保留。").arg(accountIndex + 1),
        QStringLiteral("确认归零"),
        QStringLiteral("取消"));
    if (!confirmed) {
        return;
    }

    account.initialCapital = 0.0;
    account.currentCash = 0.0;
    account.positions.clear();

    appendTradeRecord(accountIndex,
                      QStringLiteral("资金"),
                      QStringLiteral("资产归零"),
                      0.0,
                      0.0,
                      totalAssets,
                      0.0,
                      QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss"));
    updateAccountInfo(accountIndex);
    saveAccountState();
    ui->statusbar->showMessage(QStringLiteral("账户 %1 资产已归零").arg(accountIndex + 1), 3000);
}

QStringList MainWindow::accountNames() const
{
    QStringList names;
    for (const AccountState& account : m_accounts) {
        names.append(account.name);
    }
    while (names.size() < 6) {
        names.append(QStringLiteral("账户 %1").arg(names.size() + 1));
    }
    return names;
}

MainWindow::StrategyRuntime* MainWindow::runtimeForStrategy(int strategyId) const
{
    for (StrategyRuntime* runtime : m_strategyRuntimes) {
        if (runtime && runtime->id == strategyId) {
            return runtime;
        }
    }
    return nullptr;
}

QString MainWindow::strategyRuntimeLogLabel(const StrategyRuntime* runtime) const
{
    if (!runtime) {
        return QStringLiteral("策略");
    }

    const QString accountText = runtime->accountIndex >= 0 && runtime->accountIndex < m_accounts.size()
        ? QStringLiteral("账户 %1").arg(runtime->accountIndex + 1)
        : QStringLiteral("未绑定");
    const QString strategyName = runtime->config.strategyType == StrategyType::ProsperityGrowth
        ? QStringLiteral("景气成长")
        : QStringLiteral("双均线");
    const QString symbol = runtime->config.symbol.trimmed().isEmpty()
        ? QStringLiteral("未设置")
        : ui->strategyPanel->stockDisplayText(runtime->config.symbol);
    return QStringLiteral("策略 %1 - %2 - %3 - %4")
        .arg(strategyDisplayIndex(runtime->id))
        .arg(strategyName, accountText, symbol);
}

QString MainWindow::strategyInstanceLogLabel(const StrategyInstanceInfo& instance) const
{
    const QString accountText = instance.accountIndex >= 0 && instance.accountIndex < m_accounts.size()
        ? QStringLiteral("账户 %1").arg(instance.accountIndex + 1)
        : QStringLiteral("未绑定");
    const QString strategyName = instance.config.strategyType == StrategyType::ProsperityGrowth
        ? QStringLiteral("景气成长")
        : QStringLiteral("双均线");
    const QString symbol = instance.config.symbol.trimmed().isEmpty()
        ? QStringLiteral("未设置")
        : ui->strategyPanel->stockDisplayText(instance.config.symbol);
    return QStringLiteral("策略 %1 - %2 - %3 - %4")
        .arg(strategyDisplayIndex(instance.id))
        .arg(strategyName, accountText, symbol);
}

int MainWindow::strategyDisplayIndex(int strategyId) const
{
    if (ui && ui->strategyPanel) {
        const int displayIndex = ui->strategyPanel->strategyInstanceDisplayIndex(strategyId);
        if (displayIndex > 0) {
            return displayIndex;
        }
    }
    return strategyId;
}
MainWindow::StrategyRuntime* MainWindow::ensureRuntime(const StrategyInstanceInfo& instance)
{
    StrategyRuntime* runtime = runtimeForStrategy(instance.id);
    if (!runtime) {
        runtime = new StrategyRuntime;
        runtime->id = instance.id;
        runtime->marketData = new MarketDataSimulator(this);
        runtime->marketData->setFeedMode(MarketDataFeedMode::RealtimeQuoteOnly);
        connect(runtime->marketData, &MarketDataSimulator::dataUpdated, this, [this, id = instance.id](const MarketData& data) {
            onStrategyMarketDataUpdated(id, data);
        });
        connect(runtime->marketData, &MarketDataSimulator::intradayDataUpdated, this, [this, id = instance.id](const QVector<MarketData>& data) {
            onStrategyIntradayDataUpdated(id, data);
        });
        connect(runtime->marketData, &MarketDataSimulator::errorOccurred, this, [this, id = instance.id](const QString& message) {
            onStrategyMarketDataError(id, message);
        });
        m_strategyRuntimes.append(runtime);
    }

    runtime->accountIndex = instance.accountIndex;
    configureStrategyRuntime(runtime, instance.config);
    return runtime;
}

void MainWindow::configureStrategyRuntime(StrategyRuntime* runtime, const StrategyConfig& config)
{
    if (!runtime) {
        return;
    }

    const bool needsNewStrategy = !runtime->strategy || runtime->activeStrategyType != config.strategyType;
    if (needsNewStrategy) {
        if (runtime->strategy) {
            disconnect(runtime->strategy, nullptr, this, nullptr);
            delete runtime->strategy;
            runtime->strategy = nullptr;
        }

        runtime->activeStrategyType = config.strategyType;
        if (config.strategyType == StrategyType::ProsperityGrowth) {
            runtime->strategy = new ProsperityGrowthStrategy(this);
        } else {
            runtime->strategy = new MovingAverageStrategy(this);
        }
        connect(runtime->strategy, &StrategyBase::signalGenerated, this, [this, id = runtime->id](const StrategySignal& signal) {
            onStrategySignal(id, signal);
        });
    }

    runtime->config = config;
    if (runtime->strategy) {
        runtime->strategy->setConfig(config);
    }
    if (!runtime->running && runtime->marketData) {
        runtime->marketData->setSymbol(config.symbol);
    }
}

bool MainWindow::hasRunningStrategyForAccount(int accountIndex, int exceptStrategyId) const
{
    if (accountIndex < 0) {
        return false;
    }
    for (const StrategyRuntime* runtime : m_strategyRuntimes) {
        if (runtime && (runtime->running || runtime->waiting) && runtime->accountIndex == accountIndex && runtime->id != exceptStrategyId) {
            return true;
        }
    }
    return false;
}

void MainWindow::startStrategyRuntimeNow(StrategyRuntime* runtime, bool fromBatch)
{
    if (!runtime || runtime->running) {
        return;
    }

    runtime->waiting = false;
    resetStrategyProgressTracking(runtime, runtime->config);
    runtime->running = true;
    runtime->hasLastMarketData = false;
    runtime->indicatorHistory.clear();
    if (runtime->strategy) {
        runtime->strategy->reset();
        runtime->strategy->setConfig(runtime->config);
    }
    if (runtime->marketData) {
        runtime->marketData->setSymbol(runtime->config.symbol);
        runtime->marketData->startSimulation();
    }

    m_activeRuntimeId = runtime->id;
    if (m_signalPanel) {
        m_signalPanel->clearIndicators();
    }
    ui->strategyPanel->setStrategyInstanceRunning(runtime->id, true, false);
    refreshStrategyRunningState();
    ui->strategyPanel->addStrategyLog(strategyRuntimeLogLabel(runtime), QStringLiteral("已启动。"));
    if (!fromBatch) {
        ui->statusbar->showMessage(QStringLiteral("策略 %1 已启动").arg(strategyDisplayIndex(runtime->id)), 2000);
    }
}

void MainWindow::startStrategyInstance(const StrategyInstanceInfo& instance, bool fromBatch)
{
    if (instance.id <= 0) {
        return;
    }

    const QString label = strategyInstanceLogLabel(instance);
    if (instance.accountIndex < 0 || instance.accountIndex >= m_accounts.size()) {
        ui->strategyPanel->addStrategyLog(label, QStringLiteral("跳过：未绑定账户。"));
        return;
    }
    if (hasRunningStrategyForAccount(instance.accountIndex, instance.id)) {
        ui->strategyPanel->addStrategyLog(label, QStringLiteral("跳过：该账户已有运行或等待中的策略。"));
        return;
    }

    StrategyRuntime* runtime = ensureRuntime(instance);
    if (!runtime || runtime->running) {
        return;
    }

    runtime->accountIndex = instance.accountIndex;
    configureStrategyRuntime(runtime, instance.config);

    if (!MarketDataSimulator::isAShareContinuousTradingTime()) {
        runtime->running = false;
        runtime->waiting = true;
        runtime->hasLastMarketData = false;
        runtime->indicatorHistory.clear();
        if (runtime->marketData) {
            runtime->marketData->stopSimulation();
            runtime->marketData->setSymbol(instance.config.symbol);
        }
        m_activeRuntimeId = instance.id;
        if (m_signalPanel) {
            m_signalPanel->clearIndicators();
        }
        ui->strategyPanel->setStrategyInstanceRunning(instance.id, false, true);
        refreshStrategyRunningState();
        ui->strategyPanel->addStrategyLog(strategyRuntimeLogLabel(runtime), QStringLiteral("已进入等待；连续竞价时自动启动。"));
        if (!fromBatch) {
            ui->statusbar->showMessage(QStringLiteral("策略 %1 已进入等待，连续竞价时自动启动").arg(strategyDisplayIndex(instance.id)), 3000);
        }
        return;
    }

    startStrategyRuntimeNow(runtime, fromBatch);
}

void MainWindow::stopStrategyRuntime(int strategyId)
{
    StrategyRuntime* runtime = runtimeForStrategy(strategyId);
    if (!runtime || (!runtime->running && !runtime->waiting)) {
        return;
    }

    const QString label = strategyRuntimeLogLabel(runtime);
    const bool wasWaiting = runtime->waiting && !runtime->running;
    runtime->running = false;
    runtime->waiting = false;
    if (runtime->marketData) {
        runtime->marketData->stopSimulation();
    }
    ui->strategyPanel->setStrategyInstanceRunning(strategyId, false, false);
    ui->strategyPanel->addStrategyLog(label, wasWaiting
        ? QStringLiteral("已取消等待。")
        : QStringLiteral("已停止。"));
    refreshStrategyRunningState();
}

void MainWindow::refreshStrategyRunningState()
{
    bool anyActive = false;
    bool anyWaiting = false;
    for (StrategyRuntime* runtime : m_strategyRuntimes) {
        if (!runtime) {
            continue;
        }
        anyActive = anyActive || runtime->running || runtime->waiting;
        anyWaiting = anyWaiting || runtime->waiting;
    }
    m_isRunning = anyActive;
    ui->strategyPanel->setRunningState(anyActive);

    if (m_strategyStartTimer) {
        if (anyWaiting && !m_strategyStartTimer->isActive()) {
            m_strategyStartTimer->start();
        } else if (!anyWaiting && m_strategyStartTimer->isActive()) {
            m_strategyStartTimer->stop();
        }
    }
}

void MainWindow::startWaitingStrategiesIfReady()
{
    if (!MarketDataSimulator::isAShareContinuousTradingTime()) {
        return;
    }

    int started = 0;
    for (StrategyRuntime* runtime : m_strategyRuntimes) {
        if (!runtime || !runtime->waiting) {
            continue;
        }
        if (hasRunningStrategyForAccount(runtime->accountIndex, runtime->id)) {
            continue;
        }
        ui->strategyPanel->addStrategyLog(strategyRuntimeLogLabel(runtime), QStringLiteral("连续竞价开始，自动启动。"));
        startStrategyRuntimeNow(runtime, true);
        ++started;
    }

    if (started > 0) {
        ui->statusbar->showMessage(QStringLiteral("等待策略已启动：%1 个").arg(started), 3000);
    }
    refreshStrategyRunningState();
}
void MainWindow::onViewSymbolChanged(const QString& symbol)
{
    const QString normalized = MarketDataSimulator::normalizeSymbol(symbol);
    m_marketData->setSymbol(normalized);
    m_hasLastMarketData = false;
    ui->chartPanel->clearData();
}

void MainWindow::onStartStrategy()
{
    const QVector<StrategyInstanceInfo> instances = ui->strategyPanel->strategyInstances();
    QSet<int> usedAccounts;
    for (const StrategyRuntime* runtime : m_strategyRuntimes) {
        if (runtime && (runtime->running || runtime->waiting) && runtime->accountIndex >= 0) {
            usedAccounts.insert(runtime->accountIndex);
        }
    }

    int started = 0;
    int waiting = 0;
    int skipped = 0;
    for (const StrategyInstanceInfo& instance : instances) {
        const QString label = strategyInstanceLogLabel(instance);
        if (instance.accountIndex < 0 || instance.accountIndex >= m_accounts.size()) {
            ++skipped;
            ui->strategyPanel->addStrategyLog(label, QStringLiteral("跳过：未绑定账户。"));
            continue;
        }
        if (usedAccounts.contains(instance.accountIndex)) {
            StrategyRuntime* existing = runtimeForStrategy(instance.id);
            if (existing && (existing->running || existing->waiting)) {
                continue;
            }
            ++skipped;
            ui->strategyPanel->addStrategyLog(label, QStringLiteral("跳过：账户冲突。"));
            continue;
        }
        startStrategyInstance(instance, true);
        StrategyRuntime* runtime = runtimeForStrategy(instance.id);
        if (runtime && runtime->running) {
            usedAccounts.insert(instance.accountIndex);
            ++started;
        } else if (runtime && runtime->waiting) {
            usedAccounts.insert(instance.accountIndex);
            ++waiting;
        } else {
            ++skipped;
        }
    }

    refreshStrategyRunningState();
    ui->statusbar->showMessage(QStringLiteral("全部启动完成：%1 个已启动，%2 个等待，%3 个已跳过")
        .arg(started)
        .arg(waiting)
        .arg(skipped), 3000);
}
void MainWindow::onStopStrategy()
{
    for (StrategyRuntime* runtime : m_strategyRuntimes) {
        if (runtime && (runtime->running || runtime->waiting)) {
            stopStrategyRuntime(runtime->id);
        }
    }
    refreshStrategyRunningState();
    ui->statusbar->showMessage(QStringLiteral("所有策略已停止，等待已取消"), 2000);
}

void MainWindow::onStartStrategyInstance(int strategyId)
{
    const StrategyInstanceInfo instance = ui->strategyPanel->strategyInstance(strategyId);
    startStrategyInstance(instance, false);
}

void MainWindow::onStopStrategyInstance(int strategyId)
{
    stopStrategyRuntime(strategyId);
}

void MainWindow::onStrategyInstanceSelectionChanged(int strategyId)
{
    m_activeRuntimeId = strategyId;
    updateSignalIndicators();
}

void MainWindow::onUpdateParameters()
{
    const int strategyId = ui->strategyPanel->currentStrategyInstanceId();
    if (strategyId <= 0) {
        return;
    }

    m_activeRuntimeId = strategyId;
    const StrategyInstanceInfo instance = ui->strategyPanel->strategyInstance(strategyId);
    StrategyRuntime* runtime = runtimeForStrategy(strategyId);
    if (runtime && !runtime->running) {
        runtime->accountIndex = instance.accountIndex;
        configureStrategyRuntime(runtime, instance.config);
        runtime->indicatorHistory.clear();
        runtime->hasLastMarketData = false;
    }
    if (m_signalPanel) {
        updateSignalIndicators();
    }
}
