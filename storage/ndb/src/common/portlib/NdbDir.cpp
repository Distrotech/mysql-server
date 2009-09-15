/* Copyright (C) 2008-2009 Sun Microsystems, Inc.

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

#include <ndb_global.h>
#include <NdbDir.hpp>

#include <util/basestring_vsnprintf.h>

#ifndef __WIN__

#include <dirent.h>
#include <sys/stat.h>

class DirIteratorImpl {
  DIR* m_dirp;
  const char *m_path;
  char* m_buf;

  bool is_regular_file(struct dirent* dp) const {
#ifdef _DIRENT_HAVE_D_TYPE
    /*
      Using dirent's d_type field to determine if
      it's a regular file
    */
    if(dp->d_type != DT_UNKNOWN)
      return (dp->d_type == DT_REG);
#endif
    /* Using stat to read more info about the file */
    basestring_snprintf(m_buf, sizeof(m_buf),
                        "%s/%s", m_path, dp->d_name);

    struct stat buf;
    if (stat(m_buf, &buf))
      return false; // 'stat' failed

    return S_ISREG(buf.st_mode);

  }

public:
  DirIteratorImpl():
    m_dirp(NULL) {
     m_buf = new char[PATH_MAX];
  };

  ~DirIteratorImpl() {
    close();
    delete [] m_buf;
  }

  int open(const char* path){
    if ((m_dirp = opendir(path)) == NULL){
      return -1;
    }
    m_path= path;
    return 0;
  }

  void close(void)
  {
    if (m_dirp)
      closedir(m_dirp);
    m_dirp = NULL;
  }

  const char* next_entry(bool& is_directory)
  {
    struct dirent* dp = readdir(m_dirp);

    if (dp == NULL)
      return NULL;

    is_directory = !is_regular_file(dp);
    return dp->d_name;
  }
};

#else

class DirIteratorImpl {
  bool m_first;
  WIN32_FIND_DATA m_find_data;
  HANDLE m_find_handle;

  bool is_dir(const WIN32_FIND_DATA find_data) const {
    return (find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY);
  }

public:
  DirIteratorImpl():
    m_first(true),
    m_find_handle(INVALID_HANDLE_VALUE) {};

  ~DirIteratorImpl() {
    close();
  }

  int open(const char* path){
    char path_buf[PATH_MAX+2];
    m_first = true;
    basestring_snprintf(path_buf, sizeof(path_buf), "%s\\*", path);
    m_find_handle = FindFirstFile(path_buf, &m_find_data);
    if(m_find_handle == INVALID_HANDLE_VALUE)
    {
      if (GetLastError() == ERROR_FILE_NOT_FOUND)
        m_first= false; // Will do a seek in 'next_file' and return NULL
      else
       return -1;
    }
    return 0;
  }

  void close(void)
  {
    if (m_find_handle)
      FindClose(m_find_handle);
    m_find_handle = NULL;
  }

  const char* next_entry(bool& is_directory)
  {
    if (m_first || FindNextFile(m_find_handle, &m_find_data))
    {
      m_first = false;
      is_directory = is_dir(m_find_data);
      return m_find_data.cFileName;
    }
    return NULL;
  }
};

#endif


NdbDir::Iterator::Iterator() :
  m_impl(*new DirIteratorImpl())
{
}

NdbDir::Iterator::~Iterator()
{
  delete &m_impl;
}


int NdbDir::Iterator::open(const char* path)
{
  return m_impl.open(path);
}

void NdbDir::Iterator::close(void)
{
  m_impl.close();
}

const char* NdbDir::Iterator::next_file(void)
{
  bool is_dir;
  while(m_impl.next_entry(is_dir) != NULL && is_dir)
    ;
  return NULL;
}

const char* NdbDir::Iterator::next_entry(void)
{
  bool is_dir;
  return m_impl.next_entry(is_dir);
}


bool
NdbDir::create(const char *dir)
{
#ifdef _WIN32
  if (CreateDirectory(dir, NULL) == 0)
  {
    fprintf(stderr,
            "Failed to create directory '%s', error: %d",
            dir, GetLastError());
    return false;
  }
#else
  if (mkdir(dir, S_IRUSR | S_IWUSR | S_IXUSR) != 0)
  {
    fprintf(stderr,
            "Failed to create directory '%s', error: %d",
            dir, errno);
    return false;
  }
#endif
  return true;
}


NdbDir::Temp::Temp()
{
#ifdef _WIN32
  DWORD len = GetTempPath(0, NULL);
  m_path = new char[len];
  if (GetTempPath(len, (char*)m_path) == 0)
    abort();
#else
  char* tmp = getenv("TMPDIR");
  if (tmp)
    m_path = tmp;
  else
    m_path = "/tmp/";
#endif
}

NdbDir::Temp::~Temp()
{
#ifdef _WIN32
  delete [] m_path;
#endif
}


const char*
NdbDir::Temp::path(void) const {
  return m_path;
}


