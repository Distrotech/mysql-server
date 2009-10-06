/*
   Copyright (C) 2009 Sun Microsystems Inc.

   All rights reserved. Use is subject to license terms.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; version 2 of the License.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301  USA
 */

#include <NDBT.hpp>
#include <NDBT_Test.hpp>
#include <portlib/NdbDir.hpp>
#include "ConfigFactory.hpp"
#include <NdbMgmd.hpp>
#include <NdbProcess.hpp>
#include <NDBT_Find.hpp>
#include <NDBT_Workingdir.hpp>

static bool file_exists(const char* path)
{
  g_info << "File '" << path << "' ";
  if (access(path, F_OK) == 0)
  {
    g_info << "exists" << endl;
    return true;
  }
  g_info << "does not exist" << endl;
  return false;

}

// Util function that concatenate strings to form a path

static BaseString path(const char* first, ...)
{
  BaseString path;
  path.assign(first);

  const char* str;
  va_list args;
  va_start(args, first);
  while ((str = va_arg(args, const char*)) != NULL)
  {
    path.appfmt("%s%s", DIR_SEPARATOR, str);
  }
  va_end(args);
  return path;
}

class Mgmd
{
  NdbProcess* m_proc;
  int m_nodeid;
  BaseString m_name;
  BaseString m_exe;
  NdbMgmd m_mgmd_client;

  Mgmd(const Mgmd& other); // Not implemented
public:

  Mgmd(int nodeid) :
  m_proc(NULL),
  m_nodeid(nodeid)
  {
    m_name.assfmt("ndb_mgmd_%d", nodeid);

    NDBT_find_ndb_mgmd(m_exe);
  }

  ~Mgmd()
  {
    if (m_proc)
    {
      //stop the proces
      stop();
    }

  }

  const char* name(void) const
  {
    return m_name.c_str();
  }

  const char* exe(void) const
  {
    return m_exe.c_str();
  }

  bool start(const char* working_dir, NdbProcess::Args& args)
  {
    g_info << "Starting " << name() << " ";
    for (unsigned i = 0; i < args.args().size(); i++)
      g_info << args.args()[i].c_str() << " ";
    g_info << endl;

    m_proc = NdbProcess::create(name(),
                                exe(),
                                working_dir,
                                args);
    return (m_proc != NULL);
  }

  bool start_from_config_ini(const char* working_dir,
                             const char* first_extra_arg = NULL, ...)
  {
    NdbProcess::Args args;
    args.add("--configdir=.");
    args.add("-f config.ini");
    args.add("--ndb-nodeid=", m_nodeid);
    args.add("--nodaemon");
    args.add("--log-name=", name());
    args.add("--verbose");

    if (first_extra_arg)
    {
      // Append any extra args
      va_list extra_args;
      const char* str = first_extra_arg;
      va_start(extra_args, first_extra_arg);
      do
      {
        args.add(str);
      } while ((str = va_arg(extra_args, const char*)) != NULL);
      va_end(extra_args);
    }

    return start(working_dir, args);
  }

  bool start(const char* working_dir,
             const char* first_extra_arg = NULL, ...)
  {
    NdbProcess::Args args;
    args.add("--configdir=.");
    args.add("--ndb-nodeid=", m_nodeid);
    args.add("--nodaemon");
    args.add("--log-name=", name());
    args.add("--verbose");

    if (first_extra_arg)
    {
      // Append any extra args
      va_list extra_args;
      const char* str = first_extra_arg;
      va_start(extra_args, first_extra_arg);
      do
      {
        args.add(str);
      } while ((str = va_arg(extra_args, const char*)) != NULL);
      va_end(extra_args);
    }

    return start(working_dir, args);
  }

  bool stop(void)
  {
    g_info << "Stopping " << name() << endl;

    // Diconnect and close our "builtin" client
    m_mgmd_client.close();

    assert(m_proc);
    if (!m_proc->stop())
    {
      fprintf(stderr, "Failed to stop process %s\n", name());
      return false; // Can't kill with -9 -> fatal error
    }
    int ret;
    if (!m_proc->wait(ret, 300))
    {
      fprintf(stderr, "Failed to wait for process %s\n", name());
      return false; // Can't wait after kill with -9 -> fatal error
    }
    assert(ret == 9);
    delete m_proc;
    m_proc = 0;

    return true;

  }

  bool wait(int& ret, int timeout = 30)
  {
    g_info << "Waiting for " << name() << endl;

    assert(m_proc);
    if (!m_proc->wait(ret, timeout))
    {
      fprintf(stderr, "Failed to wait for process %s\n", name());
      return false;
    }
    delete m_proc;
    m_proc = 0;

    return true;

  }

