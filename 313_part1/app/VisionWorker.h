#pragma once

#include "VisionProcessor.h"

#include <QImage>
#include <QObject>
#include <QString>

#include <atomic>

class VisionWorker : public QObject {
    Q_OBJECT

public:
    explicit VisionWorker(QObject* parent = nullptr);

public slots:
    void start();
    void stop();
    void emergencyStop();
    void updateRuntimeTuning(double filter_alpha, double half_width_mm, double half_height_mm);
    void saveConfig();

signals:
    void frameReady(const QImage& image);
    void telemetryReady(
        double x_mm,
        double y_mm,
        double z_mm,
        bool coordinate_valid,
        bool network_online,
        qint64 heartbeat_age_ms,
        const QString& system_state);
    void finished();
    void configSaved(bool success);

private:
    static QImage matToQImage(const cv::Mat& frame);

    std::atomic<bool> running_{ false };
    VisionProcessor processor_;
};
