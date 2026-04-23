#pragma once

#include <QString>

#include <opencv2/core.hpp>

#include <vector>

class IDataAcquisition {
public:
    virtual ~IDataAcquisition() = default;

    // Returns ready-to-use spectrum values computed by the backend.
    virtual std::vector<double> getSpectrum() = 0;

    // Optional image frame for diagnostics/preview.
    virtual cv::Mat getImage() = 0;

    virtual QString name() const = 0;

    // Set exposure time in milliseconds (default implementation is no-op for mock backends)
    virtual void setExposure(double exposureMs) {
        (void)exposureMs;  // Avoid unused parameter warning
    }

    // Get available calibration names for this device
    virtual std::vector<QString> getAvailableCalibrations() const = 0;

    // Apply calibration by name (e.g., "Dark", "Reference", "Wavelength" for Pixelink)
    virtual void setCalibration(const QString& calibrationName) {
        (void)calibrationName;  // Avoid unused parameter warning
    }

    // Get current calibration name
    virtual QString getCurrentCalibration() const {
        return QString();
    }
};
