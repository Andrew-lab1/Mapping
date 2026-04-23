#include "HeatMapWindow.h"

#include <QAbstractItemView>
#include <QColor>
#include <QHeaderView>
#include <QLabel>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>

#include <limits>

HeatMapWindow::HeatMapWindow(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(QStringLiteral("HeatMap"));
    resize(900, 650);
    setMinimumSize(800, 600);

    auto* layout = new QVBoxLayout(this);
    m_infoLabel = new QLabel(QStringLiteral("No data"), this);
    m_table = new QTableWidget(this);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSelectionMode(QAbstractItemView::NoSelection);
    m_table->setAlternatingRowColors(true);
    m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_table->verticalHeader()->setSectionResizeMode(QHeaderView::Stretch);

    layout->addWidget(m_infoLabel);
    layout->addWidget(m_table, 1);
}

void HeatMapWindow::setHeatMapData(const QVector<QVector<double>>& matrix, const QPoint& selectedPoint)
{
    m_table->clear();
    m_table->setRowCount(matrix.size());
    m_table->setColumnCount(matrix.isEmpty() ? 0 : matrix.first().size());

    double minValue = (std::numeric_limits<double>::max)();
    double maxValue = (std::numeric_limits<double>::lowest)();

    for (const QVector<double>& row : matrix) {
        for (double value : row) {
            minValue = qMin(minValue, value);
            maxValue = qMax(maxValue, value);
        }
    }

    if (matrix.isEmpty()) {
        minValue = 0.0;
        maxValue = 0.0;
    }

    for (int r = 0; r < matrix.size(); ++r) {
        for (int c = 0; c < matrix[r].size(); ++c) {
            const double value = matrix[r][c];
            auto* item = new QTableWidgetItem(QString::number(value, 'f', 2));
            item->setBackground(colorForValue(value, minValue, maxValue));
            m_table->setItem(r, c, item);
        }
    }

    m_infoLabel->setText(QStringLiteral("Selected point: (%1, %2), min=%3 max=%4")
        .arg(selectedPoint.x())
        .arg(selectedPoint.y())
        .arg(QString::number(minValue, 'f', 2))
        .arg(QString::number(maxValue, 'f', 2)));
}

QColor HeatMapWindow::colorForValue(double value, double minValue, double maxValue) const
{
    if (qFuzzyCompare(minValue + 1.0, maxValue + 1.0)) {
        return QColor(60, 120, 220);
    }

    const double ratio = qBound(0.0, (value - minValue) / (maxValue - minValue), 1.0);
    const int hue = int((1.0 - ratio) * 240.0);
    return QColor::fromHsv(hue, 220, 240);
}
