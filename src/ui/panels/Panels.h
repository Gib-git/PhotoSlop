#pragma once

#include <QColor>
#include <QList>
#include <QPointer>
#include <QWidget>
#include <functional>

class CanvasView;
class ColorState;
class Document;
class QAction;
class QLabel;
class QSlider;
class QSpinBox;
class QUndoGroup;

// Horizontal gradient slider used by the Color panel (R, G, B channels).
class ChannelSlider : public QWidget {
    Q_OBJECT
public:
    explicit ChannelSlider(QWidget* parent = nullptr);
    void setGradient(const QColor& from, const QColor& to);
    void setValue(int v);
    int value() const { return m_value; }
    QSize sizeHint() const override { return QSize(160, 16); }
signals:
    void valueChanged(int v);

protected:
    void paintEvent(QPaintEvent* e) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;

private:
    void pick(double x);
    QColor m_from, m_to;
    int m_value = 0;
};

class ColorPanel : public QWidget {
    Q_OBJECT
public:
    explicit ColorPanel(ColorState* colors, QWidget* parent = nullptr);

protected:
    bool eventFilter(QObject* obj, QEvent* e) override;

private:
    void sync();
    ColorState* m_colors;
    ChannelSlider* m_sliders[3];
    QSpinBox* m_spins[3];
    QLabel* m_ramp;
    bool m_editBackground = false;
    bool m_syncing = false;
};

class SwatchesPanel : public QWidget {
    Q_OBJECT
public:
    explicit SwatchesPanel(ColorState* colors, QWidget* parent = nullptr);
    void addSwatch(const QColor& c);

protected:
    void paintEvent(QPaintEvent* e) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void resizeEvent(QResizeEvent* e) override;
    bool event(QEvent* e) override;

private:
    int indexAt(const QPoint& p) const;
    QRect cellRect(int i) const;
    void save();
    ColorState* m_colors;
    QList<QColor> m_swatches;
    int m_columns = 10;
};

class NavigatorPanel : public QWidget {
    Q_OBJECT
public:
    explicit NavigatorPanel(QWidget* parent = nullptr);
    void setView(CanvasView* view);

protected:
    bool eventFilter(QObject* obj, QEvent* e) override;

private:
    QRectF imageRect() const;
    void syncZoom();
    QPointer<CanvasView> m_view;
    QWidget* m_canvas;
    QSpinBox* m_zoomField;
    QSlider* m_slider;
    bool m_syncing = false;
};

class InfoPanel : public QWidget {
    Q_OBJECT
public:
    explicit InfoPanel(QWidget* parent = nullptr);
    void setView(CanvasView* view);

private:
    void showAt(const QPointF& pos, bool inside);
    QPointer<CanvasView> m_view;
    QLabel* m_rgb;
    QLabel* m_second;
    QLabel* m_xy;
    QLabel* m_wh;
    QLabel* m_doc;
};

class HistoryPanel : public QWidget {
    Q_OBJECT
public:
    HistoryPanel(QUndoGroup* group, QWidget* parent = nullptr);
    void setDocument(Document* doc);
    // History > context menu: the History Brush paints from this state (0 = as opened).
    void setHistoryBrushSource(int state);
    int historyBrushSource() const { return m_source; }

private:
    class QUndoView* m_view;
    QPointer<Document> m_doc;
    int m_source = 0;
};

class PropertiesPanel : public QWidget {
    Q_OBJECT
public:
    explicit PropertiesPanel(QWidget* parent = nullptr);
    void setDocument(Document* doc);
    void setActionLookup(std::function<QAction*(const QString&)> lookup);

private:
    void refresh();
    void setButtons(const QStringList& actionIds);
    QPointer<Document> m_doc;
    QLabel* m_title;
    QLabel* m_body;
    QWidget* m_buttons;
    QStringList m_buttonIds;
    std::function<QAction*(const QString&)> m_lookup;
};

class ChannelsPanel : public QWidget {
    Q_OBJECT
public:
    explicit ChannelsPanel(QWidget* parent = nullptr);
    void setDocument(Document* doc);

protected:
    void paintEvent(QPaintEvent* e) override;

private:
    QPointer<Document> m_doc;
    class QTimer* m_timer;
    QList<QImage> m_thumbs;
    QStringList m_names;
    void rebuild();
};

// Simple panel with a centred hint, used for panels arriving in later stages.
class PlaceholderPanel : public QWidget {
    Q_OBJECT
public:
    PlaceholderPanel(const QString& text, QWidget* parent = nullptr);
};

// Buttons that add adjustment layers (Layer > New Adjustment Layer).
class AdjustmentsPanel : public QWidget {
    Q_OBJECT
public:
    explicit AdjustmentsPanel(QWidget* parent = nullptr);
    void setActionLookup(std::function<QAction*(const QString&)> lookup);

private:
    QList<QPair<class QToolButton*, QString>> m_buttons;
};
