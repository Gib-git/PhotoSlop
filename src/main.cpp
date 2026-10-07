#include "app/Theme.h"
#include "ui/MainWindow.h"
#include "ui/Workspace.h"

#include <QApplication>
#include <QElapsedTimer>
#include <QFileOpenEvent>
#include <QPainter>
#include <QPointer>
#include <QSplashScreen>
#include <QThread>
#include <QTimer>
#include <algorithm>
#include <iterator>

namespace {

// Handles macOS "open with" / double-click in Finder.
class PhotoSlopApp : public QApplication {
public:
    using QApplication::QApplication;
    QPointer<MainWindow> window;
    QStringList pending;

protected:
    bool event(QEvent* e) override
    {
        if (e->type() == QEvent::FileOpen) {
            const QString f = static_cast<QFileOpenEvent*>(e)->file();
            if (window) window->openFiles({f});
            else pending << f;
            return true;
        }
        return QApplication::event(e);
    }
};

QPixmap splashPixmap(qreal dpr)
{
    const QSize size(620, 340);
    QPixmap pm(size * dpr);
    pm.setDevicePixelRatio(dpr);
    pm.fill(QColor(0x1f, 0x1f, 0x1f));
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    // Logo card.
    p.setBrush(Qt::white);
    p.setPen(Qt::NoPen);
    p.drawRoundedRect(QRectF(28, 28, 250, 284), 14, 14);
    p.drawPixmap(QPointF(38, 52), logoPixmap(230, dpr));
    // Text.
    p.setPen(QColor(0xf2, 0xf2, 0xf2));
    QFont f = QApplication::font();
    f.setPixelSize(40);
    f.setWeight(QFont::Light);
    p.setFont(f);
    p.drawText(QRectF(306, 70, 300, 50), Qt::AlignLeft | Qt::AlignVCenter, QStringLiteral("PhotoSlop"));
    f.setPixelSize(15);
    f.setItalic(true);
    f.setWeight(QFont::Normal);
    p.setFont(f);
    p.setPen(QColor(0xb8, 0xb8, 0xb8));
    p.drawText(QRectF(308, 122, 300, 24), Qt::AlignLeft | Qt::AlignVCenter, kSlogan);
    f.setItalic(false);
    f.setPixelSize(11);
    p.setFont(f);
    p.setPen(QColor(0x80, 0x80, 0x80));
    p.drawText(QRectF(308, 280, 300, 18), Qt::AlignLeft, QStringLiteral("Version %1").arg(QApplication::applicationVersion()));
    // Accent bar.
    p.fillRect(QRectF(306, 160, 60, 3), QColor(0x4f, 0x7f, 0xdf));
    return pm;
}

} // namespace

int main(int argc, char** argv)
{
    QApplication::setOrganizationName(QStringLiteral("PhotoSlop"));
    QApplication::setApplicationName(QStringLiteral("PhotoSlop"));
    QApplication::setApplicationVersion(QStringLiteral("0.1.0"));
    PhotoSlopApp app(argc, argv);
    Theme::apply(app);
    app.setWindowIcon(Theme::icon(QStringLiteral("app")));

    QSplashScreen splash(splashPixmap(app.devicePixelRatio()));
    splash.show();
    QElapsedTimer shown;
    shown.start();
    auto message = [&](const QString& m) {
        splash.showMessage(m, Qt::AlignBottom | Qt::AlignRight, QColor(0x9a, 0x9a, 0x9a));
        app.processEvents();
    };
    message(QStringLiteral("Initializing tools..."));

    MainWindow w;
    app.window = &w;
    message(QStringLiteral("Building panels..."));

    QStringList files = app.arguments().mid(1);
    files += app.pending;
    message(QStringLiteral("Reading preferences..."));
    // Keep the splash up long enough to be read.
    while (shown.elapsed() < 1200) {
        app.processEvents(QEventLoop::AllEvents, 50);
        QThread::msleep(15);
    }
    w.show();
    splash.finish(&w);
    if (!files.isEmpty()) w.openFiles(files);

    // Debug hook for automated UI checks: PHOTOSLOP_SCREENSHOT=out.png grabs the window and quits.
    const QString shot = qEnvironmentVariable("PHOTOSLOP_SCREENSHOT");
    if (!shot.isEmpty()) {
        QTimer::singleShot(qEnvironmentVariableIntValue("PHOTOSLOP_SCREENSHOT_DELAY") > 0
                               ? qEnvironmentVariableIntValue("PHOTOSLOP_SCREENSHOT_DELAY")
                               : 1500,
                           &w, [&w, shot] {
                               w.grab().save(shot);
                               QApplication::quit();
                           });
    }
    return app.exec();
}
