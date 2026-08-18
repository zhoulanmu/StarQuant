#include "stockscreener.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>
#include <QUrlQuery>
#include <QTimer>

#include <algorithm>
#include <cmath>

namespace {
// Some networks close the connection to the primary push2 host before a
// response is sent.  The delayed host exposes the same documented response
// shape and is intended for delayed market snapshots, which is appropriate
// for this research-only screener.
constexpr auto StockListEndpoint = "https://push2delay.eastmoney.com/api/qt/clist/get";
// The endpoint caps a page at 100 rows.  Keep this in sync with the actual
// response size so pagination does not stop after the first page.
constexpr int StockListPageSize = 100;
constexpr int MaxPageRetries = 2;

double jsonNumber(const QJsonObject& object, const char* field)
{
    const QJsonValue value = object.value(QLatin1String(field));
    if (value.isDouble()) {
        return value.toDouble();
    }
    bool ok = false;
    const double number = value.toString().toDouble(&ok);
    return ok ? number : 0.0;
}

QString normalizedSymbol(const QString& code)
{
    if (code.startsWith(QLatin1Char('6'))) {
        return code + QStringLiteral(".SH");
    }
    if (code.startsWith(QLatin1Char('8')) || code.startsWith(QLatin1Char('4'))) {
        return code + QStringLiteral(".BJ");
    }
    return code + QStringLiteral(".SZ");
}

bool inRange(double value, double minimum, double maximum)
{
    return value >= minimum && value <= maximum;
}

QString numberText(double value, int decimals = 2)
{
    return QString::number(value, 'f', decimals);
}

double cappedSimilarity(double left, double right, double scale)
{
    return qBound(0.0, 1.0 - qAbs(left - right) / scale, 1.0);
}

double logarithmicSimilarity(double left, double right, double ratioLimit)
{
    if (left <= 0.0 || right <= 0.0) {
        return -1.0;
    }
    const double distance = qAbs(std::log(left / right));
    return qBound(0.0, 1.0 - distance / std::log(ratioLimit), 1.0);
}
}

StockScreener::StockScreener(QObject* parent)
    : QObject(parent)
    , m_network(new QNetworkAccessManager(this))
{
}

void StockScreener::run(const ScreenerCriteria& criteria)
{
    cancel();

    ++m_runGeneration;
    m_loading = true;
    m_activeCriteria = criteria;
    m_pendingUniverse.clear();
    m_requestedPage = 0;
    m_expectedUniverseSize = 0;
    m_retryCount = 0;
    emit loadingChanged(true);
    requestNextPage();
}

void StockScreener::requestNextPage()
{
    if (!m_loading || m_reply) {
        return;
    }

    ++m_requestedPage;

    QUrl url(QString::fromLatin1(StockListEndpoint));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("pn"), QString::number(m_requestedPage));
    query.addQueryItem(QStringLiteral("pz"), QString::number(StockListPageSize));
    query.addQueryItem(QStringLiteral("po"), QStringLiteral("1"));
    query.addQueryItem(QStringLiteral("np"), QStringLiteral("1"));
    query.addQueryItem(QStringLiteral("fltt"), QStringLiteral("2"));
    query.addQueryItem(QStringLiteral("invt"), QStringLiteral("2"));
    query.addQueryItem(QStringLiteral("fid"), QStringLiteral("f3"));
    query.addQueryItem(QStringLiteral("fs"), QStringLiteral("m:0+t:6,m:0+t:13,m:0+t:80,m:1+t:2,m:1+t:23"));
    query.addQueryItem(QStringLiteral("fields"), QStringLiteral("f12,f14,f2,f3,f5,f6,f8,f9,f20,f21,f23"));
    url.setQuery(query);

    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("Mozilla/5.0 StarQuant/1.0"));
    request.setRawHeader("Referer", "https://quote.eastmoney.com/");
    request.setRawHeader("Accept", "application/json, text/plain, */*");
    request.setRawHeader("Accept-Language", "zh-CN,zh;q=0.9");
    request.setAttribute(QNetworkRequest::Http2AllowedAttribute, false);
    request.setTransferTimeout(15000);
    m_reply = m_network->get(request);

    QNetworkReply* reply = m_reply;
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        finishReply(reply);
    });
}

