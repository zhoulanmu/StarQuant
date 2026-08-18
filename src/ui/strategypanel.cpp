#include "strategypanel.h"
#include "ui_strategypanel.h"
#include "../market/marketdata.h"

#include <QCheckBox>
#include <QComboBox>
#include <QCompleter>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollBar>
#include <QScrollArea>
#include <QSettings>
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QSpinBox>
#include <QStringListModel>
#include <QTime>
#include <QTimer>
#include <QToolTip>
#include <QUrlQuery>
#include <QVBoxLayout>
#include <QWheelEvent>

namespace {
constexpr int SearchDelayMs = 280;
constexpr int SearchTimeoutMs = 2500;
constexpr auto SearchEndpoint = "https://searchapi.eastmoney.com/api/suggest/get";
constexpr auto SearchToken = "D43BF722C8E33EC61";

class NoWheelComboBox final : public QComboBox
{
public:
    explicit NoWheelComboBox(QWidget* parent = nullptr)
        : QComboBox(parent)
    {
    }

protected:
    void wheelEvent(QWheelEvent* event) override
    {
        if (event) {
            event->ignore();
        }
    }
};

enum class StrategyKind
{
    DoubleMA,
    ProsperityGrowth
};

struct StrategyPreset
{
    StrategyKind kind;
    QString name;
    QString description;
    int fastMA;
    int slowMA;
    double stopLoss;
    double takeProfit;
};

int normalizedMABarPeriodMinutes(int minutes);

QString displayGrowthTrackName(const QString& value)
{
    const QString text = value.trimmed();
    if (text == QStringLiteral("AI Compute")) {
        return QStringLiteral("AI 算力");
    }
    if (text == QStringLiteral("Semiconductor Equipment")) {
        return QStringLiteral("半导体设备");
    }
    if (text == QStringLiteral("Robotics Parts")) {
        return QStringLiteral("机器人零部件");
    }
    if (text == QStringLiteral("Energy Storage")) {
        return QStringLiteral("储能");
    }
    return text;
}

QStringList displayGrowthTrackNames(const QStringList& values)
{
    QStringList result;
    for (const QString& value : values) {
        const QString text = displayGrowthTrackName(value);
        if (!text.isEmpty() && !result.contains(text)) {
            result.append(text);
        }
    }
    return result;
}
QVector<StrategyPreset> commonStrategyPresets()
{
    return QVector<StrategyPreset>{
        {StrategyKind::DoubleMA,
         QStringLiteral("双均线策略"),
         QStringLiteral("快慢均线交叉买卖，并使用止损/止盈风控。"),
         5, 20, 2.0, 5.0},
        {StrategyKind::ProsperityGrowth,
         QStringLiteral("景气成长策略"),
         QStringLiteral("成长赛道回调低吸，结合人工确认和分批止盈。"),
         20, 60, 8.0, 25.0}
    };
}

StrategyKind strategyKindAt(int index)
{
    const QVector<StrategyPreset> presets = commonStrategyPresets();
    if (index < 0 || index >= presets.size()) {
        return StrategyKind::DoubleMA;
    }
    return presets[index].kind;
}

int presetIndexForStrategyType(StrategyType type)
{
    const QVector<StrategyPreset> presets = commonStrategyPresets();
    for (int i = 0; i < presets.size(); ++i) {
        if ((type == StrategyType::ProsperityGrowth && presets.at(i).kind == StrategyKind::ProsperityGrowth)
            || (type == StrategyType::DoubleMA && presets.at(i).kind == StrategyKind::DoubleMA)) {
            return i;
        }
    }
    return 0;
}

QJsonArray stringListToJson(const QStringList& values)
{
    QJsonArray array;
    for (const QString& value : values) {
        array.append(value);
    }
    return array;
}

QStringList stringListFromJson(const QJsonValue& value)
{
    QStringList result;
    if (!value.isArray()) {
        return result;
    }

    const QJsonArray array = value.toArray();
    for (const QJsonValue& item : array) {
        const QString text = displayGrowthTrackName(item.toString());
        if (!text.isEmpty()) {
            result.append(text);
        }
    }
    return result;
}

QJsonObject strategyConfigToJson(const StrategyConfig& config)
{
    QJsonObject object;
    object.insert(QStringLiteral("strategyType"), static_cast<int>(config.strategyType));
    object.insert(QStringLiteral("symbol"), config.symbol);

    QJsonObject doubleMA;
    doubleMA.insert(QStringLiteral("fastMA"), config.doubleMAConfig.fastMA);
    doubleMA.insert(QStringLiteral("slowMA"), config.doubleMAConfig.slowMA);
    doubleMA.insert(QStringLiteral("barPeriodMinutes"), normalizedMABarPeriodMinutes(config.doubleMAConfig.barPeriodMinutes));
    doubleMA.insert(QStringLiteral("minSpreadPercent"), config.doubleMAConfig.minSpreadPercent);
    doubleMA.insert(QStringLiteral("confirmationBars"), config.doubleMAConfig.confirmationBars);
    doubleMA.insert(QStringLiteral("cooldownBars"), config.doubleMAConfig.cooldownBars);
    doubleMA.insert(QStringLiteral("requireSlowMAUp"), config.doubleMAConfig.requireSlowMAUp);
    doubleMA.insert(QStringLiteral("blockLateBuy"), config.doubleMAConfig.blockLateBuy);
    doubleMA.insert(QStringLiteral("minRewardCostMultiple"), config.doubleMAConfig.minRewardCostMultiple);
    object.insert(QStringLiteral("doubleMA"), doubleMA);

    QJsonObject risk;
    risk.insert(QStringLiteral("stopLossPercent"), config.riskConfig.stopLossPercent);
    risk.insert(QStringLiteral("takeProfitPercent"), config.riskConfig.takeProfitPercent);
    risk.insert(QStringLiteral("surgeTakeProfitPercent"), config.riskConfig.surgeTakeProfitPercent);
    risk.insert(QStringLiteral("partialTakeProfitEnabled"), config.riskConfig.partialTakeProfitEnabled);
    risk.insert(QStringLiteral("breakMA60VolumeStopEnabled"), config.riskConfig.breakMA60VolumeStopEnabled);
    object.insert(QStringLiteral("risk"), risk);

    QJsonObject position;
    position.insert(QStringLiteral("maxSingleTrackPercent"), config.positionConfig.maxSingleTrackPercent);
    position.insert(QStringLiteral("diversifyTrackCount"), config.positionConfig.diversifyTrackCount);
    object.insert(QStringLiteral("position"), position);

    QJsonObject growth;
    growth.insert(QStringLiteral("enabledTracks"), stringListToJson(config.growthBuyConfig.enabledTracks));
    growth.insert(QStringLiteral("requireMA60Up"), config.growthBuyConfig.requireMA60Up);
    growth.insert(QStringLiteral("requireVolumePullback"), config.growthBuyConfig.requireVolumePullback);
    growth.insert(QStringLiteral("requirePullbackToMA20OrMA60"), config.growthBuyConfig.requirePullbackToMA20OrMA60);
    growth.insert(QStringLiteral("profitGrowthConfirmed"), config.growthBuyConfig.profitGrowthConfirmed);
    growth.insert(QStringLiteral("orderLandingConfirmed"), config.growthBuyConfig.orderLandingConfirmed);
    growth.insert(QStringLiteral("noPureConceptConfirmed"), config.growthBuyConfig.noPureConceptConfirmed);
    growth.insert(QStringLiteral("pullbackMinDays"), config.growthBuyConfig.pullbackMinDays);
    growth.insert(QStringLiteral("pullbackMaxDays"), config.growthBuyConfig.pullbackMaxDays);
    object.insert(QStringLiteral("growth"), growth);

    return object;
}

StrategyConfig strategyConfigFromJson(const QJsonObject& object, const StrategyConfig& fallback)
{
    StrategyConfig config = fallback;
    const int type = object.value(QStringLiteral("strategyType")).toInt(static_cast<int>(config.strategyType));
    config.strategyType = type == static_cast<int>(StrategyType::ProsperityGrowth)
        ? StrategyType::ProsperityGrowth
        : StrategyType::DoubleMA;
    config.symbol = MarketDataSimulator::normalizeSymbol(object.value(QStringLiteral("symbol")).toString(config.symbol));

    const QJsonObject doubleMA = object.value(QStringLiteral("doubleMA")).toObject();
    config.doubleMAConfig.fastMA = doubleMA.value(QStringLiteral("fastMA")).toInt(config.doubleMAConfig.fastMA);
    config.doubleMAConfig.slowMA = doubleMA.value(QStringLiteral("slowMA")).toInt(config.doubleMAConfig.slowMA);
    config.doubleMAConfig.barPeriodMinutes = normalizedMABarPeriodMinutes(doubleMA.value(QStringLiteral("barPeriodMinutes")).toInt(config.doubleMAConfig.barPeriodMinutes));
    config.doubleMAConfig.minSpreadPercent = doubleMA.value(QStringLiteral("minSpreadPercent")).toDouble(config.doubleMAConfig.minSpreadPercent);
    config.doubleMAConfig.confirmationBars = doubleMA.value(QStringLiteral("confirmationBars")).toInt(config.doubleMAConfig.confirmationBars);
    config.doubleMAConfig.cooldownBars = doubleMA.value(QStringLiteral("cooldownBars")).toInt(config.doubleMAConfig.cooldownBars);
    config.doubleMAConfig.requireSlowMAUp = doubleMA.value(QStringLiteral("requireSlowMAUp")).toBool(config.doubleMAConfig.requireSlowMAUp);
    config.doubleMAConfig.blockLateBuy = doubleMA.value(QStringLiteral("blockLateBuy")).toBool(config.doubleMAConfig.blockLateBuy);
    config.doubleMAConfig.minRewardCostMultiple = doubleMA.value(QStringLiteral("minRewardCostMultiple")).toDouble(config.doubleMAConfig.minRewardCostMultiple);

    const QJsonObject risk = object.value(QStringLiteral("risk")).toObject();
    config.riskConfig.stopLossPercent = risk.value(QStringLiteral("stopLossPercent")).toDouble(config.riskConfig.stopLossPercent);
    config.riskConfig.takeProfitPercent = risk.value(QStringLiteral("takeProfitPercent")).toDouble(config.riskConfig.takeProfitPercent);
    config.riskConfig.surgeTakeProfitPercent = risk.value(QStringLiteral("surgeTakeProfitPercent")).toDouble(config.riskConfig.surgeTakeProfitPercent);
    config.riskConfig.partialTakeProfitEnabled = risk.value(QStringLiteral("partialTakeProfitEnabled")).toBool(config.riskConfig.partialTakeProfitEnabled);
    config.riskConfig.breakMA60VolumeStopEnabled = risk.value(QStringLiteral("breakMA60VolumeStopEnabled")).toBool(config.riskConfig.breakMA60VolumeStopEnabled);

    const QJsonObject position = object.value(QStringLiteral("position")).toObject();
    config.positionConfig.maxSingleTrackPercent = position.value(QStringLiteral("maxSingleTrackPercent")).toDouble(config.positionConfig.maxSingleTrackPercent);
    config.positionConfig.diversifyTrackCount = position.value(QStringLiteral("diversifyTrackCount")).toInt(config.positionConfig.diversifyTrackCount);
    config.positionConfig.lotSize = 0.0;

    const QJsonObject growth = object.value(QStringLiteral("growth")).toObject();
    const QStringList enabledTracks = stringListFromJson(growth.value(QStringLiteral("enabledTracks")));
    if (!enabledTracks.isEmpty()) {
        config.growthBuyConfig.enabledTracks = enabledTracks;
    }
    config.growthBuyConfig.requireMA60Up = growth.value(QStringLiteral("requireMA60Up")).toBool(config.growthBuyConfig.requireMA60Up);
    config.growthBuyConfig.requireVolumePullback = growth.value(QStringLiteral("requireVolumePullback")).toBool(config.growthBuyConfig.requireVolumePullback);
    config.growthBuyConfig.requirePullbackToMA20OrMA60 = growth.value(QStringLiteral("requirePullbackToMA20OrMA60")).toBool(config.growthBuyConfig.requirePullbackToMA20OrMA60);
    config.growthBuyConfig.profitGrowthConfirmed = growth.value(QStringLiteral("profitGrowthConfirmed")).toBool(config.growthBuyConfig.profitGrowthConfirmed);
    config.growthBuyConfig.orderLandingConfirmed = growth.value(QStringLiteral("orderLandingConfirmed")).toBool(config.growthBuyConfig.orderLandingConfirmed);
    config.growthBuyConfig.noPureConceptConfirmed = growth.value(QStringLiteral("noPureConceptConfirmed")).toBool(config.growthBuyConfig.noPureConceptConfirmed);
    config.growthBuyConfig.pullbackMinDays = growth.value(QStringLiteral("pullbackMinDays")).toInt(config.growthBuyConfig.pullbackMinDays);
    config.growthBuyConfig.pullbackMaxDays = growth.value(QStringLiteral("pullbackMaxDays")).toInt(config.growthBuyConfig.pullbackMaxDays);
    return config;
}
void appendUniqueCandidate(QVector<StockCandidate>* candidates, const StockCandidate& candidate)
{
    if (!candidates || candidate.symbol.isEmpty()) {
        return;
    }

    for (const auto& existing : *candidates) {
        if (existing.symbol == candidate.symbol) {
            return;
        }
    }

    candidates->append(candidate);
}

void collectSearchObjects(const QJsonValue& value, QVector<QJsonObject>* objects)
{
    if (!objects) {
        return;
    }

    if (value.isArray()) {
        const QJsonArray array = value.toArray();
        for (const auto& item : array) {
            collectSearchObjects(item, objects);
        }
        return;
    }

    if (!value.isObject()) {
        return;
    }

    const QJsonObject object = value.toObject();
    const bool hasStockFields = (object.contains(QStringLiteral("Code")) || object.contains(QStringLiteral("code")))
        && (object.contains(QStringLiteral("Name")) || object.contains(QStringLiteral("name")));
    if (hasStockFields) {
        objects->append(object);
    }

    for (auto it = object.constBegin(); it != object.constEnd(); ++it) {
        collectSearchObjects(it.value(), objects);
    }
}

QString strategyTypeDisplayName(StrategyType type)
{
    return type == StrategyType::ProsperityGrowth
        ? QStringLiteral("景气成长")
        : QStringLiteral("双均线");
}

int normalizedMABarPeriodMinutes(int minutes)
{
    return (minutes == 1 || minutes == 3 || minutes == 5) ? minutes : 5;
}

QString strategyDetailTooltip(StrategyKind kind)
{
    if (kind == StrategyKind::ProsperityGrowth) {
        return QStringLiteral(
            "<qt><b>景气成长策略</b><br>"
            "回调低吸型策略，结合趋势、回调、人工确认和风控规则。<br><br>"
            "<b>基础参数</b><br>"
            "策略标的：当前策略实例运行的股票。<br>"
            "快速/慢速 MA：双均线使用；快线越小越敏感，慢线越大越平滑。<br>"
            "止损(%)：持仓亏损达到该比例时离场。<br>"
            "止盈(%)：双均线达到该盈利比例离场；景气成长在关闭分批止盈时使用。<br><br>"
            "<b>景气成长勾选项</b><br>"
            "要求 60 日均线向上：勾选=只在 60 日线走平或向上时允许买入；不勾选=忽略该趋势过滤。<br>"
            "要求缩量回调接近 20/60 日均线：勾选=要求价格回踩均线附近且量能收缩；不勾选=忽略该买点过滤。<br>"
            "利润增长已确认：勾选=你确认利润改善，允许继续判断买点；不勾选=视为未确认，阻止景气成长买入。<br>"
            "订单/收入落地已确认：勾选=你确认景气度兑现；不勾选=视为未确认，阻止买入。<br>"
            "剔除纯概念标的：勾选=你确认不是纯概念炒作；不勾选=视为未通过，阻止买入。<br>"
            "赛道：勾选=允许该赛道；不勾选=排除该赛道；全部取消时景气成长不会买入。<br>"
            "分批止盈：勾选=短期涨幅达到阈值时先卖出部分仓位；不勾选=达到止盈阈值时整仓离场。<br>"
            "放量跌破 60 日线止损：勾选=放量跌破 60 日线触发额外离场；不勾选=只保留通用止损。<br><br>"
            "<b>数值项</b><br>"
            "回调窗口：要求连续回调天数落在这个范围内。<br>"
            "单策略仓位上限：该策略在绑定账户内最多占用的资产比例。<br>"
            "分散主线数：用于记录分散要求，当前单标的执行不直接改变买卖信号。<br>"
            "短期涨幅止盈：景气成长持仓涨幅达到该比例后触发止盈判断。"
            "</qt>");
    }
    return QStringLiteral(
        "<qt><b>双均线策略</b><br>"
        "快线向上穿过慢线时买入，跌破或触发风控信号时卖出。<br><br>"
        "<b>基础交易参数</b><br>"
        "策略标的：当前策略实例运行的股票。<br>"
        "快速MA周期：使用最近几根K线收盘价计算快线，越小越敏感。<br>"
        "慢速MA周期：使用最近几根K线收盘价计算慢线，越大越平滑。<br>"
        "均线计算周期：把实时价格聚合成1/3/5分钟K线，只在K线收盘后判断金叉/死叉。<br>"
        "买入：快线从下向上穿过慢线，且 RSI 未过热时触发。<br>"
        "卖出：快线跌回慢线下方，或触发止损/止盈时卖出。<br>"
        "止损(%)：买入后相对成本价下跌达到该比例时卖出。<br>"
        "止盈(%)：买入后相对成本价上涨达到该比例时卖出。<br>"
        "下单数量：按绑定账户资产和仓位上限自动计算。"
        "</qt>");
}
}

