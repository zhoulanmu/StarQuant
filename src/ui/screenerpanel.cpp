#include "screenerpanel.h"

#include <QCheckBox>
#include <QAbstractItemView>
#include <QColor>
#include <QComboBox>
#include <QDateTime>
#include <QDoubleSpinBox>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>

namespace {
constexpr auto SavedStrategiesKey = "screener/savedStrategies";

QDoubleSpinBox* createSpinBox(QWidget* parent, double minimum, double maximum, int decimals)
{
    auto* spin = new QDoubleSpinBox(parent);
    spin->setRange(minimum, maximum);
    spin->setDecimals(decimals);
    spin->setSingleStep(decimals == 0 ? 1.0 : 0.1);
    spin->setKeyboardTracking(false);
    spin->setEnabled(false);
    return spin;
}

QString displayAmount(double amount)
{
    return QString::number(amount / 100000000.0, 'f', 2) + QStringLiteral(" 亿");
}
}

ScreenerPanel::ScreenerPanel(QWidget* parent)
    : QWidget(parent)
    , m_screener(new StockScreener(this))
{
    auto* rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(10, 10, 10, 10);
    rootLayout->setSpacing(10);

    auto* toolbar = new QHBoxLayout();
    auto* scopeLabel = new QLabel(QStringLiteral("股票池"), this);
    m_marketScope = new QComboBox(this);
    m_marketScope->addItem(QStringLiteral("全 A 股"), QStringLiteral("ALL"));
    m_marketScope->addItem(QStringLiteral("沪市主板"), QStringLiteral("SH_MAIN"));
    m_marketScope->addItem(QStringLiteral("深市主板"), QStringLiteral("SZ_MAIN"));
    m_marketScope->addItem(QStringLiteral("创业板"), QStringLiteral("CHINEXT"));
    m_marketScope->addItem(QStringLiteral("科创板"), QStringLiteral("STAR"));
    m_marketScope->addItem(QStringLiteral("北交所"), QStringLiteral("BSE"));
    m_excludeSt = new QCheckBox(QStringLiteral("排除 ST/*ST"), this);
    m_excludeSt->setChecked(true);
    m_excludeSuspended = new QCheckBox(QStringLiteral("排除停牌"), this);
    m_excludeSuspended->setChecked(true);
    toolbar->addWidget(scopeLabel);
    toolbar->addWidget(m_marketScope);
    toolbar->addWidget(m_excludeSt);
    toolbar->addWidget(m_excludeSuspended);
    toolbar->addStretch();
    rootLayout->addLayout(toolbar);

    auto* similarBox = new QGroupBox(QStringLiteral("相似标的"), this);
    auto* similarLayout = new QHBoxLayout(similarBox);
    similarLayout->addWidget(new QLabel(QStringLiteral("目标股票"), similarBox));
    m_similarStockEdit = new QLineEdit(similarBox);
    m_similarStockEdit->setPlaceholderText(QStringLiteral("输入 6 位代码或完整名称，例如 600519 / 贵州茅台"));
    m_findSimilarButton = new QPushButton(QStringLiteral("查找相似标的"), similarBox);
    similarLayout->addWidget(m_similarStockEdit, 1);
    similarLayout->addWidget(m_findSimilarButton);
    rootLayout->addWidget(similarBox);

    auto* conditionBox = new QGroupBox(QStringLiteral("筛选条件（同组条件均为“且”）"), this);
    auto* conditions = new QGridLayout(conditionBox);
    conditions->setColumnStretch(4, 1);
    m_priceRange = addRangeControl(conditions, 0, QStringLiteral("最新价"), QStringLiteral("元"), 0.0, 100000.0);
    m_changeRange = addRangeControl(conditions, 1, QStringLiteral("涨跌幅"), QStringLiteral("%"), -100.0, 100.0);
    m_marketCapRange = addRangeControl(conditions, 2, QStringLiteral("总市值"), QStringLiteral("亿元"), 0.0, 10000000.0);
    m_peRange = addRangeControl(conditions, 3, QStringLiteral("PE(TTM)"), QString(), -9999.0, 9999.0);
    m_pbRange = addRangeControl(conditions, 4, QStringLiteral("PB"), QString(), 0.0, 9999.0);
    m_turnoverRange = addRangeControl(conditions, 5, QStringLiteral("换手率"), QStringLiteral("%"), 0.0, 100.0);
    rootLayout->addWidget(conditionBox);

    auto* actionLayout = new QHBoxLayout();
    actionLayout->addWidget(new QLabel(QStringLiteral("快捷策略"), this));
    m_templateCombo = new QComboBox(this);
    m_templateCombo->addItem(QStringLiteral("趋势突破（涨幅 ≥ 2%，换手率 ≥ 3%）"));
    m_templateCombo->addItem(QStringLiteral("低估值质量（PE 0-20，PB ≤ 2）"));
    m_templateCombo->addItem(QStringLiteral("活跃小中盘（市值 50-1000 亿，换手率 ≥ 2%）"));
    actionLayout->addWidget(m_templateCombo, 1);
    auto* templateButton = new QPushButton(QStringLiteral("应用模板"), this);
    actionLayout->addWidget(templateButton);
    actionLayout->addSpacing(16);
    m_runButton = new QPushButton(QStringLiteral("运行选股"), this);
    m_runButton->setDefault(true);
    auto* resetButton = new QPushButton(QStringLiteral("重置"), this);
    m_cancelButton = new QPushButton(QStringLiteral("取消"), this);
    m_cancelButton->setEnabled(false);
    actionLayout->addWidget(m_runButton);
    actionLayout->addWidget(resetButton);
    actionLayout->addWidget(m_cancelButton);
    rootLayout->addLayout(actionLayout);

    auto* strategyLayout = new QHBoxLayout();
    auto* saveButton = new QPushButton(QStringLiteral("保存当前策略"), this);
    m_savedStrategyCombo = new QComboBox(this);
    m_savedStrategyCombo->setMinimumWidth(220);
    auto* loadButton = new QPushButton(QStringLiteral("加载"), this);
    auto* deleteButton = new QPushButton(QStringLiteral("删除"), this);
    strategyLayout->addWidget(saveButton);
    strategyLayout->addWidget(new QLabel(QStringLiteral("已保存策略"), this));
    strategyLayout->addWidget(m_savedStrategyCombo);
    strategyLayout->addWidget(loadButton);
    strategyLayout->addWidget(deleteButton);
    strategyLayout->addStretch();
    rootLayout->addLayout(strategyLayout);

    m_statusLabel = new QLabel(QStringLiteral("请选择条件后运行；数据来自公开行情源，筛选结果不构成投资建议。"), this);
    m_statusLabel->setWordWrap(true);
    rootLayout->addWidget(m_statusLabel);

    m_resultTable = new QTableWidget(this);
    m_resultTable->setColumnCount(11);
    m_resultTable->setHorizontalHeaderLabels({QStringLiteral("代码"), QStringLiteral("名称"), QStringLiteral("最新价"),
                                               QStringLiteral("相似度"), QStringLiteral("涨跌幅"), QStringLiteral("总市值"), QStringLiteral("PE"),
                                               QStringLiteral("PB"), QStringLiteral("换手率"), QStringLiteral("成交额"),
                                               QStringLiteral("命中依据")});
    m_resultTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_resultTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_resultTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_resultTable->setAlternatingRowColors(true);
    m_resultTable->verticalHeader()->setVisible(false);
    m_resultTable->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    m_resultTable->horizontalHeader()->setStretchLastSection(true);
    rootLayout->addWidget(m_resultTable, 1);

    auto* resultActions = new QHBoxLayout();
    auto* viewButton = new QPushButton(QStringLiteral("查看行情"), this);
    auto* addFavoriteButton = new QPushButton(QStringLiteral("加入自选"), this);
    resultActions->addWidget(viewButton);
    resultActions->addWidget(addFavoriteButton);
    resultActions->addStretch();
    rootLayout->addLayout(resultActions);

    connect(m_screener, &StockScreener::loadingChanged, this, &ScreenerPanel::setLoading);
    connect(m_screener, &StockScreener::resultsReady, this, &ScreenerPanel::updateResults);
    connect(m_screener, &StockScreener::similarResultsReady, this, &ScreenerPanel::updateSimilarResults);
    connect(m_screener, &StockScreener::failed, this, &ScreenerPanel::showFailure);
    connect(m_runButton, &QPushButton::clicked, this, &ScreenerPanel::runScreening);
    connect(m_findSimilarButton, &QPushButton::clicked, this, &ScreenerPanel::findSimilarStock);
    connect(m_similarStockEdit, &QLineEdit::returnPressed, this, &ScreenerPanel::findSimilarStock);
    connect(m_cancelButton, &QPushButton::clicked, m_screener, &StockScreener::cancel);
    connect(resetButton, &QPushButton::clicked, this, &ScreenerPanel::resetCriteria);
    connect(templateButton, &QPushButton::clicked, this, &ScreenerPanel::applyTemplate);
    connect(saveButton, &QPushButton::clicked, this, &ScreenerPanel::saveStrategy);
    connect(loadButton, &QPushButton::clicked, this, &ScreenerPanel::loadStrategy);
    connect(deleteButton, &QPushButton::clicked, this, &ScreenerPanel::deleteStrategy);
    connect(viewButton, &QPushButton::clicked, this, &ScreenerPanel::viewSelectedStock);
    connect(addFavoriteButton, &QPushButton::clicked, this, &ScreenerPanel::addSelectedStockToFavorite);
    connect(m_resultTable, &QTableWidget::cellDoubleClicked, this, [this](int, int) { viewSelectedStock(); });

    refreshSavedStrategies();
}

