#pragma once

#include <QHash>
#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QString>

// Window > Actions: recorded sequences of commands, grouped in sets.
//
// A step is a menu command (by its action id). Filter and adjustment dialogs built from sliders
// also store their values, so the step replays without the dialog; other dialog commands open
// their dialog when played. Each step can be switched off, and dialog steps can be set to stop
// and show their dialog ("modal control" in Photoshop).
struct ActionStep {
    QString command;              // action id, e.g. "filter.gaussianBlur"
    QString title;                // shown in the panel, e.g. "Gaussian Blur"
    QHash<QString, double> values; // dialog values, when the dialog reports them
    bool enabled = true;
    bool hasDialog = false;  // the command normally opens a dialog
    bool showDialog = false; // playback stops at the dialog

    QString detail() const; // the values as "radius: 4"
    QJsonObject toJson() const;
    static ActionStep fromJson(const QJsonObject& o);
};

struct RecordedAction {
    QString name;
    QList<ActionStep> steps;
    bool enabled = true;
};

struct ActionSet {
    QString name;
    QList<RecordedAction> actions;
};

class ActionsModel : public QObject {
    Q_OBJECT
public:
    explicit ActionsModel(QObject* parent = nullptr);

    QList<ActionSet>& sets() { return m_sets; }
    const QList<ActionSet>& sets() const { return m_sets; }
    RecordedAction* actionAt(int set, int action);

    // The sets that come with PhotoSlop.
    static QList<ActionSet> defaultSets();
    void load();
    void save();
    // Call after editing sets() directly.
    void notify();

    // Recording appends steps to one action.
    bool isRecording() const { return m_recSet >= 0; }
    void startRecording(int set, int action);
    void stopRecording();
    int recordingSet() const { return m_recSet; }
    int recordingAction() const { return m_recAction; }
    void appendStep(const ActionStep& step);

    // Adds a set or an action and returns its index.
    int addSet(const QString& name);
    int addAction(int set, const QString& name);

signals:
    void changed();
    void recordingChanged(bool recording);

private:
    QList<ActionSet> m_sets;
    int m_recSet = -1;
    int m_recAction = -1;
};