StrategyPanel::StrategyPanel(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::StrategyPanel),
    m_symbolCompleter(nullptr),
    m_strategySymbolCompleter(nullptr),
    m_watchlistSymbolCompleter(nullptr),
    m_searchModel(nullptr),
    m_strategySearchModel(nullptr),
    m_searchTimer(nullptr),
    m_searchNetwork(nullptr),
    m_searchReply(nullptr),
    m_strategySymbolEdit(nullptr),
    m_watchlistSymbolEdit(nullptr),
    m_activeSearchEdit(nullptr),
    m_strategyPresetCombo(nullptr),
    m_strategyPresetDescLabel(nullptr),
    m_strategyDetailButton(nullptr),
    m_applyStrategyPresetBtn(nullptr),
    m_strategyInstanceGroup(nullptr),
    m_strategyInstanceList(nullptr),
    m_strategyAccountCombo(nullptr),
    m_addStrategyInstanceBtn(nullptr),
    m_removeStrategyInstanceBtn(nullptr),
    m_startStrategyInstanceBtn(nullptr),
    m_stopStrategyInstanceBtn(nullptr),
    m_strategyConfigGroup(nullptr),
    m_strategyConfigHintLabel(nullptr),
    m_maBarPeriodCombo(nullptr),
    m_doubleMAMinSpreadSpin(nullptr),
    m_doubleMAConfirmBarsSpin(nullptr),
    m_doubleMACooldownBarsSpin(nullptr),
    m_doubleMACostMultipleSpin(nullptr),
    m_doubleMATrendFilterCheck(nullptr),
    m_doubleMALateBuyCheck(nullptr),
    m_ma60UpCheck(nullptr),
    m_profitGrowthCheck(nullptr),
    m_orderLandingCheck(nullptr),
    m_noPureConceptCheck(nullptr),
    m_volumePullbackCheck(nullptr),
    m_pullbackMinSpin(nullptr),
    m_pullbackMaxSpin(nullptr),
    m_sectorCapSpin(nullptr),
    m_diversifyMainlineSpin(nullptr),
    m_surgeTakeProfitSpin(nullptr),
    m_partialTakeProfitCheck(nullptr),
    m_breakMA60VolumeStopCheck(nullptr),
    m_symbolSelectorWidget(nullptr),
    m_personalSidebarWidget(nullptr),
    m_symbolAddFavoriteBtn(nullptr),
    m_strategyControlGroup(nullptr),
    m_watchlistGroup(nullptr),
    m_manualTradeGroup(nullptr),
    m_watchlistWidget(nullptr),
    m_addFavoriteBtn(nullptr),
    m_removeFavoriteBtn(nullptr),
    m_manualPriceSpin(nullptr),
    m_manualVolumeSpin(nullptr),
    m_buyFavoriteBtn(nullptr),
    m_sellFavoriteBtn(nullptr),
    m_nextStrategyInstanceId(1),
    m_currentStrategyInstanceIndex(-1),
    m_updatingSymbol(false),
    m_loadingSettings(true),
    m_loadingStrategyInstance(false)
{
    ui->setupUi(this);
    ui->groupBox->setTitle(QStringLiteral("基础交易参数"));
    ui->groupBox->setMinimumHeight(170);
    ui->groupBox->setMaximumHeight(270);
    ui->logTextEdit->setReadOnly(true);

    ui->label->setBuddy(nullptr);
    ui->label_2->setBuddy(nullptr);
    ui->label_3->setBuddy(nullptr);
    ui->label_4->setBuddy(nullptr);
    ui->label_5->setBuddy(nullptr);
    ui->label_6->setBuddy(nullptr);
    ui->label_6->hide();
    ui->lotSizeSpin->hide();

    const QString fastMATip = QStringLiteral("快速MA周期：使用最近几根K线收盘价计算快线，越小越敏感。");
    const QString slowMATip = QStringLiteral("慢速MA周期：使用最近几根K线收盘价计算慢线，越大越平滑。");
    const QString maBarPeriodTip = QStringLiteral("均线计算周期：把实时价格聚合成 1/3/5 分钟K线，只在K线收盘后更新快慢MA并判断交易。");
    const QString stopLossTip = QStringLiteral("止损(%)：买入后相对成本价下跌达到该比例时卖出。");
    const QString takeProfitTip = QStringLiteral("止盈(%)：买入后相对成本价上涨达到该比例时卖出。");
    ui->label_2->setToolTip(fastMATip);
    ui->fastMASpin->setToolTip(fastMATip);
    ui->label_3->setToolTip(slowMATip);
    ui->slowMASpin->setToolTip(slowMATip);
    auto* maBarPeriodLabel = new QLabel(QStringLiteral("均线计算周期"), ui->groupBox);
    m_maBarPeriodCombo = new NoWheelComboBox(ui->groupBox);
    m_maBarPeriodCombo->addItem(QStringLiteral("1分钟"), 1);
    m_maBarPeriodCombo->addItem(QStringLiteral("3分钟"), 3);
    m_maBarPeriodCombo->addItem(QStringLiteral("5分钟"), 5);
    m_maBarPeriodCombo->setCurrentIndex(m_maBarPeriodCombo->findData(5));
    maBarPeriodLabel->setToolTip(maBarPeriodTip);
    m_maBarPeriodCombo->setToolTip(maBarPeriodTip);
    ui->formLayout->insertRow(3, maBarPeriodLabel, m_maBarPeriodCombo);
    ui->label_4->setToolTip(stopLossTip);
    ui->stopLossSpin->setToolTip(stopLossTip);
    ui->label_5->setToolTip(takeProfitTip);
    ui->takeProfitSpin->setToolTip(takeProfitTip);

    setupCommonStrategyUi();
    setupStockSearchUi();
    loadWatchlist();
    loadViewSymbol();
    loadStrategySymbol();
    loadStrategySettings();
    loadStrategyInstances();
    loadPersonalSettings();
    m_loadingSettings = false;
    saveStrategySettings();
    saveStrategyInstances();
    savePersonalSettings();
    updateSearchSuggestions(ui->symbolEdit->text());
    addSystemLog(QStringLiteral("策略面板已就绪。"));

    connect(ui->startBtn, &QPushButton::clicked, this, &StrategyPanel::startClicked);
    connect(ui->stopBtn, &QPushButton::clicked, this, &StrategyPanel::stopClicked);
    connect(ui->symbolEdit, &QLineEdit::textChanged, this, &StrategyPanel::onSymbolTextChanged);
    connect(ui->symbolEdit, &QLineEdit::editingFinished, this, &StrategyPanel::onSymbolEditingFinished);
    connect(ui->fastMASpin, QOverload<int>::of(&QSpinBox::valueChanged), this, &StrategyPanel::on_paramChanged);
    connect(ui->slowMASpin, QOverload<int>::of(&QSpinBox::valueChanged), this, &StrategyPanel::on_paramChanged);
    if (m_maBarPeriodCombo) {
        connect(m_maBarPeriodCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &StrategyPanel::on_paramChanged);
    }
    connect(ui->stopLossSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &StrategyPanel::on_paramChanged);
    connect(ui->takeProfitSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &StrategyPanel::on_paramChanged);
}

StrategyPanel::~StrategyPanel()
{
    abortSearchReply();
    delete ui;
}

QString StrategyPanel::getSymbol() const
{
    return getStrategySymbol();
}

QString StrategyPanel::getViewSymbol() const
{
    const QString resolved = resolveSymbolText(ui->symbolEdit->text());
    return resolved.isEmpty() ? ui->symbolEdit->text().trimmed() : resolved;
}

QString StrategyPanel::getStrategySymbol() const
{
    if (!m_strategySymbolEdit) {
        return getViewSymbol();
    }

    const QString resolved = resolveSymbolText(m_strategySymbolEdit->text());
    return resolved.isEmpty() ? m_strategySymbolEdit->text().trimmed() : resolved;
}

StrategyConfig StrategyPanel::strategyConfig() const
{
    StrategyConfig config;
    const int index = m_strategyPresetCombo ? m_strategyPresetCombo->currentIndex() : 0;
    config.strategyType = strategyKindAt(index) == StrategyKind::ProsperityGrowth
        ? StrategyType::ProsperityGrowth
        : StrategyType::DoubleMA;
    config.symbol = MarketDataSimulator::normalizeSymbol(getStrategySymbol());

    config.doubleMAConfig.fastMA = getFastMA();
    config.doubleMAConfig.slowMA = getSlowMA();
    config.doubleMAConfig.barPeriodMinutes = getMABarPeriodMinutes();
    config.doubleMAConfig.minSpreadPercent = m_doubleMAMinSpreadSpin ? m_doubleMAMinSpreadSpin->value() : config.doubleMAConfig.minSpreadPercent;
    config.doubleMAConfig.confirmationBars = m_doubleMAConfirmBarsSpin ? m_doubleMAConfirmBarsSpin->value() : config.doubleMAConfig.confirmationBars;
    config.doubleMAConfig.cooldownBars = m_doubleMACooldownBarsSpin ? m_doubleMACooldownBarsSpin->value() : config.doubleMAConfig.cooldownBars;
    config.doubleMAConfig.minRewardCostMultiple = m_doubleMACostMultipleSpin ? m_doubleMACostMultipleSpin->value() : config.doubleMAConfig.minRewardCostMultiple;
    config.doubleMAConfig.requireSlowMAUp = !m_doubleMATrendFilterCheck || m_doubleMATrendFilterCheck->isChecked();
    config.doubleMAConfig.blockLateBuy = !m_doubleMALateBuyCheck || m_doubleMALateBuyCheck->isChecked();

    config.riskConfig.stopLossPercent = getStopLossPercent();
    config.riskConfig.takeProfitPercent = getTakeProfitPercent();
    config.riskConfig.surgeTakeProfitPercent = m_surgeTakeProfitSpin ? m_surgeTakeProfitSpin->value() : 25.0;
    config.riskConfig.partialTakeProfitEnabled = !m_partialTakeProfitCheck || m_partialTakeProfitCheck->isChecked();
    config.riskConfig.breakMA60VolumeStopEnabled = !m_breakMA60VolumeStopCheck || m_breakMA60VolumeStopCheck->isChecked();
    config.positionConfig.maxSingleTrackPercent = m_sectorCapSpin ? m_sectorCapSpin->value() : 20.0;
    config.positionConfig.diversifyTrackCount = m_diversifyMainlineSpin ? m_diversifyMainlineSpin->value() : 3;

    for (QCheckBox* check : m_growthTrackChecks) {
        if (check && check->isChecked()) {
            config.growthBuyConfig.enabledTracks.append(check->text());
        }
    }
    config.growthBuyConfig.requireMA60Up = !m_ma60UpCheck || m_ma60UpCheck->isChecked();
    config.growthBuyConfig.requireVolumePullback = !m_volumePullbackCheck || m_volumePullbackCheck->isChecked();
    config.growthBuyConfig.requirePullbackToMA20OrMA60 = !m_volumePullbackCheck || m_volumePullbackCheck->isChecked();
    config.growthBuyConfig.profitGrowthConfirmed = m_profitGrowthCheck && m_profitGrowthCheck->isChecked();
    config.growthBuyConfig.orderLandingConfirmed = m_orderLandingCheck && m_orderLandingCheck->isChecked();
    config.growthBuyConfig.noPureConceptConfirmed = m_noPureConceptCheck && m_noPureConceptCheck->isChecked();
    config.growthBuyConfig.pullbackMinDays = m_pullbackMinSpin ? m_pullbackMinSpin->value() : 3;
    config.growthBuyConfig.pullbackMaxDays = m_pullbackMaxSpin ? m_pullbackMaxSpin->value() : 5;

    return config;
}

int StrategyPanel::getFastMA() const
{
    return ui->fastMASpin->value();
}

int StrategyPanel::getSlowMA() const
{
    return ui->slowMASpin->value();
}

int StrategyPanel::getMABarPeriodMinutes() const
{
    if (!m_maBarPeriodCombo) {
        return 5;
    }
    return normalizedMABarPeriodMinutes(m_maBarPeriodCombo->currentData().toInt());
}

double StrategyPanel::getStopLossPercent() const
{
    return ui->stopLossSpin->value();
}

double StrategyPanel::getTakeProfitPercent() const
{
    return ui->takeProfitSpin->value();
}

double StrategyPanel::getLotSize() const
{
    return 100.0;
}

QString StrategyPanel::currentStrategyName() const
{
    const QVector<StrategyPreset> presets = commonStrategyPresets();
    const int index = m_strategyPresetCombo ? m_strategyPresetCombo->currentIndex() : 0;
    if (index < 0 || index >= presets.size()) {
        return presets.first().name;
    }
    return presets[index].name;
}

QString StrategyPanel::currentStrategyConfigurationSummary() const
{
    const StrategyConfig config = strategyConfig();
    const QString symbolText = stockDisplayText(config.symbol);
    if (config.strategyType == StrategyType::ProsperityGrowth) {
        return QStringLiteral("%1；标的:%2；赛道:%3；仓位上限:%4%；分散:%5；短期止盈:%6%")
            .arg(currentStrategyName(), symbolText, selectedGrowthTracks())
            .arg(config.positionConfig.maxSingleTrackPercent, 0, 'f', 1)
            .arg(config.positionConfig.diversifyTrackCount)
            .arg(config.riskConfig.surgeTakeProfitPercent, 0, 'f', 1);
    }

    return QStringLiteral("%1；标的:%2；周期:%3分钟；快MA:%4 慢MA:%5 止损:%6% 止盈:%7%；差值:%8%；确认:%9根；冷却:%10根；数量:自动")
        .arg(currentStrategyName(), symbolText)
        .arg(config.doubleMAConfig.barPeriodMinutes)
        .arg(config.doubleMAConfig.fastMA)
        .arg(config.doubleMAConfig.slowMA)
        .arg(config.riskConfig.stopLossPercent, 0, 'f', 1)
        .arg(config.riskConfig.takeProfitPercent, 0, 'f', 1)
        .arg(config.doubleMAConfig.minSpreadPercent, 0, 'f', 2)
        .arg(config.doubleMAConfig.confirmationBars)
        .arg(config.doubleMAConfig.cooldownBars);
}

QVector<StrategyInstanceInfo> StrategyPanel::strategyInstances()
{
    return m_strategyInstances;
}

StrategyInstanceInfo StrategyPanel::strategyInstance(int strategyId)
{
    for (const StrategyInstanceInfo& instance : m_strategyInstances) {
        if (instance.id == strategyId) {
            return instance;
        }
    }
    return StrategyInstanceInfo{};
}

int StrategyPanel::currentStrategyInstanceId() const
{
    const int index = selectedStrategyInstanceIndex();
    if (index < 0 || index >= m_strategyInstances.size()) {
        return 0;
    }
    return m_strategyInstances.at(index).id;
}

int StrategyPanel::strategyInstanceDisplayIndex(int strategyId) const
{
    for (int i = 0; i < m_strategyInstances.size(); ++i) {
        if (m_strategyInstances.at(i).id == strategyId) {
            return i + 1;
        }
    }
    return strategyId;
}

void StrategyPanel::setAccountNames(const QStringList& names)
{
    m_accountNames = names;
    if (m_accountNames.isEmpty()) {
        for (int i = 0; i < 6; ++i) {
            m_accountNames.append(QStringLiteral("账户 %1").arg(i + 1));
        }
    }

    const int selectedAccount = selectedStrategyAccountIndex();
    if (m_strategyAccountCombo) {
        QSignalBlocker blocker(m_strategyAccountCombo);
        m_strategyAccountCombo->clear();
        m_strategyAccountCombo->addItem(QStringLiteral("未绑定"), -1);
        for (int i = 0; i < m_accountNames.size(); ++i) {
            m_strategyAccountCombo->addItem(m_accountNames.at(i), i);
        }
        m_strategyAccountCombo->setCurrentIndex(qBound(0, selectedAccount + 1, m_strategyAccountCombo->count() - 1));
    }
    refreshStrategyInstanceList();
    refreshStrategyInstanceControls();
}