bool
NdbDir::remove(const char* path)
{
#ifdef _WIN32
  if (RemoveDirectory(path) != 0)
    return true; // Gone
#else
  if (rmdir(path) == 0)
    return true; // Gone
#endif
  return false;
}

bool
NdbDir::remove_recursive(const char* dir, bool only_contents)
{
  char path[PATH_MAX];
  if (basestring_snprintf(path, sizeof(path),
                          "%s%s", dir, DIR_SEPARATOR) < 0) {
    fprintf(stderr, "Too long path to remove: '%s'\n", dir);
    return false;
  }
  int start_len = strlen(path);

  const char* name;
  NdbDir::Iterator iter;
loop:
  {
    if (iter.open(path) != 0)
    {
      fprintf(stderr, "Failed to open iterator for '%s'\n",
              path);
      return false;
    }

    while ((name = iter.next_entry()) != NULL)
    {
      if ((strcmp(".", name) == 0) || (strcmp("..", name) == 0))
        continue;

      int end_len, len = strlen(path);
      if ((end_len = basestring_snprintf(path + len, sizeof(path) - len,
                                         "%s", name)) < 0)
      {
        fprintf(stderr, "Too long path detected: '%s'+'%s'\n",
                path, name);
        return false;
      }

      if (unlink(path) == 0 || NdbDir::remove(path) == true)
      {
        path[len] = 0;
        continue;
      }

      iter.close();

      // Append ending slash to the string
      int pos = len + end_len;
      if (basestring_snprintf(path + pos, sizeof(path) - pos,
                              "%s", DIR_SEPARATOR) < 0)
      {
        fprintf(stderr, "Too long path detected: '%s'+'%s'\n",
                path, DIR_SEPARATOR);
        return false;
      }

      goto loop;
    }
    iter.close();

    int len = strlen(path);
    path[len - 1] = 0; // remove ending slash

    char * prev_slash = strrchr(path, IF_WIN('\\', '/'));
    if (len > start_len && prev_slash)
    {
      // Not done yet, step up one dir level
      assert(prev_slash > path && prev_slash < path + sizeof(path));
      prev_slash[1] = 0;
      goto loop;
    }
  }

  if (only_contents == false && NdbDir::remove(dir) == false)
  {
    fprintf(stderr,
            "Failed to remove directory '%s', error: %d\n",
            dir, errno);
    return false;
  }

  return true;
}


#ifdef TEST_NDBDIR
#include <NdbTap.hpp>

#define CHECK(x) \
  if (!(x)) {					       \
    fprintf(stderr, "failed at line %d\n",  __LINE__ );	       \
    abort(); }

static void
build_tree(const char* path)
{
  char tmp[PATH_MAX];
  CHECK(NdbDir::create(path));

  // Create files in path/
  for (int i = 8; i < 14; i++){
    basestring_snprintf(tmp, sizeof(tmp), "%s%sfile%d", path, DIR_SEPARATOR, i);
    fclose(fopen(tmp, "w"));
  }

  // Create directories
  for (int i = 8; i < 14; i++){
    basestring_snprintf(tmp, sizeof(tmp), "%s%sdir%d", path, DIR_SEPARATOR, i);
    CHECK(NdbDir::create(tmp));

    // Create files in dir
    for (int j = 0; j < 6; j++){
      basestring_snprintf(tmp, sizeof(tmp), "%s%sdir%d%sfile%d",
	       path, DIR_SEPARATOR, i, DIR_SEPARATOR, j);
      fclose(fopen(tmp, "w"));
    }
  }

#ifndef _WIN32
  // Symlink the last file created to path/symlink
  char tmp2[PATH_MAX];
  basestring_snprintf(tmp2, sizeof(tmp2), "%s%ssymlink", path, DIR_SEPARATOR);
  CHECK(symlink(tmp, tmp2) == 0);
#endif
}

static bool
gone(const char *dir) {
  return (access(dir, F_OK) == -1 && errno == ENOENT);
}

TAPTEST(DirIterator)
{
  NdbDir::Temp tempdir;
  char path[PATH_MAX];
  basestring_snprintf(path, sizeof(path),"%s%s%s",
                      tempdir.path(), DIR_SEPARATOR, "ndbdir_test");

  printf("Using directory '%s'\n", path);

  // Build dir tree and remove all of it
  build_tree(path);
  CHECK(NdbDir::remove_recursive(path));
  CHECK(gone(path));

  // Remove non exisiting directory
  CHECK(!NdbDir::remove_recursive(path));
  CHECK(gone(path));

  // Build dir tree and remove everything inside it
  build_tree(path);
  CHECK(NdbDir::remove_recursive(path, true));
  CHECK(!gone(path));

  // Remoe also the empty dir
  CHECK(NdbDir::remove_recursive(path));
  CHECK(gone(path));

  // Remove non exisiting directory(again)
  CHECK(!NdbDir::remove_recursive(path));
  CHECK(gone(path));

  return 1; // OK
}
#endif