void StockScreener::findSimilar(const QString& target, const ScreenerCriteria& baseCriteria)
{
    const QString query = target.trimmed();
    if (query.isEmpty()) {
        emit failed(QStringLiteral("请输入目标股票代码或名称。"));
        return;
    }
    if (m_lastUniverse.isEmpty()) {
        emit failed(QStringLiteral("正在等待市场数据，请先加载或重新运行选股。"));
        return;
    }

    const QString normalizedQuery = query.toUpper();
    const QString code = normalizedQuery.left(6);
    const auto isExactMatch = [&normalizedQuery, &code](const ScreenerStock& stock) {
        return stock.symbol.compare(normalizedQuery, Qt::CaseInsensitive) == 0
            || stock.symbol.left(6) == code
            || stock.name.compare(normalizedQuery, Qt::CaseInsensitive) == 0;
    };
    auto targetIt = std::find_if(m_lastUniverse.cbegin(), m_lastUniverse.cend(), isExactMatch);
    if (targetIt == m_lastUniverse.cend()) {
        emit failed(QStringLiteral("未找到目标“%1”。请使用 6 位股票代码或完整股票名称。").arg(query));
        return;
    }

    ScreenerCriteria candidatesCriteria = baseCriteria;
    candidatesCriteria.priceEnabled = false;
    candidatesCriteria.changeEnabled = false;
    candidatesCriteria.marketCapEnabled = false;
    candidatesCriteria.peEnabled = false;
    candidatesCriteria.pbEnabled = false;
    candidatesCriteria.turnoverRateEnabled = false;

    const ScreenerStock seed = *targetIt;
    QVector<ScreenerStock> results;
    for (const ScreenerStock& stock : m_lastUniverse) {
        if (stock.symbol == seed.symbol || !isInMarketScope(stock, candidatesCriteria.marketScope)
            || !passesCriteria(stock, candidatesCriteria)) {
            continue;
        }
        ScreenerStock candidate = stock;
        candidate.similarityScore = similarityScore(seed, candidate);
        candidate.hitReasons = similarityReasons(seed, candidate, candidate.similarityScore);
        results.append(candidate);
    }
    std::sort(results.begin(), results.end(), [](const ScreenerStock& left, const ScreenerStock& right) {
        return left.similarityScore > right.similarityScore;
    });
    if (results.size() > 30) {
        results.resize(30);
    }
    emit similarResultsReady(seed, results, m_lastDataAsOf);
}

void StockScreener::cancel()
{
    ++m_runGeneration;
    const bool wasLoading = m_loading;
    m_loading = false;
    m_pendingUniverse.clear();
    if (m_reply) {
        QNetworkReply* reply = m_reply;
        m_reply = nullptr;
        reply->abort();
        reply->deleteLater();
    }
    if (wasLoading) {
        emit loadingChanged(false);
    }
}

bool StockScreener::isRunning() const
{
    return m_loading;
}

bool StockScreener::hasMarketSnapshot() const
{
    return !m_lastUniverse.isEmpty();
}

