

TEMPLATE = app
QT = core
TARGET = mkxp
DEPENDPATH += src shader assets
INCLUDEPATH += . src
QMAKE_LFLAGS += '-Wl,-rpath,\'\$$ORIGIN/lib\''

isEmpty(BINDING) {
	BINDING = BINDING_MRI
}

CONFIG += $$BINDING

unix {
    CONFIG += link_pkgconfig
    PKGCONFIG += sigc++-2.0 glew pixman-1 zlib sdl2 SDL2_image SDL2_ttf sfml-audio
	LIBS += -lphysfs
}

# 'slots' keyword fucks with libsigc++
DEFINES += QT_NO_KEYWORDS

# Input
HEADERS += \
	src/quadarray.h \
	src/audio.h \
	src/binding.h \
	src/bitmap.h \
	src/disposable.h \
	src/etc.h \
	src/etc-internal.h \
	src/eventthread.h \
	src/flashable.h \
	src/font.h \
	src/input.h \
	src/plane.h \
	src/scene.h \
	src/sprite.h \
	src/table.h \
	src/texpool.h \
	src/tilequad.h \
	src/transform.h \
	src/viewport.h \
	src/window.h \
	src/serializable.h \
	src/shader.h \
	src/glstate.h \
	src/quad.h \
	src/tilemap.h \
	src/graphics.h \
	src/debuglogger.h \
	src/global-ibo.h \
	src/exception.h \
	src/filesystem.h \
	src/serial-util.h \
	src/intrulist.h \
	src/binding.h \
	src/gl-util.h \
	src/util.h \
	src/config.h \
	src/tileatlas.h \
	src/perftimer.h \
	src/sharedstate.h

SOURCES += \
	src/main.cpp \
	src/audio.cpp \
	src/bitmap.cpp \
	src/eventthread.cpp \
	src/filesystem.cpp \
	src/font.cpp \
	src/input.cpp \
	src/plane.cpp \
	src/scene.cpp \
	src/sprite.cpp \
	src/table.cpp \
	src/tilequad.cpp \
	src/viewport.cpp \
	src/window.cpp \
	src/texpool.cpp \
	src/shader.cpp \
	src/glstate.cpp \
	src/tilemap.cpp \
	src/autotiles.cpp \
	src/graphics.cpp \
	src/debuglogger.cpp \
	src/etc.cpp \
	src/config.cpp \
	src/tileatlas.cpp \
	src/perftimer.cpp \
	src/sharedstate.cpp

EMBED = shader/transSimple.frag \
        shader/trans.frag \
        shader/hue.frag \
        shader/sprite.frag \
        shader/plane.frag \
        shader/bitmapBlit.frag \
        shader/simple.frag \
        shader/simpleColor.frag \
        shader/simpleAlpha.frag \
        shader/flashMap.frag \
        shader/blur.frag \
        shader/simple.vert \
        shader/simpleColor.vert \
        shader/sprite.vert \
        shader/simpleMatrix.vert \
        shader/blurH.vert \
        shader/blurV.vert \
        assets/liberation.ttf

defineReplace(xxdOutput) {
	return($$basename(1).xxd)
}

# xxd
xxd.output_function = xxdOutput
xxd.commands = xxd -i ${QMAKE_FILE_NAME} > ${QMAKE_FILE_OUT}
xxd.depends = $$EMBED
xxd.input = EMBED
xxd.variable_out = HEADERS

QMAKE_EXTRA_COMPILERS += xxd


BINDING_NULL {
	SOURCES += binding-null/binding-null.cpp
}

BINDING_MRUBY {
	LIBS += mruby/build/host/lib/libmruby.a
	INCLUDEPATH += mruby/include
	DEPENDPATH += mruby/include
	DEFINES += BINDING_MRUBY

	HEADERS += \
	binding-mruby/binding-util.h \
	binding-mruby/disposable-binding.h \
	binding-mruby/flashable-binding.h \
	binding-mruby/binding-types.h \
	binding-mruby/sceneelement-binding.h \
	binding-mruby/viewportelement-binding.h \
	binding-mruby/serializable-binding.h \
	binding-mruby/mrb-ext/file.h \
	binding-mruby/mrb-ext/rwmem.h \
	binding-mruby/mrb-ext/marshal.h

	SOURCES += \
	binding-mruby/binding-mruby.cpp \
	binding-mruby/binding-util.cpp \
	binding-mruby/window-binding.cpp \
	binding-mruby/bitmap-binding.cpp \
	binding-mruby/sprite-binding.cpp \
	binding-mruby/font-binding.cpp \
	binding-mruby/viewport-binding.cpp \
	binding-mruby/plane-binding.cpp \
	binding-mruby/audio-binding.cpp \
	binding-mruby/tilemap-binding.cpp \
	binding-mruby/etc-binding.cpp \
	binding-mruby/graphics-binding.cpp \
	binding-mruby/input-binding.cpp \
	binding-mruby/table-binding.cpp \
	binding-mruby/module_rpg.c \
	binding-mruby/mrb-ext/file.cpp \
	binding-mruby/mrb-ext/marshal.cpp \
	binding-mruby/mrb-ext/rwmem.cpp \
	binding-mruby/mrb-ext/kernel.cpp \
	binding-mruby/mrb-ext/time.cpp
}

BINDING_MRI {
	LIBS += ./ruby/libruby.so
	INCLUDEPATH += ruby/include ruby/.ext/include/x86_64-linux # need i386 paths here too!
	DEPENDPATH += ruby/include ruby/.ext/include/x86_64-linux
	DEFINES += BINDING_MRI

#	EMBED2 = binding-mri/module_rpg.rb
#	xxdp.output = ${QMAKE_FILE_NAME}.xxd
#	xxdp.commands = xxd+/xxd+ ${QMAKE_FILE_NAME} -o ${QMAKE_FILE_OUT} --string
#	xxdp.depends = $$EMBED2
#	xxdp.input = EMBED2
#	xxdp.variable_out = HEADERS
#	QMAKE_EXTRA_COMPILERS += xxdp

	HEADERS += \
	binding-mri/binding-util.h \
	binding-mri/binding-types.h \
	binding-mri/serializable-binding.h \
	binding-mri/disposable-binding.h \
	binding-mri/sceneelement-binding.h \
	binding-mri/viewportelement-binding.h \
	binding-mri/flashable-binding.h

	SOURCES += \
	binding-mri/binding-mri.cpp \
	binding-mri/binding-util.cpp \
	binding-mri/table-binding.cpp \
	binding-mri/etc-binding.cpp \
	binding-mri/bitmap-binding.cpp \
	binding-mri/font-binding.cpp \
	binding-mri/graphics-binding.cpp \
	binding-mri/input-binding.cpp \
	binding-mri/sprite-binding.cpp \
	binding-mri/viewport-binding.cpp \
	binding-mri/plane-binding.cpp \
	binding-mri/window-binding.cpp \
	binding-mri/tilemap-binding.cpp \
	binding-mri/audio-binding.cpp \
	binding-mri/module_rpg.cpp \
	binding-mri/filesystem-binding.cpp
}