void StrategyPanel::setStrategyInstanceRunning(int strategyId, bool running, bool waiting)
{
    for (StrategyInstanceInfo& instance : m_strategyInstances) {
        if (instance.id == strategyId) {
            instance.running = running;
            instance.waiting = waiting;
            break;
        }
    }
    refreshStrategyInstanceList();
    refreshStrategyInstanceControls();
}
void StrategyPanel::setRunningState(bool running)
{
    ui->startBtn->setEnabled(!m_strategyInstances.isEmpty());
    ui->stopBtn->setEnabled(running);
    refreshStrategyInstanceControls();

    if (m_addFavoriteBtn) {
        m_addFavoriteBtn->setEnabled(true);
    }
    onFavoriteSelectionChanged();
}
QWidget* StrategyPanel::takeSymbolSelectorWidget(QWidget* parent)
{
    if (!m_symbolSelectorWidget) {
        m_symbolSelectorWidget = new QWidget(parent);
        m_symbolSelectorWidget->setObjectName(QStringLiteral("symbolSelectorWidget"));
        auto* selectorLayout = new QHBoxLayout(m_symbolSelectorWidget);
        selectorLayout->setContentsMargins(0, 0, 0, 0);
        selectorLayout->setSpacing(8);

        const QFormLayout::TakeRowResult symbolRow = ui->formLayout->takeRow(ui->symbolEdit);
        delete symbolRow.labelItem;
        delete symbolRow.fieldItem;

        ui->label->setParent(m_symbolSelectorWidget);
        ui->symbolEdit->setParent(m_symbolSelectorWidget);
        ui->label->setText(QStringLiteral("股票代码"));
        ui->symbolEdit->setMinimumWidth(170);

        m_symbolAddFavoriteBtn = new QPushButton(QStringLiteral("加入自选"), m_symbolSelectorWidget);
        m_symbolAddFavoriteBtn->setObjectName(QStringLiteral("favoriteAddBtn"));

        selectorLayout->addWidget(ui->label);
        selectorLayout->addWidget(ui->symbolEdit, 1);
        selectorLayout->addWidget(m_symbolAddFavoriteBtn);

        connect(m_symbolAddFavoriteBtn, &QPushButton::clicked, this, &StrategyPanel::onAddFavoriteClicked);
    }

    m_symbolSelectorWidget->setParent(parent);
    return m_symbolSelectorWidget;
}

QWidget* StrategyPanel::takeStrategyControlWidget(QWidget* parent)
{
    if (!m_strategyControlGroup) {
        m_strategyControlGroup = new QGroupBox(QStringLiteral("策略操作"), parent);
        auto* controlLayout = new QHBoxLayout(m_strategyControlGroup);
        controlLayout->setContentsMargins(10, 12, 10, 10);
        controlLayout->setSpacing(10);

        ui->horizontalLayout_2->removeWidget(ui->startBtn);
        ui->horizontalLayout_2->removeWidget(ui->stopBtn);
        ui->verticalLayout_4->removeItem(ui->horizontalLayout_2);

        ui->startBtn->setParent(m_strategyControlGroup);
        ui->stopBtn->setParent(m_strategyControlGroup);
        ui->startBtn->setMinimumWidth(120);
        ui->stopBtn->setMinimumWidth(120);
        controlLayout->addWidget(ui->startBtn);
        controlLayout->addWidget(ui->stopBtn);
    }

    m_strategyControlGroup->setParent(parent);
    return m_strategyControlGroup;
}

QWidget* StrategyPanel::takeTradeLogWidget(QWidget* parent)
{
    ui->verticalLayout_4->removeWidget(ui->groupBox_2);
    ui->groupBox_2->setParent(parent);
    ui->groupBox_2->setMinimumHeight(190);
    ui->logTextEdit->setMinimumHeight(160);
    return ui->groupBox_2;
}

QWidget* StrategyPanel::takeWatchlistWidget(QWidget* parent)
{
    if (!m_watchlistGroup) {
        return nullptr;
    }

    if (!m_personalSidebarWidget) {
        m_personalSidebarWidget = new QWidget(parent);
        auto* sidebarLayout = new QVBoxLayout(m_personalSidebarWidget);
        sidebarLayout->setContentsMargins(0, 0, 0, 0);
        sidebarLayout->setSpacing(12);
        sidebarLayout->addWidget(m_watchlistGroup, 1);
        if (m_manualTradeGroup) {
            sidebarLayout->addWidget(m_manualTradeGroup, 1);
        }
    }

    ui->verticalLayout_4->removeWidget(m_watchlistGroup);
    if (m_manualTradeGroup) {
        ui->verticalLayout_4->removeWidget(m_manualTradeGroup);
    }
    m_personalSidebarWidget->setParent(parent);
    m_personalSidebarWidget->setMinimumWidth(300);
    m_personalSidebarWidget->setMaximumWidth(420);
    m_personalSidebarWidget->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
    m_watchlistGroup->setMinimumWidth(300);
    m_watchlistGroup->setMaximumHeight(QWIDGETSIZE_MAX);
    m_watchlistGroup->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Ignored);
    if (m_manualTradeGroup) {
        m_manualTradeGroup->setMaximumHeight(QWIDGETSIZE_MAX);
        m_manualTradeGroup->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Ignored);
    }
    if (m_watchlistWidget) {
        m_watchlistWidget->setMinimumHeight(120);
        m_watchlistWidget->setMaximumHeight(QWIDGETSIZE_MAX);
        m_watchlistWidget->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
    }
    return m_personalSidebarWidget;
}

void StrategyPanel::setManualTradePrice(const QString& symbol, double price)
{
    const QString selected = selectedFavoriteSymbol();
    if (m_manualPriceSpin && price > 0.0 && (selected.isEmpty() || MarketDataSimulator::normalizeSymbol(symbol) == selected)) {
        m_manualPriceSpin->setValue(price);
        savePersonalSettings();
    }
}
void StrategyPanel::rememberStockName(const QString& symbol, const QString& name)
{
    const QString normalized = MarketDataSimulator::normalizeSymbol(symbol);
    const QString trimmedName = name.trimmed();
    if (!isValidMarketSymbol(normalized) || trimmedName.isEmpty()) {
        return;
    }

    const bool changed = m_stockNames.value(normalized) != trimmedName;
    if (changed) {
        m_stockNames[normalized] = trimmedName;
    }
    if (MarketDataSimulator::normalizeSymbol(getViewSymbol()) == normalized) {
        m_updatingSymbol = true;
        ui->symbolEdit->setText(stockDisplayText(normalized));
        m_updatingSymbol = false;
        saveViewSymbol();
    }
    if (MarketDataSimulator::normalizeSymbol(getStrategySymbol()) == normalized) {
        if (m_strategySymbolEdit) {
            m_updatingSymbol = true;
            m_strategySymbolEdit->setText(stockDisplayText(normalized));
            m_updatingSymbol = false;
        }
        saveStrategySymbol();
        updateStrategyPresetDescription();
    }

    if (m_favorites.contains(normalized)) {
        refreshWatchlist();
        if (changed) {
            saveWatchlist();
        }
    }

    refreshStrategyInstanceList();
}

void StrategyPanel::setupStrategyInstanceUi()
{
    if (m_strategyInstanceGroup) {
        return;
    }

    if (m_accountNames.isEmpty()) {
        for (int i = 0; i < 6; ++i) {
            m_accountNames.append(QStringLiteral("账户 %1").arg(i + 1));
        }
    }

    m_strategyInstanceGroup = new QGroupBox(QStringLiteral("策略实例"), this);
    auto* rootLayout = new QVBoxLayout(m_strategyInstanceGroup);
    rootLayout->setContentsMargins(10, 12, 10, 10);
    rootLayout->setSpacing(8);

    m_strategyInstanceList = new QListWidget(m_strategyInstanceGroup);
    m_strategyInstanceList->setMinimumHeight(86);
    m_strategyInstanceList->setMaximumHeight(132);
    rootLayout->addWidget(m_strategyInstanceList);

    auto* accountLayout = new QHBoxLayout();
    accountLayout->setContentsMargins(0, 0, 0, 0);
    accountLayout->setSpacing(8);
    accountLayout->addWidget(new QLabel(QStringLiteral("绑定账户"), m_strategyInstanceGroup));
    m_strategyAccountCombo = new NoWheelComboBox(m_strategyInstanceGroup);
    m_strategyAccountCombo->addItem(QStringLiteral("未绑定"), -1);
    for (int i = 0; i < m_accountNames.size(); ++i) {
        m_strategyAccountCombo->addItem(m_accountNames.at(i), i);
    }
    accountLayout->addWidget(m_strategyAccountCombo, 1);
    rootLayout->addLayout(accountLayout);

    auto* buttonLayout = new QHBoxLayout();
    buttonLayout->setContentsMargins(0, 0, 0, 0);
    buttonLayout->setSpacing(8);
    m_addStrategyInstanceBtn = new QPushButton(QStringLiteral("新增"), m_strategyInstanceGroup);
    m_removeStrategyInstanceBtn = new QPushButton(QStringLiteral("删除"), m_strategyInstanceGroup);
    m_startStrategyInstanceBtn = new QPushButton(QStringLiteral("启动"), m_strategyInstanceGroup);
    m_stopStrategyInstanceBtn = new QPushButton(QStringLiteral("停止"), m_strategyInstanceGroup);
    m_startStrategyInstanceBtn->setObjectName(QStringLiteral("startBtn"));
    m_stopStrategyInstanceBtn->setObjectName(QStringLiteral("stopBtn"));
    buttonLayout->addWidget(m_addStrategyInstanceBtn);
    buttonLayout->addWidget(m_removeStrategyInstanceBtn);
    buttonLayout->addWidget(m_startStrategyInstanceBtn);
    buttonLayout->addWidget(m_stopStrategyInstanceBtn);
    rootLayout->addLayout(buttonLayout);

    connect(m_strategyInstanceList, &QListWidget::currentRowChanged,
            this, &StrategyPanel::onStrategyInstanceSelectionChanged);
    connect(m_strategyAccountCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &StrategyPanel::onStrategyAccountChanged);
    connect(m_addStrategyInstanceBtn, &QPushButton::clicked,
            this, &StrategyPanel::onAddStrategyInstanceClicked);
    connect(m_removeStrategyInstanceBtn, &QPushButton::clicked,
            this, &StrategyPanel::onRemoveStrategyInstanceClicked);
    connect(m_startStrategyInstanceBtn, &QPushButton::clicked,
            this, &StrategyPanel::onStrategyInstanceStartClicked);
    connect(m_stopStrategyInstanceBtn, &QPushButton::clicked,
            this, &StrategyPanel::onStrategyInstanceStopClicked);
}

void StrategyPanel::loadStrategyInstances()
{
    m_loadingStrategyInstance = true;
    m_strategyInstances.clear();
    m_nextStrategyInstanceId = 1;

    QSettings settings(QStringLiteral("StarQuant"), QStringLiteral("StarQuant"));
    const QByteArray payload = settings.value(QStringLiteral("strategy/instancesV1")).toString().toUtf8();
    if (!payload.isEmpty()) {
        QJsonParseError error;
        const QJsonDocument document = QJsonDocument::fromJson(payload, &error);
        if (error.error == QJsonParseError::NoError && document.isArray()) {
            const QJsonArray array = document.array();
            for (const QJsonValue& value : array) {
                if (!value.isObject()) {
                    continue;
                }

                const QJsonObject object = value.toObject();
                StrategyInstanceInfo instance;
                instance.id = object.value(QStringLiteral("id")).toInt(m_nextStrategyInstanceId);
                instance.name = object.value(QStringLiteral("name")).toString(QStringLiteral("策略 %1").arg(instance.id));
                if (instance.name == QStringLiteral("Strategy %1").arg(instance.id) || instance.name.trimmed().isEmpty()) {
                    instance.name = QStringLiteral("策略 %1").arg(instance.id);
                }
                instance.accountIndex = object.value(QStringLiteral("accountIndex")).toInt(-1);
                instance.accountIndex = qBound(-1, instance.accountIndex, 5);
                instance.config = strategyConfigFromJson(object.value(QStringLiteral("config")).toObject(), strategyConfig());
                instance.running = false;
                m_strategyInstances.append(instance);
                m_nextStrategyInstanceId = qMax(m_nextStrategyInstanceId, instance.id + 1);
            }
        }
    }

    if (m_strategyInstances.isEmpty()) {
        StrategyInstanceInfo instance;
        instance.id = m_nextStrategyInstanceId++;
        instance.name = QStringLiteral("策略 %1").arg(instance.id);
        instance.accountIndex = 0;
        instance.config = strategyConfig();
        instance.running = false;
        m_strategyInstances.append(instance);
    }

    const int savedId = settings.value(QStringLiteral("strategy/currentInstanceId"), m_strategyInstances.first().id).toInt();
    int selectedIndex = 0;
    for (int i = 0; i < m_strategyInstances.size(); ++i) {
        if (m_strategyInstances.at(i).id == savedId) {
            selectedIndex = i;
            break;
        }
    }

    m_currentStrategyInstanceIndex = selectedIndex;
    refreshStrategyInstanceList();
    if (m_strategyInstanceList) {
        m_strategyInstanceList->setCurrentRow(selectedIndex);
    }
    loadStrategyInstanceIntoEditor(selectedIndex);
    m_loadingStrategyInstance = false;
    refreshStrategyInstanceControls();
}

void StrategyPanel::saveStrategyInstances() const
{
    QSettings settings(QStringLiteral("StarQuant"), QStringLiteral("StarQuant"));
    QJsonArray array;
    for (const StrategyInstanceInfo& instance : m_strategyInstances) {
        QJsonObject object;
        object.insert(QStringLiteral("id"), instance.id);
        object.insert(QStringLiteral("name"), instance.name);
        object.insert(QStringLiteral("accountIndex"), instance.accountIndex);
        object.insert(QStringLiteral("config"), strategyConfigToJson(instance.config));
        array.append(object);
    }
    settings.setValue(QStringLiteral("strategy/instancesV1"), QString::fromUtf8(QJsonDocument(array).toJson(QJsonDocument::Compact)));
    settings.setValue(QStringLiteral("strategy/currentInstanceId"), currentStrategyInstanceId());
    settings.sync();
}

void StrategyPanel::saveCurrentStrategyInstanceFromEditor()
{
    saveStrategyInstanceFromEditor(selectedStrategyInstanceIndex());
}

void StrategyPanel::saveStrategyInstanceFromEditor(int index)
{
    if (m_loadingSettings || m_loadingStrategyInstance || index < 0 || index >= m_strategyInstances.size()) {
        return;
    }
    if (m_strategyInstances.at(index).running) {
        return;
    }

    m_strategyInstances[index].config = strategyConfig();
    m_strategyInstances[index].accountIndex = selectedStrategyAccountIndex();
    if (m_strategyInstances[index].name.trimmed().isEmpty()) {
        m_strategyInstances[index].name = QStringLiteral("策略 %1").arg(m_strategyInstances[index].id);
    }
    refreshStrategyInstanceList();
    saveStrategyInstances();
}