  const BaseString connectstring(const Properties& config)
  {
    const char* hostname;
    if (!get_section_string(config, m_name.c_str(), "HostName", &hostname))
      return false;

    Uint32 port;
    if (!get_section_uint32(config, m_name.c_str(), "PortNumber", &port))
      return false;

    BaseString constr;
    constr.assfmt("%s:%d", hostname, port);
    return constr;
  }

  bool connect(const Properties& config,
               int num_retries = 60, int retry_delay_in_seconds = 1)
  {
    BaseString constr = connectstring(config);
    g_info << "Connecting to " << name() << " @ " << constr.c_str() << endl;

    return m_mgmd_client.connect(constr.c_str(),
                                 num_retries,
                                 retry_delay_in_seconds);
  }

  bool wait_confirmed_config(int timeout = 30)
  {
    if (!m_mgmd_client.is_connected())
    {
      g_err << "wait_confirmed_config: not connected!" << endl;
      return false;
    }

    int retries = 0;
    Config conf;
    while (!m_mgmd_client.get_config(conf))
    {
      retries++;

      if (retries == timeout * 10)
      {
        g_err << "wait_confirmed_config: Failed to get config within "
                << timeout << " seconds" << endl;
        return false;
      }

      g_err << "Failed to get config, sleeping" << endl;
      NdbSleep_MilliSleep(100);

    }
    g_info << "wait_confirmed_config: ok" << endl;
    return true;

  }

private:

  bool get_section_string(const Properties& config,
                          const char* section_name,
                          const char* key,
                          const char** value) const
  {
    const Properties* section;
    if (!config.get(section_name, &section))
      return false;

    if (!section->get(key, value))
      return false;
    return true;
  }

  bool get_section_uint32(const Properties& config,
                          const char* section_name,
                          const char* key,
                          Uint32* value) const
  {
    const Properties* section;
    if (!config.get(section_name, &section))
      return false;

    if (!section->get(key, value))
      return false;
    return true;
  }

};

class MgmdProcessList : public Vector<Mgmd*>
{
public:

  ~MgmdProcessList()
  {
    // Delete and thus stop the mgmd(s)
    for (unsigned i = 0; i < size(); i++)
    {
      Mgmd* mgmd = this->operator[](i);
      delete mgmd;
    }
    //  delete this->[i];
    clear();
  }
};


#define CHECK(x)                                            \
  if (!(x)) {                                               \
    fprintf(stderr, "CHECK(" #x ") failed at line: %d\n", \
            __LINE__);                                      \
     return NDBT_FAILED;                                    \
  }

int runTestBasic2Mgm(NDBT_Context* ctx, NDBT_Step* step)
{
  NDBT_Workingdir wd("test_mgmd"); // temporary working directory

  // Create config.ini
  Properties config = ConfigFactory::create(2);
  CHECK(ConfigFactory::write_config_ini(config,
                                        path(wd.path(),
                                             "config.ini",
                                             NULL).c_str()));
  // Start ndb_mgmd(s)
  MgmdProcessList mgmds;
  for (int i = 1; i <= 2; i++)
  {
    Mgmd* mgmd = new Mgmd(i);
    CHECK(mgmd->start_from_config_ini(wd.path()));
    mgmds.push_back(mgmd);
  }

  // Connect the ndb_mgmd(s)
  for (unsigned i = 0; i < mgmds.size(); i++)
    CHECK(mgmds[i]->connect(config));

  // wait for confirmed config
  for (unsigned i = 0; i < mgmds.size(); i++)
    CHECK(mgmds[i]->wait_confirmed_config());

  // Check binary config files created
  CHECK(file_exists(path(wd.path(),
                         "ndb_1_config.bin.1",
                         NULL).c_str()));
  CHECK(file_exists(path(wd.path(),
                         "ndb_2_config.bin.1",
                         NULL).c_str()));

  // Stop the ndb_mgmd(s)
  for (unsigned i = 0; i < mgmds.size(); i++)
    CHECK(mgmds[i]->stop());

  // Start up the mgmd(s) again from config.bin
  for (unsigned i = 0; i < mgmds.size(); i++)
    CHECK(mgmds[i]->start_from_config_ini(wd.path()));

  // Connect the ndb_mgmd(s)
  for (unsigned i = 0; i < mgmds.size(); i++)
    CHECK(mgmds[i]->connect(config));

  // check ndb_X_config.bin.1 still exists but not ndb_X_config.bin.2
  CHECK(file_exists(path(wd.path(),
                         "ndb_1_config.bin.1",
                         NULL).c_str()));
  CHECK(file_exists(path(wd.path(),
                         "ndb_2_config.bin.1",
                         NULL).c_str()));

  CHECK(!file_exists(path(wd.path(),
                          "ndb_1_config.bin.2",
                          NULL).c_str()));
  CHECK(!file_exists(path(wd.path(),
                          "ndb_2_config.bin.2",
                          NULL).c_str()));

  return NDBT_OK;

}

