#include "HeatMap.h"

#include <QtGlobal>

void HeatMap::configure(const Options& options)
{
    m_options = options;
}

void HeatMap::clear()
{
    m_values.clear();
}

void HeatMap::addSample(const QPoint& position, const std::vector<double>& spectrum)
{
    m_values.insert(keyForPoint(position), average(spectrum));
}

double HeatMap::valueAt(const QPoint& position) const
{
    return m_values.value(keyForPoint(position), 0.0);
}

QVector<QVector<double>> HeatMap::toMatrix() const
{
    QVector<QVector<double>> matrix;

    const int step = qMax(1, m_options.stepSize);
    const int cols = qMax(1, (m_options.scanArea.width() / step) + 1);
    const int rows = qMax(1, (m_options.scanArea.height() / step) + 1);

    matrix.resize(rows);
    for (int r = 0; r < rows; ++r) {
        matrix[r].resize(cols);
    }

    const int left = m_options.scanArea.left();
    const int top = m_options.scanArea.top();

    for (int r = 0; r < rows; ++r) {
        for (int c = 0; c < cols; ++c) {
            const QPoint p(left + c * step, top + r * step);
            matrix[r][c] = valueAt(p);
        }
    }

    return matrix;
}

QString HeatMap::keyForPoint(const QPoint& point)
{
    return QStringLiteral("%1:%2").arg(point.x()).arg(point.y());
}

double HeatMap::average(const std::vector<double>& values)
{
    if (values.empty()) {
        return 0.0;
    }

    double sum = 0.0;
    for (const double value : values) {
        sum += value;
    }
    return sum / static_cast<double>(values.size());
}
