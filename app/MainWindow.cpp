#include "MainWindow.h"

#include "ConfigManager.h"

#include <algorithm>
#include <cmath>

#include <QAbstractSpinBox>
#include <QDoubleSpinBox>
#include <QFont>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLCDNumber>
#include <QPixmap>
#include <QPushButton>
#include <QResizeEvent>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QSlider>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>

namespace {
QFrame* createAxisPanel(
    const QString& axis,
    const QString& color,
    QLCDNumber*& display,
    QWidget* parent) {
    auto* panel = new QFrame(parent);
    panel->setObjectName(QStringLiteral("axisPanel"));
    panel->setMinimumHeight(132);

    auto* layout = new QVBoxLayout(panel);
    layout->setContentsMargins(18, 14, 18, 14);
    layout->setSpacing(4);

    auto* header_layout = new QHBoxLayout();
    header_layout->setContentsMargins(0, 0, 0, 0);

    auto* axis_label = new QLabel(axis, panel);
    axis_label->setStyleSheet(
        QStringLiteral("color: %1; font-size: 18px; font-weight: 700;").arg(color));

    auto* unit_label = new QLabel(QStringLiteral("MILLIMETERS"), panel);
    unit_label->setStyleSheet(
        QStringLiteral("color: #72808f; font-size: 10px; font-weight: 600;"));

    header_layout->addWidget(axis_label);
    header_layout->addStretch();
    header_layout->addWidget(unit_label);

    display = new QLCDNumber(8, panel);
    display->setSegmentStyle(QLCDNumber::Flat);
    display->setMode(QLCDNumber::Dec);
    display->setSmallDecimalPoint(true);
    display->setMinimumHeight(72);
    display->setStyleSheet(
        QStringLiteral(
            "QLCDNumber {"
            "background-color: #090d11;"
            "color: %1;"
            "border: 1px solid #26313a;"
            "border-left: 3px solid %1;"
            "}").arg(color));
    display->display(QStringLiteral("---.-"));

    layout->addLayout(header_layout);
    layout->addWidget(display);
    return panel;
}

QFrame* createTuningPanel(
    const QString& title,
    const QString& suffix,
    double minimum,
    double maximum,
    double step,
    int decimals,
    double initial_value,
    int slider_scale,
    QSlider*& slider,
    QDoubleSpinBox*& spin_box,
    QWidget* parent) {
    auto* panel = new QFrame(parent);
    panel->setObjectName(QStringLiteral("tuningPanel"));

    auto* layout = new QVBoxLayout(panel);
    layout->setContentsMargins(14, 12, 14, 12);
    layout->setSpacing(8);

    auto* header_layout = new QHBoxLayout();
    header_layout->setContentsMargins(0, 0, 0, 0);

    auto* label = new QLabel(title, panel);
    label->setObjectName(QStringLiteral("tuningTitle"));

    spin_box = new QDoubleSpinBox(panel);
    spin_box->setRange(minimum, maximum);
    spin_box->setSingleStep(step);
    spin_box->setDecimals(decimals);
    spin_box->setSuffix(suffix);
    spin_box->setValue(std::clamp(initial_value, minimum, maximum));
    spin_box->setButtonSymbols(QAbstractSpinBox::PlusMinus);
    spin_box->setMinimumWidth(112);

    slider = new QSlider(Qt::Horizontal, panel);
    slider->setRange(
        static_cast<int>(std::round(minimum * slider_scale)),
        static_cast<int>(std::round(maximum * slider_scale)));
    slider->setValue(static_cast<int>(std::round(spin_box->value() * slider_scale)));
    slider->setCursor(Qt::PointingHandCursor);

    header_layout->addWidget(label);
    header_layout->addStretch();
    header_layout->addWidget(spin_box);

    layout->addLayout(header_layout);
    layout->addWidget(slider);
    return panel;
}
}

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent) {
    const AppConfig config = ConfigManager::instance().snapshot();
    const double initial_fence_half_width = std::max(
        std::abs(config.safety.min_x_mm),
        std::abs(config.safety.max_x_mm));
    const double initial_fence_half_height = std::max(
        std::abs(config.safety.min_y_mm),
        std::abs(config.safety.max_y_mm));

    setWindowTitle("Medical Vision Controller");
    setMinimumSize(860, 720);

    auto* central_widget = new QWidget(this);
    central_widget->setObjectName(QStringLiteral("centralWidget"));
    auto* root_layout = new QVBoxLayout(central_widget);
    root_layout->setContentsMargins(22, 18, 22, 22);
    root_layout->setSpacing(16);

    auto* top_bar = new QHBoxLayout();
    top_bar->setContentsMargins(2, 0, 2, 0);

    auto* title_group = new QVBoxLayout();
    title_group->setSpacing(1);
    auto* title_label = new QLabel(QStringLiteral("MEDICAL VISION CONTROLLER"), central_widget);
    title_label->setObjectName(QStringLiteral("titleLabel"));
    auto* subtitle_label = new QLabel(QStringLiteral("REAL-TIME SPATIAL GUIDANCE SYSTEM"), central_widget);
    subtitle_label->setObjectName(QStringLiteral("subtitleLabel"));
    title_group->addWidget(title_label);
    title_group->addWidget(subtitle_label);

    system_state_label_ = new QLabel(QStringLiteral("STANDBY"), central_widget);
    system_state_label_->setObjectName(QStringLiteral("systemState"));
    system_state_label_->setAlignment(Qt::AlignCenter);
    system_state_label_->setMinimumWidth(132);

    top_bar->addLayout(title_group);
    top_bar->addStretch();
    top_bar->addWidget(system_state_label_);
    root_layout->addLayout(top_bar);

    content_grid_ = new QGridLayout();
    content_grid_->setSpacing(18);

    video_label_ = new QLabel(central_widget);
    video_label_->setMinimumSize(480, 360);
    video_label_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    video_label_->setAlignment(Qt::AlignCenter);
    video_label_->setText(QStringLiteral("AWAITING VIDEO SIGNAL"));
    video_label_->setObjectName(QStringLiteral("videoCanvas"));

    dashboard_scroll_area_ = new QScrollArea(central_widget);
    dashboard_scroll_area_->setObjectName(QStringLiteral("dashboardScroll"));
    dashboard_scroll_area_->setWidgetResizable(true);
    dashboard_scroll_area_->setFrameShape(QFrame::NoFrame);
    dashboard_scroll_area_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    dashboard_scroll_area_->setMinimumWidth(340);
    dashboard_scroll_area_->setMaximumWidth(420);
    dashboard_scroll_area_->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);

    dashboard_panel_ = new QWidget(dashboard_scroll_area_);
    dashboard_panel_->setMinimumWidth(340);
    dashboard_panel_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    auto* dashboard_layout = new QVBoxLayout(dashboard_panel_);
    dashboard_layout->setContentsMargins(0, 0, 0, 0);
    dashboard_layout->setSpacing(10);

    auto* section_header = new QHBoxLayout();
    auto* coordinates_title = new QLabel(QStringLiteral("SPATIAL COORDINATES"), dashboard_panel_);
    coordinates_title->setObjectName(QStringLiteral("sectionTitle"));
    coordinate_status_label_ = new QLabel(QStringLiteral("NO TARGET"), dashboard_panel_);
    coordinate_status_label_->setObjectName(QStringLiteral("dataStatus"));
    section_header->addWidget(coordinates_title);
    section_header->addStretch();
    section_header->addWidget(coordinate_status_label_);
    dashboard_layout->addLayout(section_header);

    dashboard_layout->addWidget(createAxisPanel(
        QStringLiteral("X AXIS"), QStringLiteral("#36d7ff"), x_display_, dashboard_panel_));
    dashboard_layout->addWidget(createAxisPanel(
        QStringLiteral("Y AXIS"), QStringLiteral("#58e39b"), y_display_, dashboard_panel_));
    dashboard_layout->addWidget(createAxisPanel(
        QStringLiteral("Z AXIS"), QStringLiteral("#ffc857"), z_display_, dashboard_panel_));

    auto* network_panel = new QFrame(dashboard_panel_);
    network_panel->setObjectName(QStringLiteral("networkPanel"));
    auto* network_layout = new QGridLayout(network_panel);
    network_layout->setContentsMargins(18, 15, 18, 15);
    network_layout->setHorizontalSpacing(12);
    network_layout->setVerticalSpacing(3);

    network_indicator_ = new QLabel(network_panel);
    network_indicator_->setFixedSize(18, 18);

    auto* network_title = new QLabel(QStringLiteral("NETWORK STATUS"), network_panel);
    network_title->setObjectName(QStringLiteral("networkTitle"));
    network_status_label_ = new QLabel(QStringLiteral("LINK OFFLINE"), network_panel);
    network_status_label_->setObjectName(QStringLiteral("networkValue"));
    heartbeat_label_ = new QLabel(QStringLiteral("Heartbeat: no signal"), network_panel);
    heartbeat_label_->setObjectName(QStringLiteral("networkDetail"));

    network_layout->addWidget(network_indicator_, 0, 0, 2, 1, Qt::AlignTop);
    network_layout->addWidget(network_title, 0, 1);
    network_layout->addWidget(network_status_label_, 1, 1);
    network_layout->addWidget(heartbeat_label_, 2, 1);
    dashboard_layout->addWidget(network_panel);

    auto* safety_label = new QLabel(QStringLiteral("SAFETY INTERLOCK"), dashboard_panel_);
    safety_label->setObjectName(QStringLiteral("sectionTitle"));
    dashboard_layout->addWidget(safety_label);

    emergency_stop_button_ = new QPushButton(QStringLiteral("EMERGENCY STOP"), dashboard_panel_);
    emergency_stop_button_->setObjectName(QStringLiteral("emergencyStop"));
    emergency_stop_button_->setCursor(Qt::PointingHandCursor);
    emergency_stop_button_->setMinimumHeight(126);
    emergency_stop_button_->setToolTip(QStringLiteral("Immediately stop all coordinate transmission"));
    dashboard_layout->addWidget(emergency_stop_button_);

    connect(emergency_stop_button_, &QPushButton::clicked, this, [this]() {
        emergency_stop_button_->setText(QStringLiteral("E-STOP ENGAGED"));
        emergency_stop_button_->setEnabled(false);
        emit emergencyStopRequested();
    });

    auto* tuning_label = new QLabel(QStringLiteral("LIVE TUNING"), dashboard_panel_);
    tuning_label->setObjectName(QStringLiteral("sectionTitle"));
    dashboard_layout->addWidget(tuning_label);

    dashboard_layout->addWidget(createTuningPanel(
        QStringLiteral("FILTER ALPHA"),
        QString(),
        0.01,
        1.0,
        0.01,
        2,
        config.control.filter_alpha,
        100,
        alpha_slider_,
        alpha_spin_box_,
        dashboard_panel_));
    dashboard_layout->addWidget(createTuningPanel(
        QStringLiteral("FENCE HALF WIDTH"),
        QStringLiteral(" mm"),
        50.0,
        500.0,
        5.0,
        1,
        initial_fence_half_width,
        10,
        fence_width_slider_,
        fence_width_spin_box_,
        dashboard_panel_));
    dashboard_layout->addWidget(createTuningPanel(
        QStringLiteral("FENCE HALF HEIGHT"),
        QStringLiteral(" mm"),
        50.0,
        500.0,
        5.0,
        1,
        initial_fence_half_height,
        10,
        fence_height_slider_,
        fence_height_spin_box_,
        dashboard_panel_));

    auto* config_actions = new QHBoxLayout();
    config_actions->setContentsMargins(0, 0, 0, 0);
    config_actions->setSpacing(10);

    save_config_button_ = new QPushButton(QStringLiteral("SAVE CURRENT CONFIG"), dashboard_panel_);
    save_config_button_->setObjectName(QStringLiteral("saveConfig"));
    save_config_button_->setCursor(Qt::PointingHandCursor);
    save_config_button_->setMinimumHeight(42);
    config_save_status_label_ = new QLabel(QStringLiteral("Runtime only"), dashboard_panel_);
    config_save_status_label_->setObjectName(QStringLiteral("configSaveStatus"));
    config_save_status_label_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);

    config_actions->addWidget(save_config_button_, 1);
    config_actions->addWidget(config_save_status_label_);
    dashboard_layout->addLayout(config_actions);

    const auto bindTuningControl = [this](QSlider* slider, QDoubleSpinBox* spin_box, double scale) {
        connect(slider, &QSlider::valueChanged, this, [this, spin_box, scale](int value) {
            const QSignalBlocker blocker(spin_box);
            spin_box->setValue(static_cast<double>(value) / scale);
            emitRuntimeTuningChanged();
        });
        connect(
            spin_box,
            qOverload<double>(&QDoubleSpinBox::valueChanged),
            this,
            [this, slider, scale](double value) {
                const QSignalBlocker blocker(slider);
                slider->setValue(static_cast<int>(std::round(value * scale)));
                emitRuntimeTuningChanged();
            });
    };

    bindTuningControl(alpha_slider_, alpha_spin_box_, 100.0);
    bindTuningControl(fence_width_slider_, fence_width_spin_box_, 10.0);
    bindTuningControl(fence_height_slider_, fence_height_spin_box_, 10.0);

    connect(save_config_button_, &QPushButton::clicked, this, [this]() {
        save_config_button_->setEnabled(false);
        config_save_status_label_->setText(QStringLiteral("Saving..."));
        emit saveConfigRequested();
    });
    dashboard_scroll_area_->setWidget(dashboard_panel_);

    root_layout->addLayout(content_grid_, 1);
    setCentralWidget(central_widget);
    resize(1240, 780);
    applyResponsiveLayout();

    setStyleSheet(QStringLiteral(
        "QWidget#centralWidget { background-color: #10151a; color: #dce5ec; }"
        "QLabel#titleLabel { color: #f1f5f8; font-size: 20px; font-weight: 700; }"
        "QLabel#subtitleLabel { color: #667684; font-size: 10px; font-weight: 600; }"
        "QLabel#systemState {"
        "  color: #ffc857; background-color: #211f17; border: 1px solid #665629;"
        "  padding: 8px 14px; font-size: 12px; font-weight: 700;"
        "}"
        "QLabel#videoCanvas {"
        "  background-color: #070a0d; color: #56636e; border: 1px solid #303b44;"
        "  font-size: 15px; font-weight: 600;"
        "}"
        "QScrollArea#dashboardScroll { background-color: transparent; border: none; }"
        "QScrollArea#dashboardScroll > QWidget > QWidget { background-color: transparent; }"
        "QLabel#sectionTitle { color: #aab7c2; font-size: 12px; font-weight: 700; }"
        "QLabel#dataStatus { color: #6f7c87; font-size: 10px; font-weight: 700; }"
        "QFrame#axisPanel, QFrame#networkPanel {"
        "  background-color: #151c22; border: 1px solid #2a353e;"
        "}"
        "QLabel#networkTitle { color: #8e9ca8; font-size: 10px; font-weight: 700; }"
        "QLabel#networkValue { color: #e7edf2; font-size: 14px; font-weight: 700; }"
        "QLabel#networkDetail { color: #71808c; font-size: 11px; }"
        "QPushButton#emergencyStop {"
        "  color: #ffffff; background-color: #df1825;"
        "  border: 5px solid #ff6670; border-bottom: 10px solid #710b13;"
        "  border-radius: 6px; padding: 10px;"
        "  font-size: 24px; font-weight: 900;"
        "}"
        "QPushButton#emergencyStop:hover {"
        "  background-color: #f02231; border-color: #ff8990;"
        "  border-bottom-color: #7d0b14;"
        "}"
        "QPushButton#emergencyStop:pressed {"
        "  background-color: #a90d18; border: 7px solid #650810;"
        "  padding-top: 16px; padding-bottom: 4px;"
        "}"
        "QPushButton#emergencyStop:disabled {"
        "  color: #fff4f4; background-color: #8f0b14;"
        "  border: 7px solid #4d080d;"
        "  padding-top: 16px; padding-bottom: 4px;"
        "}"
        "QFrame#tuningPanel {"
        "  background-color: #151c22; border: 1px solid #2a353e;"
        "}"
        "QLabel#tuningTitle { color: #d8e1e8; font-size: 11px; font-weight: 700; }"
        "QSlider::groove:horizontal {"
        "  height: 6px; background: #28333c; border-radius: 3px;"
        "}"
        "QSlider::sub-page:horizontal { background: #36d7ff; border-radius: 3px; }"
        "QSlider::handle:horizontal {"
        "  width: 18px; margin: -6px 0; background: #f1f5f8;"
        "  border: 2px solid #36d7ff; border-radius: 9px;"
        "}"
        "QDoubleSpinBox {"
        "  color: #f1f5f8; background-color: #090d11; border: 1px solid #33414b;"
        "  padding: 5px 7px; font-size: 12px;"
        "}"
        "QPushButton#saveConfig {"
        "  color: #071016; background-color: #58e39b; border: 1px solid #8df0bd;"
        "  border-radius: 4px; font-size: 12px; font-weight: 800;"
        "}"
        "QPushButton#saveConfig:hover { background-color: #6ef0aa; }"
        "QPushButton#saveConfig:disabled { color: #9ba7b0; background-color: #26333b; border-color: #33414b; }"
        "QLabel#configSaveStatus { color: #71808c; font-size: 11px; }"));

    network_blink_timer_ = new QTimer(this);
    network_blink_timer_->setInterval(450);
    connect(network_blink_timer_, &QTimer::timeout, this, [this]() {
        network_blink_phase_ = !network_blink_phase_;
        updateNetworkIndicator();
    });
    network_blink_timer_->start();
    updateNetworkIndicator();
}

