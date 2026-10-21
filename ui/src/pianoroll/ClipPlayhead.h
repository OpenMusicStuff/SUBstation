#pragma once

// The clip view's playhead over the audio clips' waveforms, while the
// arrangement plays inside a clip shown: a line through that clip's waveform
// where its file is playing, and a triangle at the ruler's foot (one clip
// open). An item of its own over the ClipWaveform, so following playback
// redraws only this, not the waveforms (as RollPlayhead does for the notes).

#include "pianoroll/ClipWaveform.h"
#include "sg/SgCanvas.h"

#include <QPointer>
#include <QtQml/qqmlregistration.h>

namespace sub::ui {

class ClipPlayhead : public SgCanvas {
    Q_OBJECT
    QML_ELEMENT
    // The waveforms it is drawn over (it fills them).
    Q_PROPERTY(sub::ui::ClipWaveform* waveform READ waveform WRITE setWaveform NOTIFY waveformChanged)

public:
    explicit ClipPlayhead(QQuickItem* parent = nullptr);

    ClipWaveform* waveform() const { return waveform_; }
    void setWaveform(ClipWaveform* waveform);

Q_SIGNALS:
    void waveformChanged();

protected:
    void paint(SgPainter& painter) override;

private:
    QPointer<ClipWaveform> waveform_;
};

}  // namespace sub::ui