int runTestBug45495(NDBT_Context* ctx, NDBT_Step* step)
{
  NDBT_Workingdir wd("test_mgmd"); // temporary working directory

  g_err << "** Create config.ini" << endl;
  Properties config = ConfigFactory::create(2);
  CHECK(ConfigFactory::write_config_ini(config,
                                        path(wd.path(),
                                             "config.ini",
                                             NULL).c_str()));
  // Start ndb_mgmd(s)
  MgmdProcessList mgmds;
  for (int i = 1; i <= 2; i++)
  {
    Mgmd* mgmd = new Mgmd(i);
    CHECK(mgmd->start_from_config_ini(wd.path()));
    mgmds.push_back(mgmd);
  }

  // Connect the ndb_mgmd(s)
  for (unsigned i = 0; i < mgmds.size(); i++)
    CHECK(mgmds[i]->connect(config));

  // wait for confirmed config
  for (unsigned i = 0; i < mgmds.size(); i++)
    CHECK(mgmds[i]->wait_confirmed_config());

  // Check binary config files created
  CHECK(file_exists(path(wd.path(),
                         "ndb_1_config.bin.1",
                         NULL).c_str()));
  CHECK(file_exists(path(wd.path(),
                         "ndb_2_config.bin.1",
                         NULL).c_str()));

  g_err << "** Restart one ndb_mgmd at a time --reload + --initial" << endl;
  for (unsigned i = 0; i < mgmds.size(); i++)
  {
    CHECK(mgmds[i]->stop());
    CHECK(mgmds[i]->start_from_config_ini(wd.path(),
                                          "--reload", "--initial", NULL));
    CHECK(mgmds[i]->connect(config));
    CHECK(mgmds[i]->wait_confirmed_config());

    // check ndb_X_config.bin.1 still exists but not ndb_X_config.bin.2
    CHECK(file_exists(path(wd.path(),
                           "ndb_1_config.bin.1",
                           NULL).c_str()));
    CHECK(file_exists(path(wd.path(),
                           "ndb_2_config.bin.1",
                           NULL).c_str()));

    CHECK(!file_exists(path(wd.path(),
                            "ndb_1_config.bin.2",
                            NULL).c_str()));
    CHECK(!file_exists(path(wd.path(),
                            "ndb_2_config.bin.2",
                            NULL).c_str()));
  }

  g_err << "** Restart one ndb_mgmd at a time --initial" << endl;
  for (unsigned i = 0; i < mgmds.size(); i++)
  {
    CHECK(mgmds[i]->stop());
    CHECK(mgmds[i]->start_from_config_ini(wd.path(),
                                          "--initial", NULL));
    CHECK(mgmds[i]->connect(config));
    CHECK(mgmds[i]->wait_confirmed_config());

    // check ndb_X_config.bin.1 still exists but not ndb_X_config.bin.2
    CHECK(file_exists(path(wd.path(),
                           "ndb_1_config.bin.1",
                           NULL).c_str()));
    CHECK(file_exists(path(wd.path(),
                           "ndb_2_config.bin.1",
                           NULL).c_str()));

    CHECK(!file_exists(path(wd.path(),
                            "ndb_1_config.bin.2",
                            NULL).c_str()));
    CHECK(!file_exists(path(wd.path(),
                            "ndb_2_config.bin.2",
                            NULL).c_str()));
  }

  g_err << "** Create config2.ini" << endl;
  CHECK(ConfigFactory::put(config, "ndb_mgmd", 1, "ArbitrationDelay", 100));
  CHECK(ConfigFactory::write_config_ini(config,
                                        path(wd.path(),
                                             "config2.ini",
                                             NULL).c_str()));

  g_err << "** Restart one ndb_mgmd at a time --initial should not work" << endl;
  for (unsigned i = 0; i < mgmds.size(); i++)
  {
    CHECK(mgmds[i]->stop());
    // Start from config2.ini
    CHECK(mgmds[i]->start_from_config_ini(wd.path(),
                                          "-f config2.ini",
                                          "--initial", NULL));

    // Wait for mgmd to exit and check return status
    int ret;
    CHECK(mgmds[i]->wait(ret));
    CHECK(ret == 1);

    // check config files exist only for the still running mgmd(s)
    for (unsigned j = 0; j < mgmds.size(); j++)
    {
      BaseString tmp;
      tmp.assfmt("ndb_%d_config.bin.1", j+1);
      CHECK(file_exists(path(wd.path(),
                             tmp.c_str(),
                             NULL).c_str()) == (j != i));
    }

    // Start from config.ini again
    CHECK(mgmds[i]->start_from_config_ini(wd.path(),
                                          "--initial",
                                          "--reload",
                                          NULL));
    CHECK(mgmds[i]->connect(config));
    CHECK(mgmds[i]->wait_confirmed_config());
  }

  g_err << "** Reload from config2.ini" << endl;
  for (unsigned i = 0; i < mgmds.size(); i++)
  {
    CHECK(mgmds[i]->stop());
    // Start from config2.ini
    CHECK(mgmds[i]->start_from_config_ini(wd.path(),
                                          "-f config2.ini",
                                          "--reload", NULL));
    CHECK(mgmds[i]->connect(config));
    CHECK(mgmds[i]->wait_confirmed_config());

    CHECK(file_exists(path(wd.path(),
                           "ndb_1_config.bin.1",
                           NULL).c_str()));
    CHECK(file_exists(path(wd.path(),
                           "ndb_2_config.bin.1",
                           NULL).c_str()));

    CHECK(file_exists(path(wd.path(),
                            "ndb_1_config.bin.2",
                            NULL).c_str()));
    CHECK(file_exists(path(wd.path(),
                            "ndb_2_config.bin.2",
                            NULL).c_str()));
  }

  g_err << "** Reload mgmd initial(from generation=2)" << endl;
  for (unsigned i = 0; i < mgmds.size(); i++)
  {
    CHECK(mgmds[i]->stop());
    CHECK(mgmds[i]->start_from_config_ini(wd.path(),
                                          "-f config2.ini",
                                          "--reload", "--initial", NULL));
    CHECK(mgmds[i]->connect(config));
    CHECK(mgmds[i]->wait_confirmed_config());

     // check config files exist
    for (unsigned j = 0; j < mgmds.size(); j++)
    {
      BaseString tmp;
      tmp.assfmt("ndb_%d_config.bin.1", j+1);
      CHECK(file_exists(path(wd.path(),
                             tmp.c_str(),
                             NULL).c_str()) == (i < j));

      tmp.assfmt("ndb_%d_config.bin.2", j+1);
      CHECK(file_exists(path(wd.path(),
                             tmp.c_str(),
                             NULL).c_str()));
    }
  }

  return NDBT_OK;

}