void MainWindow::emitRuntimeTuningChanged() {
    if (!alpha_spin_box_ || !fence_width_spin_box_ || !fence_height_spin_box_) {
        return;
    }

    if (config_save_status_label_) {
        config_save_status_label_->setText(QStringLiteral("Unsaved"));
    }

    emit runtimeTuningChanged(
        alpha_spin_box_->value(),
        fence_width_spin_box_->value(),
        fence_height_spin_box_->value());
}

void MainWindow::updateConfigSaveStatus(bool success) {
    if (save_config_button_) {
        save_config_button_->setEnabled(true);
    }

    if (!config_save_status_label_) {
        return;
    }

    config_save_status_label_->setText(success ? QStringLiteral("Saved") : QStringLiteral("Save failed"));
    config_save_status_label_->setStyleSheet(
        success
            ? QStringLiteral("color: #58e39b; font-size: 11px;")
            : QStringLiteral("color: #ff5a66; font-size: 11px;"));
}

void MainWindow::displayFrame(const QImage& image) {
    if (image.isNull() || !video_label_) {
        return;
    }

    last_frame_ = image;
    rescaleVideoFrame();
}

void MainWindow::resizeEvent(QResizeEvent* event) {
    QMainWindow::resizeEvent(event);
    applyResponsiveLayout();
    rescaleVideoFrame();
}

