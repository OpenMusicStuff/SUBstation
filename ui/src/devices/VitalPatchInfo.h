#pragma once

// A Vital device's patch as its editor shows it besides its parameters: the
// modulations in use (slot, source and destination, with their names), every
// source and destination Vital's engine has (for the matrix's lists), the
// preset's name and the macros' names. It reads the project again whenever
// the device's state changes. Edits go through the editor
// (ProjectEditor::setVitalModulation, addVitalModulation), so they undo.
//
//   VitalPatchInfo { id: patch; session: Session; trackId: ...; deviceId: ... }
//   Repeater { model: patch.modulations; ... modelData.sourceName ... }

#include <QObject>
#include <QStringList>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

#include "session/Session.h"

namespace sub::ui {

class VitalPatchInfo : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(sub::app::Session* session READ session WRITE setSession NOTIFY targetChanged)
    Q_PROPERTY(QString trackId READ trackId WRITE setTrackId NOTIFY targetChanged)
    Q_PROPERTY(QString deviceId READ deviceId WRITE setDeviceId NOTIFY targetChanged)
    // [{slot, source, sourceName, destination, destinationName}], by slot.
    Q_PROPERTY(QVariantList modulations READ modulations NOTIFY changed)
    Q_PROPERTY(int freeSlots READ freeSlots NOTIFY changed)
    Q_PROPERTY(QString presetName READ presetName NOTIFY changed)
    Q_PROPERTY(QStringList macroNames READ macroNames NOTIFY changed)
    // The oscillators' wavetables' names, the LFOs' shapes' names; the basic shapes either can be set to.
    Q_PROPERTY(QStringList wavetableNames READ wavetableNames NOTIFY changed)
    Q_PROPERTY(QStringList lfoNames READ lfoNames NOTIFY changed)
    Q_PROPERTY(QStringList shapeNames READ shapeNames CONSTANT)
    // [{id, name}], sorted by name: every source, every destination.
    Q_PROPERTY(QVariantList sources READ sources CONSTANT)
    Q_PROPERTY(QVariantList destinations READ destinations CONSTANT)

public:
    explicit VitalPatchInfo(QObject* parent = nullptr);

    sub::app::Session* session() const { return session_; }
    void setSession(sub::app::Session* session);
    QString trackId() const { return trackId_; }
    void setTrackId(const QString& trackId);
    QString deviceId() const { return deviceId_; }
    void setDeviceId(const QString& deviceId);

    QVariantList modulations() const { return modulations_; }
    int freeSlots() const { return freeSlots_; }
    QString presetName() const { return presetName_; }
    QStringList macroNames() const { return macroNames_; }
    QStringList wavetableNames() const { return wavetableNames_; }
    QStringList lfoNames() const { return lfoNames_; }
    QStringList shapeNames() const;
    QVariantList sources() const;
    QVariantList destinations() const;

    // A new modulation (the first free slot): its slot, -1 if none is free.
    Q_INVOKABLE int add(const QString& source, const QString& destination);
    // A slot's source and destination changed; or cleared.
    Q_INVOKABLE bool change(int slot, const QString& source, const QString& destination);
    Q_INVOKABLE bool remove(int slot);
    // An oscillator's wavetable from a file (.vitaltable, or audio: `style` 0
    // Splice, 1 Vocode, 2 Pitch), one undo step: "" if it loaded, else why not.
    Q_INVOKABLE QString loadWavetable(int oscillator, const QString& path, int style);
    // An oscillator's wavetable, an LFO's shape: a basic shape (shapeNames' index). One undo step each.
    Q_INVOKABLE bool setWavetableShape(int oscillator, int shape);
    Q_INVOKABLE bool setLfoShape(int lfo, int shape);
    // The parameter of a slot ("amount", "bipolar", "power", "stereo", "bypass").
    Q_INVOKABLE QString slotParam(int slot, const QString& what) const;

Q_SIGNALS:
    void targetChanged();
    void changed();

private:
    void refresh();

    QPointer<sub::app::Session> session_;
    QString trackId_;
    QString deviceId_;
    QVariantList modulations_;
    int freeSlots_ = 0;
    QString presetName_;
    QStringList macroNames_;
    QStringList wavetableNames_;
    QStringList lfoNames_;
    QList<QMetaObject::Connection> connections_;
};

}  // namespace sub::ui
