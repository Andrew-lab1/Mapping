#pragma once

#include "Options.h"

#include <QMap>
#include <QPoint>
#include <QString>
#include <QVector>

#include <vector>

class HeatMap {
public:
    void configure(const Options& options);
    void clear();
    void addSample(const QPoint& position, const std::vector<double>& spectrum);
    double valueAt(const QPoint& position) const;
    QVector<QVector<double>> toMatrix() const;

private:
    static QString keyForPoint(const QPoint& point);
    static double average(const std::vector<double>& values);

    Options m_options;
    QMap<QString, double> m_values;
};