void StrategyPanel::loadStrategyInstanceIntoEditor(int index)
{
    if (index < 0 || index >= m_strategyInstances.size()) {
        return;
    }

    m_loadingStrategyInstance = true;
    const StrategyInstanceInfo instance = m_strategyInstances.at(index);
    if (m_strategyPresetCombo) {
        m_strategyPresetCombo->setCurrentIndex(presetIndexForStrategyType(instance.config.strategyType));
    }
    if (m_strategySymbolEdit) {
        m_strategySymbolEdit->setText(stockDisplayText(instance.config.symbol));
    }
    ui->fastMASpin->setValue(instance.config.doubleMAConfig.fastMA);
    ui->slowMASpin->setValue(instance.config.doubleMAConfig.slowMA);
    if (m_maBarPeriodCombo) {
        const int periodIndex = m_maBarPeriodCombo->findData(normalizedMABarPeriodMinutes(instance.config.doubleMAConfig.barPeriodMinutes));
        if (periodIndex >= 0) {
            m_maBarPeriodCombo->setCurrentIndex(periodIndex);
        }
    }
    ui->stopLossSpin->setValue(instance.config.riskConfig.stopLossPercent);
    ui->takeProfitSpin->setValue(instance.config.riskConfig.takeProfitPercent);
    if (m_doubleMAMinSpreadSpin) m_doubleMAMinSpreadSpin->setValue(instance.config.doubleMAConfig.minSpreadPercent);
    if (m_doubleMAConfirmBarsSpin) m_doubleMAConfirmBarsSpin->setValue(instance.config.doubleMAConfig.confirmationBars);
    if (m_doubleMACooldownBarsSpin) m_doubleMACooldownBarsSpin->setValue(instance.config.doubleMAConfig.cooldownBars);
    if (m_doubleMACostMultipleSpin) m_doubleMACostMultipleSpin->setValue(instance.config.doubleMAConfig.minRewardCostMultiple);
    if (m_doubleMATrendFilterCheck) m_doubleMATrendFilterCheck->setChecked(instance.config.doubleMAConfig.requireSlowMAUp);
    if (m_doubleMALateBuyCheck) m_doubleMALateBuyCheck->setChecked(instance.config.doubleMAConfig.blockLateBuy);

    for (QCheckBox* check : m_growthTrackChecks) {
        if (check) {
            check->setChecked(instance.config.growthBuyConfig.enabledTracks.isEmpty()
                || instance.config.growthBuyConfig.enabledTracks.contains(check->text()));
        }
    }
    if (m_ma60UpCheck) m_ma60UpCheck->setChecked(instance.config.growthBuyConfig.requireMA60Up);
    if (m_volumePullbackCheck) m_volumePullbackCheck->setChecked(instance.config.growthBuyConfig.requireVolumePullback);
    if (m_profitGrowthCheck) m_profitGrowthCheck->setChecked(instance.config.growthBuyConfig.profitGrowthConfirmed);
    if (m_orderLandingCheck) m_orderLandingCheck->setChecked(instance.config.growthBuyConfig.orderLandingConfirmed);
    if (m_noPureConceptCheck) m_noPureConceptCheck->setChecked(instance.config.growthBuyConfig.noPureConceptConfirmed);
    if (m_pullbackMinSpin) m_pullbackMinSpin->setValue(instance.config.growthBuyConfig.pullbackMinDays);
    if (m_pullbackMaxSpin) m_pullbackMaxSpin->setValue(instance.config.growthBuyConfig.pullbackMaxDays);
    if (m_sectorCapSpin) m_sectorCapSpin->setValue(instance.config.positionConfig.maxSingleTrackPercent);
    if (m_diversifyMainlineSpin) m_diversifyMainlineSpin->setValue(instance.config.positionConfig.diversifyTrackCount);
    if (m_surgeTakeProfitSpin) m_surgeTakeProfitSpin->setValue(instance.config.riskConfig.surgeTakeProfitPercent);
    if (m_partialTakeProfitCheck) m_partialTakeProfitCheck->setChecked(instance.config.riskConfig.partialTakeProfitEnabled);
    if (m_breakMA60VolumeStopCheck) m_breakMA60VolumeStopCheck->setChecked(instance.config.riskConfig.breakMA60VolumeStopEnabled);

    if (m_strategyAccountCombo) {
        m_strategyAccountCombo->setCurrentIndex(qBound(0, instance.accountIndex + 1, m_strategyAccountCombo->count() - 1));
    }

    updateCurrentStrategyConfigUi();
    updateStrategyPresetDescription();
    m_loadingStrategyInstance = false;
    refreshStrategyInstanceControls();
}

void StrategyPanel::refreshStrategyInstanceList()
{
    if (!m_strategyInstanceList) {
        return;
    }

    const int selectedId = currentStrategyInstanceId();
    QSignalBlocker blocker(m_strategyInstanceList);
    m_strategyInstanceList->clear();
    int selectedRow = -1;
    for (int i = 0; i < m_strategyInstances.size(); ++i) {
        const StrategyInstanceInfo& instance = m_strategyInstances.at(i);
        auto* item = new QListWidgetItem(strategyInstanceDisplayText(instance, i + 1));
        item->setData(Qt::UserRole, instance.id);
        m_strategyInstanceList->addItem(item);
        if (instance.id == selectedId) {
            selectedRow = i;
        }
    }
    if (selectedRow < 0 && !m_strategyInstances.isEmpty()) {
        selectedRow = 0;
    }
    if (selectedRow >= 0) {
        m_strategyInstanceList->setCurrentRow(selectedRow);
        m_currentStrategyInstanceIndex = selectedRow;
    }
}

void StrategyPanel::refreshStrategyInstanceControls()
{
    const int index = selectedStrategyInstanceIndex();
    const bool hasSelection = index >= 0 && index < m_strategyInstances.size();
    const bool selectedRunning = hasSelection && m_strategyInstances.at(index).running;
    const bool selectedWaiting = hasSelection && m_strategyInstances.at(index).waiting;
    const bool selectedActive = selectedRunning || selectedWaiting;
    const bool editorEnabled = hasSelection && !selectedActive;

    if (m_removeStrategyInstanceBtn) {
        m_removeStrategyInstanceBtn->setEnabled(editorEnabled && m_strategyInstances.size() > 1);
    }
    if (m_startStrategyInstanceBtn) {
        m_startStrategyInstanceBtn->setEnabled(hasSelection && !selectedActive && m_strategyInstances.at(index).accountIndex >= 0);
    }
    if (m_stopStrategyInstanceBtn) {
        m_stopStrategyInstanceBtn->setEnabled(selectedActive);
    }
    if (m_strategyAccountCombo) {
        m_strategyAccountCombo->setEnabled(editorEnabled);
    }
    if (m_strategySymbolEdit) {
        m_strategySymbolEdit->setEnabled(editorEnabled);
    }
    ui->fastMASpin->setEnabled(editorEnabled);
    ui->slowMASpin->setEnabled(editorEnabled);
    ui->stopLossSpin->setEnabled(editorEnabled);
    ui->takeProfitSpin->setEnabled(editorEnabled);
    if (m_strategyPresetCombo) {
        m_strategyPresetCombo->setEnabled(editorEnabled);
    }
    if (m_applyStrategyPresetBtn) {
        m_applyStrategyPresetBtn->setEnabled(editorEnabled);
    }

    const StrategyKind kind = strategyKindAt(m_strategyPresetCombo ? m_strategyPresetCombo->currentIndex() : 0);
    for (QWidget* widget : m_doubleMAConfigWidgets) {
        if (widget) {
            widget->setEnabled(editorEnabled && kind == StrategyKind::DoubleMA);
        }
    }
    setGrowthConfigEnabled(editorEnabled && kind == StrategyKind::ProsperityGrowth);
}

int StrategyPanel::selectedStrategyInstanceIndex() const
{
    if (!m_strategyInstanceList) {
        return m_currentStrategyInstanceIndex;
    }
    const int row = m_strategyInstanceList->currentRow();
    return row >= 0 ? row : m_currentStrategyInstanceIndex;
}

int StrategyPanel::selectedStrategyAccountIndex() const
{
    if (!m_strategyAccountCombo) {
        return -1;
    }
    const QVariant data = m_strategyAccountCombo->currentData();
    return data.isValid() ? data.toInt() : -1;
}

int StrategyPanel::nextAvailableAccountIndex() const
{
    for (int accountIndex = 0; accountIndex < 6; ++accountIndex) {
        bool used = false;
        for (const StrategyInstanceInfo& instance : m_strategyInstances) {
            if (instance.accountIndex == accountIndex) {
                used = true;
                break;
            }
        }
        if (!used) {
            return accountIndex;
        }
    }
    return -1;
}

QString StrategyPanel::strategyInstanceDisplayText(const StrategyInstanceInfo& instance, int displayIndex) const
{
    const QString accountText = instance.accountIndex >= 0 && instance.accountIndex < m_accountNames.size()
        ? m_accountNames.at(instance.accountIndex)
        : QStringLiteral("未绑定");
    const QString stateText = instance.running
        ? QStringLiteral("运行")
        : (instance.waiting ? QStringLiteral("等待") : QStringLiteral("空闲"));
    return QStringLiteral("策略 %1 | %2 | %3 | %4 | %5")
        .arg(displayIndex)
        .arg(strategyTypeDisplayName(instance.config.strategyType), accountText, stockDisplayText(instance.config.symbol), stateText);
}
void StrategyPanel::setupCommonStrategyUi()
{
    auto* presetGroup = new QGroupBox(QStringLiteral("当前策略"), this);
    auto* presetLayout = new QVBoxLayout(presetGroup);
    presetLayout->setContentsMargins(10, 12, 10, 10);
    presetLayout->setSpacing(8);

    auto* strategySymbolWidget = new QWidget(presetGroup);
    auto* strategySymbolLayout = new QHBoxLayout(strategySymbolWidget);
    strategySymbolLayout->setContentsMargins(0, 0, 0, 0);
    strategySymbolLayout->setSpacing(8);
    auto* strategySymbolLabel = new QLabel(QStringLiteral("策略标的"), strategySymbolWidget);
    m_strategySymbolEdit = new QLineEdit(strategySymbolWidget);
    m_strategySymbolEdit->setPlaceholderText(QStringLiteral("代码 / 名称 / 拼音"));
    m_strategySymbolEdit->setText(ui->symbolEdit->text());
    strategySymbolLayout->addWidget(strategySymbolLabel);
    strategySymbolLayout->addWidget(m_strategySymbolEdit, 1);
    presetLayout->addWidget(strategySymbolWidget);

    m_strategyPresetCombo = new NoWheelComboBox(presetGroup);
    const QVector<StrategyPreset> presets = commonStrategyPresets();
    for (const auto& preset : presets) {
        m_strategyPresetCombo->addItem(preset.name);
    }

    QSettings settings(QStringLiteral("StarQuant"), QStringLiteral("StarQuant"));
    const int savedType = settings.value(QStringLiteral("strategy/currentType"), 0).toInt();
    if (savedType >= 0 && savedType < presets.size()) {
        m_strategyPresetCombo->setCurrentIndex(savedType);
    }

    m_strategyPresetDescLabel = new QLabel(presetGroup);
    m_strategyPresetDescLabel->setWordWrap(true);
    m_strategyPresetDescLabel->setMinimumHeight(42);

    m_strategyDetailButton = new QPushButton(QStringLiteral("说明"), presetGroup);
    m_strategyDetailButton->setMinimumWidth(120);
    m_strategyDetailButton->setToolTipDuration(60000);

    m_applyStrategyPresetBtn = new QPushButton(QStringLiteral("应用当前配置"), presetGroup);

    auto* descLayout = new QHBoxLayout();
    descLayout->setContentsMargins(0, 0, 0, 0);
    descLayout->setSpacing(8);
    descLayout->addWidget(m_strategyPresetDescLabel, 1);
    descLayout->addWidget(m_strategyDetailButton);

    presetLayout->addWidget(m_strategyPresetCombo);
    presetLayout->addLayout(descLayout);
    presetLayout->addWidget(m_applyStrategyPresetBtn);
    presetGroup->setMinimumHeight(190);
    presetGroup->setMaximumHeight(260);

    setupStrategyInstanceUi();
    ui->verticalLayout_4->removeWidget(ui->groupBox);
    ui->verticalLayout_4->insertWidget(0, m_strategyInstanceGroup);
    ui->verticalLayout_4->insertWidget(1, presetGroup);
    ui->verticalLayout_4->insertWidget(2, ui->groupBox);

    setupCurrentStrategyConfigUi();
    ui->verticalLayout_4->setStretch(0, 0);
    ui->verticalLayout_4->setStretch(1, 0);
    ui->verticalLayout_4->setStretch(2, 0);
    ui->verticalLayout_4->setStretch(3, 1);

    connect(m_strategyPresetCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &StrategyPanel::onStrategyPresetChanged);
    connect(m_applyStrategyPresetBtn, &QPushButton::clicked,
            this, &StrategyPanel::onApplyStrategyPresetClicked);
    connect(m_strategyDetailButton, &QPushButton::clicked, this, [this]() {
        if (!m_strategyDetailButton || m_strategyDetailButton->toolTip().isEmpty()) {
            return;
        }
        const QPoint pos = m_strategyDetailButton->mapToGlobal(QPoint(0, m_strategyDetailButton->height() + 4));
        QToolTip::showText(pos, m_strategyDetailButton->toolTip(), m_strategyDetailButton);
    });

    onStrategyPresetChanged(m_strategyPresetCombo->currentIndex());
}

void StrategyPanel::updateStrategyPresetDescription()
{
    if (!m_strategyPresetDescLabel || !m_strategyPresetCombo) {
        return;
    }

    const QVector<StrategyPreset> presets = commonStrategyPresets();
    const int index = m_strategyPresetCombo->currentIndex();
    if (index < 0 || index >= presets.size()) {
        return;
    }

    const StrategyConfig config = strategyConfig();
    const QString symbolText = config.symbol.trimmed().isEmpty()
        ? QStringLiteral("未设置")
        : stockDisplayText(config.symbol);
    const QString description = presets.at(index).description;

    if (config.strategyType == StrategyType::DoubleMA) {
        m_strategyPresetDescLabel->setText(
            QStringLiteral("%1  标的:%2 周期:%3分钟 快MA:%4 慢MA:%5 差值:%6% 确认:%7根 冷却:%8根 止损:%9% 止盈:%10%")
                .arg(description, symbolText)
                .arg(config.doubleMAConfig.barPeriodMinutes)
                .arg(config.doubleMAConfig.fastMA)
                .arg(config.doubleMAConfig.slowMA)
                .arg(config.doubleMAConfig.minSpreadPercent, 0, 'f', 2)
                .arg(config.doubleMAConfig.confirmationBars)
                .arg(config.doubleMAConfig.cooldownBars)
                .arg(config.riskConfig.stopLossPercent, 0, 'f', 1)
                .arg(config.riskConfig.takeProfitPercent, 0, 'f', 1));
        return;
    }

    m_strategyPresetDescLabel->setText(
        QStringLiteral("%1  标的:%2 快MA:%3 慢MA:%4 止损:%5% 止盈:%6% 仓位上限:%7% 分散:%8")
            .arg(description, symbolText)
            .arg(config.doubleMAConfig.fastMA)
            .arg(config.doubleMAConfig.slowMA)
            .arg(config.riskConfig.stopLossPercent, 0, 'f', 1)
            .arg(config.riskConfig.takeProfitPercent, 0, 'f', 1)
            .arg(config.positionConfig.maxSingleTrackPercent, 0, 'f', 1)
            .arg(config.positionConfig.diversifyTrackCount));
}

