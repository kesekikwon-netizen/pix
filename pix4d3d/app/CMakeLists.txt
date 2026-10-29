# 발굴 단면뷰어 (Qt Widgets + OpenGL). moc 없이 빌드(Q_OBJECT 미사용) → 교차 빌드에서 Qt 도구 불필요
set(APP_SRC main.cpp mainwindow.cpp mainwindow_ui.cpp sheetexport.cpp planview.cpp sectionview.cpp)
if(ASEC_QT_WIN_DIR)
  # Windows 교차 빌드: MinGW 용 Qt 가져오기 라이브러리 직접 연결
  set(Q ${ASEC_QT_WIN_DIR})
  enable_language(RC)
  add_executable(SectionViewer WIN32 ${APP_SRC} ${CMAKE_SOURCE_DIR}/packaging/app.rc)
  target_include_directories(SectionViewer PRIVATE ${Q}/include ${Q}/include/QtCore ${Q}/include/QtGui ${Q}/include/QtWidgets
                             ${Q}/include/QtOpenGL ${Q}/include/QtOpenGLWidgets ${Q}/mkspecs/win32-g++)
  target_link_libraries(SectionViewer PRIVATE archsection asec_stb
    ${Q}/lib/libQt6OpenGLWidgets.a ${Q}/lib/libQt6OpenGL.a ${Q}/lib/libQt6Widgets.a ${Q}/lib/libQt6Gui.a ${Q}/lib/libQt6Core.a
    opengl32 shell32 ole32 uuid)
  target_compile_definitions(SectionViewer PRIVATE UNICODE _UNICODE _USE_MATH_DEFINES QT_NO_DEBUG QT_CORE_LIB QT_GUI_LIB QT_WIDGETS_LIB
                             QT_OPENGL_LIB QT_OPENGLWIDGETS_LIB)
  target_compile_options(SectionViewer PRIVATE -finput-charset=UTF-8 -fexec-charset=UTF-8 -O2)
  target_link_options(SectionViewer PRIVATE -mwindows -static)
  set_source_files_properties(${CMAKE_SOURCE_DIR}/packaging/app.rc PROPERTIES COMPILE_FLAGS "-c 65001")
else()
  find_package(Qt6 REQUIRED COMPONENTS Widgets OpenGLWidgets OpenGL)
  add_executable(SectionViewer ${APP_SRC})
  target_link_libraries(SectionViewer PRIVATE archsection asec_stb Qt6::Widgets Qt6::OpenGLWidgets Qt6::OpenGL)
  target_compile_definitions(SectionViewer PRIVATE _USE_MATH_DEFINES)
  if(NOT MSVC)
    target_compile_options(SectionViewer PRIVATE -O2)  # PIX4D 개조: MSVC는 릴리스 플래그를 알아서 씀
  endif()
endif()
