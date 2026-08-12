#pragma once

#include <QImage>
#include <QLabel>
#include <QMainWindow>

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);

public slots:
    void displayFrame(const QImage& image);

private:
    QLabel* video_label_ = nullptr;
};
