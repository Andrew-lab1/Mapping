#pragma once

#include "IDataAcquisition.h"

#include <random>

class PixelinkConnect : public IDataAcquisition {
public:
    PixelinkConnect();

    std::vector<double> getSpectrum() override;
    cv::Mat getImage() override;
    QString name() const override;

    std::vector<QString> getAvailableCalibrations() const override;
    void setCalibration(const QString& calibrationName) override;
    QString getCurrentCalibration() const override;

private:
    std::mt19937 m_rng;
    QString m_currentCalibration = QStringLiteral("Dark");
};
