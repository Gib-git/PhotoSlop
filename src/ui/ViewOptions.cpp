#include "ui/ViewOptions.h"

#include <QSettings>

ViewOptions::ViewOptions(QObject* parent)
    : QObject(parent)
{
    QSettings s;
    s.beginGroup(QStringLiteral("view"));
    rulers = s.value(QStringLiteral("rulers"), rulers).toBool();
    snap = s.value(QStringLiteral("snap"), snap).toBool();
    snapGuides = s.value(QStringLiteral("snapGuides"), snapGuides).toBool();
    snapGrid = s.value(QStringLiteral("snapGrid"), snapGrid).toBool();
    snapBounds = s.value(QStringLiteral("snapBounds"), snapBounds).toBool();
    units = Units(s.value(QStringLiteral("units"), int(units)).toInt());
    gridEvery = s.value(QStringLiteral("gridEvery"), gridEvery).toDouble();
    gridUnits = Units(s.value(QStringLiteral("gridUnits"), int(gridUnits)).toInt());
    gridSubdivisions = s.value(QStringLiteral("gridSubdivisions"), gridSubdivisions).toInt();
}

double ViewOptions::gridSpacing(double dpi, int docWidth) const
{
    switch (gridUnits) {
    case Units::Pixels: return gridEvery;
    case Units::Centimeters: return gridEvery * dpi / 2.54;
    case Units::Millimeters: return gridEvery * dpi / 25.4;
    case Units::Percent: return gridEvery * docWidth / 100.0;
    default: return gridEvery * dpi;
    }
}

void ViewOptions::notify()
{
    QSettings s;
    s.beginGroup(QStringLiteral("view"));
    s.setValue(QStringLiteral("rulers"), rulers);
    s.setValue(QStringLiteral("snap"), snap);
    s.setValue(QStringLiteral("snapGuides"), snapGuides);
    s.setValue(QStringLiteral("snapGrid"), snapGrid);
    s.setValue(QStringLiteral("snapBounds"), snapBounds);
    s.setValue(QStringLiteral("units"), int(units));
    s.setValue(QStringLiteral("gridEvery"), gridEvery);
    s.setValue(QStringLiteral("gridUnits"), int(gridUnits));
    s.setValue(QStringLiteral("gridSubdivisions"), gridSubdivisions);
    emit changed();
}