void MainWindow::applyResponsiveLayout() {
    if (!content_grid_ || !video_label_ || !dashboard_scroll_area_) {
        return;
    }

    const bool should_use_compact_layout = width() < 1040;
    if (should_use_compact_layout == compact_layout_ && content_grid_->count() > 0) {
        return;
    }

    compact_layout_ = should_use_compact_layout;
    content_grid_->removeWidget(video_label_);
    content_grid_->removeWidget(dashboard_scroll_area_);

    if (compact_layout_) {
        dashboard_scroll_area_->setMaximumWidth(QWIDGETSIZE_MAX);
        dashboard_scroll_area_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        video_label_->setMinimumSize(480, 320);
        content_grid_->addWidget(video_label_, 0, 0);
        content_grid_->addWidget(dashboard_scroll_area_, 1, 0);
        content_grid_->setColumnStretch(0, 1);
        content_grid_->setColumnStretch(1, 0);
        content_grid_->setRowStretch(0, 5);
        content_grid_->setRowStretch(1, 0);
    } else {
        dashboard_scroll_area_->setMaximumWidth(420);
        dashboard_scroll_area_->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
        video_label_->setMinimumSize(480, 360);
        content_grid_->addWidget(video_label_, 0, 0);
        content_grid_->addWidget(dashboard_scroll_area_, 0, 1);
        content_grid_->setColumnStretch(0, 1);
        content_grid_->setColumnStretch(1, 0);
        content_grid_->setRowStretch(0, 1);
        content_grid_->setRowStretch(1, 0);
    }
}