void StockScreener::finishReply(QNetworkReply* reply)
{
    if (reply != m_reply) {
        reply->deleteLater();
        return;
    }

    m_reply = nullptr;

    if (reply->error() != QNetworkReply::NoError) {
        const QNetworkReply::NetworkError error = reply->error();
        const QString errorText = reply->errorString();
        reply->deleteLater();
        if (error == QNetworkReply::OperationCanceledError) {
            return;
        }
        if (m_retryCount < MaxPageRetries) {
            ++m_retryCount;
            --m_requestedPage;
            const quint64 requestGeneration = m_runGeneration;
            QTimer::singleShot(500 * m_retryCount, this, [this, requestGeneration]() {
                if (m_loading && m_runGeneration == requestGeneration) {
                    requestNextPage();
                }
            });
            return;
        }
        m_loading = false;
        emit loadingChanged(false);
        emit failed(QStringLiteral("选股数据获取失败：第 %1 页重试 %2 次后仍失败（%3）。请检查网络或稍后重试。")
                        .arg(m_requestedPage).arg(MaxPageRetries).arg(errorText));
        return;
    }

    const QJsonDocument document = QJsonDocument::fromJson(reply->readAll());
    reply->deleteLater();
    const QJsonObject data = document.object().value(QStringLiteral("data")).toObject();
    const QJsonArray rows = data.value(QStringLiteral("diff")).toArray();
    if (rows.isEmpty()) {
        m_loading = false;
        emit loadingChanged(false);
        emit failed(QStringLiteral("选股数据源未返回可用股票列表，请稍后重试。"));
        return;
    }

    m_retryCount = 0;
    m_expectedUniverseSize = qMax(m_expectedUniverseSize,
                                  static_cast<int>(jsonNumber(data, "total")));
    for (const QJsonValue& value : rows) {
        const QJsonObject row = value.toObject();
        const QString code = row.value(QStringLiteral("f12")).toString();
        const QString name = row.value(QStringLiteral("f14")).toString();
        if (code.size() != 6 || name.isEmpty()) {
            continue;
        }

        ScreenerStock stock;
        stock.symbol = normalizedSymbol(code);
        stock.name = name;
        stock.price = jsonNumber(row, "f2");
        stock.changePercent = jsonNumber(row, "f3");
        stock.amount = jsonNumber(row, "f6");
        stock.turnoverRate = jsonNumber(row, "f8");
        stock.pe = jsonNumber(row, "f9");
        stock.marketCap = jsonNumber(row, "f20");
        stock.pb = jsonNumber(row, "f23");
        m_pendingUniverse.append(stock);
    }

    const bool hasMoreRows = rows.size() == StockListPageSize
        && (m_expectedUniverseSize <= 0 || m_pendingUniverse.size() < m_expectedUniverseSize);
    if (hasMoreRows) {
        requestNextPage();
        return;
    }
    finishScreening();
}

void StockScreener::finishScreening()
{
    QVector<ScreenerStock> results;
    int universeCount = 0;
    for (ScreenerStock stock : m_pendingUniverse) {
        if (!isInMarketScope(stock, m_activeCriteria.marketScope)) {
            continue;
        }
        ++universeCount;
        if (!passesCriteria(stock, m_activeCriteria)) {
            continue;
        }
        stock.hitReasons = hitReasons(stock, m_activeCriteria);
        results.append(stock);
    }

    m_lastUniverse = m_pendingUniverse;
    m_pendingUniverse.clear();
    m_loading = false;
    m_lastDataAsOf = QDateTime::currentDateTime();
    std::sort(results.begin(), results.end(), [](const ScreenerStock& left, const ScreenerStock& right) {
        return left.changePercent > right.changePercent;
    });
    emit loadingChanged(false);
    emit resultsReady(results, universeCount, m_lastDataAsOf);
}

bool StockScreener::isInMarketScope(const ScreenerStock& stock, const QString& scope)
{
    const QString code = stock.symbol.left(6);
    if (scope == QStringLiteral("ALL")) {
        return true;
    }
    if (scope == QStringLiteral("SH_MAIN")) {
        return code.startsWith(QStringLiteral("60")) && !code.startsWith(QStringLiteral("688"));
    }
    if (scope == QStringLiteral("SZ_MAIN")) {
        return code.startsWith(QStringLiteral("000")) || code.startsWith(QStringLiteral("001"))
            || code.startsWith(QStringLiteral("002"));
    }
    if (scope == QStringLiteral("CHINEXT")) {
        return code.startsWith(QStringLiteral("300")) || code.startsWith(QStringLiteral("301"));
    }
    if (scope == QStringLiteral("STAR")) {
        return code.startsWith(QStringLiteral("688"));
    }
    if (scope == QStringLiteral("BSE")) {
        return stock.symbol.endsWith(QStringLiteral(".BJ"));
    }
    return false;
}

