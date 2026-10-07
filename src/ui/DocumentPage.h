#pragma once

#include <QWidget>

class CanvasView;
class Document;
class QLabel;
class QLineEdit;
class Ruler;
class ToolManager;
class ViewOptions;

// One document tab: the canvas plus Photoshop's per-document status bar.
class DocumentPage : public QWidget {
    Q_OBJECT
public:
    DocumentPage(Document* doc, ToolManager* tools, ViewOptions* options, QWidget* parent = nullptr);
    ~DocumentPage() override;
    Document* document() const { return m_doc; }
    CanvasView* view() const { return m_view; }
    // Title shown on the tab: "name @ 100% (Layer 1, RGB/8) *", or "(Quick Mask/8)".
    QString tabTitle() const;

signals:
    void titleChanged();

private:
    void updateStatus();
    void updateRulers();
    Document* m_doc;
    CanvasView* m_view;
    ViewOptions* m_options;
    Ruler* m_hRuler;
    Ruler* m_vRuler;
    QWidget* m_corner;
    QLineEdit* m_zoom;
    QLabel* m_info;
};

QString formatZoom(double zoom);
