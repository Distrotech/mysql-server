# Copyright (C) 2007 MySQL AB
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; version 2 of the License.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA

INCLUDE_DIRECTORIES(${CMAKE_CURRENT_SOURCE_DIR}
                    ${CMAKE_BINARY_DIR}/include
                    ${CMAKE_BINARY_DIR}/storage/ndb/include
                    ${CMAKE_SOURCE_DIR}/include
                    ${CMAKE_SOURCE_DIR}/storage/ndb/include
                    ${CMAKE_SOURCE_DIR}/storage/ndb/src/kernel/vm
                    ${CMAKE_SOURCE_DIR}/storage/ndb/src/kernel/error
                    ${CMAKE_SOURCE_DIR}/storage/ndb/src/kernel/blocks
                    ${CMAKE_SOURCE_DIR}/storage/ndb/src/kernel
                    ${CMAKE_SOURCE_DIR}/storage/ndb/include/kernel
                    ${CMAKE_SOURCE_DIR}/storage/ndb/include/transporter
                    ${CMAKE_SOURCE_DIR}/storage/ndb/include/debugger
                    ${CMAKE_SOURCE_DIR}/storage/ndb/include/mgmapi
                    ${CMAKE_SOURCE_DIR}/storage/ndb/include/mgmcommon
                    ${CMAKE_SOURCE_DIR}/storage/ndb/include/ndbapi
                    ${CMAKE_SOURCE_DIR}/storage/ndb/include/util
                    ${CMAKE_SOURCE_DIR}/storage/ndb/include/portlib
                    ${CMAKE_SOURCE_DIR}/storage/ndb/include/logger
                    ${CMAKE_SOURCE_DIR}/zlib)

