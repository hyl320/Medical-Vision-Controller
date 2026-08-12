#include "MainWindow.h"

#include <QFrame>
#include <QVBoxLayout>
#include <QPixmap>
#include <QWidget>

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent) {
    setWindowTitle("Medical Vision Controller");

    auto* central_widget = new QWidget(this);
    auto* layout = new QVBoxLayout(central_widget);
    layout->setContentsMargins(16, 16, 16, 16);
    layout->setAlignment(Qt::AlignCenter);

    video_label_ = new QLabel(central_widget);
    video_label_->setFixedSize(640, 640);
    video_label_->setAlignment(Qt::AlignCenter);
    video_label_->setText("Video Canvas");
    video_label_->setFrameShape(QFrame::Box);
    video_label_->setStyleSheet(
        "QLabel {"
        "background-color: #101418;"
        "color: #d7dde5;"
        "border: 1px solid #4d5966;"
        "font-size: 20px;"
        "}"
    );

    layout->addWidget(video_label_);
    setCentralWidget(central_widget);
    resize(720, 720);
}

void MainWindow::displayFrame(const QImage& image) {
    if (image.isNull() || !video_label_) {
        return;
    }

    video_label_->setPixmap(QPixmap::fromImage(image).scaled(
        video_label_->size(),
        Qt::KeepAspectRatio,
        Qt::SmoothTransformation
    ));
}