ScreenerPanel::RangeWidgets ScreenerPanel::addRangeControl(QGridLayout* layout, int row, const QString& label,
                                                            const QString& suffix, double minimum, double maximum,
                                                            int decimals)
{
    RangeWidgets controls;
    controls.enabled = new QCheckBox(label, this);
    controls.minimum = createSpinBox(this, minimum, maximum, decimals);
    controls.maximum = createSpinBox(this, minimum, maximum, decimals);
    controls.minimum->setValue(minimum);
    controls.maximum->setValue(maximum);
    layout->addWidget(controls.enabled, row, 0);
    layout->addWidget(new QLabel(QStringLiteral("最小"), this), row, 1);
    layout->addWidget(controls.minimum, row, 2);
    layout->addWidget(new QLabel(QStringLiteral("最大"), this), row, 3);
    layout->addWidget(controls.maximum, row, 4);
    if (!suffix.isEmpty()) {
        layout->addWidget(new QLabel(suffix, this), row, 5);
    }
    connect(controls.enabled, &QCheckBox::toggled, this, [this, controls](bool) { updateRangeState(controls); });
    return controls;
}

void ScreenerPanel::updateRangeState(const RangeWidgets& widgets)
{
    const bool enabled = widgets.enabled->isChecked();
    widgets.minimum->setEnabled(enabled);
    widgets.maximum->setEnabled(enabled);
}