void StrategyPanel::setupCurrentStrategyConfigUi()
{
    auto* configScrollArea = new QScrollArea(this);
    configScrollArea->setWidgetResizable(true);
    configScrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    configScrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    configScrollArea->setMinimumHeight(260);
    configScrollArea->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    m_strategyConfigGroup = new QGroupBox(QStringLiteral("策略配置"), configScrollArea);
    m_strategyConfigGroup->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Minimum);
    auto* configLayout = new QVBoxLayout(m_strategyConfigGroup);
    configLayout->setContentsMargins(10, 12, 10, 10);
    configLayout->setSpacing(8);

    m_strategyConfigHintLabel = new QLabel(m_strategyConfigGroup);
    m_strategyConfigHintLabel->setWordWrap(true);
    configLayout->addWidget(m_strategyConfigHintLabel);

    auto* doubleMAGroup = new QGroupBox(QStringLiteral("双均线过滤"), m_strategyConfigGroup);
    auto* doubleMALayout = new QFormLayout(doubleMAGroup);
    m_doubleMAMinSpreadSpin = new QDoubleSpinBox(doubleMAGroup);
    m_doubleMAMinSpreadSpin->setRange(0.0, 5.0);
    m_doubleMAMinSpreadSpin->setDecimals(2);
    m_doubleMAMinSpreadSpin->setSingleStep(0.05);
    m_doubleMAMinSpreadSpin->setValue(0.25);
    m_doubleMAMinSpreadSpin->setSuffix(QStringLiteral("%"));
    m_doubleMAMinSpreadSpin->setToolTip(QStringLiteral("快MA必须至少高于慢MA这个比例，低于该差值的微弱交叉会被忽略。"));
    doubleMALayout->addRow(QStringLiteral("最小均线差"), m_doubleMAMinSpreadSpin);

    m_doubleMAConfirmBarsSpin = new QSpinBox(doubleMAGroup);
    m_doubleMAConfirmBarsSpin->setRange(1, 5);
    m_doubleMAConfirmBarsSpin->setValue(2);
    m_doubleMAConfirmBarsSpin->setToolTip(QStringLiteral("买入或死叉卖出信号需要连续成立几根K线，数值越大越稳但反应越慢。"));
    doubleMALayout->addRow(QStringLiteral("确认K线"), m_doubleMAConfirmBarsSpin);

    m_doubleMACooldownBarsSpin = new QSpinBox(doubleMAGroup);
    m_doubleMACooldownBarsSpin->setRange(0, 20);
    m_doubleMACooldownBarsSpin->setValue(5);
    m_doubleMACooldownBarsSpin->setToolTip(QStringLiteral("卖出后等待几根K线才允许重新买入，用来减少刚卖又买。"));
    doubleMALayout->addRow(QStringLiteral("卖出冷却K线"), m_doubleMACooldownBarsSpin);

    m_doubleMACostMultipleSpin = new QDoubleSpinBox(doubleMAGroup);
    m_doubleMACostMultipleSpin->setRange(0.0, 10.0);
    m_doubleMACostMultipleSpin->setDecimals(1);
    m_doubleMACostMultipleSpin->setSingleStep(0.5);
    m_doubleMACostMultipleSpin->setValue(2.0);
    m_doubleMACostMultipleSpin->setToolTip(QStringLiteral("止盈空间至少覆盖估算往返交易成本的倍数；0 表示关闭成本过滤。"));
    doubleMALayout->addRow(QStringLiteral("成本覆盖倍数"), m_doubleMACostMultipleSpin);

    m_doubleMATrendFilterCheck = new QCheckBox(QStringLiteral("只在慢MA走平或向上时买入"), doubleMAGroup);
    m_doubleMATrendFilterCheck->setChecked(true);
    m_doubleMATrendFilterCheck->setToolTip(QStringLiteral("勾选：慢MA不向下且价格在慢MA上方才允许买；不勾选：只看快慢MA关系。"));
    doubleMALayout->addRow(QStringLiteral("趋势过滤"), m_doubleMATrendFilterCheck);

    m_doubleMALateBuyCheck = new QCheckBox(QStringLiteral("14:45 后不新开仓"), doubleMAGroup);
    m_doubleMALateBuyCheck->setChecked(true);
    m_doubleMALateBuyCheck->setToolTip(QStringLiteral("勾选：尾盘不再买入，只保留已有持仓的卖出、止损、止盈判断。"));
    doubleMALayout->addRow(QStringLiteral("尾盘控制"), m_doubleMALateBuyCheck);

    configLayout->addWidget(doubleMAGroup);
    m_doubleMAConfigWidgets.append(doubleMAGroup);

    auto* autoGroup = new QGroupBox(QStringLiteral("自动过滤"), m_strategyConfigGroup);
    auto* autoLayout = new QVBoxLayout(autoGroup);
    m_ma60UpCheck = new QCheckBox(QStringLiteral("要求 60 日均线向上"), autoGroup);
    m_volumePullbackCheck = new QCheckBox(QStringLiteral("要求缩量回调接近 20/60 日均线"), autoGroup);
    m_ma60UpCheck->setChecked(true);
    m_volumePullbackCheck->setChecked(true);
    m_ma60UpCheck->setToolTip(QStringLiteral("勾选：买入前要求 60 日均线走平或向上；不勾选：忽略该趋势过滤。"));
    m_volumePullbackCheck->setToolTip(QStringLiteral("勾选：要求价格回踩 20/60 日均线附近且量能收缩；不勾选：忽略该买点过滤。"));
    autoLayout->addWidget(m_ma60UpCheck);
    autoLayout->addWidget(m_volumePullbackCheck);
    autoLayout->addWidget(new QLabel(QStringLiteral("实时行情会先估算，接入日线/板块数据后自动使用完整过滤。"), autoGroup));
    configLayout->addWidget(autoGroup);
    m_growthConfigWidgets.append(autoGroup);

    auto* dataGroup = new QGroupBox(QStringLiteral("人工确认"), m_strategyConfigGroup);
    auto* dataLayout = new QVBoxLayout(dataGroup);
    m_profitGrowthCheck = new QCheckBox(QStringLiteral("利润增长已确认"), dataGroup);
    m_orderLandingCheck = new QCheckBox(QStringLiteral("订单/收入落地已确认"), dataGroup);
    m_noPureConceptCheck = new QCheckBox(QStringLiteral("剔除纯概念标的"), dataGroup);
    m_profitGrowthCheck->setToolTip(QStringLiteral("勾选：确认利润改善；不勾选：视为未确认，景气成长不会买入。"));
    m_orderLandingCheck->setToolTip(QStringLiteral("勾选：确认订单或收入兑现；不勾选：视为未确认，景气成长不会买入。"));
    m_noPureConceptCheck->setToolTip(QStringLiteral("勾选：确认不是纯概念炒作；不勾选：视为未通过，景气成长不会买入。"));
    for (QCheckBox* check : {m_profitGrowthCheck, m_orderLandingCheck, m_noPureConceptCheck}) {
        check->setChecked(true);
        dataLayout->addWidget(check);
        connect(check, &QCheckBox::toggled, this, &StrategyPanel::onStrategyConfigChanged);
    }
    configLayout->addWidget(dataGroup);
    m_growthConfigWidgets.append(dataGroup);

    auto* userGroup = new QGroupBox(QStringLiteral("用户规则"), m_strategyConfigGroup);
    auto* userLayout = new QVBoxLayout(userGroup);
    auto* trackGroup = new QGroupBox(QStringLiteral("赛道"), userGroup);
    auto* trackLayout = new QGridLayout(trackGroup);
    const QStringList tracks = {QStringLiteral("AI 算力"), QStringLiteral("半导体设备"), QStringLiteral("机器人零部件"), QStringLiteral("储能")};
    for (int i = 0; i < tracks.size(); ++i) {
        auto* check = new QCheckBox(tracks.at(i), trackGroup);
        check->setChecked(true);
        check->setToolTip(QStringLiteral("勾选：允许该赛道；不勾选：排除该赛道；全部取消会阻止景气成长买入。"));
        m_growthTrackChecks.append(check);
        trackLayout->addWidget(check, i / 2, i % 2);
        connect(check, &QCheckBox::toggled, this, &StrategyPanel::onStrategyConfigChanged);
    }
    userLayout->addWidget(trackGroup);

    auto* ruleGroup = new QGroupBox(QStringLiteral("买点 / 仓位 / 风控"), userGroup);
    auto* ruleLayout = new QFormLayout(ruleGroup);
    m_pullbackMinSpin = new QSpinBox(ruleGroup);
    m_pullbackMinSpin->setRange(1, 10);
    m_pullbackMinSpin->setValue(3);
    m_pullbackMinSpin->setToolTip(QStringLiteral("连续回调天数下限，低于该天数不触发景气成长买点。"));
    m_pullbackMaxSpin = new QSpinBox(ruleGroup);
    m_pullbackMaxSpin->setRange(1, 10);
    m_pullbackMaxSpin->setValue(5);
    m_pullbackMaxSpin->setToolTip(QStringLiteral("连续回调天数上限，高于该天数视为回调过深或节奏不合适。"));
    auto* pullbackLayout = new QHBoxLayout();
    pullbackLayout->addWidget(m_pullbackMinSpin);
    pullbackLayout->addWidget(new QLabel(QStringLiteral("至"), ruleGroup));
    pullbackLayout->addWidget(m_pullbackMaxSpin);
    pullbackLayout->addWidget(new QLabel(QStringLiteral("天"), ruleGroup));
    ruleLayout->addRow(QStringLiteral("回调窗口"), pullbackLayout);

    m_sectorCapSpin = new QDoubleSpinBox(ruleGroup);
    m_sectorCapSpin->setRange(5.0, 50.0);
    m_sectorCapSpin->setDecimals(1);
    m_sectorCapSpin->setSingleStep(1.0);
    m_sectorCapSpin->setValue(20.0);
    m_sectorCapSpin->setSuffix(QStringLiteral("%"));
    m_sectorCapSpin->setToolTip(QStringLiteral("该策略在绑定账户中最多占用的资产比例，实际买入会按账户现金和一手股数自动计算。"));
    ruleLayout->addRow(QStringLiteral("单策略仓位上限"), m_sectorCapSpin);

    m_diversifyMainlineSpin = new QSpinBox(ruleGroup);
    m_diversifyMainlineSpin->setRange(1, 6);
    m_diversifyMainlineSpin->setValue(3);
    m_diversifyMainlineSpin->setToolTip(QStringLiteral("用于记录分散要求；当前单标的执行不直接改变买卖信号。"));
    ruleLayout->addRow(QStringLiteral("分散主线数"), m_diversifyMainlineSpin);

    m_surgeTakeProfitSpin = new QDoubleSpinBox(ruleGroup);
    m_surgeTakeProfitSpin->setRange(5.0, 80.0);
    m_surgeTakeProfitSpin->setDecimals(1);
    m_surgeTakeProfitSpin->setSingleStep(1.0);
    m_surgeTakeProfitSpin->setValue(25.0);
    m_surgeTakeProfitSpin->setSuffix(QStringLiteral("%"));
    m_surgeTakeProfitSpin->setToolTip(QStringLiteral("景气成长持仓涨幅达到该比例后触发短期止盈判断。"));
    ruleLayout->addRow(QStringLiteral("短期涨幅止盈"), m_surgeTakeProfitSpin);

    m_partialTakeProfitCheck = new QCheckBox(QStringLiteral("分批止盈"), ruleGroup);
    m_partialTakeProfitCheck->setChecked(true);
    m_partialTakeProfitCheck->setToolTip(QStringLiteral("勾选：达到短期止盈阈值时先卖出部分仓位；不勾选：达到止盈阈值时整仓离场。"));
    ruleLayout->addRow(QStringLiteral("止盈"), m_partialTakeProfitCheck);

    m_breakMA60VolumeStopCheck = new QCheckBox(QStringLiteral("放量跌破 60 日线止损"), ruleGroup);
    m_breakMA60VolumeStopCheck->setChecked(true);
    m_breakMA60VolumeStopCheck->setToolTip(QStringLiteral("勾选：放量跌破 60 日线时额外止损；不勾选：只保留通用止损。"));
    ruleLayout->addRow(QStringLiteral("止损"), m_breakMA60VolumeStopCheck);

    userLayout->addWidget(ruleGroup);
    configLayout->addWidget(userGroup);
    m_growthConfigWidgets.append(userGroup);
    configScrollArea->setWidget(m_strategyConfigGroup);
    ui->verticalLayout_4->insertWidget(3, configScrollArea);
    ui->verticalLayout_4->setStretch(3, 1);

    connect(m_doubleMAMinSpreadSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &StrategyPanel::onStrategyConfigChanged);
    connect(m_doubleMAConfirmBarsSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, &StrategyPanel::onStrategyConfigChanged);
    connect(m_doubleMACooldownBarsSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, &StrategyPanel::onStrategyConfigChanged);
    connect(m_doubleMACostMultipleSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &StrategyPanel::onStrategyConfigChanged);
    connect(m_doubleMATrendFilterCheck, &QCheckBox::toggled, this, &StrategyPanel::onStrategyConfigChanged);
    connect(m_doubleMALateBuyCheck, &QCheckBox::toggled, this, &StrategyPanel::onStrategyConfigChanged);
    connect(m_ma60UpCheck, &QCheckBox::toggled, this, &StrategyPanel::onStrategyConfigChanged);
    connect(m_volumePullbackCheck, &QCheckBox::toggled, this, &StrategyPanel::onStrategyConfigChanged);
    connect(m_pullbackMinSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, &StrategyPanel::onStrategyConfigChanged);
    connect(m_pullbackMaxSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, &StrategyPanel::onStrategyConfigChanged);
    connect(m_sectorCapSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &StrategyPanel::onStrategyConfigChanged);
    connect(m_diversifyMainlineSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, &StrategyPanel::onStrategyConfigChanged);
    connect(m_surgeTakeProfitSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &StrategyPanel::onStrategyConfigChanged);
    connect(m_partialTakeProfitCheck, &QCheckBox::toggled, this, &StrategyPanel::onStrategyConfigChanged);
    connect(m_breakMA60VolumeStopCheck, &QCheckBox::toggled, this, &StrategyPanel::onStrategyConfigChanged);
}

void StrategyPanel::updateCurrentStrategyConfigUi()
{
    const StrategyKind kind = strategyKindAt(m_strategyPresetCombo ? m_strategyPresetCombo->currentIndex() : 0);
    const bool growth = kind == StrategyKind::ProsperityGrowth;

    if (m_strategyConfigHintLabel) {
        m_strategyConfigHintLabel->setText(growth
            ? QStringLiteral("景气成长策略：使用自动过滤、人工确认和用户风控规则。")
            : QStringLiteral("双均线策略先聚合1/3/5分钟K线，再用K线收盘价计算快慢均线；下单数量按账户资产计算。"));
    }

    if (m_strategyConfigGroup) {
        m_strategyConfigGroup->setTitle(growth ? QStringLiteral("策略配置：景气成长") : QStringLiteral("策略配置：双均线"));
    }

    for (QWidget* widget : m_growthConfigWidgets) {
        if (widget) {
            widget->setVisible(growth);
        }
    }
    for (QWidget* widget : m_doubleMAConfigWidgets) {
        if (widget) {
            widget->setVisible(!growth);
            widget->setEnabled(!growth && (!m_strategyPresetCombo || m_strategyPresetCombo->isEnabled()));
        }
    }

    setGrowthConfigEnabled(growth && (!m_strategyPresetCombo || m_strategyPresetCombo->isEnabled()));
}

void StrategyPanel::setGrowthConfigEnabled(bool enabled)
{
    for (QCheckBox* check : m_growthTrackChecks) {
        check->setEnabled(enabled);
    }
    for (QCheckBox* check : {m_ma60UpCheck, m_profitGrowthCheck, m_orderLandingCheck, m_noPureConceptCheck,
                             m_volumePullbackCheck, m_partialTakeProfitCheck, m_breakMA60VolumeStopCheck}) {
        if (check) {
            check->setEnabled(enabled);
        }
    }
    for (QWidget* widget : {static_cast<QWidget*>(m_pullbackMinSpin), static_cast<QWidget*>(m_pullbackMaxSpin),
                            static_cast<QWidget*>(m_sectorCapSpin), static_cast<QWidget*>(m_diversifyMainlineSpin),
                            static_cast<QWidget*>(m_surgeTakeProfitSpin)}) {
        if (widget) {
            widget->setEnabled(enabled);
        }
    }
}

QString StrategyPanel::selectedGrowthTracks() const
{
    QStringList tracks;
    for (QCheckBox* check : m_growthTrackChecks) {
        if (check && check->isChecked()) {
            tracks.append(check->text());
        }
    }
    return tracks.isEmpty() ? QStringLiteral("未选择") : tracks.join(QStringLiteral("，"));
}

