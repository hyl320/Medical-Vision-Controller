#include "MainWindow.h"
#include "ConfigManager.h"
#include "Logger.h"
#include "VisionWorker.h"

#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QThread>

#include <string_view>

namespace {
bool hasHeadlessFlag(int argc, char* argv[]) {
    for (int i = 1; i < argc; ++i) {
        if (std::string_view(argv[i]) == "--headless") {
            return true;
        }
    }
    return false;
}
}  // namespace

int main(int argc, char* argv[]) {
    const bool headless = hasHeadlessFlag(argc, argv);

    if (headless) {
        QCoreApplication app(argc, argv);
        Logger::LogInfo("System started in HEADLESS mode.");

        auto* worker_thread = new QThread(&app);
        auto* worker = new VisionWorker();
        worker->moveToThread(worker_thread);

        QObject::connect(worker_thread, &QThread::started, worker, &VisionWorker::start);
        QObject::connect(&app, &QCoreApplication::aboutToQuit, worker, &VisionWorker::stop);
        QObject::connect(worker, &VisionWorker::finished, worker_thread, &QThread::quit);
        QObject::connect(worker, &VisionWorker::finished, worker, &VisionWorker::deleteLater);
        QObject::connect(worker_thread, &QThread::finished, worker_thread, &QThread::deleteLater);
        QObject::connect(worker_thread, &QThread::finished, &app, &QCoreApplication::quit);

        worker_thread->start();
        return app.exec();
    }

    QApplication app(argc, argv);
    const QString config_path = QDir(QCoreApplication::applicationDirPath()).filePath("config.json");
    ConfigManager::instance().load(config_path.toStdString());

    MainWindow window;
    window.show();

    auto* worker_thread = new QThread(&app);
    auto* worker = new VisionWorker();
    worker->moveToThread(worker_thread);

    QObject::connect(worker_thread, &QThread::started, worker, &VisionWorker::start);
    QObject::connect(worker, &VisionWorker::frameReady, &window, &MainWindow::displayFrame);
    QObject::connect(worker, &VisionWorker::telemetryReady, &window, &MainWindow::updateTelemetry);
    QObject::connect(
        &window,
        &MainWindow::runtimeTuningChanged,
        worker,
        &VisionWorker::updateRuntimeTuning);
    QObject::connect(
        &window,
        &MainWindow::saveConfigRequested,
        worker,
        &VisionWorker::saveConfig);
    QObject::connect(worker, &VisionWorker::configSaved, &window, &MainWindow::updateConfigSaveStatus);
    QObject::connect(
        &window,
        &MainWindow::emergencyStopRequested,
        worker,
        &VisionWorker::emergencyStop);
    QObject::connect(worker, &VisionWorker::finished, worker_thread, &QThread::quit);
    QObject::connect(worker, &VisionWorker::finished, worker, &VisionWorker::deleteLater);
    QObject::connect(worker_thread, &QThread::finished, worker_thread, &QThread::deleteLater);
    QObject::connect(&app, &QApplication::aboutToQuit, worker, &VisionWorker::stop);

    worker_thread->start();

    const int exit_code = app.exec();
    worker_thread->wait();

    return exit_code;
}
