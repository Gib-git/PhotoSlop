#include "ui/ActionsModel.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QSettings>
#include <algorithm>

QString ActionStep::detail() const
{
    QStringList keys = values.keys();
    keys.sort();
    QStringList parts;
    for (const QString& k : keys) {
        if (k == QLatin1String("seed")) continue;
        parts << QStringLiteral("%1: %2").arg(k, QString::number(values.value(k), 'g', 4));
    }
    return parts.join(QStringLiteral(", "));
}

QJsonObject ActionStep::toJson() const
{
    QJsonObject o{{QStringLiteral("command"), command},
                  {QStringLiteral("title"), title},
                  {QStringLiteral("enabled"), enabled},
                  {QStringLiteral("hasDialog"), hasDialog},
                  {QStringLiteral("showDialog"), showDialog}};
    if (!values.isEmpty()) {
        QJsonObject v;
        for (auto it = values.cbegin(); it != values.cend(); ++it) v.insert(it.key(), it.value());
        o.insert(QStringLiteral("values"), v);
    }
    return o;
}

ActionStep ActionStep::fromJson(const QJsonObject& o)
{
    ActionStep s;
    s.command = o.value(QStringLiteral("command")).toString();
    s.title = o.value(QStringLiteral("title")).toString(s.command);
    s.enabled = o.value(QStringLiteral("enabled")).toBool(true);
    s.hasDialog = o.value(QStringLiteral("hasDialog")).toBool();
    s.showDialog = o.value(QStringLiteral("showDialog")).toBool();
    const QJsonObject v = o.value(QStringLiteral("values")).toObject();
    for (auto it = v.begin(); it != v.end(); ++it) s.values.insert(it.key(), it.value().toDouble());
    return s;
}

ActionsModel::ActionsModel(QObject* parent)
    : QObject(parent)
{
    load();
}

RecordedAction* ActionsModel::actionAt(int set, int action)
{
    if (set < 0 || set >= m_sets.size() || action < 0 || action >= m_sets[set].actions.size()) return nullptr;
    return &m_sets[set].actions[action];
}

QList<ActionSet> ActionsModel::defaultSets()
{
    auto step = [](const char* command, const char* title, QHash<QString, double> values = {}, bool dialog = false) {
        ActionStep s;
        s.command = QString::fromLatin1(command);
        s.title = QString::fromLatin1(title);
        s.values = values;
        s.hasDialog = dialog || !values.isEmpty();
        return s;
    };
    ActionSet set;
    set.name = QStringLiteral("Default Actions");
    RecordedAction sepia;
    sepia.name = QStringLiteral("Sepia Toning (layer)");
    sepia.steps = {step("layer.viaCopy", "Layer via Copy"),
                   step("image.blackWhite", "Black and White",
                        {{QStringLiteral("w0"), 40}, {QStringLiteral("w1"), 60}, {QStringLiteral("w2"), 40}, {QStringLiteral("w3"), 60},
                         {QStringLiteral("w4"), 20}, {QStringLiteral("w5"), 80}, {QStringLiteral("tint"), 1},
                         {QStringLiteral("hue"), 35}, {QStringLiteral("saturation"), 25}})};
    RecordedAction gray;
    gray.name = QStringLiteral("Custom RGB to Grayscale");
    ActionStep bw = step("image.blackWhite", "Black and White",
                         {{QStringLiteral("w0"), 40}, {QStringLiteral("w1"), 60}, {QStringLiteral("w2"), 40}, {QStringLiteral("w3"), 60},
                          {QStringLiteral("w4"), 20}, {QStringLiteral("w5"), 80}, {QStringLiteral("tint"), 0},
                          {QStringLiteral("hue"), 42}, {QStringLiteral("saturation"), 20}});
    bw.showDialog = true; // pick the channel mix each time
    gray.steps = {step("layer.flatten", "Flatten Image"), bw, step("image.modeGray", "Grayscale")};
    RecordedAction soft;
    soft.name = QStringLiteral("Soft Glow (layer)");
    ActionStep blur = step("filter.gaussianBlur", "Gaussian Blur", {{QStringLiteral("radius"), 6}});
    soft.steps = {step("layer.viaCopy", "Layer via Copy"), blur};
    RecordedAction web;
    web.name = QStringLiteral("Sharpen for Screen");
    web.steps = {step("filter.unsharpMask", "Unsharp Mask",
                      {{QStringLiteral("amount"), 80}, {QStringLiteral("radius"), 0.8}, {QStringLiteral("threshold"), 2}})};
    set.actions = {sepia, gray, soft, web};
    return {set};
}

void ActionsModel::load()
{
    const QByteArray json = QSettings().value(QStringLiteral("actions/sets")).toByteArray();
    const QJsonArray sets = QJsonDocument::fromJson(json).array();
    if (json.isEmpty()) {
        m_sets = defaultSets();
        return;
    }
    m_sets.clear();
    for (const QJsonValue& sv : sets) {
        const QJsonObject so = sv.toObject();
        ActionSet set;
        set.name = so.value(QStringLiteral("name")).toString();
        for (const QJsonValue& av : so.value(QStringLiteral("actions")).toArray()) {
            const QJsonObject ao = av.toObject();
            RecordedAction a;
            a.name = ao.value(QStringLiteral("name")).toString();
            a.enabled = ao.value(QStringLiteral("enabled")).toBool(true);
            for (const QJsonValue& st : ao.value(QStringLiteral("steps")).toArray()) a.steps.append(ActionStep::fromJson(st.toObject()));
            set.actions.append(a);
        }
        m_sets.append(set);
    }
}

void ActionsModel::save()
{
    QJsonArray sets;
    for (const ActionSet& set : std::as_const(m_sets)) {
        QJsonArray actions;
        for (const RecordedAction& a : set.actions) {
            QJsonArray steps;
            for (const ActionStep& s : a.steps) steps.append(s.toJson());
            actions.append(QJsonObject{{QStringLiteral("name"), a.name}, {QStringLiteral("enabled"), a.enabled},
                                       {QStringLiteral("steps"), steps}});
        }
        sets.append(QJsonObject{{QStringLiteral("name"), set.name}, {QStringLiteral("actions"), actions}});
    }
    QSettings().setValue(QStringLiteral("actions/sets"), QJsonDocument(sets).toJson(QJsonDocument::Compact));
}

void ActionsModel::notify()
{
    if (m_recSet >= m_sets.size() || (m_recSet >= 0 && m_recAction >= m_sets[m_recSet].actions.size())) stopRecording();
    save();
    emit changed();
}

void ActionsModel::startRecording(int set, int action)
{
    if (!actionAt(set, action)) return;
    m_recSet = set;
    m_recAction = action;
    emit recordingChanged(true);
}

void ActionsModel::stopRecording()
{
    if (m_recSet < 0) return;
    m_recSet = m_recAction = -1;
    emit recordingChanged(false);
}

void ActionsModel::appendStep(const ActionStep& step)
{
    RecordedAction* a = actionAt(m_recSet, m_recAction);
    if (!a) return;
    a->steps.append(step);
    notify();
}

int ActionsModel::addSet(const QString& name)
{
    m_sets.append(ActionSet{name, {}});
    notify();
    return int(m_sets.size()) - 1;
}

int ActionsModel::addAction(int set, const QString& name)
{
    if (set < 0 || set >= m_sets.size()) return -1;
    m_sets[set].actions.append(RecordedAction{name, {}, true});
    notify();
    return int(m_sets[set].actions.size()) - 1;
}
