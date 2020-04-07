# Qt + bgfx with multiple backend

Since Qt 5.14, it's now possible to select a RHI (OpenGL/DX11/Metal/...) to render QML scene.

This project experiment the interoperability of the new Qt RHI with bgfx in a multi window / multi QML scene context.

![Preview](qml-bgfx-dx11.png)

# Dependencies

You need at least Qt 5.14<br>

> git clone https://github.com/VirtualGeo/qt-rhi-bgfx<br>
> mkdir build && cd build<br>
> cmake ..<br>

Build bgfx
> git clone --recurse-submodules https://github.com/VirtualGeo/bgfx.cmake<br>
> cd bgfx.cmake/bgfx<br>
> git pull<br>

Back to the bgfx.cmake directory and generate solution.<br>

> mkdir build/x64<br>
> cd build/x64<br>
> cmake ../.. -DBGFX_BUILD_EXAMPLES=OFF -DBGFX_BUILD_TOOLS=OFF -DCMAKE_INSTALL_PREFIX=../../bgfx-install/x64<br>
> cmake --build .<br>
> cmake --install ../../bgfx-install/x64<br>