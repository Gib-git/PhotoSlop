#pragma once

#include <QColor>
#include <QObject>

// The values behind Edit > Preferences, with their defaults.
struct PreferenceValues {
    enum class PaintingCursor { Standard, Precise, BrushTip };
    enum class OtherCursor { Standard, Precise };
    enum class CheckerSize { None, Small, Medium, Large };
    enum class CheckerColors { Light, Medium, Dark };
    enum class GridStyle { Lines, DashedLines, Dots };
    enum class FontSize { Small, Medium, Large };

    // General
    bool showHomeScreen = true;
    bool zoomWithScrollWheel = false;
    // Interface (the font size applies at the next start)
    FontSize uiFontSize = FontSize::Medium;
    // Performance
    int historyStates = 50;
    bool useGraphicsProcessor = true;
    // Cursors
    PaintingCursor paintingCursor = PaintingCursor::BrushTip;
    bool brushCrosshair = false;
    OtherCursor otherCursors = OtherCursor::Standard;
    // Transparency
    CheckerSize checkerSize = CheckerSize::Medium;
    CheckerColors checkerColors = CheckerColors::Light;
    // Guides and grid
    QColor guideColor{74, 255, 255};
    QColor gridColor{150, 150, 150};
    GridStyle gridStyle = GridStyle::Lines;
    // File Handling
    int recentFileCount = 20;
};

// Edit > Preferences (Ctrl+K): application settings kept between sessions. Edit the public
// fields, then call notify() to save them and tell the windows to refresh.
class Preferences : public QObject, public PreferenceValues {
    Q_OBJECT
public:
    static Preferences& instance();

    // Pixel size of the UI font.
    int uiFontPixels() const;
    // The two checkerboard colours (light first) and the square size in screen pixels (0 = none).
    QColor checkerLight() const;
    QColor checkerDark() const;
    int checkerPixels() const;

    void load();
    // Saves the fields and emits changed().
    void notify();
    // Puts every field back to its default (call notify() to save).
    void resetToDefaults();

signals:
    void changed();

private:
    explicit Preferences(QObject* parent = nullptr);
};
