/* -*- mode: java; c-basic-offset: 4; indent-tabs-mode: nil; -*-
 *  vim:expandtab:shiftwidth=4:tabstop=4:smarttab:
 *
 *  Copyright (C) 2009 MySQL
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include <iostream>
#include <sstream>
#include <fstream>
#include <string>
#include <vector>
#include <cassert>
#include <ctime>

#include "helpers.hpp"
#include "string_helpers.hpp"

#include "Driver.hpp"

using std::cout;
using std::flush;
using std::endl;
using std::ios_base;
using std::ofstream;
using std::ostringstream;
using std::string;
using std::wstring;
using std::vector;

using utils::Properties;
using utils::toBool;
using utils::toInt;
using utils::toString;

//---------------------------------------------------------------------------

vector< string > Driver::propFileNames;
string Driver::logFileName;

void
Driver::exitUsage()
{
    cout << "usage: [options]" << endl
         << "    [-p <file name>]...    properties file name" << endl
         << "    [-l <file name>]       log file name for data output" << endl
         << "    [-h|--help]            print usage message and exit" << endl
         << endl;
    exit(1); // return an error code
}

void
Driver::parseArguments(int argc, const char* argv[])
{
    for (int i = 1; i < argc; i++) {
        const string arg = argv[i];
        if (arg.compare("-p") == 0) {
            if (i >= argc) {
                exitUsage();
            }
            propFileNames.push_back(argv[++i]);
        } else if (arg.compare("-l") == 0) {
            if (i >= argc) {
                exitUsage();
            }
            logFileName = argv[++i];
        } else if (arg.compare("-h") == 0 || arg.compare("--help") == 0) {
            exitUsage();
        } else {
            cout << "unknown option: " << arg << endl;
            exitUsage();
        }
    }

    if (propFileNames.size() == 0) {
        propFileNames.push_back("run.properties");
    }

    if (logFileName.empty()) {
        logFileName = "log_";

        // format, destination strings (static size)
        const char format[] = "%Y%m%d_%H%M%S";
        const int size = sizeof("yyyymmdd_HHMMSS");
        char dest[size];

        // get time, convert to timeinfo (statically allocated) then to string
        const time_t now = time(0);
        const int nchars = strftime(dest, size, format, localtime(&now));
        assert (nchars == size-1);
        (void)nchars;

        logFileName += dest;
        logFileName += ".txt";
        //cout << "logFileName='" << logFileName << "'" << endl;
    }
}

// ----------------------------------------------------------------------

void
Driver::run() {
    init();

    // warmup runs
    for (int i = 0; i < warmupRuns; i++) {
        runTests();
    }

    // truncate log file, reset log buffers
    cout << endl
         << "++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++" << endl
         << "start logging results ..." << endl
         << "++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++" << endl
         << endl;
    header.rdbuf()->str("");
    rtimes.rdbuf()->str("");
    closeLogFile();
    openLogFile();

    // hot runs
    for (int i = 0; i < hotRuns; i++) {
        runTests();
    }

    // write log buffers
    if (logRealTime) {
        // doesn't work: ostream << ostringstream->rdbuf()
        log << descr << ", rtime[ms]"
            << header.rdbuf()->str() << endl
            << rtimes.rdbuf()->str() << endl << endl << endl;
    }
    if (logCpuTime) {
        // doesn't work: ostream << ostringstream->rdbuf()
        log << descr << ", ctime[ms]"
            << header.rdbuf()->str() << endl
            << ctimes.rdbuf()->str() << endl << endl << endl;
    }

    close();
}

void
Driver::init() {
    loadProperties();
    initProperties();
    printProperties();
    openLogFile();

    // clear log buffers
    logHeader = true;
    header.rdbuf()->str("");
    rtimes.rdbuf()->str("");
}

void
Driver::close() {
    // clear log buffers
    header.rdbuf()->str("");
    rtimes.rdbuf()->str("");

    closeLogFile();
}

void
Driver::loadProperties() {
    cout << endl;
    for (vector<string>::const_iterator i = propFileNames.begin();
         i != propFileNames.end(); ++i) {
        cout << "reading properties file:        " << *i << endl;
        props.load(i->c_str());
        props.load(i->c_str());
        //wcout << props << endl;
    }
}

void
Driver::initProperties() {
    cout << "setting driver properties ..." << flush;

    ostringstream msg;

    logRealTime = toBool(props[L"logRealTime"], true);
    logCpuTime = toBool(props[L"logCpuTime"], false);
    renewConnection = toBool(props[L"renewConnection"], false);

    warmupRuns = toInt(props[L"warmupRuns"], 0, -1);
    if (warmupRuns < 0) {
        msg << "[ignored] warmupRuns:        '"
            << toString(props[L"warmupRuns"]) << "'" << endl;
        warmupRuns = 0;
    }

    hotRuns = toInt(props[L"hotRuns"], 1, -1);
    if (hotRuns < 0) {
        msg << "[ignored] hotRuns:           '"
            << toString(props[L"hotRuns"]) << "'" << endl;
        hotRuns = 1;
    }

    if (msg.tellp() == 0) {
        cout << "   [ok]" << endl;
    } else {
        cout << endl << msg.str() << endl;
    }
}

void
Driver::printProperties() {
    const ios_base::fmtflags f = cout.flags();
    // no effect calling manipulator function, not sure why
    //cout << ios_base::boolalpha;
    cout.flags(ios_base::boolalpha);

    cout << endl << "driver settings ..." << endl;
    cout << "logRealTime:                    " << logRealTime << endl;
    cout << "logCpuTime:                     " << logCpuTime << endl;
    cout << "renewConnection:                " << renewConnection << endl;
    cout << "warmupRuns:                     " << warmupRuns << endl;
    cout << "hotRuns:                        " << hotRuns << endl;

    cout.flags(f);
}

void
Driver::openLogFile() {
    cout << endl
         << "writing results to file:        " << logFileName << endl;
    //log.open(logFileName.c_str());
    log.open(logFileName.c_str(), ios_base::out | ios_base::trunc);
    assert (log.good());
}

void
Driver::closeLogFile() {
    cout << "closing files ..." << flush;
    log.close();
    cout << "               [ok]" << endl;
}

// ----------------------------------------------------------------------

void
Driver::begin(const string& name) {
    cout << endl;
    cout << name << endl;

    if (logRealTime && logCpuTime) {
        s0 = hrt_tnow(&t0);
    } else if (logRealTime) {
        s0 = hrt_rtnow(&t0.rtstamp);
    } else if (logCpuTime) {
        s0 = hrt_ctnow(&t0.ctstamp);
    }
}

void
Driver::commit(const string& name) {
    if (logRealTime && logCpuTime) {
        s1 = hrt_tnow(&t1);
    } else if (logRealTime) {
        s1 = hrt_rtnow(&t1.rtstamp);
    } else if (logCpuTime) {
        s1 = hrt_ctnow(&t1.ctstamp);
    }

    if (logRealTime) {
        if (s0 | s1) {
            cout << "ERROR: failed to get the system's real time.";
            rtimes << "\tERROR";
        } else {
            long t = long(hrt_rtmicros(&t1.rtstamp, &t0.rtstamp)/1000);
            cout << "tx real time:                   " << t
                 << "\tms" << endl;
            rtimes << "\t" << t;
            rta += t;
        }
    }

    if (logCpuTime) {
        if (s0 | s1) {
            cout << "ERROR: failed to get this process's cpu time.";
            ctimes << "\tERROR";
        } else {
            long t = long(hrt_ctmicros(&t1.ctstamp, &t0.ctstamp)/1000);
            cout << "tx cpu time:                    " << t
                 << "\tms" << endl;
            ctimes << "\t" << t;
            cta += t;
        }
    }

    if (logHeader)
        header << "\t" << name;
}

//---------------------------------------------------------------------------
