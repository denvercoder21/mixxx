#include <util/synthesizedmouseevents.h>
#include <QtDebug>
#include <QTouchEvent>
#include "mixxxapplication.h"

#include "library/crate/crateid.h"
#include "control/controlproxy.h"
#include "mixxx.h"
#include "soundio/soundmanagerutil.h"

// When linking Qt statically on Windows we have to Q_IMPORT_PLUGIN all the
// plugins we link in build/depends.py.
#ifdef QT_NODLL
#include <QtPlugin>
#if QT_VERSION >= 0x050000
// sqldrivers plugins
Q_IMPORT_PLUGIN(QSQLiteDriverPlugin)
// platform plugins
Q_IMPORT_PLUGIN(QWindowsIntegrationPlugin)
// style plugins
Q_IMPORT_PLUGIN(QWindowsVistaStylePlugin)
// imageformats plugins
Q_IMPORT_PLUGIN(QSvgPlugin)
Q_IMPORT_PLUGIN(QSvgIconPlugin)
Q_IMPORT_PLUGIN(QICOPlugin)
Q_IMPORT_PLUGIN(QTgaPlugin)
Q_IMPORT_PLUGIN(QJpegPlugin)
Q_IMPORT_PLUGIN(QGifPlugin)
// accessible plugins
// TODO(rryan): This is supposed to exist but does not in our builds.
//Q_IMPORT_PLUGIN(AccessibleFactory)
#else 
// iconengines plugins
Q_IMPORT_PLUGIN(qsvgicon)
// imageformats plugins
Q_IMPORT_PLUGIN(qsvg)
Q_IMPORT_PLUGIN(qico)
Q_IMPORT_PLUGIN(qtga)
// accessible plugins
Q_IMPORT_PLUGIN(qtaccessiblewidgets)
#endif
#endif

namespace {

/// This class allows to change the button of a mouse event on the fly. 
/// This is required because we want to change the behaviour of Qts mouse
/// buttony synthesizer without duplicate all the code. 
class QMouseEventEditable : public QMouseEvent {
  public:
    void setButton(Qt::MouseButton button) {
        b = button;
    }
};

} // anonymous namespace

MixxxApplication::MixxxApplication(int& argc, char** argv)
        : QApplication(argc, argv),
          m_rightPressedButtons(0),
          m_pTouchShift(nullptr) {
    registerMetaTypes();
}

MixxxApplication::~MixxxApplication() {
}

void MixxxApplication::registerMetaTypes() {
    // Register custom data types for signal processing
    qRegisterMetaType<TrackId>("TrackId");
    qRegisterMetaType<QList<TrackId>>("QList<TrackId>");
    qRegisterMetaType<QSet<TrackId>>("QSet<TrackId>");
    qRegisterMetaType<CrateId>("CrateId");
    qRegisterMetaType<QList<CrateId>>("QList<CrateId>");
    qRegisterMetaType<QSet<CrateId>>("QSet<CrateId>");
    qRegisterMetaType<TrackPointer>("TrackPointer");
    qRegisterMetaType<mixxx::ReplayGain>("mixxx::ReplayGain");
    qRegisterMetaType<mixxx::Bpm>("mixxx::Bpm");
    qRegisterMetaType<mixxx::Duration>("mixxx::Duration");
    qRegisterMetaType<SoundDeviceId>("SoundDeviceId");
    QMetaType::registerComparators<SoundDeviceId>();
}

// Macs do not have touchscreens
#ifndef Q_OS_MAC
#if QT_VERSION < QT_VERSION_CHECK(5, 0, 0)

extern void qt_translateRawTouchEvent(QWidget *window,
        QTouchEvent::DeviceType deviceType,
        const QList<QTouchEvent::TouchPoint> &touchPoints);

