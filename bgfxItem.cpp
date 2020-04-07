#include "bgfxItem.h"

#include <QtCore/QRunnable>
#include <QtQuick/QQuickWindow>
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QOpenGLExtraFunctions>
#include <QtPlatformHeaders/QWGLNativeContext>

#include <d3d11.h>
#include <bx/bx.h>
#include <bgfx/bgfx.h>
#include <bgfx/platform.h>

#define HRESULT_CHECK(call_) do { HRESULT result_ = call_;	assert(result_ == S_OK); } while(0);
#define SAFE_RELEASE(p) { if ( (p) ) { (p)->Release(); (p) = 0; } }

#define GL_CHECK() assert(gl->glGetError() == 0);

/******************************************************************************/
struct bgfxRendererGlobal
{
    bool m_initialized = false;
    bgfx::RendererType::Enum m_backend;
    InteropMode::Enum m_interopMode;
    void* m_context = nullptr;
    void init(void* pContext)
    {
        if (!m_initialized)
        {
            m_context = pContext;

            bgfx::Init init;
            init.type = m_backend;
            init.platformData.context = pContext;   // D3DDevice
            bgfx::renderFrame();                    // switch bgfx to singlethread rendering
            bgfx::init(init);

            // Don't work with offscreen rendering
            //bgfx::setDebug(BGFX_DEBUG_TEXT | BGFX_DEBUG_STATS);

            m_initialized = true;
        }
        assert(m_context == pContext); // should not be change
    }

    void shutdown()
    {
        if (m_initialized)
        {
            bgfx::shutdown();
        }
    }
};
static bgfxRendererGlobal bgfxGlobal;


/******************************************************************************/
// ugly but it's to get bgfxGlobal access in cubes.h
#include "cubes.h"

/******************************************************************************/
// Initialize BGFX
bool InitQt_BGFX_Backend(QSGRendererInterface::GraphicsApi pBackend, InteropMode::Enum pInteropMode)
{
    //QRhiD3D11InitParams params;
    //params.enableDebugLayer = true;
    //rhi = QRhi::create(QRhi::D3D11, &params);

    assert(bgfxGlobal.m_initialized == false);
    QSGRendererInterface::GraphicsApi lBackend = pBackend == QSGRendererInterface::Direct3D11Rhi ? QSGRendererInterface::Direct3D11Rhi : QSGRendererInterface::OpenGLRhi;    
    QQuickWindow::setSceneGraphBackend(lBackend);
    bgfxGlobal.m_interopMode = pInteropMode;
    bgfxGlobal.m_backend = lBackend == QSGRendererInterface::Direct3D11Rhi ? bgfx::RendererType::Direct3D11 : bgfx::RendererType::OpenGL;
    return true;
}

/******************************************************************************/
void FinalizeQt_BGFX_Backend()
{
    bgfxGlobal.shutdown();
}

/******************************************************************************/
class bgfxRenderer : public QObject
{
    Q_OBJECT
public:
    bgfxRenderer();
    ~bgfxRenderer();

    void setViewportSize(const QSize &size)
    {
        if
            (m_viewportSize != size)
        {
            m_viewportSize = size;

            if (m_initialized)
            {
                resize();
            }
        }
    }
    void setWindow(QQuickWindow *window) { m_window = window; }

public slots:
    void frameStart();
    void mainPassRecordingStart();

private:
    InteropMode::Enum m_interopMode = InteropMode::OffscreenFramebuffer;

    void resize();
    void render_Common(); // render bgfx stuff

    void render_ExternPlatform_DX11();             // Use platformData to set backBuffer
    void render_SynchroFramebuffer_DX11();         // Create a bgfx::Framebuffer synchronized with extern Texture handle given by Qt
    void render_OffscreenFramebuffer_DX11();       // Create a bgfx::Framebuffer, render to it, then blit result

    void resize_ExternPlatform_DX11();
    void resize_SynchroFramebuffer_DX11();
    void resize_OffscreenFramebuffer_DX11();


    void render_ExternPlatform_GL();             // Use platformData to set backBuffer
    void render_SynchroFramebuffer_GL();         // Create a bgfx::Framebuffer synchronized with extern Texture handle given by Qt
    void render_OffscreenFramebuffer_GL();       // Create a bgfx::Framebuffer, render to it, then blit result

    void resize_ExternPlatform_GL();
    void resize_SynchroFramebuffer_GL();
    void resize_OffscreenFramebuffer_GL();


