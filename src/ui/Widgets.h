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
signals:
    void modeChanged(BlendMode mode);
};

// Flat icon-only tool button used throughout the panels and options bar.
QToolButton* makeIconButton(const QString& icon, const QString& tooltip, QWidget* parent,
                            bool checkable = false, int size = 22);
// Vertical separator line for horizontal toolbars.
QWidget* makeVSeparator(QWidget* parent);
