#pragma once

#include <QDialog>
#include <QColor>
#include <QPoint>
#include <QVector>

class QLabel;
class QTableWidget;

class HeatMapWindow : public QDialog {
public:
    explicit HeatMapWindow(QWidget* parent = nullptr);

    void setHeatMapData(const QVector<QVector<double>>& matrix, const QPoint& selectedPoint);

private:
    QColor colorForValue(double value, double minValue, double maxValue) const;

    QLabel* m_infoLabel = nullptr;
    QTableWidget* m_table = nullptr;
};