void MainWindow::rescaleVideoFrame() {
    if (last_frame_.isNull() || !video_label_) {
        return;
    }

    video_label_->setPixmap(QPixmap::fromImage(last_frame_).scaled(
        video_label_->size(),
        Qt::KeepAspectRatio,
        Qt::SmoothTransformation
    ));
}

void MainWindow::updateTelemetry(
    double x_mm,
    double y_mm,
    double z_mm,
    bool coordinate_valid,
    bool network_online,
    qint64 heartbeat_age_ms,
    const QString& system_state) {
    if (coordinate_valid) {
        x_display_->display(QString::number(x_mm, 'f', 1));
        y_display_->display(QString::number(y_mm, 'f', 1));
        z_display_->display(QString::number(z_mm, 'f', 1));
        coordinate_status_label_->setText(QStringLiteral("TARGET LOCKED"));
        coordinate_status_label_->setStyleSheet(
            QStringLiteral("color: #58e39b; font-size: 10px; font-weight: 700;"));
    } else {
        x_display_->display(QStringLiteral("---.-"));
        y_display_->display(QStringLiteral("---.-"));
        z_display_->display(QStringLiteral("---.-"));
        coordinate_status_label_->setText(QStringLiteral("NO TARGET"));
        coordinate_status_label_->setStyleSheet(
            QStringLiteral("color: #6f7c87; font-size: 10px; font-weight: 700;"));
    }

    network_online_ = network_online;
    network_status_label_->setText(
        network_online_ ? QStringLiteral("UDP STREAM ACTIVE") : QStringLiteral("LINK OFFLINE"));
    network_status_label_->setStyleSheet(
        network_online_
            ? QStringLiteral("color: #58e39b; font-size: 14px; font-weight: 700;")
            : QStringLiteral("color: #ff5a66; font-size: 14px; font-weight: 700;"));
    heartbeat_label_->setText(
        heartbeat_age_ms >= 0
            ? QStringLiteral("Heartbeat latency: %1 ms").arg(heartbeat_age_ms)
            : QStringLiteral("Heartbeat: no signal"));
    updateNetworkIndicator();

    system_state_label_->setText(system_state);
    if (system_state == QStringLiteral("TRACKING")) {
        system_state_label_->setStyleSheet(
            QStringLiteral(
                "color: #58e39b; background-color: #13231d; border: 1px solid #2d7252;"
                "padding: 8px 14px; font-size: 12px; font-weight: 700;"));
    } else if (system_state == QStringLiteral("E-STOP")) {
        system_state_label_->setStyleSheet(
            QStringLiteral(
                "color: #ff6671; background-color: #2a1418; border: 1px solid #8e3039;"
                "padding: 8px 14px; font-size: 12px; font-weight: 700;"));
    } else {
        system_state_label_->setStyleSheet(
            QStringLiteral(
                "color: #ffc857; background-color: #211f17; border: 1px solid #665629;"
                "padding: 8px 14px; font-size: 12px; font-weight: 700;"));
    }
}

void MainWindow::updateNetworkIndicator() {
    const QString color = network_online_
        ? QStringLiteral("#39e58c")
        : (network_blink_phase_ ? QStringLiteral("#ff3f4d") : QStringLiteral("#571d23"));
    const QString glow = network_online_
        ? QStringLiteral("#174c34")
        : (network_blink_phase_ ? QStringLiteral("#6e2029") : QStringLiteral("#261316"));

    network_indicator_->setStyleSheet(
        QStringLiteral(
            "QLabel {"
            "background-color: %1;"
            "border: 4px solid %2;"
            "border-radius: 9px;"
            "}").arg(color, glow));
}