bool StockScreener::passesCriteria(const ScreenerStock& stock, const ScreenerCriteria& criteria)
{
    if (criteria.excludeSt && stock.name.contains(QStringLiteral("ST"), Qt::CaseInsensitive)) {
        return false;
    }
    if (criteria.excludeSuspended && stock.price <= 0.0) {
        return false;
    }
    if (criteria.priceEnabled && !inRange(stock.price, criteria.minPrice, criteria.maxPrice)) {
        return false;
    }
    if (criteria.changeEnabled && !inRange(stock.changePercent, criteria.minChange, criteria.maxChange)) {
        return false;
    }
    if (criteria.marketCapEnabled && !inRange(stock.marketCap / 100000000.0,
                                               criteria.minMarketCapYi, criteria.maxMarketCapYi)) {
        return false;
    }
    if (criteria.peEnabled && !inRange(stock.pe, criteria.minPe, criteria.maxPe)) {
        return false;
    }
    if (criteria.pbEnabled && !inRange(stock.pb, criteria.minPb, criteria.maxPb)) {
        return false;
    }
    if (criteria.turnoverRateEnabled
        && !inRange(stock.turnoverRate, criteria.minTurnoverRate, criteria.maxTurnoverRate)) {
        return false;
    }
    return true;
}

QStringList StockScreener::hitReasons(const ScreenerStock& stock, const ScreenerCriteria& criteria)
{
    QStringList reasons;
    if (criteria.priceEnabled) {
        reasons << QStringLiteral("价格 %1").arg(numberText(stock.price));
    }
    if (criteria.changeEnabled) {
        reasons << QStringLiteral("涨跌幅 %1%").arg(numberText(stock.changePercent));
    }
    if (criteria.marketCapEnabled) {
        reasons << QStringLiteral("市值 %1 亿").arg(numberText(stock.marketCap / 100000000.0));
    }
    if (criteria.peEnabled) {
        reasons << QStringLiteral("PE %1").arg(numberText(stock.pe));
    }
    if (criteria.pbEnabled) {
        reasons << QStringLiteral("PB %1").arg(numberText(stock.pb));
    }
    if (criteria.turnoverRateEnabled) {
        reasons << QStringLiteral("换手 %1%").arg(numberText(stock.turnoverRate));
    }
    if (reasons.isEmpty()) {
        reasons << QStringLiteral("符合股票池与基础过滤");
    }
    return reasons;
}

double StockScreener::similarityScore(const ScreenerStock& target, const ScreenerStock& candidate)
{
    struct Component { double score; double weight; };
    const QVector<Component> components = {
        {logarithmicSimilarity(target.marketCap, candidate.marketCap, 20.0), 0.35},
        {logarithmicSimilarity(target.pe, candidate.pe, 10.0), 0.20},
        {logarithmicSimilarity(target.pb, candidate.pb, 10.0), 0.15},
        {cappedSimilarity(target.turnoverRate, candidate.turnoverRate, 15.0), 0.15},
        {logarithmicSimilarity(target.amount, candidate.amount, 30.0), 0.10},
        {cappedSimilarity(target.changePercent, candidate.changePercent, 10.0), 0.05}
    };
    double weightedScore = 0.0;
    double availableWeight = 0.0;
    for (const Component& component : components) {
        if (component.score < 0.0) {
            continue;
        }
        weightedScore += component.score * component.weight;
        availableWeight += component.weight;
    }
    return availableWeight > 0.0 ? weightedScore / availableWeight * 100.0 : 0.0;
}

QStringList StockScreener::similarityReasons(const ScreenerStock& target, const ScreenerStock& candidate,
                                             double score)
{
    QStringList reasons;
    reasons << QStringLiteral("相似度 %1%").arg(numberText(score, 1));
    reasons << QStringLiteral("市值 %1 亿（目标 %2 亿）")
                   .arg(numberText(candidate.marketCap / 100000000.0),
                        numberText(target.marketCap / 100000000.0));
    if (target.pe > 0.0 && candidate.pe > 0.0) {
        reasons << QStringLiteral("PE %1（目标 %2）").arg(numberText(candidate.pe), numberText(target.pe));
    }
    if (target.pb > 0.0 && candidate.pb > 0.0) {
        reasons << QStringLiteral("PB %1（目标 %2）").arg(numberText(candidate.pb), numberText(target.pb));
    }
    reasons << QStringLiteral("换手率 %1%（目标 %2%）")
                   .arg(numberText(candidate.turnoverRate), numberText(target.turnoverRate));
    return reasons;
}