bool MixxxApplication::notify(QObject* target, QEvent* event) {
    if (isMouseEventSynthesized()) {
        if (target->isWidgetType()) {
            QWidget* w = static_cast<QWidget*>(target);
            if (w->testAttribute(Qt::WA_AcceptTouchEvents)) {
                // consume and discards Synthesized Mouse events
                // generated by the OS, to not perform the action
                // twice
                return true;
            }
        }
    }

    switch (event->type()) {
    case QEvent::TouchBegin:
    case QEvent::TouchUpdate:
    case QEvent::TouchEnd:
    {
        QTouchEvent* touchEvent = static_cast<QTouchEvent*>(event);
        QList<QTouchEvent::TouchPoint> touchPoints = touchEvent->touchPoints();
        QEvent::Type eventType = QEvent::None;
        Qt::MouseButtons buttons = Qt::NoButton;
        QWidget* fakeMouseWidget = NULL;
        bool baseReturn;

        //qDebug() << "&" << touchEvent->type() << target;

        if (touchEvent->deviceType() !=  QTouchEvent::TouchScreen) {
            break;
        }

        switch (event->type()) {
        case QEvent::TouchBegin:
            // try to deliver as touch event
            baseReturn = QApplication::notify(target, event);
            if (dynamic_cast<MixxxMainWindow*>(touchEvent->widget())) {
                // the touchEvent has fallen trough to the MixxxMainWindow, because there
                // was no touch enabled widget found.
                // Now we resent this event and all following events for this touch point
                // as Mouse events.
                eventType = QEvent::MouseButtonPress;
                if (touchIsRightButton()) {
                    // touch is right click
                    m_activeTouchButton = Qt::RightButton;
                    buttons = Qt::RightButton;
                } else {
                    m_activeTouchButton = Qt::LeftButton;
                    buttons = Qt::LeftButton;
                }
                m_fakeMouseSourcePointId = touchPoints.first().id();
                m_fakeMouseWidget = dynamic_cast<QWidget*>(target);
                fakeMouseWidget = m_fakeMouseWidget;
                break;
            }
            return baseReturn;
        case QEvent::TouchUpdate:
            if (m_fakeMouseWidget) {
                eventType = QEvent::MouseMove;
                buttons = m_activeTouchButton;
                fakeMouseWidget = m_fakeMouseWidget;
                break;
            }
            return QApplication::notify(target, event);
        case QEvent::TouchEnd:
            if (m_fakeMouseWidget) {
                eventType = QEvent::MouseButtonRelease;
                m_fakeMouseSourcePointId = touchPoints.first().id();
                fakeMouseWidget = m_fakeMouseWidget;
                m_fakeMouseWidget = NULL;
                break;
            }
            return QApplication::notify(target, event);
        default:
            return QApplication::notify(target, event);
        }

        for (int i = 0; i < touchPoints.count(); ++i) {
             const QTouchEvent::TouchPoint& touchPoint = touchPoints.at(i);
             if (touchPoint.id() == m_fakeMouseSourcePointId) {
                 QMouseEvent mouseEvent(eventType,
                                        fakeMouseWidget->mapFromGlobal(touchPoint.screenPos().toPoint()),
                                        touchPoint.screenPos().toPoint(),
                                        m_activeTouchButton, // Button that causes the event
                                        buttons,
                                        touchEvent->modifiers());

                 //qDebug() << "#" << mouseEvent.type() << mouseEvent.button() << mouseEvent.buttons() << mouseEvent.pos() << mouseEvent.globalPos();

                 //if (m_fakeMouseWidget->focusPolicy() & Qt::ClickFocus) {
                 //    fakeMouseWidget->setFocus();
                 //}
                 QApplication::notify(fakeMouseWidget, &mouseEvent);
                 return true;
             }
        }
        //qDebug() << "return false";
        return false;
        break;
    }
    case QEvent::MouseButtonRelease:
    {
        bool ret = QApplication::notify(target, event);
        if (m_fakeMouseWidget) {
            // It may happen the faked mouse event was grabbed by a non touch window.
            // eg.: if we have started to drag by touch.
            // In this case X11 generates a MouseButtonRelease instead of a TouchPointReleased Event.
            // QApplication still tracks the Touch point and prevent touch to other widgets
            // So we need to fake the Touch release event as well to clean up
            // QApplicationPrivate::widgetForTouchPointId and QApplicationPrivate::appCurrentTouchPoints;
            m_fakeMouseWidget = NULL; // Disable MouseButtonRelease fake
            QList<QTouchEvent::TouchPoint> touchPoints;
            QTouchEvent::TouchPoint tp;
            tp.setId(m_fakeMouseSourcePointId);
            tp.setState(Qt::TouchPointReleased);
            touchPoints.append(tp);
            qt_translateRawTouchEvent(NULL, QTouchEvent::TouchScreen, touchPoints);
        }
        return ret;
    }
    default:
        break;
    }
    // No touch event
    bool ret = QApplication::notify(target, event);
    return ret;
}
#endif // QT_VERSION < QT_VERSION_CHECK(5, 0, 0)
#endif // Q_OS_MAC

bool MixxxApplication::notify(QObject* target, QEvent* event) {
    // All touch events are translated in two simultaneous events one for
    // target QWidgetWindow and one for the target QWidget
    // A second touch becomes a mouse move without additional press and release
    // events
    switch (event->type()) {
    case QEvent::MouseButtonPress: {
        QMouseEventEditable* mouseEvent = static_cast<QMouseEventEditable*>(event);
        if (mouseEvent->source() == Qt::MouseEventSynthesizedByQt &&
                mouseEvent->button() == Qt::LeftButton &&
                touchIsRightButton()) {
            // Assert the assumption that QT synthesizes only one click at a time
            // = two events (see above)
            VERIFY_OR_DEBUG_ASSERT(m_rightPressedButtons < 2) {
                break;
            }
            mouseEvent->setButton(Qt::RightButton);
            m_rightPressedButtons++;
        }
        break;
    }
    case QEvent::MouseButtonRelease: {
        QMouseEventEditable* mouseEvent = static_cast<QMouseEventEditable*>(event);
        if (mouseEvent->source() == Qt::MouseEventSynthesizedByQt &&
                mouseEvent->button() == Qt::LeftButton &&
                m_rightPressedButtons > 0) {
            mouseEvent->setButton(Qt::RightButton);
            m_rightPressedButtons--;
        }
        break;
    }
    default:
        break;
    }
    return QApplication::notify(target, event);
}

bool MixxxApplication::touchIsRightButton() {
    if (!m_pTouchShift) {
        m_pTouchShift = new ControlProxy(
                "[Controls]", "touch_shift", this);
    }
    return m_pTouchShift->toBool();
}
