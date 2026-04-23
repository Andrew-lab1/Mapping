#include "PixelinkConnect.h"

#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cmath>

PixelinkConnect::PixelinkConnect()
    : m_rng(std::random_device{}())
{
}

std::vector<double> PixelinkConnect::getSpectrum()
{
    cv::Mat img = getImage();
    if (img.empty()) {
        return {};
    }

    cv::Mat projection;
    cv::reduce(img, projection, 0, cv::REDUCE_AVG, CV_64F);

    std::vector<double> spectrum;
    spectrum.reserve(static_cast<size_t>(projection.cols));
    for (int x = 0; x < projection.cols; ++x) {
        spectrum.push_back(projection.at<double>(0, x));
    }
    return spectrum;
}

cv::Mat PixelinkConnect::getImage()
{
    cv::Mat image(128, 256, CV_8UC1, cv::Scalar(20));
    std::normal_distribution<double> noise(0.0, 8.0);

    for (int y = 0; y < image.rows; ++y) {
        for (int x = 0; x < image.cols; ++x) {
            const double wave = 90.0 + 60.0 * std::sin(static_cast<double>(x) / 16.0);
            const double value = wave + noise(m_rng);
            image.at<uchar>(y, x) = static_cast<uchar>(std::clamp(value, 0.0, 255.0));
        }
    }

    cv::GaussianBlur(image, image, cv::Size(5, 5), 0.0);
    return image;
}

QString PixelinkConnect::name() const
{
    return QStringLiteral("PixelinkConnect");
}

std::vector<QString> PixelinkConnect::getAvailableCalibrations() const
{
    return {
        QStringLiteral("Reference"),
        QStringLiteral("Lines"),
        QStringLiteral("Mods")
    };
}

void PixelinkConnect::setCalibration(const QString& calibrationName)
{
    const auto calibrations = getAvailableCalibrations();
    if (std::find(calibrations.begin(), calibrations.end(), calibrationName) != calibrations.end()) {
        m_currentCalibration = calibrationName;
    }
}

QString PixelinkConnect::getCurrentCalibration() const
{
    return m_currentCalibration;
}

