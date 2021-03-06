# Qt styles module

HEADERS += \
        styles/qdrawutil.h \
        styles/qstyle.h \
        styles/qstyleanimation_p.h \
        styles/qstylefactory.h \
        styles/qstyleoption.h \
        styles/qstyleplugin.h \
        styles/qcommonstylepixmaps_p.h \
        styles/qcommonstyle.h \
        styles/qstylehelper_p.h \
        styles/qproxystyle.h \
        styles/qproxystyle_p.h \
        styles/qcommonstyle_p.h \
        styles/qstylepainter.h \
        styles/qstylesheetstyle_p.h \
        styles/qpixmapstyle_p.h \
        styles/qpixmapstyle_p_p.h

SOURCES += \
        styles/qdrawutil.cpp \
        styles/qstyle.cpp \
        styles/qstyleanimation.cpp \
        styles/qstylefactory.cpp \
        styles/qstyleoption.cpp \
        styles/qstyleplugin.cpp \
        styles/qstylehelper.cpp \
        styles/qcommonstyle.cpp \
        styles/qproxystyle.cpp \
        styles/qstylepainter.cpp \
        styles/qstylesheetstyle.cpp \
        styles/qstylesheetstyle_default.cpp \
        styles/qpixmapstyle.cpp

RESOURCES += styles/qstyle.qrc

contains( styles, all ) {
    styles = fusion mac windows windowsxp windowsvista
}

!macx:styles -= mac

contains( styles, mac ) {
    HEADERS += \
        styles/qmacstyle_mac_p.h \
        styles/qmacstyle_mac_p_p.h
    OBJECTIVE_SOURCES += styles/qmacstyle_mac.mm
    LIBS_PRIVATE += -framework Carbon
} else {
    DEFINES += QT_NO_STYLE_MAC
}

contains( styles, windowsvista ) {
    HEADERS += styles/qwindowsvistastyle_p.h
    HEADERS += styles/qwindowsvistastyle_p_p.h
    SOURCES += styles/qwindowsvistastyle.cpp
    !contains( styles, windowsxp ) {
        message( windowsvista requires windowsxp )
        styles += windowsxp
    }
} else {
    DEFINES += QT_NO_STYLE_WINDOWSVISTA
}

contains( styles, windowsxp ) {
    HEADERS += styles/qwindowsxpstyle_p.h
    HEADERS += styles/qwindowsxpstyle_p_p.h
    SOURCES += styles/qwindowsxpstyle.cpp
    !contains( styles, windows ) {
        message( windowsxp requires windows )
        styles  += windows
    }
} else {
    DEFINES += QT_NO_STYLE_WINDOWSXP
}

contains( styles, windows ) {
    HEADERS += styles/qwindowsstyle_p.h
    HEADERS += styles/qwindowsstyle_p_p.h
    SOURCES += styles/qwindowsstyle.cpp
} else {
    DEFINES += QT_NO_STYLE_WINDOWS
}

contains( styles, fusion ) {
        HEADERS += styles/qfusionstyle_p.h
        HEADERS += styles/qfusionstyle_p_p.h
        SOURCES += styles/qfusionstyle.cpp
} else {
    DEFINES += QT_NO_STYLE_FUSION
}

contains( styles, android ) {
    HEADERS += styles/qandroidstyle_p.h
    SOURCES += styles/qandroidstyle.cpp
} else {
    DEFINES += QT_NO_STYLE_ANDROID
}
