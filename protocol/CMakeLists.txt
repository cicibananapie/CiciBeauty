# SPDX-FileCopyrightText: BANANAPIE LIMITED
# SPDX-License-Identifier: MIT
# See LICENSES/MIT.txt for the full license text.

add_library(ciciprotocol INTERFACE
    account.h
    assets.h
    call.h
    catalog.h
    helpers.h
    message.h
    preview.h
    session.h
    settings.h
    statistics.h
    video.h
)
target_include_directories(ciciprotocol INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/..")
target_link_libraries(ciciprotocol INTERFACE cicibase)

