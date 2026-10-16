#pragma once

#include <QImage>
#include <QLabel>
#include <QMainWindow>

class QGridLayout;
class QLCDNumber;
class QDoubleSpinBox;
class QPushButton;
class QResizeEvent;
class QScrollArea;
class QSlider;
class QTimer;
class QWidget;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);

public slots:
    void displayFrame(const QImage& image);
    void updateTelemetry(
        double x_mm,
        double y_mm,
        double z_mm,
        bool coordinate_valid,
        bool network_online,
        qint64 heartbeat_age_ms,
        const QString& system_state);
    void updateConfigSaveStatus(bool success);

signals:
    void emergencyStopRequested();
    void runtimeTuningChanged(double filter_alpha, double half_width_mm, double half_height_mm);
    void saveConfigRequested();

protected:
    void resizeEvent(QResizeEvent* event) override;

private:
    void applyResponsiveLayout();
    void rescaleVideoFrame();
    void updateNetworkIndicator();
    void emitRuntimeTuningChanged();

    QLabel* video_label_ = nullptr;
    QScrollArea* dashboard_scroll_area_ = nullptr;
    QWidget* dashboard_panel_ = nullptr;
    QGridLayout* content_grid_ = nullptr;
    QLCDNumber* x_display_ = nullptr;
    QLCDNumber* y_display_ = nullptr;
    QLCDNumber* z_display_ = nullptr;
    QLabel* coordinate_status_label_ = nullptr;
    QLabel* network_indicator_ = nullptr;
    QLabel* network_status_label_ = nullptr;
    QLabel* heartbeat_label_ = nullptr;
    QLabel* system_state_label_ = nullptr;
    QPushButton* emergency_stop_button_ = nullptr;
    QSlider* alpha_slider_ = nullptr;
    QDoubleSpinBox* alpha_spin_box_ = nullptr;
    QSlider* fence_width_slider_ = nullptr;
    QDoubleSpinBox* fence_width_spin_box_ = nullptr;
    QSlider* fence_height_slider_ = nullptr;
    QDoubleSpinBox* fence_height_spin_box_ = nullptr;
    QPushButton* save_config_button_ = nullptr;
    QLabel* config_save_status_label_ = nullptr;
    QTimer* network_blink_timer_ = nullptr;
    QImage last_frame_;
    bool network_online_ = false;
    bool network_blink_phase_ = true;
    bool compact_layout_ = false;
};