int runTestBug42015(NDBT_Context* ctx, NDBT_Step* step)
{
  NDBT_Workingdir wd("test_mgmd"); // temporary working directory

  g_err << "** Create config.ini" << endl;
  Properties config = ConfigFactory::create(2);
  CHECK(ConfigFactory::write_config_ini(config,
                                        path(wd.path(),
                                             "config.ini",
                                             NULL).c_str()));

  MgmdProcessList mgmds;
  // Start ndb_mgmd 1 from config.ini
  Mgmd* mgmd = new Mgmd(1);
  CHECK(mgmd->start_from_config_ini(wd.path()));
  mgmds.push_back(mgmd);

  // Start ndb_mgmd 2 by fetching from first
  Mgmd* mgmd2 = new Mgmd(2);
  CHECK(mgmd2->start(wd.path(),
                     "--ndb-connectstring",
                     mgmd->connectstring(config).c_str(),
                     NULL));
  mgmds.push_back(mgmd2);

  // Connect the ndb_mgmd(s)
  for (unsigned i = 0; i < mgmds.size(); i++)
    CHECK(mgmds[i]->connect(config));

  // wait for confirmed config
  for (unsigned i = 0; i < mgmds.size(); i++)
    CHECK(mgmds[i]->wait_confirmed_config());

  // Check binary config files created
  CHECK(file_exists(path(wd.path(),
                         "ndb_1_config.bin.1",
                         NULL).c_str()));
  CHECK(file_exists(path(wd.path(),
                         "ndb_2_config.bin.1",
                         NULL).c_str()));

  return NDBT_OK;

}

NDBT_TESTSUITE(testMgmd);
DRIVER(DummyDriver); /* turn off use of NdbApi */

TESTCASE("Basic2Mgm",
         "Basic test with two mgmd")
{
  INITIALIZER(runTestBasic2Mgm);
}

TESTCASE("Bug45495",
         "Test that mgmd can be restarted in any order")
{
  INITIALIZER(runTestBug45495);
}

TESTCASE("Bug42015",
         "Test that mgmd can fetch configuration from another mgmd")
{
  INITIALIZER(runTestBug42015);
}

NDBT_TESTSUITE_END(testMgmd);

int main(int argc, const char** argv)
{
  ndb_init();
  NDBT_TESTSUITE_INSTANCE(testMgmd);
  testMgmd.setCreateTable(false);
  testMgmd.setRunAllTables(true);
  testMgmd.setConnectCluster(false);
  return testMgmd.execute(argc, argv);
}

template class Vector<Mgmd*>;