    void init();
    QSize m_viewportSize;
    QQuickWindow *m_window;

    // D3d device
    ID3D11Device *m_device = nullptr;
    ID3D11DeviceContext *m_context = nullptr;

    // GL
    QOpenGLContext* m_glcontext = nullptr;
    void* m_nativeglcontext = nullptr;

    ExampleCubes bgfxExample;
    bool m_initialized = false;
    bool m_needreset = true;

    void resizeOffscreenFB();

    // --- Synchronized Framebuffer
    bgfx::FrameBufferHandle offscreenFB = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle backBuffer = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle depthBuffer = BGFX_INVALID_HANDLE;
    uintptr_t backBufferNative = 0;
    uintptr_t depthBufferNative = 0;
    // --- 
};

/******************************************************************************/
BgfxItem::BgfxItem()
: mRenderer(nullptr)
{
    connect(this, &QQuickItem::windowChanged, this, &BgfxItem::handleWindowChanged);
}

/******************************************************************************/
QSGNode* BgfxItem::updatePaintNode(QSGNode* node, UpdatePaintNodeData* data)
{
    QSGNode* r = QQuickItem::updatePaintNode(node, data);
    update(); // render every frame
    return r;
}

/******************************************************************************/
void BgfxItem::handleWindowChanged(QQuickWindow *win)
{
    if (win) {
        connect(win, &QQuickWindow::beforeSynchronizing, this, &BgfxItem::sync, Qt::DirectConnection);
        connect(win, &QQuickWindow::sceneGraphInvalidated, this, &BgfxItem::cleanup, Qt::DirectConnection);

        // Ensure we start with cleared to black. The squircle's blend mode relies on this.
        win->setColor(Qt::black);
    }
}

/******************************************************************************/
bgfxRenderer::bgfxRenderer()
{
}

/******************************************************************************/
// The safe way to release custom graphics resources it to both connect to
// sceneGraphInvalidated() and implement releaseResources(). To support
// threaded render loops the latter performs the bgfxRenderer destruction
// via scheduleRenderJob(). Note that the bgfxItem may be gone by the time
// the QRunnable is invoked.
void BgfxItem::cleanup()
{
    delete mRenderer;
    mRenderer = nullptr;
}

/******************************************************************************/
class CleanupJob : public QRunnable
{
public:
    CleanupJob(bgfxRenderer* pRenderer) : mRenderer(pRenderer) { }
    void run() override { delete mRenderer; }
private:
    bgfxRenderer * mRenderer;
};

/******************************************************************************/
void BgfxItem::releaseResources()
{
    window()->scheduleRenderJob(new CleanupJob(mRenderer), QQuickWindow::BeforeSynchronizingStage);
    mRenderer = nullptr;
}

/******************************************************************************/
bgfxRenderer::~bgfxRenderer()
{
    qDebug("cleanup");
    /* crash
    bgfxExample.shutdown();

    if (bgfx::isValid(backBuffer))
        bgfx::destroy(backBuffer);

    if (bgfx::isValid(depthBuffer))
        bgfx::destroy(depthBuffer);

    if (bgfx::isValid(offscreenFB))
        bgfx::destroy(offscreenFB);

    bgfx::frame();
    */
}

/******************************************************************************/
void BgfxItem::sync()
{
    if (!mRenderer) {
        mRenderer = new bgfxRenderer;
        connect(window(), &QQuickWindow::beforeRendering, mRenderer, &bgfxRenderer::frameStart, Qt::DirectConnection);
        connect(window(), &QQuickWindow::beforeRenderPassRecording, mRenderer, &bgfxRenderer::mainPassRecordingStart, Qt::DirectConnection);
    }
    mRenderer->setViewportSize(window()->size() * window()->devicePixelRatio());
    mRenderer->setWindow(window());
}


