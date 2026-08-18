#pragma once

#include <QWidget>
#include <QJsonObject>

#include "screener/stockscreener.h"

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QGridLayout;
class QLabel;
class QLineEdit;
class QPushButton;
class QTableWidget;

class ScreenerPanel : public QWidget
{
    Q_OBJECT

public:
    explicit ScreenerPanel(QWidget* parent = nullptr);

signals:
    void viewStockRequested(const QString& symbol, const QString& name);
    void addFavoriteRequested(const QString& symbol, const QString& name);

private slots:
    void runScreening();
    void resetCriteria();
    void applyTemplate();
    void saveStrategy();
    void loadStrategy();
    void deleteStrategy();
    void updateResults(const QVector<ScreenerStock>& results, int universeCount,
                       const QDateTime& dataAsOf);
    void updateSimilarResults(const ScreenerStock& target, const QVector<ScreenerStock>& results,
                              const QDateTime& dataAsOf);
    void setLoading(bool loading);
    void showFailure(const QString& message);
    void findSimilarStock();
    void viewSelectedStock();
    void addSelectedStockToFavorite();

private:
    struct RangeWidgets {
        QCheckBox* enabled = nullptr;
        QDoubleSpinBox* minimum = nullptr;
        QDoubleSpinBox* maximum = nullptr;
    };

    RangeWidgets addRangeControl(QGridLayout* layout, int row, const QString& label,
                                 const QString& suffix, double minimum, double maximum,
                                 int decimals = 2);
    ScreenerCriteria currentCriteria() const;
    void applyCriteria(const ScreenerCriteria& criteria);
    void updateRangeState(const RangeWidgets& widgets);
    void refreshSavedStrategies();
    void populateResults(const QVector<ScreenerStock>& results, bool similarMode);
    ScreenerStock selectedStock() const;
    static QJsonObject criteriaToJson(const ScreenerCriteria& criteria);
    static ScreenerCriteria criteriaFromJson(const QJsonObject& object);

    StockScreener* m_screener;
    QComboBox* m_marketScope = nullptr;
    QCheckBox* m_excludeSt = nullptr;
    QCheckBox* m_excludeSuspended = nullptr;
    QComboBox* m_templateCombo = nullptr;
    QComboBox* m_savedStrategyCombo = nullptr;
    QLineEdit* m_similarStockEdit = nullptr;
    QPushButton* m_runButton = nullptr;
    QPushButton* m_findSimilarButton = nullptr;
    QPushButton* m_cancelButton = nullptr;
    QLabel* m_statusLabel = nullptr;
    QTableWidget* m_resultTable = nullptr;
    QVector<ScreenerStock> m_results;
    QString m_pendingSimilarQuery;
    ScreenerCriteria m_pendingSimilarCriteria;
    RangeWidgets m_priceRange;
    RangeWidgets m_changeRange;
    RangeWidgets m_marketCapRange;
    RangeWidgets m_peRange;
    RangeWidgets m_pbRange;
    RangeWidgets m_turnoverRange;
};