void StrategyPanel::setupStockSearchUi()
{
    ui->symbolEdit->setPlaceholderText(QStringLiteral("代码 / 名称 / 拼音"));

    m_searchModel = new QStringListModel(this);
    m_symbolCompleter = new QCompleter(m_searchModel, this);
    m_symbolCompleter->setCaseSensitivity(Qt::CaseInsensitive);
    m_symbolCompleter->setCompletionMode(QCompleter::PopupCompletion);
    m_symbolCompleter->setFilterMode(Qt::MatchContains);
    ui->symbolEdit->setCompleter(m_symbolCompleter);
    connect(m_symbolCompleter, QOverload<const QString&>::of(&QCompleter::activated),
            this, &StrategyPanel::onCompleterActivated);

    m_strategySearchModel = new QStringListModel(this);
    m_strategySymbolCompleter = new QCompleter(m_strategySearchModel, this);
    m_strategySymbolCompleter->setCaseSensitivity(Qt::CaseInsensitive);
    m_strategySymbolCompleter->setCompletionMode(QCompleter::PopupCompletion);
    m_strategySymbolCompleter->setFilterMode(Qt::MatchContains);
    if (m_strategySymbolEdit) {
        m_strategySymbolEdit->setCompleter(m_strategySymbolCompleter);
        connect(m_strategySymbolEdit, &QLineEdit::textChanged, this, &StrategyPanel::onStrategySymbolTextChanged);
        connect(m_strategySymbolEdit, &QLineEdit::editingFinished, this, &StrategyPanel::onStrategySymbolEditingFinished);
    }
    connect(m_strategySymbolCompleter, QOverload<const QString&>::of(&QCompleter::activated),
            this, &StrategyPanel::onStrategyCompleterActivated);

    m_searchTimer = new QTimer(this);
    m_searchTimer->setSingleShot(true);
    m_searchTimer->setInterval(SearchDelayMs);
    connect(m_searchTimer, &QTimer::timeout, this, &StrategyPanel::onSearchTimerTimeout);

    m_searchNetwork = new QNetworkAccessManager(this);

    m_watchlistGroup = new QGroupBox(QStringLiteral("自选股"), this);
    m_watchlistGroup->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
    auto favoriteLayout = new QVBoxLayout(m_watchlistGroup);
    favoriteLayout->setContentsMargins(10, 12, 10, 10);
    favoriteLayout->setSpacing(8);

    m_watchlistSymbolEdit = new QLineEdit(m_watchlistGroup);
    m_watchlistSymbolEdit->setPlaceholderText(QStringLiteral("代码 / 名称 / 拼音"));
    m_watchlistSymbolCompleter = new QCompleter(m_searchModel, this);
    m_watchlistSymbolCompleter->setCaseSensitivity(Qt::CaseInsensitive);
    m_watchlistSymbolCompleter->setCompletionMode(QCompleter::PopupCompletion);
    m_watchlistSymbolCompleter->setFilterMode(Qt::MatchContains);
    m_watchlistSymbolEdit->setCompleter(m_watchlistSymbolCompleter);
    favoriteLayout->addWidget(m_watchlistSymbolEdit);

    auto buttonLayout = new QHBoxLayout();
    buttonLayout->setContentsMargins(0, 0, 0, 0);
    buttonLayout->setSpacing(8);
    m_addFavoriteBtn = new QPushButton(QStringLiteral("新增"), m_watchlistGroup);
    m_addFavoriteBtn->setObjectName(QStringLiteral("favoriteAddBtn"));
    m_removeFavoriteBtn = new QPushButton(QStringLiteral("删除"), m_watchlistGroup);
    m_removeFavoriteBtn->setObjectName(QStringLiteral("favoriteRemoveBtn"));
    buttonLayout->addWidget(m_addFavoriteBtn);
    buttonLayout->addWidget(m_removeFavoriteBtn);
    favoriteLayout->addLayout(buttonLayout);

    m_watchlistWidget = new QListWidget(m_watchlistGroup);
    m_watchlistWidget->setMinimumHeight(120);
    m_watchlistWidget->setMaximumHeight(QWIDGETSIZE_MAX);
    m_watchlistWidget->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
    m_watchlistWidget->setAlternatingRowColors(false);
    favoriteLayout->addWidget(m_watchlistWidget, 1);

    m_manualTradeGroup = new QGroupBox(QStringLiteral("手动交易"), this);
    m_manualTradeGroup->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
    auto* manualLayout = new QFormLayout(m_manualTradeGroup);
    manualLayout->setContentsMargins(10, 12, 10, 10);
    manualLayout->setSpacing(8);

    m_manualPriceSpin = new QDoubleSpinBox(m_manualTradeGroup);
    m_manualPriceSpin->setRange(0.0, 100000.0);
    m_manualPriceSpin->setDecimals(2);
    m_manualPriceSpin->setSingleStep(0.01);
    m_manualPriceSpin->setSpecialValueText(QStringLiteral("自动"));
    manualLayout->addRow(QStringLiteral("价格"), m_manualPriceSpin);

    m_manualVolumeSpin = new QDoubleSpinBox(m_manualTradeGroup);
    m_manualVolumeSpin->setRange(100.0, 1000000.0);
    m_manualVolumeSpin->setDecimals(0);
    m_manualVolumeSpin->setSingleStep(100.0);
    m_manualVolumeSpin->setValue(100.0);
    manualLayout->addRow(QStringLiteral("数量"), m_manualVolumeSpin);

    auto* tradeButtonLayout = new QHBoxLayout();
    tradeButtonLayout->setContentsMargins(0, 0, 0, 0);
    tradeButtonLayout->setSpacing(8);
    m_buyFavoriteBtn = new QPushButton(QStringLiteral("买入"), m_manualTradeGroup);
    m_buyFavoriteBtn->setObjectName(QStringLiteral("favoriteBuyBtn"));
    m_sellFavoriteBtn = new QPushButton(QStringLiteral("卖出"), m_manualTradeGroup);
    m_sellFavoriteBtn->setObjectName(QStringLiteral("favoriteSellBtn"));
    tradeButtonLayout->addWidget(m_buyFavoriteBtn);
    tradeButtonLayout->addWidget(m_sellFavoriteBtn);
    manualLayout->addRow(tradeButtonLayout);

    ui->verticalLayout_4->insertWidget(3, m_watchlistGroup);
    ui->verticalLayout_4->insertWidget(4, m_manualTradeGroup);

    connect(m_watchlistSymbolEdit, &QLineEdit::textChanged, this, &StrategyPanel::onFavoriteSearchTextChanged);
    connect(m_watchlistSymbolEdit, &QLineEdit::editingFinished, this, &StrategyPanel::onFavoriteSearchEditingFinished);
    connect(m_watchlistSymbolCompleter, QOverload<const QString&>::of(&QCompleter::activated),
            this, &StrategyPanel::onFavoriteCompleterActivated);
    connect(m_addFavoriteBtn, &QPushButton::clicked, this, &StrategyPanel::onAddFavoriteClicked);
    connect(m_removeFavoriteBtn, &QPushButton::clicked, this, &StrategyPanel::onRemoveFavoriteClicked);
    connect(m_buyFavoriteBtn, &QPushButton::clicked, this, &StrategyPanel::onFavoriteBuyClicked);
    connect(m_sellFavoriteBtn, &QPushButton::clicked, this, &StrategyPanel::onFavoriteSellClicked);
    connect(m_watchlistWidget, &QListWidget::itemClicked, this, &StrategyPanel::onFavoriteActivated);
    connect(m_watchlistWidget, &QListWidget::itemDoubleClicked, this, &StrategyPanel::onFavoriteActivated);
    connect(m_watchlistWidget, &QListWidget::itemSelectionChanged, this, &StrategyPanel::onFavoriteSelectionChanged);
    connect(m_manualPriceSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double) {
        if (!m_loadingSettings) {
            savePersonalSettings();
        }
    });
    connect(m_manualVolumeSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double) {
        if (!m_loadingSettings) {
            savePersonalSettings();
        }
    });
}
void StrategyPanel::loadWatchlist()
{
    QSettings settings(QStringLiteral("StarQuant"), QStringLiteral("StarQuant"));
    const QStringList entries = settings.value(QStringLiteral("watchlist/items")).toStringList();

    m_favorites.clear();
    for (const QString& entry : entries) {
        const QStringList parts = entry.split(QLatin1Char('|'));
        const QString symbol = MarketDataSimulator::normalizeSymbol(parts.value(0));
        if (!isValidMarketSymbol(symbol) || m_favorites.contains(symbol)) {
            continue;
        }

        const QString name = parts.value(1).trimmed();
        if (!name.isEmpty()) {
            m_stockNames[symbol] = name;
        }
        m_favorites.append(symbol);
    }

    refreshWatchlist();
}

void StrategyPanel::loadViewSymbol()
{
    QSettings settings(QStringLiteral("StarQuant"), QStringLiteral("StarQuant"));
    const QString symbol = MarketDataSimulator::normalizeSymbol(
        settings.value(QStringLiteral("market/viewSymbol"),
                       settings.value(QStringLiteral("market/currentSymbol"), ui->symbolEdit->text())).toString());
    const QString name = settings.value(QStringLiteral("market/viewName"),
                                        settings.value(QStringLiteral("market/currentName"))).toString().trimmed();

    if (!isValidMarketSymbol(symbol)) {
        return;
    }

    if (!name.isEmpty()) {
        m_stockNames[symbol] = name;
    }

    selectSymbol(symbol, name, false);
}

void StrategyPanel::saveViewSymbol() const
{
    const QString symbol = MarketDataSimulator::normalizeSymbol(getViewSymbol());
    if (!isValidMarketSymbol(symbol)) {
        return;
    }

    QSettings settings(QStringLiteral("StarQuant"), QStringLiteral("StarQuant"));
    settings.setValue(QStringLiteral("market/viewSymbol"), symbol);
    settings.setValue(QStringLiteral("market/viewName"), stockNameForSymbol(symbol));
    settings.setValue(QStringLiteral("market/currentSymbol"), symbol);
    settings.setValue(QStringLiteral("market/currentName"), stockNameForSymbol(symbol));
    settings.sync();
}

void StrategyPanel::loadStrategySymbol()
{
    if (!m_strategySymbolEdit) {
        return;
    }

    QSettings settings(QStringLiteral("StarQuant"), QStringLiteral("StarQuant"));
    const QString symbol = MarketDataSimulator::normalizeSymbol(
        settings.value(QStringLiteral("strategy/currentSymbol"), getViewSymbol()).toString());
    const QString name = settings.value(QStringLiteral("strategy/currentName")).toString().trimmed();

    if (!isValidMarketSymbol(symbol)) {
        return;
    }

    if (!name.isEmpty()) {
        m_stockNames[symbol] = name;
    }

    selectStrategySymbol(symbol, name, false);
}

void StrategyPanel::saveStrategySymbol() const
{
    if (!m_strategySymbolEdit) {
        return;
    }

    const QString symbol = MarketDataSimulator::normalizeSymbol(getStrategySymbol());
    if (!isValidMarketSymbol(symbol)) {
        return;
    }

    QSettings settings(QStringLiteral("StarQuant"), QStringLiteral("StarQuant"));
    settings.setValue(QStringLiteral("strategy/currentSymbol"), symbol);
    settings.setValue(QStringLiteral("strategy/currentName"), stockNameForSymbol(symbol));
    settings.sync();
}

void StrategyPanel::saveCurrentStrategyType() const
{
    if (!m_strategyPresetCombo) {
        return;
    }

    QSettings settings(QStringLiteral("StarQuant"), QStringLiteral("StarQuant"));
    settings.setValue(QStringLiteral("strategy/currentType"), m_strategyPresetCombo->currentIndex());
    settings.sync();
}

void StrategyPanel::loadStrategySettings()
{
    QSettings settings(QStringLiteral("StarQuant"), QStringLiteral("StarQuant"));
    const QVector<StrategyPreset> presets = commonStrategyPresets();
    const int savedType = settings.value(QStringLiteral("strategy/currentType"),
                                         m_strategyPresetCombo ? m_strategyPresetCombo->currentIndex() : 0).toInt();
    if (m_strategyPresetCombo && savedType >= 0 && savedType < presets.size()) {
        m_strategyPresetCombo->setCurrentIndex(savedType);
    }

    if (settings.contains(QStringLiteral("strategy/basic/fastMA"))) {
        ui->fastMASpin->setValue(settings.value(QStringLiteral("strategy/basic/fastMA")).toInt());
    }
    if (settings.contains(QStringLiteral("strategy/basic/slowMA"))) {
        ui->slowMASpin->setValue(settings.value(QStringLiteral("strategy/basic/slowMA")).toInt());
    }
    if (m_maBarPeriodCombo && settings.contains(QStringLiteral("strategy/basic/barPeriodMinutes"))) {
        const int period = normalizedMABarPeriodMinutes(settings.value(QStringLiteral("strategy/basic/barPeriodMinutes"), 5).toInt());
        const int index = m_maBarPeriodCombo->findData(period);
        if (index >= 0) {
            m_maBarPeriodCombo->setCurrentIndex(index);
        }
    }
    if (settings.contains(QStringLiteral("strategy/basic/stopLossPercent"))) {
        ui->stopLossSpin->setValue(settings.value(QStringLiteral("strategy/basic/stopLossPercent")).toDouble());
    }
    if (settings.contains(QStringLiteral("strategy/basic/takeProfitPercent"))) {
        ui->takeProfitSpin->setValue(settings.value(QStringLiteral("strategy/basic/takeProfitPercent")).toDouble());
    }
    if (m_doubleMAMinSpreadSpin && settings.contains(QStringLiteral("strategy/doubleMA/minSpreadPercent"))) {
        m_doubleMAMinSpreadSpin->setValue(settings.value(QStringLiteral("strategy/doubleMA/minSpreadPercent")).toDouble());
    }
    if (m_doubleMAConfirmBarsSpin && settings.contains(QStringLiteral("strategy/doubleMA/confirmationBars"))) {
        m_doubleMAConfirmBarsSpin->setValue(settings.value(QStringLiteral("strategy/doubleMA/confirmationBars")).toInt());
    }
    if (m_doubleMACooldownBarsSpin && settings.contains(QStringLiteral("strategy/doubleMA/cooldownBars"))) {
        m_doubleMACooldownBarsSpin->setValue(settings.value(QStringLiteral("strategy/doubleMA/cooldownBars")).toInt());
    }
    if (m_doubleMACostMultipleSpin && settings.contains(QStringLiteral("strategy/doubleMA/minRewardCostMultiple"))) {
        m_doubleMACostMultipleSpin->setValue(settings.value(QStringLiteral("strategy/doubleMA/minRewardCostMultiple")).toDouble());
    }
    if (m_doubleMATrendFilterCheck && settings.contains(QStringLiteral("strategy/doubleMA/requireSlowMAUp"))) {
        m_doubleMATrendFilterCheck->setChecked(settings.value(QStringLiteral("strategy/doubleMA/requireSlowMAUp")).toBool());
    }
    if (m_doubleMALateBuyCheck && settings.contains(QStringLiteral("strategy/doubleMA/blockLateBuy"))) {
        m_doubleMALateBuyCheck->setChecked(settings.value(QStringLiteral("strategy/doubleMA/blockLateBuy")).toBool());
    }

    const auto applyCheck = [&settings](QCheckBox* check, const QString& key) {
        if (check && settings.contains(key)) {
            check->setChecked(settings.value(key).toBool());
        }
    };

    if (settings.contains(QStringLiteral("strategy/growth/enabledTracks"))) {
        const QStringList enabledTracks = displayGrowthTrackNames(settings.value(QStringLiteral("strategy/growth/enabledTracks")).toStringList());
        for (QCheckBox* check : m_growthTrackChecks) {
            if (check) {
                check->setChecked(enabledTracks.contains(check->text()));
            }
        }
    }

    applyCheck(m_ma60UpCheck, QStringLiteral("strategy/growth/requireMA60Up"));
    applyCheck(m_volumePullbackCheck, QStringLiteral("strategy/growth/requireVolumePullback"));
    applyCheck(m_profitGrowthCheck, QStringLiteral("strategy/growth/profitGrowthConfirmed"));
    applyCheck(m_orderLandingCheck, QStringLiteral("strategy/growth/orderLandingConfirmed"));
    applyCheck(m_noPureConceptCheck, QStringLiteral("strategy/growth/noPureConceptConfirmed"));
    applyCheck(m_partialTakeProfitCheck, QStringLiteral("strategy/growth/partialTakeProfitEnabled"));
    applyCheck(m_breakMA60VolumeStopCheck, QStringLiteral("strategy/growth/breakMA60VolumeStopEnabled"));

    if (m_pullbackMinSpin && settings.contains(QStringLiteral("strategy/growth/pullbackMinDays"))) {
        m_pullbackMinSpin->setValue(settings.value(QStringLiteral("strategy/growth/pullbackMinDays")).toInt());
    }
    if (m_pullbackMaxSpin && settings.contains(QStringLiteral("strategy/growth/pullbackMaxDays"))) {
        m_pullbackMaxSpin->setValue(settings.value(QStringLiteral("strategy/growth/pullbackMaxDays")).toInt());
    }
    if (m_sectorCapSpin && settings.contains(QStringLiteral("strategy/growth/maxSingleTrackPercent"))) {
        m_sectorCapSpin->setValue(settings.value(QStringLiteral("strategy/growth/maxSingleTrackPercent")).toDouble());
    }
    if (m_diversifyMainlineSpin && settings.contains(QStringLiteral("strategy/growth/diversifyTrackCount"))) {
        m_diversifyMainlineSpin->setValue(settings.value(QStringLiteral("strategy/growth/diversifyTrackCount")).toInt());
    }
    if (m_surgeTakeProfitSpin && settings.contains(QStringLiteral("strategy/growth/surgeTakeProfitPercent"))) {
        m_surgeTakeProfitSpin->setValue(settings.value(QStringLiteral("strategy/growth/surgeTakeProfitPercent")).toDouble());
    }

    updateCurrentStrategyConfigUi();
}

void StrategyPanel::saveStrategySettings() const
{
    QSettings settings(QStringLiteral("StarQuant"), QStringLiteral("StarQuant"));
    if (m_strategyPresetCombo) {
        settings.setValue(QStringLiteral("strategy/currentType"), m_strategyPresetCombo->currentIndex());
    }

    const QString symbol = MarketDataSimulator::normalizeSymbol(getStrategySymbol());
    if (isValidMarketSymbol(symbol)) {
        settings.setValue(QStringLiteral("strategy/currentSymbol"), symbol);
        settings.setValue(QStringLiteral("strategy/currentName"), stockNameForSymbol(symbol));
    }

    settings.setValue(QStringLiteral("strategy/basic/fastMA"), ui->fastMASpin->value());
    settings.setValue(QStringLiteral("strategy/basic/slowMA"), ui->slowMASpin->value());
    settings.setValue(QStringLiteral("strategy/basic/barPeriodMinutes"), getMABarPeriodMinutes());
    settings.setValue(QStringLiteral("strategy/basic/stopLossPercent"), ui->stopLossSpin->value());
    settings.setValue(QStringLiteral("strategy/basic/takeProfitPercent"), ui->takeProfitSpin->value());
    settings.remove(QStringLiteral("strategy/basic/lotSize"));
    if (m_doubleMAMinSpreadSpin) settings.setValue(QStringLiteral("strategy/doubleMA/minSpreadPercent"), m_doubleMAMinSpreadSpin->value());
    if (m_doubleMAConfirmBarsSpin) settings.setValue(QStringLiteral("strategy/doubleMA/confirmationBars"), m_doubleMAConfirmBarsSpin->value());
    if (m_doubleMACooldownBarsSpin) settings.setValue(QStringLiteral("strategy/doubleMA/cooldownBars"), m_doubleMACooldownBarsSpin->value());
    if (m_doubleMACostMultipleSpin) settings.setValue(QStringLiteral("strategy/doubleMA/minRewardCostMultiple"), m_doubleMACostMultipleSpin->value());
    settings.setValue(QStringLiteral("strategy/doubleMA/requireSlowMAUp"), m_doubleMATrendFilterCheck && m_doubleMATrendFilterCheck->isChecked());
    settings.setValue(QStringLiteral("strategy/doubleMA/blockLateBuy"), m_doubleMALateBuyCheck && m_doubleMALateBuyCheck->isChecked());

    QStringList enabledTracks;
    for (QCheckBox* check : m_growthTrackChecks) {
        if (check && check->isChecked()) {
            enabledTracks.append(check->text());
        }
    }
    settings.setValue(QStringLiteral("strategy/growth/enabledTracks"), enabledTracks);
    settings.setValue(QStringLiteral("strategy/growth/requireMA60Up"), m_ma60UpCheck && m_ma60UpCheck->isChecked());
    settings.setValue(QStringLiteral("strategy/growth/requireVolumePullback"), m_volumePullbackCheck && m_volumePullbackCheck->isChecked());
    settings.setValue(QStringLiteral("strategy/growth/profitGrowthConfirmed"), m_profitGrowthCheck && m_profitGrowthCheck->isChecked());
    settings.setValue(QStringLiteral("strategy/growth/orderLandingConfirmed"), m_orderLandingCheck && m_orderLandingCheck->isChecked());
    settings.setValue(QStringLiteral("strategy/growth/noPureConceptConfirmed"), m_noPureConceptCheck && m_noPureConceptCheck->isChecked());
    if (m_pullbackMinSpin) settings.setValue(QStringLiteral("strategy/growth/pullbackMinDays"), m_pullbackMinSpin->value());
    if (m_pullbackMaxSpin) settings.setValue(QStringLiteral("strategy/growth/pullbackMaxDays"), m_pullbackMaxSpin->value());
    if (m_sectorCapSpin) settings.setValue(QStringLiteral("strategy/growth/maxSingleTrackPercent"), m_sectorCapSpin->value());
    if (m_diversifyMainlineSpin) settings.setValue(QStringLiteral("strategy/growth/diversifyTrackCount"), m_diversifyMainlineSpin->value());
    if (m_surgeTakeProfitSpin) settings.setValue(QStringLiteral("strategy/growth/surgeTakeProfitPercent"), m_surgeTakeProfitSpin->value());
    settings.setValue(QStringLiteral("strategy/growth/partialTakeProfitEnabled"), m_partialTakeProfitCheck && m_partialTakeProfitCheck->isChecked());
    settings.setValue(QStringLiteral("strategy/growth/breakMA60VolumeStopEnabled"), m_breakMA60VolumeStopCheck && m_breakMA60VolumeStopCheck->isChecked());
    settings.sync();
}