/******************************************************************************/
void bgfxRenderer::resizeOffscreenFB()
{
    if (bgfx::isValid(offscreenFB))
    {
        bgfx::destroy(offscreenFB);
        offscreenFB = BGFX_INVALID_HANDLE;
    }

    if (bgfx::isValid(backBuffer))
    {
        bgfx::destroy(backBuffer);
        backBuffer = BGFX_INVALID_HANDLE;
    }

    if (bgfx::isValid(depthBuffer))
    {
        bgfx::destroy(depthBuffer);
        backBuffer = BGFX_INVALID_HANDLE;
    }

    backBuffer = bgfx::createTexture2D(m_viewportSize.width(), m_viewportSize.height(), false, 1, bgfx::TextureFormat::RGBA8, BGFX_TEXTURE_RT, NULL);
    depthBuffer = bgfx::createTexture2D(m_viewportSize.width(), m_viewportSize.height(), false, 1, bgfx::TextureFormat::D24S8, BGFX_TEXTURE_RT, NULL);
    bgfx::TextureHandle fbtextures[2] = { backBuffer, depthBuffer };
    offscreenFB = bgfx::createFrameBuffer(BX_COUNTOF(fbtextures), fbtextures, false);
    bgfx::setViewFrameBuffer(0, offscreenFB);
}

/******************************************************************************/
void bgfxRenderer::resize()
{
    if (bgfxGlobal.m_backend == bgfx::RendererType::Direct3D11)
    {
        switch (m_interopMode)
        {
        case InteropMode::ExternPlatform: resize_ExternPlatform_DX11(); break;
        case InteropMode::SynchroFramebuffer: resize_SynchroFramebuffer_DX11(); break;
        case InteropMode::OffscreenFramebuffer: resize_OffscreenFramebuffer_DX11(); break;
        default:
            break;
        }
    }
    else
    {
        switch (m_interopMode)
        {
        case InteropMode::ExternPlatform: resize_ExternPlatform_GL(); break;
        case InteropMode::SynchroFramebuffer: resize_SynchroFramebuffer_GL(); break;
        case InteropMode::OffscreenFramebuffer: resize_OffscreenFramebuffer_GL(); break;
        default:
            break;
        }
    }
}
/******************************************************************************/
void bgfxRenderer::frameStart()
{
    QSGRendererInterface *rif = m_window->rendererInterface();
    if (rif->graphicsApi() == QSGRendererInterface::Direct3D11Rhi)
    {
        Q_ASSERT(rif->graphicsApi() == QSGRendererInterface::Direct3D11Rhi);

        ID3D11Device* new_device = reinterpret_cast<ID3D11Device*>(rif->getResource(m_window, QSGRendererInterface::DeviceResource));
        Q_ASSERT(!m_initialized || (m_device == new_device));
        m_device = new_device;
        Q_ASSERT(m_device);

        ID3D11DeviceContext* new_context = reinterpret_cast<ID3D11DeviceContext*>(rif->getResource(m_window, QSGRendererInterface::DeviceContextResource));
        Q_ASSERT(!m_initialized || (m_context == new_context));
        m_context = new_context;
        Q_ASSERT(m_context);
        if (!m_initialized)
            init();
    }
    else
    {
        Q_ASSERT(rif->graphicsApi() == QSGRendererInterface::OpenGLRhi);
        m_glcontext = reinterpret_cast<QOpenGLContext*>(rif->getResource(m_window, QSGRendererInterface::OpenGLContextResource));
        bool success = m_glcontext->nativeHandle().canConvert<QWGLNativeContext>();
        Q_ASSERT(success);
        QWGLNativeContext native = m_glcontext->nativeHandle().value<QWGLNativeContext>();
        m_nativeglcontext = (void*)native.context();
        if (!m_initialized)
            init();        
    }
}

static const float vertices[] = {
    -1, -1,
    1, -1,
    -1, 1,
    1, 1
};



/******************************************************************************/
void bgfxRenderer::render_ExternPlatform_GL()
{
    QOpenGLContext* current_context = QOpenGLContext::currentContext();

    WId wid = m_window->winId();
    QOpenGLContext* context = m_window->openglContext();
    QOpenGLFramebufferObject* fbo = m_window->renderTarget();
    uint fboId = m_window->renderTargetId();
    QSurface* surface = context->surface();
    QSurface::SurfaceClass classtype = surface->surfaceClass();
    QSurface::SurfaceType type = surface->surfaceType();
    QPlatformSurface* native_surface = surface->surfaceHandle();
    QPlatformWindow* platform_window = reinterpret_cast<QPlatformWindow*>(native_surface);
    
    GLint framebufferId = -1;
    QOpenGLFunctions* gl = m_glcontext->functions();
    gl->glGetIntegerv(GL_FRAMEBUFFER_BINDING, &framebufferId);

    GLint renderbufferId = -1;
    gl->glGetIntegerv(GL_RENDERBUFFER_BINDING, &renderbufferId);

    bgfx::PlatformData pdata;
    //pdata.nwh = (void*)wid;
    pdata.context = m_nativeglcontext;
    bgfx::setPlatformData(pdata);

    // Here we need to find a way to update the internal bgfx width/height when no framebuffer are used
    // reset works but is to bad
    // bgfx::reset(m_viewportSize.width(), m_viewportSize.height());

    bgfx::setViewFrameBuffer(0, BGFX_INVALID_HANDLE);
    //bgfx::reset(m_viewportSize.width(), m_viewportSize.height(), BGFX_RESET_NONE);        
    
    render_Common();

    // Restore RenderTarget in case of MultiPass rendering leave a different output
}

