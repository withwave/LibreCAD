// GUI regression: run with the native cocoa platform on macOS.
// Native print dialogs are inspected and cancelled; no printer jobs are sent.
#include "qc_applicationwindow.h"
#include "qc_mdiwindow.h"
#include "qg_graphicview.h"
#include "rs_debug.h"
#include "rs_fontlist.h"
#include "rs_graphic.h"
#include "rs_line.h"
#include "rs_settings.h"
#include "rs_system.h"
#include <QAction>
#include <QApplication>
#include <QDebug>
#include <QFileDialog>
#include <QFileInfo>
#include <QLineEdit>
#include <QMenu>
#include <QMouseEvent>
#include <QPointer>
#include <QPrintDialog>
#include <QProcess>
#include <QTemporaryDir>
#include <QTimer>
#include <functional>
#include <cstdlib>
QString LCReleaseLabel() { return "test"; }
int main(int argc, char **argv) {
    QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
    QApplication app(argc, argv);
    app.setQuitOnLastWindowClosed(false);
    RS_DEBUG->setLevel(RS_Debug::D_NOTHING);
    RS_Settings::init("LibreCAD", "LibreCAD-print-action-regression");
    RS_SYSTEM->init("LibreCAD", "test", "librecad", argv[0]);
    RS_FONTLIST->init();
    auto &window = QC_ApplicationWindow::getAppWindow();
    window->show();
    window->slotFileNewFromDefaultTemplate();
    auto *graphic = window->getCurrentMDIWindow()->getDocument()->getGraphic();
    struct WindowCleanup {
        std::function<void()> close;
        ~WindowCleanup() { close(); }
    } cleanup{[&] {
        if (!window) return;
        graphic->setModified(false);
        window->close();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        window.reset();
    }};
    graphic->setUnit(RS2::Millimeter);
    graphic->setPaperFormat(RS2::A4, false);
    graphic->addEntity(new RS_Line(graphic, RS_LineData({0., 0.}, {100., 100.})));
    graphic->calculateBorders();
    window->slotFilePrintPreview(true);
    app.processEvents();
    auto *view = window->getCurrentMDIWindow()->getGraphicView();
    auto *print = window->findChild<QAction *>("FilePrint");
    auto *pdf = window->findChild<QAction *>("FilePrintPDF");
    if (!print || !pdf)
        return 1;
    QTemporaryDir dir;
    QString output = dir.filePath("test.pdf");
    enum Phase { Native, PDF, Context, CancelContext };
    Phase phase = Native;
    int nativeCount = 0, pdfCount = 0, menuCount = 0, destroyCount = 0;
    bool running = true;
    bool nativeSeen = false;
    QPointer<QMenu> pendingPopup;
    std::function<void()> tick;
    tick = [&]() {
        if (!running)
            return;
        QTimer::singleShot(20, &app, tick);
        for (auto *w : QApplication::topLevelWidgets()) {
            if (auto *d = qobject_cast<QPrintDialog *>(w)) {
                // Cocoa owns the visible NSPrintPanel; the Qt wrapper can
                // report isVisible()==false while exec() is running.
                if (!nativeSeen) { ++nativeCount; nativeSeen = true; }
#ifdef Q_OS_MACOS
                // Qt reject() does not consistently dismiss the native modal
                // panel on macOS. Cancel the test process's panel explicitly.
                QProcess::execute("osascript", {"-e",
                    "tell application \"System Events\" to tell process \"librecad_printing_ui_tests\" "
                    "\nif exists window \"Print\" then\n"
                    "click button \"Cancel\" of splitter group 1 of window \"Print\"\n"
                    "end if\nend tell"});
#endif
                d->reject();
                return;
            }
            if (!w->isVisible())
                continue;
            if (auto *d = qobject_cast<QFileDialog *>(w)) {
                if (pendingPopup) {
                    // Model the native save panel destroying the closed popup in its nested event loop.
                    delete pendingPopup.data();
                    if (pendingPopup)
                        qFatal("Popup must be destroyed while save dialog is open");
                }
                ++pdfCount;
                if (phase == Native || phase == CancelContext)
                    d->reject();
                else {
                    d->findChild<QLineEdit *>("fileNameEdit")->setText(output);
                    QMetaObject::invokeMethod(d, "accept");
                }
                return;
            }
            if ((phase == Context || phase == CancelContext))
                if (auto *m = qobject_cast<QMenu *>(w)) {
                    if (m->parentWidget() != view)
                        continue;
                    ++menuCount;
                    QObject::connect(m, &QObject::destroyed, &app, [&]() { ++destroyCount; });
                    // QMenu hides/closes before dispatching its action. A modal save dialog
                    // then processes that popup's DeferredDelete while exec() is on the stack.
                    pendingPopup = m;
                    m->close();
                    pdf->trigger();
                    return;
                }
        }
    };
    QTimer::singleShot(20, &app, tick);
    QTimer::singleShot(60000, &app, [] {
        qCritical() << "Printing action regression timed out";
        std::_Exit(7);
    });
    // Deliberately deliver both checked states: neither may choose PDF.
    QMetaObject::invokeMethod(print, "triggered", Q_ARG(bool, true));
    nativeSeen = false;
    QMetaObject::invokeMethod(print, "triggered", Q_ARG(bool, false));
    if (nativeCount != 2 || pdfCount != 0) {
        qCritical() << "PRINT_ROUTE_FAILED" << nativeCount << pdfCount;
        return 2;
    }
    qInfo() << "NATIVE_PRINT_ROUTE_OK";
    phase = PDF;
    pdf->trigger();
    if (!QFileInfo::exists(output))
        return 3;
    qInfo() << "PDF_ACTION_OK";
    for (auto p : {Context, CancelContext, Context}) {
        phase = p;
        QFile::remove(output);
        QMouseEvent release(QEvent::MouseButtonRelease, QPointF(20, 20), view->mapToGlobal(QPoint(20, 20)),
                            Qt::RightButton, Qt::NoButton, Qt::NoModifier);
        QCoreApplication::sendEvent(view, &release);
        if ((p == Context) != QFileInfo::exists(output))
            return 4;
        app.processEvents();
    }
    if (menuCount != 3 || destroyCount != 3) {
        qCritical() << "MENU_COUNTS" << menuCount << destroyCount;
        return 5;
    }
    qInfo() << "CONTEXT_PDF_SAVE_CANCEL_REPEAT_OK" << menuCount << destroyCount;
    running = false;
    // Exercise normal shutdown while the action context and document listeners
    // are still alive; resetting the singleton bypasses closeEvent cleanup.
    graphic->setModified(false);
    if (!window->close())
        return 6;
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    window.reset();
    qInfo() << "NORMAL_SHUTDOWN_OK";
    return 0;
}
