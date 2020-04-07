#pragma once
#include <QtQuick/QQuickItem>
#include <QtQuick/QSGRendererInterface>

class bgfxRenderer;

struct InteropMode
{
    enum Enum
    {
        ExternPlatform,             // Use platformData to set backBuffer
        SynchroFramebuffer,         // Create a bgfx::Framebuffer synchronized with extern Texture handle given by Qt
        OffscreenFramebuffer,       // Create a bgfx::Framebuffer, render to it, then blit result
        Count
    };
};

// Initialize BGFX
bool InitQt_BGFX_Backend(QSGRendererInterface::GraphicsApi pbackend, InteropMode::Enum pInteropMode );
void FinalizeQt_BGFX_Backend();

class BgfxItem : public QQuickItem
{
    Q_OBJECT
    //Q_PROPERTY(qreal t READ t WRITE setT NOTIFY tChanged)

public:
    BgfxItem();

signals:
    void tChanged();

public slots:
    void sync();
    void cleanup();
    
private slots:
    void handleWindowChanged(QQuickWindow *win);

private:
    virtual QSGNode* updatePaintNode(QSGNode* node, UpdatePaintNodeData*);
    void releaseResources() override;
    bgfxRenderer *mRenderer = nullptr;
};