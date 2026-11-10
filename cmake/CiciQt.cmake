# SPDX-FileCopyrightText: BANANAPIE LIMITED
# SPDX-License-Identifier: MIT
# See LICENSES/MIT.txt for the full license text.

set(CICI_DEPENDENCIES_DIR "${CMAKE_SOURCE_DIR}/depends" CACHE PATH "Downloaded public build dependencies")
set(QT_PATH ${CICI_DEPENDENCIES_DIR}/obsQT)
set(LUPDATE_PATH ${CICI_DEPENDENCIES_DIR}/QTTool/bin/lupdate)
set(LRELEASE_PATH ${CICI_DEPENDENCIES_DIR}/QTTool/bin/lrelease)
set(RCC_PATH ${CICI_DEPENDENCIES_DIR}/QTTool/bin/rcc)
set(UIC_PATH ${CICI_DEPENDENCIES_DIR}/QTTool/bin/uic)
set(CMAKE_PREFIX_PATH "${QT_PATH};${CMAKE_PREFIX_PATH}")