/******************************************************************************/
void bgfxRenderer::render_SynchroFramebuffer_GL()
{
    // glGet(Framebuffer blabla)
}

/******************************************************************************/
void bgfxRenderer::render_OffscreenFramebuffer_GL()
{
    // glGet(Framebuffer blabla)

    bgfx::setViewFrameBuffer(0, offscreenFB);
    render_Common();

    //QOpenGLFunctions* gl = m_glcontext->functions();
    QOpenGLExtraFunctions* gl = m_glcontext->extraFunctions();

    // Add a method to retrieve the handle of the FrameBuffer
    //uintptr_t handle = bgfx::getInternal(offscreenFB); 
    // BGFX store framebuffer id in the s_contex->m_frameBuffers[handle].m_fbo[0]
    // m_fbo[1] is the id of the resolved frame buffer if have one (MSAA), we should use this one if available
    // Here we don't check if it's a renderbuffer or a framebuffer
    uintptr_t attch0 = bgfx::getInternal(backBuffer);
    GLuint srcFB;
    gl->glGenFramebuffers(1, &srcFB); GL_CHECK();
    gl->glBindFramebuffer(GL_FRAMEBUFFER, srcFB); GL_CHECK();
    gl->glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, attch0, 0); GL_CHECK();
    //gl->glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, depth_texture, 0); GL_CHECK();
    GLenum status = gl->glCheckFramebufferStatus(GL_FRAMEBUFFER); GL_CHECK();
    assert(status == GL_FRAMEBUFFER_COMPLETE);
    // Only blit the color buffer (attachement 0)
    gl->glBindFramebuffer(GL_READ_FRAMEBUFFER, srcFB); GL_CHECK();
    gl->glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0); GL_CHECK();
    gl->glBlitFramebuffer(0, 0, m_viewportSize.width(), m_viewportSize.height(), 0, 0, m_viewportSize.width(), m_viewportSize.height(), GL_COLOR_BUFFER_BIT, GL_NEAREST); GL_CHECK();
    gl->glBindFramebuffer(GL_FRAMEBUFFER, 0); GL_CHECK();
    gl->glDeleteFramebuffers(1,&srcFB); GL_CHECK();
}

/******************************************************************************/
void bgfxRenderer::resize_ExternPlatform_GL()
{
    bgfx::PlatformData pdata;
    //pdata.nwh = (void*)m_window->winId(); // nwh = null -> bgfx consider external context
    pdata.context = m_nativeglcontext;
    bgfx::setPlatformData(pdata);
    // It's not a good usage to do this every frame
    // We should call this on resize event only
    bgfx::setViewFrameBuffer(0, BGFX_INVALID_HANDLE);
    bgfx::reset(m_viewportSize.width(), m_viewportSize.height());  
    bgfx::frame();
}

/******************************************************************************/
void bgfxRenderer::resize_SynchroFramebuffer_GL()
{
    bgfx::PlatformData pdata;
    //pdata.nwh = (void*)m_window->winId(); // nwh = null -> bgfx consider external context
    pdata.context = m_nativeglcontext;
    bgfx::setPlatformData(pdata);
    // not necessary with the 'proto-bgfx' branch (modification in renderer_d3d11->udpateResolution)
    // It's not a good usage to do this every frame
    // We should call this on resize event only
    bgfx::setViewFrameBuffer(0, offscreenFB);
    bgfx::reset(m_viewportSize.width(), m_viewportSize.height());
    bgfx::frame();
}

/******************************************************************************/
void bgfxRenderer::resize_OffscreenFramebuffer_GL()
{
    resizeOffscreenFB(),
    bgfx::setViewFrameBuffer(0, offscreenFB);
}

