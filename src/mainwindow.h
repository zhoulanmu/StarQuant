#pragma once

#include <QMainWindow>
#include <QMap>
#include <QString>
#include <QStringList>
#include <QVector>
#include "market/marketdata.h"
#include "strategy/strategybase.h"
#include "ui/accountpanel.h"
#include "ui/statisticspanel.h"
#include "ui/signalsignalpanel.h"
#include "ui/newspanel.h"
#include "ui/screenerpanel.h"

class QTabWidget;
class QWidget;
class QDoubleSpinBox;
class QTimer;
struct StrategyInstanceInfo;

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(bool guestMode = true, const QString& accountName = QString(), QWidget *parent = nullptr);
    ~MainWindow();

private slots:
    void onMarketDataUpdated(const MarketData& data);
    void onIntradayDataUpdated(const QVector<MarketData>& data);
    void onFallbackIntradayDataUsed(const QString& symbol, const QString& reason);
    void onMarketDataError(const QString& message);
    void onStrategyMarketDataUpdated(int strategyId, const MarketData& data);
    void onStrategyIntradayDataUpdated(int strategyId, const QVector<MarketData>& data);
    void onStrategyMarketDataError(int strategyId, const QString& message);
    void onStrategySignal(int strategyId, const StrategySignal& signal);
    void onStartStrategy();
    void onStopStrategy();
    void onStartStrategyInstance(int strategyId);
    void onStopStrategyInstance(int strategyId);
    void onStrategyInstanceSelectionChanged(int strategyId);
    void onUpdateParameters();
    void onViewSymbolChanged(const QString& symbol);
    void onFavoriteSelected(const QString& symbol);
    void onFavoriteBuyRequested(const QString& symbol, double price, double volume);
    void onFavoriteSellRequested(const QString& symbol, double price, double volume);
    void onManualTradeQuoteUpdated(const MarketData& data);
    void onManualTradeQuoteError(const QString& message);
    void onScreenerViewStock(const QString& symbol, const QString& name);
    void onScreenerAddFavorite(const QString& symbol, const QString& name);
    void showLegalInformation();

private:
    struct StrategyRuntime;
    void updateAccountInfo();
    void updateAccountInfo(int accountIndex);
    void loadAccountState();
    void saveAccountState() const;
    void appendTradeRecord(int accountIndex, const QString& symbol, const QString& type, double price, double volume, double amount, double fee, const QString& time);
    void executeOrder(int strategyId, const StrategySignal& signal);
    void buildTabbedLayout();
    QWidget* createMainTab();
    QWidget* createStrategyTab();
    QWidget* createPersonalTab();
    QWidget* createNewsTab();
    QWidget* createScreenerTab();
    void configureStrategyRuntime(StrategyRuntime* runtime, const StrategyConfig& config);
    void updateSignalIndicators();
    void resetStrategyProgressTracking(StrategyRuntime* runtime, const StrategyConfig& config);
    void appendStrategyProgressLog(StrategyRuntime* runtime);
    void updatePositionsPrice(const QString& symbol, double price);
    StrategyRuntime* runtimeForStrategy(int strategyId) const;
    QString strategyRuntimeLogLabel(const StrategyRuntime* runtime) const;
    QString strategyInstanceLogLabel(const StrategyInstanceInfo& instance) const;
    int strategyDisplayIndex(int strategyId) const;
    StrategyRuntime* ensureRuntime(const StrategyInstanceInfo& instance);
    void startStrategyRuntimeNow(StrategyRuntime* runtime, bool fromBatch);
    void startStrategyInstance(const StrategyInstanceInfo& instance, bool fromBatch);
    void stopStrategyRuntime(int strategyId);
    bool hasRunningStrategyForAccount(int accountIndex, int exceptStrategyId = 0) const;
    void refreshStrategyRunningState();
    void startWaitingStrategiesIfReady();
    void transferAccountCash(int accountIndex, double amount);
    void resetAccountAssets(int accountIndex);
    QStringList accountNames() const;
    double latestPriceForSymbol(const QString& symbol) const;
    void requestManualTrade(const QString& symbol, SignalType type, double price, double volume);
    void executeManualTrade(const QString& symbol, SignalType type, double price, double volume);

private:
    Ui::MainWindow *ui;
    MarketDataSimulator* m_marketData;
    MarketDataSimulator* m_manualTradeMarketData;
    AccountPanel* m_accountPanel;
    StatisticsPanel* m_statisticsPanel;
    SignalPanel* m_signalPanel;
    NewsPanel* m_newsPanel;
    ScreenerPanel* m_screenerPanel;
    QTabWidget* m_mainTabs;
    QTabWidget* m_accountTabs;
    QTimer* m_strategyStartTimer;
    bool m_isRunning;
    struct TradeRecordInfo {
        QString time;
        QString symbol;
        QString type;
        double price = 0.0;
        double volume = 0.0;
        double amount = 0.0;
        double fee = 0.0;
    };

    struct AccountState {
        QString name;
        double initialCapital = 100000.0;
        double currentCash = 100000.0;
        QMap<QString, PositionInfo> positions;
        QVector<TradeRecordInfo> tradeRecords;
    };

    struct StrategyRuntime {
        int id = 0;
        int accountIndex = -1;
        StrategyConfig config;
        MarketDataSimulator* marketData = nullptr;
        StrategyBase* strategy = nullptr;
        StrategyType activeStrategyType = StrategyType::DoubleMA;
        bool running = false;
        bool waiting = false;
        MarketData lastMarketData;
        bool hasLastMarketData = false;
        QVector<MarketData> indicatorHistory;
        QVector<double> closeSamples;
        int lastProgressSampleLogged = 0;
        int lastMonitorSampleLogged = 0;
        bool firstQuoteLogged = false;
        bool monitorEntered = false;
        bool tradeTriggeredOnTick = false;
    };

    QVector<AccountState> m_accounts;
    QVector<AccountPanel*> m_accountPanels;
    QVector<StrategyRuntime*> m_strategyRuntimes;
    int m_activeAccountIndex;
    int m_activeRuntimeId;
    double m_currentPrice;
    MarketData m_lastMarketData;
    bool m_hasLastMarketData;
    MarketData m_lastStrategyMarketData;
    bool m_hasLastStrategyMarketData;
    QDateTime m_lastMarketDataErrorLoggedAt;
    QString m_lastMarketDataErrorMessage;
    int m_lastStrategyProgressSampleLogged;
    int m_lastStrategyMonitorSampleLogged;
    bool m_strategyFirstQuoteLogged;
    bool m_strategyMonitorEntered;
    bool m_strategyTradeTriggeredOnTick;
    QString m_pendingManualTradeSymbol;
    QString m_manualPriceQuoteSymbol;
    SignalType m_pendingManualTradeType;
    double m_pendingManualTradeVolume;
    bool m_guestMode;
    QString m_accountName;
};
