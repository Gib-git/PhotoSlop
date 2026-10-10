#include "app/Preferences.h"

#include <QSettings>
#include <algorithm>

Preferences& Preferences::instance()
{
    static Preferences* prefs = new Preferences;
    return *prefs;
}

Preferences::Preferences(QObject* parent)
    : QObject(parent)
{
    load();
}

int Preferences::uiFontPixels() const
{
    switch (uiFontSize) {
    case FontSize::Small: return 11;
    case FontSize::Large: return 14;
    default: return 12;
    }
}

QColor Preferences::checkerLight() const
{
    switch (checkerColors) {
    case CheckerColors::Medium: return QColor(0x99, 0x99, 0x99);
    case CheckerColors::Dark: return QColor(0x66, 0x66, 0x66);
    default: return QColor(0xff, 0xff, 0xff);
    }
}

QColor Preferences::checkerDark() const
{
    switch (checkerColors) {
    case CheckerColors::Medium: return QColor(0x66, 0x66, 0x66);
    case CheckerColors::Dark: return QColor(0x33, 0x33, 0x33);
    default: return QColor(0xcc, 0xcc, 0xcc);
    }
}

int Preferences::checkerPixels() const
{
    switch (checkerSize) {
    case CheckerSize::None: return 0;
    case CheckerSize::Small: return 4;
    case CheckerSize::Large: return 16;
    default: return 8;
    }
}

void Preferences::load()
{
    QSettings s;
    s.beginGroup(QStringLiteral("preferences"));
    auto i = [&](const char* key, int def) { return s.value(QLatin1String(key), def).toInt(); };
    auto b = [&](const char* key, bool def) { return s.value(QLatin1String(key), def).toBool(); };
    auto c = [&](const char* key, const QColor& def) {
        const QColor v(s.value(QLatin1String(key), def.name()).toString());
        return v.isValid() ? v : def;
    };
    const PreferenceValues d;
    showHomeScreen = b("showHomeScreen", d.showHomeScreen);
    zoomWithScrollWheel = b("zoomWithScrollWheel", d.zoomWithScrollWheel);
    uiFontSize = FontSize(std::clamp(i("uiFontSize", int(d.uiFontSize)), 0, 2));
    historyStates = std::clamp(i("historyStates", d.historyStates), 1, 1000);
    useGraphicsProcessor = b("useGraphicsProcessor", d.useGraphicsProcessor);
    paintingCursor = PaintingCursor(std::clamp(i("paintingCursor", int(d.paintingCursor)), 0, 2));
    brushCrosshair = b("brushCrosshair", d.brushCrosshair);
    otherCursors = OtherCursor(std::clamp(i("otherCursors", int(d.otherCursors)), 0, 1));
    checkerSize = CheckerSize(std::clamp(i("checkerSize", int(d.checkerSize)), 0, 3));
    checkerColors = CheckerColors(std::clamp(i("checkerColors", int(d.checkerColors)), 0, 2));
    guideColor = c("guideColor", d.guideColor);
    gridColor = c("gridColor", d.gridColor);
    gridStyle = GridStyle(std::clamp(i("gridStyle", int(d.gridStyle)), 0, 2));
    recentFileCount = std::clamp(i("recentFileCount", d.recentFileCount), 0, 100);
}

void Preferences::notify()
{
    QSettings s;
    s.beginGroup(QStringLiteral("preferences"));
    s.setValue(QStringLiteral("showHomeScreen"), showHomeScreen);
    s.setValue(QStringLiteral("zoomWithScrollWheel"), zoomWithScrollWheel);
    s.setValue(QStringLiteral("uiFontSize"), int(uiFontSize));
    s.setValue(QStringLiteral("historyStates"), historyStates);
    s.setValue(QStringLiteral("useGraphicsProcessor"), useGraphicsProcessor);
    s.setValue(QStringLiteral("paintingCursor"), int(paintingCursor));
    s.setValue(QStringLiteral("brushCrosshair"), brushCrosshair);
    s.setValue(QStringLiteral("otherCursors"), int(otherCursors));
    s.setValue(QStringLiteral("checkerSize"), int(checkerSize));
    s.setValue(QStringLiteral("checkerColors"), int(checkerColors));
    s.setValue(QStringLiteral("guideColor"), guideColor.name());
    s.setValue(QStringLiteral("gridColor"), gridColor.name());
    s.setValue(QStringLiteral("gridStyle"), int(gridStyle));
    s.setValue(QStringLiteral("recentFileCount"), recentFileCount);
    emit changed();
}

void Preferences::resetToDefaults()
{
    static_cast<PreferenceValues&>(*this) = PreferenceValues();
}
