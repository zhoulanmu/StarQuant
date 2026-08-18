#pragma once

#include <QDateTime>
#include <QObject>
#include <QStringList>
#include <QVector>

class QNetworkAccessManager;
class QNetworkReply;

struct ScreenerCriteria
{
    QString marketScope = QStringLiteral("ALL");
    bool excludeSt = true;
    bool excludeSuspended = true;

    bool priceEnabled = false;
    double minPrice = 0.0;
    double maxPrice = 99999.0;
    bool changeEnabled = false;
    double minChange = -100.0;
    double maxChange = 100.0;
    bool marketCapEnabled = false;
    double minMarketCapYi = 0.0;
    double maxMarketCapYi = 999999.0;
    bool peEnabled = false;
    double minPe = -9999.0;
    double maxPe = 9999.0;
    bool pbEnabled = false;
    double minPb = 0.0;
    double maxPb = 9999.0;
    bool turnoverRateEnabled = false;
    double minTurnoverRate = 0.0;
    double maxTurnoverRate = 100.0;
};

struct ScreenerStock
{
    QString symbol;
    QString name;
    double price = 0.0;
    double changePercent = 0.0;
    double amount = 0.0;
    double turnoverRate = 0.0;
    double marketCap = 0.0;
    double pe = 0.0;
    double pb = 0.0;
    double similarityScore = -1.0;
    QStringList hitReasons;
};

class StockScreener : public QObject
{
    Q_OBJECT

public:
    explicit StockScreener(QObject* parent = nullptr);

    void run(const ScreenerCriteria& criteria);
    void findSimilar(const QString& target, const ScreenerCriteria& baseCriteria);
    void cancel();
    bool isRunning() const;
    bool hasMarketSnapshot() const;

signals:
    void loadingChanged(bool loading);
    void resultsReady(const QVector<ScreenerStock>& results, int universeCount,
                      const QDateTime& dataAsOf);
    void similarResultsReady(const ScreenerStock& target, const QVector<ScreenerStock>& results,
                             const QDateTime& dataAsOf);
    void failed(const QString& message);

private:
    void requestNextPage();
    void finishReply(QNetworkReply* reply);
    void finishScreening();
    static bool isInMarketScope(const ScreenerStock& stock, const QString& scope);
    static bool passesCriteria(const ScreenerStock& stock, const ScreenerCriteria& criteria);
    static QStringList hitReasons(const ScreenerStock& stock, const ScreenerCriteria& criteria);
    static double similarityScore(const ScreenerStock& target, const ScreenerStock& candidate);
    static QStringList similarityReasons(const ScreenerStock& target, const ScreenerStock& candidate,
                                         double score);

    QNetworkAccessManager* m_network;
    QNetworkReply* m_reply = nullptr;
    ScreenerCriteria m_activeCriteria;
    QVector<ScreenerStock> m_pendingUniverse;
    int m_requestedPage = 0;
    int m_expectedUniverseSize = 0;
    int m_retryCount = 0;
    quint64 m_runGeneration = 0;
    bool m_loading = false;
    QVector<ScreenerStock> m_lastUniverse;
    QDateTime m_lastDataAsOf;
};
