/* Copyright (C) 2009 Sun Microsystems, Inc.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; version 2 of the License.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA */

#include <gtest/gtest.h>

#include "mdl.h"                                // SHOULD BE my_sys.h
#include "my_getopt.h"

#include <stdlib.h>

namespace {

my_bool opt_use_tap= true;
my_bool opt_help= false;

struct my_option unittest_options[] =
{
  { "tap-output", 1, "TAP (default) or gunit output.",
    (uchar**) &opt_use_tap, (uchar**) &opt_use_tap, NULL,
    GET_BOOL, OPT_ARG,
    opt_use_tap, 0, 1, 0,
    0, NULL
  },
  { "help", 2, "Help.",
    (uchar**) &opt_help, (uchar**) &opt_help, NULL,
    GET_BOOL, NO_ARG,
    opt_help, 0, 1, 0,
    0, NULL
  },
  {0, 0, 0, 0, 0, 0, GET_NO_ARG, NO_ARG, 0, 0, 0, 0, 0, 0}
};


void print_usage()
{
  printf("\n\nTest options: [--[disable-]tap-output]\n");
}

my_bool
get_one_option(int optid,
               const struct my_option *opt,
               char *argument)
{
  return false;
}


int handle_unittest_options(int *argc, char ***argv)
{
  int error= handle_options(argc, argv, unittest_options, get_one_option);
  return error;
}

}  // namespace


extern void install_tap_listener();

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  MY_INIT(argv[0]);

  if (handle_unittest_options(&argc, &argv))
    return EXIT_FAILURE;
  if (opt_use_tap)
    install_tap_listener();
  if (opt_help)
    print_usage();

  return RUN_ALL_TESTS();
}
