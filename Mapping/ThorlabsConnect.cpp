#include "ThorlabsConnect.h"

#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cmath>

ThorlabsConnect::ThorlabsConnect()
    : m_rng(std::random_device{}())
{
}

std::vector<double> ThorlabsConnect::getSpectrum()
{
    cv::Mat img = getImage();
    if (img.empty()) {
        return {};
    }

    cv::Mat projection;
    cv::reduce(img, projection, 1, cv::REDUCE_AVG, CV_64F);

    std::vector<double> spectrum;
    spectrum.reserve(static_cast<size_t>(projection.rows));
    for (int y = 0; y < projection.rows; ++y) {
        spectrum.push_back(projection.at<double>(y, 0));
    }
    return spectrum;
}

cv::Mat ThorlabsConnect::getImage()
{
    cv::Mat image(160, 160, CV_8UC1, cv::Scalar(10));
    std::uniform_int_distribution<int> spikeDist(0, image.rows - 1);

    for (int x = 0; x < image.cols; ++x) {
        const double envelope = 180.0 * std::exp(-std::pow((x - 80.0) / 30.0, 2.0));
        for (int y = 0; y < image.rows; ++y) {
            const double line = envelope * std::exp(-std::pow((y - 80.0) / 22.0, 2.0));
            image.at<uchar>(y, x) = static_cast<uchar>(std::clamp(line + 12.0, 0.0, 255.0));
        }
    }

    for (int i = 0; i < 100; ++i) {
        image.at<uchar>(spikeDist(m_rng), spikeDist(m_rng)) = 255;
    }

    cv::GaussianBlur(image, image, cv::Size(3, 3), 0.0);
    return image;
}

QString ThorlabsConnect::name() const
{
    return QStringLiteral("ThorlabsConnect");
}

std::vector<QString> ThorlabsConnect::getAvailableCalibrations() const
{
    return { QStringLiteral("Gold") };
}

void ThorlabsConnect::setCalibration(const QString& calibrationName)
{
    if (calibrationName == QStringLiteral("Gold")) {
        m_currentCalibration = calibrationName;
    }
}

QString ThorlabsConnect::getCurrentCalibration() const
{
    return m_currentCalibration;
}

