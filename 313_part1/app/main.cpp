#include "MainWindow.h"
#include "Logger.h"
#include "VisionWorker.h"

#include <QApplication>
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
        Logger::LogInfo("System started in HEADLESS mode.");

        QThread worker_thread;
        VisionWorker worker;
        worker.moveToThread(&worker_thread);

        QObject::connect(&worker_thread, &QThread::started, &worker, &VisionWorker::start);
        QObject::connect(&worker, &VisionWorker::finished, &worker_thread, &QThread::quit);

        worker_thread.start();
        return worker_thread.wait() ? 0 : 1;
    }

    QApplication app(argc, argv);

    MainWindow window;
    window.show();

    auto* worker_thread = new QThread(&app);
    auto* worker = new VisionWorker();
    worker->moveToThread(worker_thread);

    QObject::connect(worker_thread, &QThread::started, worker, &VisionWorker::start);
    QObject::connect(worker, &VisionWorker::frameReady, &window, &MainWindow::displayFrame);
    QObject::connect(worker, &VisionWorker::finished, worker_thread, &QThread::quit);
    QObject::connect(worker, &VisionWorker::finished, worker, &VisionWorker::deleteLater);
    QObject::connect(worker_thread, &QThread::finished, worker_thread, &QThread::deleteLater);
    QObject::connect(&app, &QApplication::aboutToQuit, worker, &VisionWorker::stop, Qt::DirectConnection);

    worker_thread->start();

    const int exit_code = app.exec();
    worker->stop();
    worker_thread->quit();
    worker_thread->wait();

    return exit_code;
}
