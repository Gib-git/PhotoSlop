#pragma once

#include "core/BlendMode.h"

#include <QComboBox>
#include <QLabel>
#include <QSpinBox>
#include <QToolButton>
#include <QWidget>

// A label that changes a spin box when dragged horizontally ("scrubby slider").
class ScrubbyLabel : public QLabel {
    Q_OBJECT
public:
    ScrubbyLabel(const QString& text, QSpinBox* target, QWidget* parent = nullptr);

protected:
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;

private:
    QSpinBox* m_target;
    int m_startX = 0;
    int m_startValue = 0;
    bool m_dragging = false;
};

// "Opacity: [100%][v]" — scrubby label, numeric field and an optional pop-up slider.
class ValueField : public QWidget {
    Q_OBJECT
public:
    ValueField(const QString& label, int min, int max, const QString& suffix, bool popupSlider,
               QWidget* parent = nullptr);
    int value() const;
    void setValue(int v);        // does not emit
    void setFieldWidth(int w);
    QSpinBox* spinBox() const { return m_spin; }

signals:
    void valueChanged(int value);

private:
    void showPopup();
    QSpinBox* m_spin;
    QToolButton* m_drop = nullptr;
};

// Blend-mode combo box with Photoshop's group separators.
class BlendModeCombo : public QComboBox {
    Q_OBJECT
public:
    explicit BlendModeCombo(QWidget* parent = nullptr, bool includeClearBehind = false);
    BlendMode mode() const;
    void setMode(BlendMode mode); // does not emit
    // Groups add Pass Through at the top of the list.
    void setPassThroughAllowed(bool allowed);
signals:
    void modeChanged(BlendMode mode);
};

// A colour swatch that opens the Color Picker when clicked. With `optional`, a "None" entry
// (shown as a red slash) is allowed: Shift-click toggles it.
class ColorButton : public QToolButton {
    Q_OBJECT
public:
    explicit ColorButton(const QString& pickerTitle, QWidget* parent = nullptr);
    QColor color() const { return m_color; }
    void setColor(const QColor& c); // does not emit
    bool isNone() const { return m_none; }
    void setNone(bool none);        // does not emit
    void setOptional(bool optional) { m_optional = optional; }

signals:
    void colorChanged(const QColor& c);
    void noneChanged(bool none);

protected:
    void paintEvent(QPaintEvent* e) override;
    void mousePressEvent(QMouseEvent* e) override;

private:
    QString m_title;
    QColor m_color = Qt::black;
    bool m_none = false;
    bool m_optional = false;
};

// Flat icon-only tool button used throughout the panels and options bar.
QToolButton* makeIconButton(const QString& icon, const QString& tooltip, QWidget* parent,
                            bool checkable = false, int size = 22);
// Vertical separator line for horizontal toolbars.
QWidget* makeVSeparator(QWidget* parent);
