cmake_minimum_required(VERSION 3.24)

project(StarQuant LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -finput-charset=UTF-8 -fexec-charset=UTF-8")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -finput-charset=UTF-8 -fexec-charset=UTF-8")

set(CMAKE_PREFIX_PATH $ENV{MY_QT_CMAKE_PREFIX_PATH})

if (MSVC)
    string(REGEX REPLACE "mingw_64" "msvc2019_64" CMAKE_PREFIX_PATH $ENV{MY_QT_CMAKE_PREFIX_PATH})
elseif (MINGW)
    set(CMAKE_PREFIX_PATH $ENV{MY_QT_CMAKE_PREFIX_PATH})
endif()

set(QT_VERSION 6)
set(REQUIRED_LIBS Core Widgets Network)
set(REQUIRED_LIBS_QUALIFIED Qt${QT_VERSION}::Core Qt${QT_VERSION}::Widgets Qt${QT_VERSION}::Network)

find_package(Qt${QT_VERSION} COMPONENTS ${REQUIRED_LIBS} REQUIRED)

qt_standard_project_setup()

add_executable(StarQuant WIN32
    src/main.cpp
    resources/starquant.rc
    resources/starquant.qrc
    src/mainwindow.cpp
    src/mainwindow.h
    src/mainwindow.ui
    src/market/marketdata.cpp
    src/market/marketdata.h
    src/screener/stockscreener.cpp
    src/screener/stockscreener.h
    src/indicator/indicators.cpp
    src/indicator/indicators.h
    src/strategy/strategybase.cpp
    src/strategy/strategybase.h
    src/strategy/movingaveragestrategy.cpp
    src/strategy/movingaveragestrategy.h
    src/strategy/prosperitygrowthstrategy.cpp
    src/strategy/prosperitygrowthstrategy.h
    src/ui/strategypanel.cpp
    src/ui/strategypanel.h
    src/ui/strategypanel.ui
    src/ui/marketpanel.cpp
    src/ui/marketpanel.h
    src/ui/marketpanel.ui
    src/ui/screenerpanel.cpp
    src/ui/screenerpanel.h
    src/ui/chartpanel.cpp
    src/ui/chartpanel.h
    src/ui/chartpanel.ui
    src/ui/accountpanel.cpp
    src/ui/accountpanel.h
    src/ui/accountpanel.ui
    src/ui/candlestickwidget.cpp
    src/ui/candlestickwidget.h
    src/ui/statisticspanel.cpp
    src/ui/statisticspanel.h
    src/ui/signalsignalpanel.cpp
    src/ui/signalsignalpanel.h
    src/ui/newspanel.cpp
    src/ui/newspanel.h
    src/ui/logindialog.cpp
    src/ui/logindialog.h
    src/ui/logindialog.ui
    src/ui/legalconsentdialog.cpp
    src/ui/legalconsentdialog.h
)

target_link_libraries(StarQuant PRIVATE ${REQUIRED_LIBS_QUALIFIED})

target_include_directories(StarQuant PRIVATE
    ${PROJECT_SOURCE_DIR}/src
    ${PROJECT_SOURCE_DIR}/src/market
    ${PROJECT_SOURCE_DIR}/src/screener
    ${PROJECT_SOURCE_DIR}/src/indicator
    ${PROJECT_SOURCE_DIR}/src/strategy
    ${PROJECT_SOURCE_DIR}/src/ui
)

if (WIN32)
    set(DEBUG_SUFFIX)
    if (MSVC AND CMAKE_BUILD_TYPE MATCHES "Debug")
        set(DEBUG_SUFFIX "d")
    endif ()
    set(QT_INSTALL_PATH "${CMAKE_PREFIX_PATH}")
    if (NOT EXISTS "${QT_INSTALL_PATH}/bin")
        set(QT_INSTALL_PATH "${QT_INSTALL_PATH}/..")
        if (NOT EXISTS "${QT_INSTALL_PATH}/bin")
            set(QT_INSTALL_PATH "${QT_INSTALL_PATH}/..")
        endif ()
    endif ()
    if (EXISTS "${QT_INSTALL_PATH}/plugins/platforms/qwindows${DEBUG_SUFFIX}.dll")
        add_custom_command(TARGET StarQuant POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E make_directory
                "$<TARGET_FILE_DIR:StarQuant>/plugins/platforms/")
        add_custom_command(TARGET StarQuant POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy
                "${QT_INSTALL_PATH}/plugins/platforms/qwindows${DEBUG_SUFFIX}.dll"
                "$<TARGET_FILE_DIR:StarQuant>/plugins/platforms/")
    endif ()
    foreach (QT_LIB Core Widgets Network)
        add_custom_command(TARGET StarQuant POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy
                "${QT_INSTALL_PATH}/bin/Qt6${QT_LIB}${DEBUG_SUFFIX}.dll"
                "$<TARGET_FILE_DIR:StarQuant>")
    endforeach (QT_LIB)
endif ()