/******************************************************************************/
void bgfxRenderer::resize_ExternPlatform_DX11()
{
    const int countRT = 1; // Note that the RenderTarget[8] is not null, dunno what it is use for
    ID3D11RenderTargetView* pRenderTarget[countRT] = {};
    ID3D11DepthStencilView* pDepthTarget[countRT] = {};
    m_context->OMGetRenderTargets(countRT, pRenderTarget, pDepthTarget); //OMGetRenderTargetsAndUnorderedAccessViews ?

    // require https://github.com/VirtualGeo/bgfx/commit/0a4193bd31c902ed64288a067d02bfac95114a0d)
    bgfx::PlatformData pdata;
    pdata.context = m_device;
    pdata.backBuffer = pRenderTarget[0];
    pdata.backBufferDS = pDepthTarget[0];
    bgfx::setPlatformData(pdata);
    bgfx::setViewFrameBuffer(0, BGFX_INVALID_HANDLE);
   

    // not necessary with the 'proto-bgfx' branch (modification in renderer_d3d11->udpateResolution)
    // require https://github.com/VirtualGeo/bgfx/commit/0a4193bd31c902ed64288a067d02bfac95114a0d)
    //bgfx::reset(m_viewportSize.width(), m_viewportSize.height());
    //bgfx::frame();

    
    //m_context->OMSetRenderTargets(countRT, pRenderTarget, pDepthTarget[0]);

    // OMGetRenderTargets add a reference
    for (int i = 0; i < countRT; ++i)
    {
        if (pRenderTarget[i]) pRenderTarget[i]->Release();
        if (pDepthTarget[i]) pDepthTarget[i]->Release();
    }

    m_needreset = true;
}

/******************************************************************************/
void bgfxRenderer::resize_SynchroFramebuffer_DX11()
{
    bgfx::setViewFrameBuffer(0, offscreenFB);
    bgfx::reset(m_viewportSize.width(), m_viewportSize.height());
    bgfx::frame();

}

/******************************************************************************/
void bgfxRenderer::resize_OffscreenFramebuffer_DX11()
{
    resizeOffscreenFB(),
    bgfx::setViewFrameBuffer(0, offscreenFB);
    //bgfx::reset(m_viewportSize.width(), m_viewportSize.height());
    //bgfx::frame();
}

/******************************************************************************/
void bgfxRenderer::render_Common()
{
    bgfxExample.setSize(m_viewportSize.width(), m_viewportSize.height());
    bgfxExample.update();
}

/******************************************************************************/
// Use platformData to set backBuffer
void bgfxRenderer::render_ExternPlatform_DX11()
{
    // Synchronize BGFX RenderTarget
    const int countRT = 1; // Note that the RenderTarget[8] is not null, dunno what it is use for
    ID3D11RenderTargetView* pRenderTarget[countRT] = {};
    ID3D11DepthStencilView* pDepthTarget[countRT] = {};
    m_context->OMGetRenderTargets(countRT, pRenderTarget, pDepthTarget); //OMGetRenderTargetsAndUnorderedAccessViews ?

    bgfx::PlatformData pdata;
    pdata.context = m_device;
    pdata.backBuffer = pRenderTarget[0];
    pdata.backBufferDS = pDepthTarget[0];
    bgfx::setPlatformData(pdata);
    // not necessary with the 'proto-bgfx' branch (modification in renderer_d3d11->udpateResolution)
    // It's not a good usage to do this every frame
    // We should call this on resize event only
    // bgfx::reset(m_viewportSize.width(), m_viewportSize.height(), BGFX_RESET_NONE);        
    bgfx::setViewFrameBuffer(0, BGFX_INVALID_HANDLE);

    render_Common();

    // Restore RenderTarget in case of MultiPass rendering leave a different output
    m_context->OMSetRenderTargets(countRT, pRenderTarget, pDepthTarget[0]);

    // OMGetRenderTargets add a reference
    for (int i = 0; i < countRT; ++i)
    {
        SAFE_RELEASE(pRenderTarget[i]);
        SAFE_RELEASE(pDepthTarget[i]);
    }
}

