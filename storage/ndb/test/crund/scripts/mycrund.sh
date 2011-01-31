#!/bin/bash

# Copyright (C) 2007, 2010, Oracle and/or its affiliates. All rights reserved.
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

#for f in run.ndbapi.opt run.ndbjtie.opt run.clusterj.opt run.mysql.opt run.openjpa.mysql.opt run.openjpa.clusterj.opt ; do
#for f in run.ndbapi.opt run.ndbjtie.opt run.clusterj.opt run.mysql.opt ; do
#for f in run.ndbjtie.opt ; do
#for f in run.ndbapi.opt ; do
for f in run.clusterj.opt ; do
  echo testing $f
  ./mycrundjava.sh $f
  mv -v results/xxx  results/xxx_$f
done
