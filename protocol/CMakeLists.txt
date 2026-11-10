# SPDX-FileCopyrightText: BANANAPIE LIMITED
# SPDX-License-Identifier: MIT
# See LICENSES/MIT.txt for the full license text.

add_library(ciciprotocol INTERFACE)
target_include_directories(ciciprotocol INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/..")
target_link_libraries(ciciprotocol INTERFACE cicibase)