ScreenerCriteria ScreenerPanel::currentCriteria() const
{
    ScreenerCriteria criteria;
    criteria.marketScope = m_marketScope->currentData().toString();
    criteria.excludeSt = m_excludeSt->isChecked();
    criteria.excludeSuspended = m_excludeSuspended->isChecked();
    const auto setRange = [](const RangeWidgets& widgets, bool& enabled, double& minimum, double& maximum) {
        enabled = widgets.enabled->isChecked();
        minimum = widgets.minimum->value();
        maximum = widgets.maximum->value();
    };
    setRange(m_priceRange, criteria.priceEnabled, criteria.minPrice, criteria.maxPrice);
    setRange(m_changeRange, criteria.changeEnabled, criteria.minChange, criteria.maxChange);
    setRange(m_marketCapRange, criteria.marketCapEnabled, criteria.minMarketCapYi, criteria.maxMarketCapYi);
    setRange(m_peRange, criteria.peEnabled, criteria.minPe, criteria.maxPe);
    setRange(m_pbRange, criteria.pbEnabled, criteria.minPb, criteria.maxPb);
    setRange(m_turnoverRange, criteria.turnoverRateEnabled, criteria.minTurnoverRate, criteria.maxTurnoverRate);
    return criteria;
}

void ScreenerPanel::applyCriteria(const ScreenerCriteria& criteria)
{
    const int scope = m_marketScope->findData(criteria.marketScope);
    m_marketScope->setCurrentIndex(scope >= 0 ? scope : 0);
    m_excludeSt->setChecked(criteria.excludeSt);
    m_excludeSuspended->setChecked(criteria.excludeSuspended);
    const auto setRange = [this](const RangeWidgets& widgets, bool enabled, double minimum, double maximum) {
        widgets.enabled->setChecked(enabled);
        widgets.minimum->setValue(minimum);
        widgets.maximum->setValue(maximum);
        updateRangeState(widgets);
    };
    setRange(m_priceRange, criteria.priceEnabled, criteria.minPrice, criteria.maxPrice);
    setRange(m_changeRange, criteria.changeEnabled, criteria.minChange, criteria.maxChange);
    setRange(m_marketCapRange, criteria.marketCapEnabled, criteria.minMarketCapYi, criteria.maxMarketCapYi);
    setRange(m_peRange, criteria.peEnabled, criteria.minPe, criteria.maxPe);
    setRange(m_pbRange, criteria.pbEnabled, criteria.minPb, criteria.maxPb);
    setRange(m_turnoverRange, criteria.turnoverRateEnabled, criteria.minTurnoverRate, criteria.maxTurnoverRate);
}