void StrategyPanel::loadPersonalSettings()
{
    QSettings settings(QStringLiteral("StarQuant"), QStringLiteral("StarQuant"));
    if (m_watchlistSymbolEdit) {
        m_watchlistSymbolEdit->setText(settings.value(QStringLiteral("personal/watchlistInput"),
                                                     m_watchlistSymbolEdit->text()).toString());
    }
    if (m_manualPriceSpin && settings.contains(QStringLiteral("personal/manualPrice"))) {
        m_manualPriceSpin->setValue(settings.value(QStringLiteral("personal/manualPrice")).toDouble());
    }
    if (m_manualVolumeSpin && settings.contains(QStringLiteral("personal/manualVolume"))) {
        m_manualVolumeSpin->setValue(settings.value(QStringLiteral("personal/manualVolume")).toDouble());
    }

    const QString selected = MarketDataSimulator::normalizeSymbol(settings.value(QStringLiteral("personal/selectedFavorite")).toString());
    if (m_watchlistWidget && !selected.isEmpty()) {
        for (int row = 0; row < m_watchlistWidget->count(); ++row) {
            QListWidgetItem* item = m_watchlistWidget->item(row);
            if (item && item->data(Qt::UserRole).toString() == selected) {
                m_watchlistWidget->setCurrentItem(item);
                break;
            }
        }
    }
    onFavoriteSelectionChanged();
}

void StrategyPanel::savePersonalSettings() const
{
    QSettings settings(QStringLiteral("StarQuant"), QStringLiteral("StarQuant"));
    if (m_watchlistSymbolEdit) {
        settings.setValue(QStringLiteral("personal/watchlistInput"), m_watchlistSymbolEdit->text().trimmed());
    }
    if (m_manualPriceSpin) {
        settings.setValue(QStringLiteral("personal/manualPrice"), m_manualPriceSpin->value());
    }
    if (m_manualVolumeSpin) {
        settings.setValue(QStringLiteral("personal/manualVolume"), m_manualVolumeSpin->value());
    }
    settings.setValue(QStringLiteral("personal/selectedFavorite"), selectedFavoriteSymbol());
    settings.sync();
}

void StrategyPanel::saveWatchlist() const
{
    QStringList entries;
    for (const QString& symbol : m_favorites) {
        entries.append(symbol + QLatin1Char('|') + m_stockNames.value(symbol));
    }

    QSettings settings(QStringLiteral("StarQuant"), QStringLiteral("StarQuant"));
    settings.setValue(QStringLiteral("watchlist/items"), entries);
    settings.sync();
}

void StrategyPanel::refreshWatchlist()
{
    if (!m_watchlistWidget) {
        return;
    }

    m_watchlistWidget->clear();
    for (const QString& symbol : m_favorites) {
        const QString name = stockNameForSymbol(symbol);
        auto* item = new QListWidgetItem(name.isEmpty()
            ? symbol
            : QStringLiteral("%1  %2").arg(name, symbol));
        item->setData(Qt::UserRole, symbol);
        m_watchlistWidget->addItem(item);
    }
    onFavoriteSelectionChanged();
}
void StrategyPanel::selectSymbol(const QString& symbol, const QString& name, bool emitChange)
{
    const QString normalized = MarketDataSimulator::normalizeSymbol(symbol);
    if (!isValidMarketSymbol(normalized)) {
        return;
    }

    if (!name.trimmed().isEmpty()) {
        m_stockNames[normalized] = name.trimmed();
    }

    m_updatingSymbol = true;
    ui->symbolEdit->setText(stockDisplayText(normalized));
    updateSearchSuggestions(normalized);
    m_updatingSymbol = false;
    saveViewSymbol();

    if (emitChange) {
        emit viewSymbolChanged(normalized);
    }
}

void StrategyPanel::selectStrategySymbol(const QString& symbol, const QString& name, bool emitChange)
{
    if (!m_strategySymbolEdit) {
        return;
    }

    const QString normalized = MarketDataSimulator::normalizeSymbol(symbol);
    if (!isValidMarketSymbol(normalized)) {
        return;
    }

    if (!name.trimmed().isEmpty()) {
        m_stockNames[normalized] = name.trimmed();
    }

    m_updatingSymbol = true;
    m_strategySymbolEdit->setText(stockDisplayText(normalized));
    updateSearchSuggestions(normalized);
    m_updatingSymbol = false;
    saveStrategySymbol();
    updateStrategyPresetDescription();
    Q_UNUSED(emitChange)
}
QString StrategyPanel::selectedFavoriteSymbol() const
{
    if (!m_watchlistWidget || !m_watchlistWidget->currentItem()) {
        return QString();
    }
    return m_watchlistWidget->currentItem()->data(Qt::UserRole).toString();
}

void StrategyPanel::addFavoriteSymbol(const QString& symbol, const QString& name)
{
    const QString normalized = MarketDataSimulator::normalizeSymbol(symbol);
    if (!isValidMarketSymbol(normalized)) {
        addSystemLog(QStringLiteral("请选择有效股票。"));
        return;
    }

    if (!name.trimmed().isEmpty()) {
        m_stockNames[normalized] = name.trimmed();
    }

    if (!m_favorites.contains(normalized)) {
        m_favorites.append(normalized);
        saveWatchlist();
        refreshWatchlist();
    }

    if (m_watchlistWidget) {
        for (int row = 0; row < m_watchlistWidget->count(); ++row) {
            QListWidgetItem* item = m_watchlistWidget->item(row);
            if (item && item->data(Qt::UserRole).toString() == normalized) {
                m_watchlistWidget->setCurrentItem(item);
                break;
            }
        }
    }

    selectSymbol(normalized, stockNameForSymbol(normalized), true);
    savePersonalSettings();
}

QString StrategyPanel::resolveSymbolText(const QString& text) const
{
    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty()) {
        return QString();
    }

    if (m_completionSymbols.contains(trimmed)) {
        return m_completionSymbols.value(trimmed);
    }

    const QRegularExpression symbolPattern(QStringLiteral(R"((\d{6})(?:\.(SH|SZ))?)"),
                                           QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch match = symbolPattern.match(trimmed);
    if (match.hasMatch()) {
        const QString code = match.captured(1);
        const QString suffix = match.captured(2).toUpper();
        if (!suffix.isEmpty()) {
            return MarketDataSimulator::normalizeSymbol(code + QLatin1Char('.') + suffix);
        }
        return MarketDataSimulator::normalizeSymbol(code);
    }

    for (auto it = m_stockNames.constBegin(); it != m_stockNames.constEnd(); ++it) {
        if (it.value() == trimmed || it.value().contains(trimmed, Qt::CaseInsensitive)) {
            return it.key();
        }
    }

    const QVector<StockCandidate> localMatches = searchLocalStocks(trimmed);
    if (!localMatches.isEmpty()) {
        return localMatches.first().symbol;
    }

    const QString normalized = MarketDataSimulator::normalizeSymbol(trimmed);
    return isValidMarketSymbol(normalized) ? normalized : QString();
}

QString StrategyPanel::stockNameForSymbol(const QString& symbol) const
{
    const QString normalized = MarketDataSimulator::normalizeSymbol(symbol);
    if (m_stockNames.contains(normalized)) {
        return m_stockNames.value(normalized);
    }

    for (const auto& candidate : localStockCatalog()) {
        if (candidate.symbol == normalized) {
            return candidate.name;
        }
    }

    return QString();
}

QString StrategyPanel::stockDisplayText(const QString& symbol) const
{
    const QString normalized = MarketDataSimulator::normalizeSymbol(symbol);
    if (!isValidMarketSymbol(normalized)) {
        return symbol.trimmed();
    }

    const QString name = stockNameForSymbol(normalized).trimmed();
    return name.isEmpty()
        ? normalized
        : QStringLiteral("%1  %2").arg(name, normalized);
}

QVector<StockCandidate> StrategyPanel::localStockCatalog() const
{
    return QVector<StockCandidate>{
        {QStringLiteral("000001.SH"), QStringLiteral("上证指数")},
        {QStringLiteral("399001.SZ"), QStringLiteral("深证成指")},
        {QStringLiteral("399006.SZ"), QStringLiteral("创业板指")},
        {QStringLiteral("000001.SZ"), QStringLiteral("平安银行")},
        {QStringLiteral("002156.SZ"), QStringLiteral("通富微电")},
        {QStringLiteral("600519.SH"), QStringLiteral("贵州茅台")},
        {QStringLiteral("300750.SZ"), QStringLiteral("宁德时代")},
        {QStringLiteral("000858.SZ"), QStringLiteral("五粮液")},
        {QStringLiteral("601398.SH"), QStringLiteral("工商银行")},
        {QStringLiteral("600036.SH"), QStringLiteral("招商银行")},
        {QStringLiteral("601318.SH"), QStringLiteral("中国平安")},
        {QStringLiteral("002594.SZ"), QStringLiteral("比亚迪")},
        {QStringLiteral("600900.SH"), QStringLiteral("长江电力")},
        {QStringLiteral("600276.SH"), QStringLiteral("恒瑞医药")},
        {QStringLiteral("000333.SZ"), QStringLiteral("美的集团")},
        {QStringLiteral("002415.SZ"), QStringLiteral("海康威视")},
        {QStringLiteral("601012.SH"), QStringLiteral("隆基绿能")},
        {QStringLiteral("600030.SH"), QStringLiteral("中信证券")},
        {QStringLiteral("300059.SZ"), QStringLiteral("东方财富")},
        {QStringLiteral("002475.SZ"), QStringLiteral("立讯精密")},
        {QStringLiteral("603259.SH"), QStringLiteral("药明康德")}
    };
}QVector<StockCandidate> StrategyPanel::searchLocalStocks(const QString& keyword) const
{
    QVector<StockCandidate> results;
    const QString key = keyword.trimmed();
    if (key.isEmpty()) {
        return results;
    }

    const QString upperKey = key.toUpper();
    for (const auto& candidate : localStockCatalog()) {
        const QString compactSymbol = candidate.symbol.left(6);
        if (candidate.symbol.contains(upperKey, Qt::CaseInsensitive)
            || compactSymbol.contains(upperKey, Qt::CaseInsensitive)
            || candidate.name.contains(key, Qt::CaseInsensitive)) {
            appendUniqueCandidate(&results, candidate);
        }
    }

    for (auto it = m_stockNames.constBegin(); it != m_stockNames.constEnd(); ++it) {
        if (it.key().contains(upperKey, Qt::CaseInsensitive)
            || it.key().left(6).contains(upperKey, Qt::CaseInsensitive)
            || it.value().contains(key, Qt::CaseInsensitive)) {
            appendUniqueCandidate(&results, StockCandidate{it.key(), it.value()});
        }
    }

    return results;
}

QVector<StockCandidate> StrategyPanel::parseSearchResponse(const QByteArray& payload) const
{
    QVector<StockCandidate> results;
    QJsonParseError jsonError;
    const QJsonDocument document = QJsonDocument::fromJson(payload, &jsonError);
    if (jsonError.error != QJsonParseError::NoError || document.isNull()) {
        return results;
    }

    QVector<QJsonObject> objects;
    collectSearchObjects(document.isArray() ? QJsonValue(document.array()) : QJsonValue(document.object()), &objects);

    for (const QJsonObject& object : objects) {
        const QString code = object.value(QStringLiteral("Code")).toString(object.value(QStringLiteral("code")).toString()).trimmed();
        const QString name = object.value(QStringLiteral("Name")).toString(object.value(QStringLiteral("name")).toString()).trimmed();
        const QString quoteId = object.value(QStringLiteral("QuoteID")).toString(object.value(QStringLiteral("quoteId")).toString()).trimmed();
        const QString symbol = symbolFromQuoteId(quoteId, code);
        if (!isValidMarketSymbol(symbol) || name.isEmpty()) {
            continue;
        }

        appendUniqueCandidate(&results, StockCandidate{symbol, name});
        if (results.size() >= 12) {
            break;
        }
    }

    return results;
}

void StrategyPanel::updateSearchSuggestions(const QString& keyword, const QVector<StockCandidate>& remoteCandidates)
{
    QVector<StockCandidate> candidates = searchLocalStocks(keyword);

    const QString resolved = resolveSymbolText(keyword);
    if (isValidMarketSymbol(resolved)) {
        appendUniqueCandidate(&candidates, StockCandidate{resolved, stockNameForSymbol(resolved)});
    }

    for (const auto& candidate : remoteCandidates) {
        appendUniqueCandidate(&candidates, candidate);
    }

    showSearchCandidates(candidates);
}

void StrategyPanel::showSearchCandidates(const QVector<StockCandidate>& candidates)
{
    QStringList displays;
    m_completionSymbols.clear();

    for (const auto& candidate : candidates) {
        if (!isValidMarketSymbol(candidate.symbol)) {
            continue;
        }

        const QString display = displayTextForCandidate(candidate);
        displays.append(display);
        m_completionSymbols[display] = candidate.symbol;
        m_completionSymbols[candidate.symbol] = candidate.symbol;
        if (!candidate.name.isEmpty()) {
            m_stockNames[candidate.symbol] = candidate.name;
        }
    }

    m_searchModel->setStringList(displays);
    if (m_strategySearchModel) {
        m_strategySearchModel->setStringList(displays);
    }

    if (!m_updatingSymbol && !displays.isEmpty()) {
        if (ui->symbolEdit->hasFocus()) {
            m_symbolCompleter->complete();
        } else if (m_strategySymbolEdit && m_strategySymbolEdit->hasFocus() && m_strategySymbolCompleter) {
            m_strategySymbolCompleter->complete();
        } else if (m_watchlistSymbolEdit && m_watchlistSymbolEdit->hasFocus() && m_watchlistSymbolCompleter) {
            m_watchlistSymbolCompleter->complete();
        }
    }
}

void StrategyPanel::abortSearchReply()
{
    if (!m_searchReply) {
        return;
    }

    disconnect(m_searchReply, nullptr, this, nullptr);
    m_searchReply->abort();
    m_searchReply->deleteLater();
    m_searchReply = nullptr;
}

bool StrategyPanel::isValidMarketSymbol(const QString& symbol) const
{
    static const QRegularExpression pattern(QStringLiteral(R"(^\d{6}\.(SH|SZ)$)"));
    return pattern.match(symbol).hasMatch();
}

bool StrategyPanel::containsChinese(const QString& text) const
{
    for (const QChar ch : text) {
        const ushort unicode = ch.unicode();
        if (unicode >= 0x4E00 && unicode <= 0x9FFF) {
            return true;
        }
    }
    return false;
}

QString StrategyPanel::displayTextForCandidate(const StockCandidate& candidate)
{
    return candidate.name.isEmpty()
        ? candidate.symbol
        : QStringLiteral("%1  %2").arg(candidate.name, candidate.symbol);
}

