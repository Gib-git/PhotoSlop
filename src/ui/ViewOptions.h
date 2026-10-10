#pragma once

#include <QObject>

// View ▸ settings shared by every document window: extras, rulers, grid, guides and snapping.
// Edit the public fields, then call notify() to save them and refresh the views.
class ViewOptions : public QObject {
    Q_OBJECT
public:
    enum class Units { Pixels, Inches, Centimeters, Millimeters, Percent };

    explicit ViewOptions(QObject* parent = nullptr);

    bool extras = true; // master switch for the items below marked (extra)
    bool selectionEdges = true; // (extra)
    bool pixelGrid = true; // (extra)
    bool grid = false; // (extra)
    bool guides = true; // (extra)
    bool rulers = false;
    bool lockGuides = false;
    bool snap = true;
    bool snapGuides = true;
    bool snapGrid = true;
    bool snapBounds = true;
    Units units = Units::Pixels;
    // Photoshop's defaults: a gridline every inch, four subdivisions.
    double gridEvery = 1.0;
    Units gridUnits = Units::Inches;
    int gridSubdivisions = 4;
    // Canvas pixels between major gridlines.
    double gridSpacing(double dpi, int docWidth) const;

    bool showsSelectionEdges() const { return extras && selectionEdges; }
    bool showsPixelGrid() const { return extras && pixelGrid; }
    bool showsGrid() const { return extras && grid; }
    bool showsGuides() const { return extras && guides; }

    void notify();

signals:
    void changed();
};
