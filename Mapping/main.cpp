#include <QApplication>
#include <QMetaType>
#include <QPalette>
#include <QTimer>
#include <QStyleFactory>
#include <QColor>

#include <opencv2/core.hpp>

#include <vector>

#include "Mapping.h"
#include "Options.h"
#include "worker.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setStyle(QStyleFactory::create(QStringLiteral("Fusion")));

    QPalette palette;
    palette.setColor(QPalette::Window, QColor(28, 28, 32));
    palette.setColor(QPalette::WindowText, Qt::white);
    palette.setColor(QPalette::Base, QColor(20, 20, 24));
    palette.setColor(QPalette::AlternateBase, QColor(34, 34, 40));
    palette.setColor(QPalette::ToolTipBase, Qt::white);
    palette.setColor(QPalette::ToolTipText, Qt::white);
    palette.setColor(QPalette::Text, Qt::white);
    palette.setColor(QPalette::Button, QColor(38, 38, 44));
    palette.setColor(QPalette::ButtonText, Qt::white);
    palette.setColor(QPalette::BrightText, Qt::red);
    palette.setColor(QPalette::Highlight, QColor(70, 120, 200));
    palette.setColor(QPalette::HighlightedText, Qt::white);
    app.setPalette(palette);

    qRegisterMetaType<std::vector<double>>("std::vector<double>");
    qRegisterMetaType<cv::Mat>("cv::Mat");
    qRegisterMetaType<Options>("Options");
    qRegisterMetaType<State>("State");

    Mapping window;
    window.show();
    QTimer::singleShot(0, &window, [&window]() {
        window.showMaximized();
    });

    return app.exec();
}