/******************************************************************************/
// Create a bgfx::Framebuffer synchronized with extern Texture handle given by Qt
void bgfxRenderer::render_SynchroFramebuffer_DX11()
{
    // Synchronize BGFX RenderTarget
    const int countRT = 1; // Note that the RenderTarget[8] is not null, dunno what it is use for
    ID3D11RenderTargetView* pRenderTarget[countRT] = {};
    ID3D11DepthStencilView* pDepthTarget[countRT] = {};
    m_context->OMGetRenderTargets(countRT, pRenderTarget, pDepthTarget); //OMGetRenderTargetsAndUnorderedAccessViews ?

        // Create a bgfx counter part synchronize on directx handle
    if (pRenderTarget[0] && pDepthTarget[0])
    {
        D3D11_RENDER_TARGET_VIEW_DESC d3d11RenderTargetViewDesc;
        pRenderTarget[0]->GetDesc(&d3d11RenderTargetViewDesc);

        D3D11_DEPTH_STENCIL_VIEW_DESC d3d11DepthViewDesc;
        pDepthTarget[0]->GetDesc(&d3d11DepthViewDesc);

        if (d3d11RenderTargetViewDesc.ViewDimension == D3D11_RTV_DIMENSION_TEXTURE2D)
        {
            ID3D11Resource* resBackBufferRT = {};
            pRenderTarget[0]->GetResource(&resBackBufferRT);
            ID3D11Texture2D* backBufferRT = {};
            HRESULT_CHECK(resBackBufferRT->QueryInterface<ID3D11Texture2D>(&backBufferRT));

            ID3D11Resource* resDepthBufferRT = {};
            pDepthTarget[0]->GetResource(&resDepthBufferRT);
            ID3D11Texture2D* depthBufferRT = {};
            HRESULT_CHECK(resDepthBufferRT->QueryInterface<ID3D11Texture2D>(&depthBufferRT));


            if (!bgfx::isValid(backBuffer)
                || m_needreset)
            {
                if (bgfx::isValid(backBuffer))
                {
                    bgfx::destroy(backBuffer); backBuffer = BGFX_INVALID_HANDLE;
                    bgfx::destroy(depthBuffer); depthBuffer = BGFX_INVALID_HANDLE;
                }
                backBuffer = bgfx::createTexture2D(m_viewportSize.width(), m_viewportSize.height(), false, 1, bgfx::TextureFormat::RGBA8, BGFX_TEXTURE_RT | BGFX_TEXTURE_RT_WRITE_ONLY, NULL);
                depthBuffer = bgfx::createTexture2D(m_viewportSize.width(), m_viewportSize.height(), false, 1, bgfx::TextureFormat::D24S8, BGFX_TEXTURE_RT | BGFX_TEXTURE_RT_WRITE_ONLY, NULL);
                bgfx::frame(); // can't override if texture not already created
            }

            if (backBufferNative != (uintptr_t)backBufferRT
                || depthBufferNative != (uintptr_t)depthBufferRT)
            {
                backBufferNative = bgfx::overrideInternal(backBuffer, (uintptr_t)backBufferRT);
                depthBufferNative = bgfx::overrideInternal(depthBuffer, (uintptr_t)depthBufferRT);
                m_needreset = true;
            }

            SAFE_RELEASE(backBufferRT);
            SAFE_RELEASE(depthBufferRT);
            SAFE_RELEASE(resDepthBufferRT);
            SAFE_RELEASE(resBackBufferRT);
        }
    }
    else
    {
        // OMGetRenderTargets add a reference
        for (int i = 0; i < countRT; ++i)
        {
            if (pRenderTarget[i]) pRenderTarget[i]->Release();
            if (pDepthTarget[i]) pDepthTarget[i]->Release();
        }
        return;
    }

    if (!bgfx::isValid(offscreenFB) || m_needreset)
    {
        if (bgfx::isValid(offscreenFB))
        {
            bgfx::destroy(offscreenFB);
            offscreenFB = BGFX_INVALID_HANDLE;
        }
        bgfx::TextureHandle fbtextures[2] = { backBuffer, depthBuffer };
        offscreenFB = bgfx::createFrameBuffer(BX_COUNTOF(fbtextures), fbtextures, false);

        m_needreset = false;
    }
    bgfx::setViewFrameBuffer(0, offscreenFB);

    render_Common();

    // Restore RenderTarget in case of MultiPass rendering leave a different output
    m_context->OMSetRenderTargets(countRT, pRenderTarget, pDepthTarget[0]);

    // OMGetRenderTargets add a reference
    for (int i = 0; i < countRT; ++i)
    {
        SAFE_RELEASE(pRenderTarget[i]);
        SAFE_RELEASE(pDepthTarget[i]);
    }
}