void ScreenerPanel::runScreening()
{
    const ScreenerCriteria criteria = currentCriteria();
    const auto invalidRange = [](bool enabled, double minimum, double maximum) { return enabled && minimum > maximum; };
    if (invalidRange(criteria.priceEnabled, criteria.minPrice, criteria.maxPrice)
        || invalidRange(criteria.changeEnabled, criteria.minChange, criteria.maxChange)
        || invalidRange(criteria.marketCapEnabled, criteria.minMarketCapYi, criteria.maxMarketCapYi)
        || invalidRange(criteria.peEnabled, criteria.minPe, criteria.maxPe)
        || invalidRange(criteria.pbEnabled, criteria.minPb, criteria.maxPb)
        || invalidRange(criteria.turnoverRateEnabled, criteria.minTurnoverRate, criteria.maxTurnoverRate)) {
        QMessageBox::warning(this, QStringLiteral("条件无效"), QStringLiteral("每个条件的最小值不能大于最大值。"));
        return;
    }
    m_statusLabel->setText(QStringLiteral("正在加载全市场行情并执行筛选…"));
    m_screener->run(criteria);
}

void ScreenerPanel::resetCriteria()
{
    applyCriteria(ScreenerCriteria());
    m_statusLabel->setText(QStringLiteral("已重置筛选条件。"));
}

void ScreenerPanel::applyTemplate()
{
    ScreenerCriteria criteria;
    const int index = m_templateCombo->currentIndex();
    if (index == 0) {
        criteria.changeEnabled = true;
        criteria.minChange = 2.0;
        criteria.maxChange = 100.0;
        criteria.turnoverRateEnabled = true;
        criteria.minTurnoverRate = 3.0;
        criteria.maxTurnoverRate = 100.0;
    } else if (index == 1) {
        criteria.peEnabled = true;
        criteria.minPe = 0.0;
        criteria.maxPe = 20.0;
        criteria.pbEnabled = true;
        criteria.minPb = 0.0;
        criteria.maxPb = 2.0;
    } else {
        criteria.marketCapEnabled = true;
        criteria.minMarketCapYi = 50.0;
        criteria.maxMarketCapYi = 1000.0;
        criteria.turnoverRateEnabled = true;
        criteria.minTurnoverRate = 2.0;
        criteria.maxTurnoverRate = 100.0;
    }
    applyCriteria(criteria);
    m_statusLabel->setText(QStringLiteral("已应用快捷策略，请点击“运行选股”。"));
}

