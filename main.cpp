#include <QGuiApplication>
#include <QOpenGLContext>
#include <QtQuick/QQuickView>
#include "bgfxItem.h"


int main(int argc, char **argv)
{
    //https://doc.qt.io/qt-5/qtquick-visualcanvas-scenegraph.html#scene-graph-and-rendering
    // "basic/windows/threaded"
    qputenv("QSG_RENDER_LOOP", "basic");
    //qputenv("QSG_RENDER_LOOP", "threaded");

    QSurfaceFormat format = QSurfaceFormat::defaultFormat();
    format.setVersion(3, 3);
    QSurfaceFormat::setDefaultFormat(format);
    QCoreApplication::setAttribute(Qt::AA_ShareOpenGLContexts);

    QGuiApplication app(argc, argv);
    qmlRegisterType<BgfxItem>("BgfxItemQML", 1, 0, "BgfxItem");
 
    // QSGRendererInterface::OpenGLRhi / QSGRendererInterface::Direct3D11Rhi
    // ExternPlatform,             // Use platformData to set backBuffer (require this 'hack' https://github.com/VirtualGeo/bgfx/commit/0a4193bd31c902ed64288a067d02bfac95114a0d)
    // SynchroFramebuffer,         // Create a bgfx::Framebuffer synchronized with extern Texture handle given by Qt (don't work well, require some hack)
    // OffscreenFramebuffer,       // Create a bgfx::Framebuffer, render to it, then blit result (works)
    InitQt_BGFX_Backend(QSGRendererInterface::Direct3D11Rhi, InteropMode::OffscreenFramebuffer);
    qDebug() << "MainThread " << GetCurrentThreadId();

    QQuickView view;
    view.setResizeMode(QQuickView::SizeRootObjectToView);
    view.setSource(QUrl("qrc:///main.qml"));
    view.show();

    QQuickView view2;
    view2.setResizeMode(QQuickView::SizeRootObjectToView);
    view2.setSource(QUrl("qrc:///main.qml"));
    view2.show();

    int lReturn = app.exec();

    FinalizeQt_BGFX_Backend();

    return lReturn;
}