/******************************************************************************/
// Create a bgfx::Framebuffer, render to it, then blit result
void bgfxRenderer::render_OffscreenFramebuffer_DX11()
{
    // Synchronize BGFX RenderTarget
    const int countRT = 1; // Note that the RenderTarget[8] is not null, dunno what it is use for
    ID3D11RenderTargetView* pRenderTarget[countRT] = {};
    ID3D11DepthStencilView* pDepthTarget[countRT] = {};
    m_context->OMGetRenderTargets(countRT, pRenderTarget, pDepthTarget); //OMGetRenderTargetsAndUnorderedAccessViews ?

    bgfx::setViewFrameBuffer(0, offscreenFB);
    render_Common();

    // Restore RenderTarget in case of MultiPass rendering leave a different output
    m_context->OMSetRenderTargets(countRT, pRenderTarget, pDepthTarget[0]);
    
    if (pRenderTarget[0])
    {
        ID3D11Texture2D* backBufferTexture2D = (ID3D11Texture2D*)bgfx::getInternal(backBuffer);
        if (backBufferTexture2D)
        {
            ID3D11Resource* src = {};
            HRESULT_CHECK(backBufferTexture2D->QueryInterface<ID3D11Resource>(&src));

            //D3D11_RENDER_TARGET_VIEW_DESC d3d11RenderTargetViewDesc;
            //pRenderTarget[0]->GetDesc(&d3d11RenderTargetViewDesc);
            //D3D11_DEPTH_STENCIL_VIEW_DESC d3d11DepthViewDesc;
            //pDepthTarget[0]->GetDesc(&d3d11DepthViewDesc);
            //assert(d3d11RenderTargetViewDesc.ViewDimension == D3D11_RTV_DIMENSION_TEXTURE2D);
            ID3D11Resource* dst = {};
            pRenderTarget[0]->GetResource(&dst);

            if (src != nullptr && dst != nullptr)
            {
                m_context->CopyResource(dst, src);
                m_context->Flush();
            }

            SAFE_RELEASE(dst);
            SAFE_RELEASE(src);
        }
    }

    // OMGetRenderTargets add a reference
    for (int i = 0; i < countRT; ++i)
    {
        SAFE_RELEASE(pRenderTarget[i]);
        SAFE_RELEASE(pDepthTarget[i]);
    }
}

/******************************************************************************/
void bgfxRenderer::mainPassRecordingStart()
{
    //qDebug() << "mainPassRecordingStart tid=" << GetCurrentThreadId();

    m_window->beginExternalCommands();

    if (bgfxGlobal.m_backend == bgfx::RendererType::Direct3D11)
    {
        switch (m_interopMode)
        {
        case InteropMode::ExternPlatform: render_ExternPlatform_DX11(); break;
        case InteropMode::SynchroFramebuffer: render_SynchroFramebuffer_DX11(); break;
        case InteropMode::OffscreenFramebuffer: render_OffscreenFramebuffer_DX11(); break;
        default:
            break;
        }
    }
    else
    {
        switch (m_interopMode)
        {
        case InteropMode::ExternPlatform: render_ExternPlatform_GL(); break;
        case InteropMode::SynchroFramebuffer: render_SynchroFramebuffer_GL(); break;
        case InteropMode::OffscreenFramebuffer: render_OffscreenFramebuffer_GL(); break;
        default:
            break;
        }

        m_window->resetOpenGLState();
    }

    m_window->endExternalCommands();  

    m_window->update();
}

/******************************************************************************/
void bgfxRenderer::init()
{
    WId wid = m_window->winId();
    m_interopMode = bgfxGlobal.m_interopMode;

    DWORD tid = GetCurrentThreadId();
    qDebug() << "bgfxItem Thread " << tid;
    m_initialized = true;

    

    // init if not already done
    if (bgfxGlobal.m_backend == bgfx::RendererType::Direct3D11)
    {
        bgfxGlobal.init(m_device);
    }
    else
    {
        bgfxGlobal.init(m_nativeglcontext);
    }

    if (m_interopMode == InteropMode::OffscreenFramebuffer)
    {

        bgfx::createFrameBuffer((void*)wid, m_viewportSize.width(), m_viewportSize.height());

        resizeOffscreenFB();
        bgfx::setViewFrameBuffer(0, offscreenFB);
    }

    resize();

    // Create example resources
    bgfxExample.init();
}

#include "bgfxItem.moc"
