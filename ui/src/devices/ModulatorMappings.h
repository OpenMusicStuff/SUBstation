#pragma once

// What a modulator (an LFO, an Envelope) moves, for its editor's list: a row
// per mapping (Device::modulations), each with its device and parameter's
// names, the range it moves over (normalized `low`..`high`, and as the
// parameter's own texts), which way up; and its Map button
// (Session.modulationLearn). Edits go through the editor: a range dragged is
// one undo step per gesture key.
//
//   ModulatorMappings { id: m; session: Session; trackId: editor.trackId; deviceId: editor.deviceId }
//   Repeater { model: m.rows; delegate: ... modelData.name, modelData.low ... }

#include <QObject>
#include <QPointer>
#include <QString>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

#include "session/Session.h"

namespace sub::ui {

class ModulatorMappings : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(sub::app::Session* session READ session WRITE setSession NOTIFY targetChanged)
    Q_PROPERTY(QString trackId READ trackId WRITE setTrackId NOTIFY targetChanged)
    Q_PROPERTY(QString deviceId READ deviceId WRITE setDeviceId NOTIFY targetChanged)
    // [{deviceId, paramId, device, param, low, high, invert, lowText, highText}], in the order they were mapped.
    Q_PROPERTY(QVariantList rows READ rows NOTIFY rowsChanged)
    Q_PROPERTY(int count READ count NOTIFY rowsChanged)
    // Its Map waits for a parameter to be touched.
    Q_PROPERTY(bool learning READ learning NOTIFY learningChanged)

public:
    explicit ModulatorMappings(QObject* parent = nullptr);

    sub::app::Session* session() const { return session_; }
    void setSession(sub::app::Session* session);
    QString trackId() const { return trackId_; }
    void setTrackId(const QString& trackId);
    QString deviceId() const { return deviceId_; }
    void setDeviceId(const QString& deviceId);
    QVariantList rows() const { return rows_; }
    int count() const { return int(rows_.size()); }
    bool learning() const;

    Q_INVOKABLE void toggleLearning();
    Q_INVOKABLE void stopLearning();
    Q_INVOKABLE void setRange(const QString& deviceId, const QString& paramId, double low, double high,
                              const QString& mergeKey = QString());
    Q_INVOKABLE void setInverted(const QString& deviceId, const QString& paramId, bool inverted);
    Q_INVOKABLE void remove(const QString& deviceId, const QString& paramId);
    Q_INVOKABLE void clear();
    // A mapped parameter's value at a normalized position, in its units (a range's ends while dragging).
    Q_INVOKABLE QString valueText(const QString& deviceId, const QString& paramId, double normalized) const;

Q_SIGNALS:
    void targetChanged();
    void rowsChanged();
    void learningChanged();

private:
    void connectSession();
    void refresh();

    QPointer<sub::app::Session> session_;
    QString trackId_;
    QString deviceId_;
    QVariantList rows_;
    QList<QMetaObject::Connection> connections_;
};

}  // namespace sub::ui
