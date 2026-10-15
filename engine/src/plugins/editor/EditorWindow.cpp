// What every system's editor window does with the plug-in's view (see
// EditorWindow.h; the windows themselves are EditorWindowWin32.cpp's...).

#include "plugins/editor/EditorWindow.h"

#include "pluginterfaces/gui/iplugviewcontentscalesupport.h"

#if SMTG_OS_LINUX
#include "plugins/editor/Vst3RunLoop.h"
#endif

namespace sub::vst3 {

void EditorWindow::RunLoopDeleter::operator()(RunLoop* loop) const noexcept {
#if SMTG_OS_LINUX
    delete loop;
#else
    (void)loop;  // (there is none elsewhere)
#endif
}

tresult PLUGIN_API EditorWindow::queryInterface(const Steinberg::TUID queried, void** obj) {
    using Steinberg::FUnknownPrivate::iidEqual;
    if (iidEqual(queried, Steinberg::IPlugFrame::iid) || iidEqual(queried, Steinberg::FUnknown::iid)) {
        *obj = static_cast<Steinberg::IPlugFrame*>(this);
        return Steinberg::kResultOk;
    }
#if SMTG_OS_LINUX
    if (runLoop_ && iidEqual(queried, Steinberg::Linux::IRunLoop::iid)) {
        *obj = static_cast<Steinberg::Linux::IRunLoop*>(runLoop_.get());
        return Steinberg::kResultOk;
    }
#endif
    *obj = nullptr;
    return Steinberg::kNoInterface;
}

void EditorWindow::detachView() {
    if (!view_) return;
    view_->setFrame(nullptr);
    view_->removed();
    view_ = nullptr;
#if SMTG_OS_LINUX
    if (runLoop_) runLoop_->clear();  // what the plug-in left registered
#endif
}

void EditorWindow::sizeView(Steinberg::ViewRect& size) {
    Steinberg::ViewRect current{};
    if (view_->getSize(&current) != Steinberg::kResultTrue || current.getWidth() != size.getWidth() ||
        current.getHeight() != size.getHeight()) {
        view_->onSize(&size);
    }
}

void EditorWindow::setContentScale(float scale) {
    scale_ = scale;
    Steinberg::FUnknownPtr<Steinberg::IPlugViewContentScaleSupport> support(view_);
    if (support) support->setContentScaleFactor(scale_);
}

}  // namespace sub::vst3
