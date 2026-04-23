#pragma once

#include <QRect>
#include <QMetaType>
#include <QString>

#include <vector>

class Options {
public:
    int stepSize = 20;
    QRect scanArea = QRect(0, 0, 200, 200);
    bool previewEnabled = true;
    QString acquisitionDevice = QStringLiteral("PixelinkConnect");
    QString calibrationName;
    QString motorPortX;
    QString motorPortY;
    std::vector<double> exposureSequence;  // Exposure times in milliseconds for each measurement per point
};

Q_DECLARE_METATYPE(Options)
