#pragma once

#include "VisionProcessor.h"

#include <QImage>
#include <QObject>

#include <atomic>

class VisionWorker : public QObject {
    Q_OBJECT

public:
    explicit VisionWorker(QObject* parent = nullptr);

public slots:
    void start();
    void stop();

signals:
    void frameReady(const QImage& image);
    void finished();

private:
    static QImage matToQImage(const cv::Mat& frame);

    std::atomic<bool> running_{ false };
    VisionProcessor processor_;
};