void ScreenerPanel::setLoading(bool loading)
{
    m_runButton->setEnabled(!loading);
    m_findSimilarButton->setEnabled(!loading);
    m_cancelButton->setEnabled(loading);
}

void ScreenerPanel::updateResults(const QVector<ScreenerStock>& results, int universeCount, const QDateTime& dataAsOf)
{
    if (!m_pendingSimilarQuery.isEmpty()) {
        const QString query = m_pendingSimilarQuery;
        const ScreenerCriteria criteria = m_pendingSimilarCriteria;
        m_pendingSimilarQuery.clear();
        m_screener->findSimilar(query, criteria);
        return;
    }
    populateResults(results, false);
    m_statusLabel->setText(QStringLiteral("股票池 %1 只，命中 %2 只；数据截至 %3。")
                               .arg(universeCount)
                               .arg(results.size())
                               .arg(dataAsOf.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"))));
}

void ScreenerPanel::updateSimilarResults(const ScreenerStock& target, const QVector<ScreenerStock>& results,
                                         const QDateTime& dataAsOf)
{
    populateResults(results, true);
    m_statusLabel->setText(QStringLiteral("以 %1（%2）为目标，返回 %3 只相似标的；数据截至 %4。")
                               .arg(target.name, target.symbol)
                               .arg(results.size())
                               .arg(dataAsOf.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"))));
}

void ScreenerPanel::populateResults(const QVector<ScreenerStock>& results, bool similarMode)
{
    m_results = results;
    m_resultTable->setRowCount(results.size());
    for (int row = 0; row < results.size(); ++row) {
        const ScreenerStock& stock = results.at(row);
        const QStringList values = {stock.symbol, stock.name,
                                    QString::number(stock.price, 'f', 2),
                                    similarMode ? QString::number(stock.similarityScore, 'f', 1) + QStringLiteral("%") : QStringLiteral("--"),
                                    QString::number(stock.changePercent, 'f', 2) + QStringLiteral("%"),
                                    displayAmount(stock.marketCap), QString::number(stock.pe, 'f', 2),
                                    QString::number(stock.pb, 'f', 2),
                                    QString::number(stock.turnoverRate, 'f', 2) + QStringLiteral("%"),
                                    displayAmount(stock.amount), stock.hitReasons.join(QStringLiteral("；"))};
        for (int column = 0; column < values.size(); ++column) {
            auto* item = new QTableWidgetItem(values.at(column));
            if (column == 0) {
                item->setData(Qt::UserRole, row);
            }
            if (column == 3 && similarMode) {
                item->setForeground(QColor(QStringLiteral("#63b3ed")));
            }
            if (column == 4) {
                item->setForeground(stock.changePercent >= 0.0 ? QColor(QStringLiteral("#d9534f"))
                                                               : QColor(QStringLiteral("#35a16b")));
            }
            m_resultTable->setItem(row, column, item);
        }
    }
}

void ScreenerPanel::showFailure(const QString& message)
{
    m_pendingSimilarQuery.clear();
    m_statusLabel->setText(message);
    QMessageBox::warning(this, QStringLiteral("选股失败"), message);
}

void ScreenerPanel::findSimilarStock()
{
    const QString query = m_similarStockEdit->text().trimmed();
    if (query.isEmpty()) {
        QMessageBox::information(this, QStringLiteral("相似标的"), QStringLiteral("请输入目标股票代码或完整名称。"));
        return;
    }
    const ScreenerCriteria criteria = currentCriteria();
    if (!m_screener->hasMarketSnapshot()) {
        m_pendingSimilarQuery = query;
        m_pendingSimilarCriteria = criteria;
        m_statusLabel->setText(QStringLiteral("正在加载市场快照，随后查找“%1”的相似标的…").arg(query));
        m_screener->run(criteria);
        return;
    }
    m_screener->findSimilar(query, criteria);
}

ScreenerStock ScreenerPanel::selectedStock() const
{
    const int row = m_resultTable->currentRow();
    return row >= 0 && row < m_results.size() ? m_results.at(row) : ScreenerStock();
}

void ScreenerPanel::viewSelectedStock()
{
    const ScreenerStock stock = selectedStock();
    if (stock.symbol.isEmpty()) {
        return;
    }
    emit viewStockRequested(stock.symbol, stock.name);
}

void ScreenerPanel::addSelectedStockToFavorite()
{
    const ScreenerStock stock = selectedStock();
    if (stock.symbol.isEmpty()) {
        return;
    }
    emit addFavoriteRequested(stock.symbol, stock.name);
    m_statusLabel->setText(QStringLiteral("已请求将 %1 加入自选。") .arg(stock.name));
}

QJsonObject ScreenerPanel::criteriaToJson(const ScreenerCriteria& criteria)
{
    QJsonObject object;
    object.insert(QStringLiteral("marketScope"), criteria.marketScope);
    object.insert(QStringLiteral("excludeSt"), criteria.excludeSt);
    object.insert(QStringLiteral("excludeSuspended"), criteria.excludeSuspended);
    object.insert(QStringLiteral("priceEnabled"), criteria.priceEnabled);
    object.insert(QStringLiteral("minPrice"), criteria.minPrice);
    object.insert(QStringLiteral("maxPrice"), criteria.maxPrice);
    object.insert(QStringLiteral("changeEnabled"), criteria.changeEnabled);
    object.insert(QStringLiteral("minChange"), criteria.minChange);
    object.insert(QStringLiteral("maxChange"), criteria.maxChange);
    object.insert(QStringLiteral("marketCapEnabled"), criteria.marketCapEnabled);
    object.insert(QStringLiteral("minMarketCapYi"), criteria.minMarketCapYi);
    object.insert(QStringLiteral("maxMarketCapYi"), criteria.maxMarketCapYi);
    object.insert(QStringLiteral("peEnabled"), criteria.peEnabled);
    object.insert(QStringLiteral("minPe"), criteria.minPe);
    object.insert(QStringLiteral("maxPe"), criteria.maxPe);
    object.insert(QStringLiteral("pbEnabled"), criteria.pbEnabled);
    object.insert(QStringLiteral("minPb"), criteria.minPb);
    object.insert(QStringLiteral("maxPb"), criteria.maxPb);
    object.insert(QStringLiteral("turnoverRateEnabled"), criteria.turnoverRateEnabled);
    object.insert(QStringLiteral("minTurnoverRate"), criteria.minTurnoverRate);
    object.insert(QStringLiteral("maxTurnoverRate"), criteria.maxTurnoverRate);
    return object;
}

ScreenerCriteria ScreenerPanel::criteriaFromJson(const QJsonObject& object)
{
    ScreenerCriteria criteria;
    const auto boolValue = [&object](const char* key, bool fallback) { return object.value(QLatin1String(key)).toBool(fallback); };
    const auto doubleValue = [&object](const char* key, double fallback) {
        const QJsonValue value = object.value(QLatin1String(key));
        return value.isDouble() ? value.toDouble() : fallback;
    };
    criteria.marketScope = object.value(QStringLiteral("marketScope")).toString(criteria.marketScope);
    criteria.excludeSt = boolValue("excludeSt", criteria.excludeSt);
    criteria.excludeSuspended = boolValue("excludeSuspended", criteria.excludeSuspended);
    criteria.priceEnabled = boolValue("priceEnabled", false);
    criteria.minPrice = doubleValue("minPrice", criteria.minPrice);
    criteria.maxPrice = doubleValue("maxPrice", criteria.maxPrice);
    criteria.changeEnabled = boolValue("changeEnabled", false);
    criteria.minChange = doubleValue("minChange", criteria.minChange);
    criteria.maxChange = doubleValue("maxChange", criteria.maxChange);
    criteria.marketCapEnabled = boolValue("marketCapEnabled", false);
    criteria.minMarketCapYi = doubleValue("minMarketCapYi", criteria.minMarketCapYi);
    criteria.maxMarketCapYi = doubleValue("maxMarketCapYi", criteria.maxMarketCapYi);
    criteria.peEnabled = boolValue("peEnabled", false);
    criteria.minPe = doubleValue("minPe", criteria.minPe);
    criteria.maxPe = doubleValue("maxPe", criteria.maxPe);
    criteria.pbEnabled = boolValue("pbEnabled", false);
    criteria.minPb = doubleValue("minPb", criteria.minPb);
    criteria.maxPb = doubleValue("maxPb", criteria.maxPb);
    criteria.turnoverRateEnabled = boolValue("turnoverRateEnabled", false);
    criteria.minTurnoverRate = doubleValue("minTurnoverRate", criteria.minTurnoverRate);
    criteria.maxTurnoverRate = doubleValue("maxTurnoverRate", criteria.maxTurnoverRate);
    return criteria;
}

void ScreenerPanel::refreshSavedStrategies()
{
    const QString current = m_savedStrategyCombo->currentData().toString();
    m_savedStrategyCombo->clear();
    QSettings settings;
    const QJsonDocument document = QJsonDocument::fromJson(settings.value(QString::fromLatin1(SavedStrategiesKey)).toByteArray());
    const QJsonObject strategies = document.object();
    QStringList names = strategies.keys();
    names.sort(Qt::CaseInsensitive);
    for (const QString& name : names) {
        m_savedStrategyCombo->addItem(name, name);
    }
    const int index = m_savedStrategyCombo->findData(current);
    if (index >= 0) {
        m_savedStrategyCombo->setCurrentIndex(index);
    }
}

void ScreenerPanel::saveStrategy()
{
    bool ok = false;
    const QString name = QInputDialog::getText(this, QStringLiteral("保存选股策略"), QStringLiteral("策略名称"),
                                               QLineEdit::Normal, QString(), &ok).trimmed();
    if (!ok || name.isEmpty()) {
        return;
    }
    QSettings settings;
    QJsonDocument document = QJsonDocument::fromJson(settings.value(QString::fromLatin1(SavedStrategiesKey)).toByteArray());
    QJsonObject strategies = document.object();
    if (strategies.contains(name)
        && QMessageBox::question(this, QStringLiteral("覆盖策略"), QStringLiteral("已存在同名策略，是否覆盖？")) != QMessageBox::Yes) {
        return;
    }
    strategies.insert(name, criteriaToJson(currentCriteria()));
    settings.setValue(QString::fromLatin1(SavedStrategiesKey), QJsonDocument(strategies).toJson(QJsonDocument::Compact));
    refreshSavedStrategies();
    m_savedStrategyCombo->setCurrentIndex(m_savedStrategyCombo->findData(name));
    m_statusLabel->setText(QStringLiteral("已保存策略：%1").arg(name));
}

void ScreenerPanel::loadStrategy()
{
    const QString name = m_savedStrategyCombo->currentData().toString();
    if (name.isEmpty()) {
        return;
    }
    QSettings settings;
    const QJsonObject strategies = QJsonDocument::fromJson(settings.value(QString::fromLatin1(SavedStrategiesKey)).toByteArray()).object();
    const QJsonObject content = strategies.value(name).toObject();
    if (content.isEmpty()) {
        return;
    }
    applyCriteria(criteriaFromJson(content));
    m_statusLabel->setText(QStringLiteral("已加载策略：%1").arg(name));
}

void ScreenerPanel::deleteStrategy()
{
    const QString name = m_savedStrategyCombo->currentData().toString();
    if (name.isEmpty()) {
        return;
    }
    if (QMessageBox::question(this, QStringLiteral("删除策略"), QStringLiteral("确定删除“%1”？").arg(name)) != QMessageBox::Yes) {
        return;
    }
    QSettings settings;
    QJsonObject strategies = QJsonDocument::fromJson(settings.value(QString::fromLatin1(SavedStrategiesKey)).toByteArray()).object();
    strategies.remove(name);
    settings.setValue(QString::fromLatin1(SavedStrategiesKey), QJsonDocument(strategies).toJson(QJsonDocument::Compact));
    refreshSavedStrategies();
}