QString StrategyPanel::symbolFromQuoteId(const QString& quoteId, const QString& code)
{
    const QString trimmedCode = code.trimmed();
    if (trimmedCode.size() != 6) {
        return QString();
    }

    const QString trimmedQuoteId = quoteId.trimmed();
    if (trimmedQuoteId.startsWith(QStringLiteral("1."))) {
        return trimmedCode + QStringLiteral(".SH");
    }
    if (trimmedQuoteId.startsWith(QStringLiteral("0."))) {
        return trimmedCode + QStringLiteral(".SZ");
    }

    return MarketDataSimulator::normalizeSymbol(trimmedCode);
}

void StrategyPanel::addSystemLog(const QString &message)
{
    const QString logLine = QStringLiteral("%1 [系统] %2")
            .arg(QTime::currentTime().toString(QStringLiteral("HH:mm:ss")), message);

    ui->logTextEdit->append(logLine);

    QScrollBar *scrollBar = ui->logTextEdit->verticalScrollBar();
    scrollBar->setValue(scrollBar->maximum());
}

void StrategyPanel::addStrategyLog(const QString& context, const QString& message)
{
    const QString prefix = context.trimmed().isEmpty()
        ? QStringLiteral("策略")
        : context.trimmed();
    const QString logLine = QStringLiteral("%1 [%2] %3")
            .arg(QTime::currentTime().toString(QStringLiteral("HH:mm:ss")), prefix, message);

    ui->logTextEdit->append(logLine);

    QScrollBar *scrollBar = ui->logTextEdit->verticalScrollBar();
    scrollBar->setValue(scrollBar->maximum());
}

void StrategyPanel::addSignalLog(const StrategySignal &signal, const QString& context)
{
    QString timeStr = signal.timestamp.toString("HH:mm:ss");
    QString typeStr;

    switch (signal.type) {
    case SignalType::BUY:
        typeStr = QStringLiteral("[买入]");
        break;
    case SignalType::SELL:
        typeStr = QStringLiteral("[卖出]");
        break;
    case SignalType::STOP_LOSS:
        typeStr = QStringLiteral("[止损]");
        break;
    case SignalType::TAKE_PROFIT:
        typeStr = QStringLiteral("[止盈]");
        break;
    default:
        typeStr = QStringLiteral("[信号]");
    }

    const QString volumeText = signal.volume > 0.0
        ? QString::number(signal.volume, 'f', 0)
        : QStringLiteral("自动");
    const QString contextText = context.trimmed();
    const QString logLine = contextText.isEmpty()
        ? QStringLiteral("%1 %2 %3 价格: %4 数量: %5 %6")
            .arg(timeStr)
            .arg(typeStr)
            .arg(signal.symbol)
            .arg(signal.price, 0, 'f', 2)
            .arg(volumeText)
            .arg(signal.comment)
        : QStringLiteral("%1 [%2] %3 价格: %4 数量: %5 %6")
            .arg(timeStr)
            .arg(contextText)
            .arg(typeStr)
            .arg(signal.price, 0, 'f', 2)
            .arg(volumeText)
            .arg(signal.comment);

    ui->logTextEdit->append(logLine);

    QScrollBar *scrollBar = ui->logTextEdit->verticalScrollBar();
    scrollBar->setValue(scrollBar->maximum());
}
void StrategyPanel::on_paramChanged()
{
    updateStrategyPresetDescription();
}

void StrategyPanel::onSymbolTextChanged(const QString& text)
{
    if (m_updatingSymbol) {
        return;
    }

    m_activeSearchEdit = ui->symbolEdit;
    updateSearchSuggestions(text);

    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty()) {
        return;
    }

    if (trimmed.size() >= 2 || containsChinese(trimmed)) {
        m_searchTimer->start();
    }
}

void StrategyPanel::onSymbolEditingFinished()
{
    const QString symbol = resolveSymbolText(ui->symbolEdit->text());
    if (isValidMarketSymbol(symbol)) {
        selectSymbol(symbol, stockNameForSymbol(symbol), true);
    }
}

void StrategyPanel::onStrategySymbolTextChanged(const QString& text)
{
    if (m_updatingSymbol) {
        return;
    }

    m_activeSearchEdit = m_strategySymbolEdit;
    updateSearchSuggestions(text);

    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty()) {
        return;
    }

    if (trimmed.size() >= 2 || containsChinese(trimmed)) {
        m_searchTimer->start();
    }
}

void StrategyPanel::onStrategySymbolEditingFinished()
{
    if (!m_strategySymbolEdit) {
        return;
    }

    const QString symbol = resolveSymbolText(m_strategySymbolEdit->text());
    if (isValidMarketSymbol(symbol)) {
        selectStrategySymbol(symbol, stockNameForSymbol(symbol), true);
    }
}

void StrategyPanel::onFavoriteSearchTextChanged(const QString& text)
{
    if (m_updatingSymbol) {
        return;
    }

    m_activeSearchEdit = m_watchlistSymbolEdit;
    updateSearchSuggestions(text);
    if (!m_loadingSettings) {
        savePersonalSettings();
    }

    const QString trimmed = text.trimmed();
    if (!trimmed.isEmpty() && (trimmed.size() >= 2 || containsChinese(trimmed))) {
        m_searchTimer->start();
    }
}

void StrategyPanel::onFavoriteSearchEditingFinished()
{
    if (!m_watchlistSymbolEdit) {
        return;
    }

    const QString symbol = resolveSymbolText(m_watchlistSymbolEdit->text());
    if (isValidMarketSymbol(symbol)) {
        m_updatingSymbol = true;
        m_watchlistSymbolEdit->setText(symbol);
        updateSearchSuggestions(symbol);
        m_updatingSymbol = false;
    }
}
void StrategyPanel::onSearchTimerTimeout()
{
    QLineEdit* sourceEdit = m_activeSearchEdit ? m_activeSearchEdit : ui->symbolEdit;
    const QString keyword = sourceEdit->text().trimmed();
    if (keyword.size() < 2 && !containsChinese(keyword)) {
        return;
    }

    abortSearchReply();

    QUrl url(QString::fromLatin1(SearchEndpoint));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("input"), keyword);
    query.addQueryItem(QStringLiteral("type"), QStringLiteral("14"));
    query.addQueryItem(QStringLiteral("token"), QString::fromLatin1(SearchToken));
    query.addQueryItem(QStringLiteral("count"), QStringLiteral("12"));
    url.setQuery(query);

    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader,
                      QStringLiteral("Mozilla/5.0 StarQuant/0.1 MarketSearch"));
    request.setRawHeader("Referer", "https://quote.eastmoney.com/");
    request.setTransferTimeout(SearchTimeoutMs);

    m_searchReply = m_searchNetwork->get(request);
    connect(m_searchReply, &QNetworkReply::finished, this, &StrategyPanel::onSearchReplyFinished);
}

void StrategyPanel::onSearchReplyFinished()
{
    QNetworkReply* reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply) {
        return;
    }

    if (reply == m_searchReply) {
        m_searchReply = nullptr;
    }

    const QByteArray payload = reply->readAll();
    const QNetworkReply::NetworkError error = reply->error();
    reply->deleteLater();

    if (error != QNetworkReply::NoError) {
        return;
    }

    QLineEdit* sourceEdit = m_activeSearchEdit ? m_activeSearchEdit : ui->symbolEdit;
    updateSearchSuggestions(sourceEdit->text(), parseSearchResponse(payload));
}

void StrategyPanel::onCompleterActivated(const QString& text)
{
    const QString symbol = m_completionSymbols.value(text, resolveSymbolText(text));
    if (!isValidMarketSymbol(symbol)) {
        return;
    }

    selectSymbol(symbol, stockNameForSymbol(symbol), true);
}

void StrategyPanel::onStrategyCompleterActivated(const QString& text)
{
    const QString symbol = m_completionSymbols.value(text, resolveSymbolText(text));
    if (!isValidMarketSymbol(symbol)) {
        return;
    }

    selectStrategySymbol(symbol, stockNameForSymbol(symbol), true);
}

void StrategyPanel::onFavoriteCompleterActivated(const QString& text)
{
    if (!m_watchlistSymbolEdit) {
        return;
    }

    const QString symbol = m_completionSymbols.value(text, resolveSymbolText(text));
    if (!isValidMarketSymbol(symbol)) {
        return;
    }

    m_updatingSymbol = true;
    m_watchlistSymbolEdit->setText(symbol);
    updateSearchSuggestions(symbol);
    m_updatingSymbol = false;
}

void StrategyPanel::onStrategyPresetChanged(int index)
{
    const QVector<StrategyPreset> presets = commonStrategyPresets();
    if (!m_strategyPresetDescLabel || index < 0 || index >= presets.size()) {
        return;
    }

    updateStrategyPresetDescription();
    if (m_strategyDetailButton) {
        m_strategyDetailButton->setToolTip(strategyDetailTooltip(presets.at(index).kind));
    }

    updateCurrentStrategyConfigUi();
}

void StrategyPanel::onApplyStrategyPresetClicked()
{
    const QVector<StrategyPreset> presets = commonStrategyPresets();
    const int index = m_strategyPresetCombo ? m_strategyPresetCombo->currentIndex() : -1;
    if (index < 0 || index >= presets.size()) {
        return;
    }

    updateStrategyPresetDescription();
    updateCurrentStrategyConfigUi();
    saveStrategySettings();
    saveCurrentStrategyInstanceFromEditor();
    addSystemLog(QStringLiteral("已应用当前配置：%1").arg(currentStrategyName()));
    addSystemLog(currentStrategyConfigurationSummary());
    if (m_applyStrategyPresetBtn) {
        const QString normalText = m_applyStrategyPresetBtn->text();
        m_applyStrategyPresetBtn->setText(QStringLiteral("已应用"));
        m_applyStrategyPresetBtn->setStyleSheet(QStringLiteral("background-color: #16a34a; color: white; font-weight: 600;"));
        QTimer::singleShot(1200, this, [this, normalText]() {
            if (!m_applyStrategyPresetBtn) {
                return;
            }
            m_applyStrategyPresetBtn->setText(normalText);
            m_applyStrategyPresetBtn->setStyleSheet(QString());
        });
    }
    emit parametersChanged();
}

void StrategyPanel::onStrategyConfigChanged()
{
    if (m_pullbackMinSpin && m_pullbackMaxSpin && m_pullbackMinSpin->value() > m_pullbackMaxSpin->value()) {
        m_pullbackMaxSpin->setValue(m_pullbackMinSpin->value());
    }
    updateStrategyPresetDescription();
}
void StrategyPanel::onStrategyInstanceSelectionChanged()
{
    if (m_loadingStrategyInstance) {
        return;
    }

    const int newIndex = selectedStrategyInstanceIndex();
    if (newIndex == m_currentStrategyInstanceIndex) {
        refreshStrategyInstanceControls();
        return;
    }

    m_currentStrategyInstanceIndex = newIndex;
    loadStrategyInstanceIntoEditor(newIndex);
    refreshStrategyInstanceList();
    saveStrategyInstances();
    emit strategyInstanceSelectionChanged(currentStrategyInstanceId());
}

void StrategyPanel::onAddStrategyInstanceClicked()
{
    StrategyInstanceInfo instance;
    instance.id = m_nextStrategyInstanceId++;
    instance.name = QStringLiteral("策略 %1").arg(instance.id);
    instance.accountIndex = nextAvailableAccountIndex();
    instance.config = strategyConfig();
    instance.running = false;
    m_strategyInstances.append(instance);

    m_currentStrategyInstanceIndex = m_strategyInstances.size() - 1;
    refreshStrategyInstanceList();
    if (m_strategyInstanceList) {
        m_strategyInstanceList->setCurrentRow(m_currentStrategyInstanceIndex);
    }
    loadStrategyInstanceIntoEditor(m_currentStrategyInstanceIndex);
    saveStrategyInstances();
    emit strategyInstanceSelectionChanged(instance.id);
    emit parametersChanged();
}

void StrategyPanel::onRemoveStrategyInstanceClicked()
{
    const int index = selectedStrategyInstanceIndex();
    if (index < 0 || index >= m_strategyInstances.size() || m_strategyInstances.at(index).running) {
        return;
    }
    if (m_strategyInstances.size() <= 1) {
        return;
    }

    m_strategyInstances.removeAt(index);
    m_currentStrategyInstanceIndex = qMin(index, m_strategyInstances.size() - 1);
    refreshStrategyInstanceList();
    if (m_strategyInstanceList) {
        m_strategyInstanceList->setCurrentRow(m_currentStrategyInstanceIndex);
    }
    loadStrategyInstanceIntoEditor(m_currentStrategyInstanceIndex);
    saveStrategyInstances();
    emit strategyInstanceSelectionChanged(currentStrategyInstanceId());
    emit parametersChanged();
}

void StrategyPanel::onStrategyAccountChanged(int index)
{
    Q_UNUSED(index)
    refreshStrategyInstanceControls();
}

void StrategyPanel::onStrategyInstanceStartClicked()
{
    const int id = currentStrategyInstanceId();
    if (id > 0) {
        emit strategyInstanceStartRequested(id);
    }
}

void StrategyPanel::onStrategyInstanceStopClicked()
{
    const int id = currentStrategyInstanceId();
    if (id > 0) {
        emit strategyInstanceStopRequested(id);
    }
}
void StrategyPanel::onAddFavoriteClicked()
{
    QLineEdit* sourceEdit = ui->symbolEdit;
    if (sender() == m_addFavoriteBtn && m_watchlistSymbolEdit && !m_watchlistSymbolEdit->text().trimmed().isEmpty()) {
        sourceEdit = m_watchlistSymbolEdit;
    }

    const QString symbol = resolveSymbolText(sourceEdit->text());
    addFavoriteSymbol(symbol, stockNameForSymbol(symbol));
    if (sourceEdit == m_watchlistSymbolEdit && isValidMarketSymbol(symbol)) {
        m_watchlistSymbolEdit->setText(MarketDataSimulator::normalizeSymbol(symbol));
    }
    savePersonalSettings();
}

void StrategyPanel::selectViewSymbol(const QString& symbol, const QString& name)
{
    if (!isValidMarketSymbol(symbol)) {
        return;
    }
    selectSymbol(symbol, name.isEmpty() ? stockNameForSymbol(symbol) : name, true);
}

void StrategyPanel::addFavorite(const QString& symbol, const QString& name)
{
    if (!isValidMarketSymbol(symbol)) {
        return;
    }
    addFavoriteSymbol(symbol, name.isEmpty() ? stockNameForSymbol(symbol) : name);
    savePersonalSettings();
}

void StrategyPanel::onRemoveFavoriteClicked()
{
    if (!m_watchlistWidget || !m_watchlistWidget->currentItem()) {
        return;
    }

    const QString symbol = m_watchlistWidget->currentItem()->data(Qt::UserRole).toString();
    m_favorites.removeAll(symbol);
    saveWatchlist();
    refreshWatchlist();
    savePersonalSettings();
}

void StrategyPanel::onFavoriteActivated(QListWidgetItem* item)
{
    if (!item) {
        return;
    }

    const QString symbol = item->data(Qt::UserRole).toString();
    selectSymbol(symbol, stockNameForSymbol(symbol), true);
    emit favoriteSelected(symbol);
}

void StrategyPanel::onFavoriteSelectionChanged()
{
    const bool hasSelection = m_watchlistWidget && m_watchlistWidget->currentItem();
    if (m_removeFavoriteBtn) {
        m_removeFavoriteBtn->setEnabled(hasSelection);
    }
    if (m_buyFavoriteBtn) {
        m_buyFavoriteBtn->setEnabled(hasSelection);
    }
    if (m_sellFavoriteBtn) {
        m_sellFavoriteBtn->setEnabled(hasSelection);
    }
    if (!m_loadingSettings) {
        savePersonalSettings();
    }
    if (hasSelection) {
        emit favoriteSelected(selectedFavoriteSymbol());
    }
}

void StrategyPanel::onFavoriteBuyClicked()
{
    const QString symbol = selectedFavoriteSymbol();
    if (symbol.isEmpty()) {
        addSystemLog(QStringLiteral("请选择有效股票。"));
        return;
    }
    const double price = m_manualPriceSpin ? m_manualPriceSpin->value() : 0.0;
    const double volume = m_manualVolumeSpin ? m_manualVolumeSpin->value() : getLotSize();
    emit favoriteBuyRequested(symbol, price, volume);
}

void StrategyPanel::onFavoriteSellClicked()
{
    const QString symbol = selectedFavoriteSymbol();
    if (symbol.isEmpty()) {
        addSystemLog(QStringLiteral("请选择有效股票。"));
        return;
    }
    const double price = m_manualPriceSpin ? m_manualPriceSpin->value() : 0.0;
    const double volume = m_manualVolumeSpin ? m_manualVolumeSpin->value() : getLotSize();
    emit favoriteSellRequested(symbol, price, volume);
}
