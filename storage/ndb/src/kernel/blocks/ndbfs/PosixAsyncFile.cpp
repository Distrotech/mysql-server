/* Copyright (C) 2003 MySQL AB

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
#include <my_sys.h>
#include <my_pthread.h>

#ifdef HAVE_XFS_XFS_H
#include <xfs/xfs.h>
#endif

#include "AsyncFile.hpp"
#include "PosixAsyncFile.hpp"

#include <ErrorHandlingMacros.hpp>
#include <kernel_types.h>
#include <ndbd_malloc.hpp>
#include <NdbThread.h>
#include <signaldata/FsRef.hpp>
#include <signaldata/FsOpenReq.hpp>
#include <signaldata/FsReadWriteReq.hpp>

// use this to test broken pread code
//#define HAVE_BROKEN_PREAD

#ifdef HAVE_BROKEN_PREAD
#undef HAVE_PWRITE
#undef HAVE_PREAD
#endif

// For readv and writev
#include <sys/uio.h>

#include <dirent.h>

PosixAsyncFile::PosixAsyncFile(SimulatedBlock& fs) :
  AsyncFile(fs),
  theFd(-1),
  use_gz(0)
{
  memset(&azf,0,sizeof(azf));
}

int PosixAsyncFile::init()
{
  // Create write buffer for bigger writes
  theWriteBufferSize = WRITEBUFFERSIZE;
  theWriteBufferUnaligned = (char *) ndbd_malloc(theWriteBufferSize +
                                                 NDB_O_DIRECT_WRITE_ALIGNMENT-1);
  theWriteBuffer = (char *)
    (((UintPtr)theWriteBufferUnaligned + NDB_O_DIRECT_WRITE_ALIGNMENT - 1) &
     ~(UintPtr)(NDB_O_DIRECT_WRITE_ALIGNMENT - 1));

  azfBufferUnaligned= (Byte*)ndbd_malloc((AZ_BUFSIZE_READ+AZ_BUFSIZE_WRITE)
                                         +NDB_O_DIRECT_WRITE_ALIGNMENT-1);

  azf.inbuf= (Byte*)(((UintPtr)azfBufferUnaligned
                      + NDB_O_DIRECT_WRITE_ALIGNMENT - 1) &
                     ~(UintPtr)(NDB_O_DIRECT_WRITE_ALIGNMENT - 1));

  azf.outbuf= azf.inbuf + AZ_BUFSIZE_READ;

  az_mempool.size = az_mempool.mfree = az_inflate_mem_size()+az_deflate_mem_size();

  ndbout_c("NDBFS/AsyncFile: Allocating %u for In/Deflate buffer",
           (unsigned int)az_mempool.size);
  az_mempool.mem = (char*) ndbd_malloc(az_mempool.size);

  azf.stream.opaque= &az_mempool;

  if (!theWriteBuffer) {
    DEBUG(ndbout_c("AsyncFile::writeReq, Failed allocating write buffer"));
    return -1;
  }//if

  return 0;
}//AsyncFile::init()

#ifdef O_DIRECT
static char g_odirect_readbuf[2*GLOBAL_PAGE_SIZE -1];
#endif

int PosixAsyncFile::check_odirect_write(Uint32 flags, int& new_flags, int mode)
{
  assert(new_flags & (O_CREAT | O_TRUNC));
#ifdef O_DIRECT
  int ret;
  char * bufptr = (char*)((UintPtr(g_odirect_readbuf)+(GLOBAL_PAGE_SIZE - 1)) & ~(GLOBAL_PAGE_SIZE - 1));
  while (((ret = ::write(theFd, bufptr, GLOBAL_PAGE_SIZE)) == -1) &&
         (errno == EINTR));
  if (ret == -1)
  {
    new_flags &= ~O_DIRECT;
    ndbout_c("%s Failed to write using O_DIRECT, disabling",
             theFileName.c_str());
  }

  close(theFd);
  theFd = ::open(theFileName.c_str(), new_flags, mode);
  if (theFd == -1)
    return errno;
#endif

  return 0;
}

int PosixAsyncFile::check_odirect_read(Uint32 flags, int &new_flags, int mode)
{
#ifdef O_DIRECT
  int ret;
  char * bufptr = (char*)((UintPtr(g_odirect_readbuf)+(GLOBAL_PAGE_SIZE - 1)) & ~(GLOBAL_PAGE_SIZE - 1));
  while (((ret = ::read(theFd, bufptr, GLOBAL_PAGE_SIZE)) == -1) &&
         (errno == EINTR));
  if (ret == -1)
  {
    ndbout_c("%s Failed to read using O_DIRECT, disabling",
             theFileName.c_str());
    goto reopen;
  }

  if(lseek(theFd, 0, SEEK_SET) != 0)
  {
    return errno;
  }

  if ((flags & FsOpenReq::OM_CHECK_SIZE) == 0)
  {
    struct stat buf;
    if ((fstat(theFd, &buf) == -1))
    {
      return errno;
    }
    else if ((buf.st_size % GLOBAL_PAGE_SIZE) != 0)
    {
      ndbout_c("%s filesize not a multiple of %d, disabling O_DIRECT",
               theFileName.c_str(), GLOBAL_PAGE_SIZE);
      goto reopen;
    }
  }

  return 0;

reopen:
  close(theFd);
  new_flags &= ~O_DIRECT;
  theFd = ::open(theFileName.c_str(), new_flags, mode);
  if (theFd == -1)
    return errno;
#endif
  return 0;
}

void PosixAsyncFile::openReq(Request *request)
{
  m_auto_sync_freq = 0;
  m_write_wo_sync = 0;
  m_open_flags = request->par.open.flags;

  // for open.flags, see signal FSOPENREQ
  Uint32 flags = request->par.open.flags;
  int new_flags = 0;

  // Convert file open flags from Solaris to Liux
  if (flags & FsOpenReq::OM_CREATE)
  {
    new_flags |= O_CREAT;
  }

  if (flags & FsOpenReq::OM_TRUNCATE){
      new_flags |= O_TRUNC;
  }

  if (flags & FsOpenReq::OM_AUTOSYNC)
  {
    m_auto_sync_freq = request->par.open.auto_sync_size;
  }

  if (flags & FsOpenReq::OM_APPEND){
    new_flags |= O_APPEND;
  }

  if (flags & FsOpenReq::OM_DIRECT)
#ifdef O_DIRECT
  {
    new_flags |= O_DIRECT;
  }
#endif

  if ((flags & FsOpenReq::OM_SYNC) && ! (flags & FsOpenReq::OM_INIT))
  {
#ifdef O_SYNC
    new_flags |= O_SYNC;
#endif
  }

  const char * rw = "";
  switch(flags & 0x3){
  case FsOpenReq::OM_READONLY:
    rw = "r";
    new_flags |= O_RDONLY;
    break;
  case FsOpenReq::OM_WRITEONLY:
    rw = "w";
    new_flags |= O_WRONLY;
    break;
  case FsOpenReq::OM_READWRITE:
    rw = "rw";
    new_flags |= O_RDWR;
    break;
  default:
    request->error = 1000;
    break;
    return;
  }
  if(flags & FsOpenReq::OM_GZ)
    use_gz= 1;

  // allow for user to choose any permissionsa with umask
  const int mode = S_IRUSR | S_IWUSR |
	           S_IRGRP | S_IWGRP |
		   S_IROTH | S_IWOTH;
  if (flags & FsOpenReq::OM_CREATE_IF_NONE)
  {
    Uint32 tmp_flags = new_flags;
#ifdef O_DIRECT
    tmp_flags &= ~O_DIRECT;
#endif
    if ((theFd = ::open(theFileName.c_str(), tmp_flags, mode)) != -1)
    {
      close(theFd);
      request->error = FsRef::fsErrFileExists;
      return;
    }
    new_flags |= O_CREAT;
  }

no_odirect:
  theFd = ::open(theFileName.c_str(), new_flags, mode);
  if (-1 == theFd)
  {
    PRINT_ERRORANDFLAGS(new_flags);
    if ((errno == ENOENT) && (new_flags & O_CREAT))
    {
      createDirectories();
      theFd = ::open(theFileName.c_str(), new_flags, mode);
      if (-1 == theFd)
      {
#ifdef O_DIRECT
	if (new_flags & O_DIRECT)
	{
	  new_flags &= ~O_DIRECT;
	  goto no_odirect;
	}
#endif
        PRINT_ERRORANDFLAGS(new_flags);
        request->error = errno;
	return;
      }
    }
#ifdef O_DIRECT
    else if (new_flags & O_DIRECT)
    {
      new_flags &= ~O_DIRECT;
      goto no_odirect;
    }
#endif
    else
    {
      request->error = errno;
      return;
    }
  }

  if (flags & FsOpenReq::OM_CHECK_SIZE)
  {
    struct stat buf;
    if ((fstat(theFd, &buf) == -1))
    {
      request->error = errno;
    }
    else if((Uint64)buf.st_size != request->par.open.file_size)
    {
      request->error = FsRef::fsErrInvalidFileSize;
    }
    if (request->error)
      return;
  }

  if (flags & FsOpenReq::OM_INIT)
  {
    off_t off = 0;
    const off_t sz = request->par.open.file_size;
    SignalT<25> tmp;
    Signal * signal = (Signal*)(&tmp);
    bzero(signal, sizeof(tmp));
    FsReadWriteReq* req = (FsReadWriteReq*)signal->getDataPtrSend();

    Uint32 index = 0;
    Uint32 block = refToBlock(request->theUserReference);

#ifdef HAVE_XFS_XFS_H
    if(platform_test_xfs_fd(theFd))
    {
      ndbout_c("Using xfsctl(XFS_IOC_RESVSP64) to allocate disk space");
      xfs_flock64_t fl;
      fl.l_whence= 0;
      fl.l_start= 0;
      fl.l_len= (off64_t)sz;
      if(xfsctl(NULL, theFd, XFS_IOC_RESVSP64, &fl) < 0)
        ndbout_c("failed to optimally allocate disk space");
    }
#endif
#ifdef HAVE_POSIX_FALLOCATE
    posix_fallocate(theFd, 0, sz);
#endif

    while(off < sz)
    {
      req->filePointer = 0;          // DATA 0
      req->userPointer = request->theUserPointer;          // DATA 2
      req->numberOfPages = 1;        // DATA 5
      req->varIndex = index++;
      req->data.pageData[0] = m_page_ptr.i;

      m_fs.EXECUTE_DIRECT(block, GSN_FSWRITEREQ, signal,
			  FsReadWriteReq::FixedLength + 1,
                          0 // wl4391_todo This EXECUTE_DIRECT is thread safe
                          );
  retry:
      Uint32 size = request->par.open.page_size;
      char* buf = (char*)m_page_ptr.p;
      while(size > 0){
        int n;
	if(use_gz)
          n= azwrite(&azf,buf,size);
        else
          n= write(theFd, buf, size);
	if(n == -1 && errno == EINTR)
	{
	  continue;
	}
	if(n == -1 || n == 0)
	{
          ndbout_c("azwrite|write returned %d: errno: %d my_errno: %d",n,errno,my_errno);
	  break;
	}
	size -= n;
	buf += n;
      }
      if(size != 0)
      {
	int err = errno;
#ifdef O_DIRECT
	if ((new_flags & O_DIRECT) && off == 0)
	{
	  ndbout_c("error on first write(%d), disable O_DIRECT", err);
	  new_flags &= ~O_DIRECT;
	  close(theFd);
	  theFd = ::open(theFileName.c_str(), new_flags, mode);
	  if (theFd != -1)
	    goto retry;
	}
#endif
	close(theFd);
	unlink(theFileName.c_str());
	request->error = err;
	return;
      }
      off += request->par.open.page_size;
    }
    if(lseek(theFd, 0, SEEK_SET) != 0)
      request->error = errno;
  }
  else if (flags & FsOpenReq::OM_DIRECT)
  {
#ifdef O_DIRECT
    if (flags & (FsOpenReq::OM_TRUNCATE | FsOpenReq::OM_CREATE))
    {
      request->error = check_odirect_write(flags, new_flags, mode);
    }
    else
    {
      request->error = check_odirect_read(flags, new_flags, mode);
    }

    if (request->error)
      return;
#endif
  }
#ifdef VM_TRACE
  if (flags & FsOpenReq::OM_DIRECT)
  {
#ifdef O_DIRECT
    ndbout_c("%s %s O_DIRECT: %d",
             theFileName.c_str(), rw,
             !!(new_flags & O_DIRECT));
#else
    ndbout_c("%s %s O_DIRECT: 0",
             theFileName.c_str(), rw);
#endif
  }
#endif
  if ((flags & FsOpenReq::OM_SYNC) && (flags & FsOpenReq::OM_INIT))
  {
#ifdef O_SYNC
    /**
     * reopen file with O_SYNC
     */
    close(theFd);
    new_flags &= ~(O_CREAT | O_TRUNC);
    new_flags |= O_SYNC;
    theFd = ::open(theFileName.c_str(), new_flags, mode);
    if (theFd == -1)
    {
      request->error = errno;
    }
#endif
  }
  if(use_gz)
  {
    int err;
    if((err= azdopen(&azf, theFd, new_flags)) < 1)
    {
      ndbout_c("Stewart's brain broke: %d %d %s",
               err, my_errno, theFileName.c_str());
      abort();
    }
  }
}

int PosixAsyncFile::readBuffer(Request *req, char *buf,
                               size_t size, off_t offset)
{
  int return_value;
  req->par.readWrite.pages[0].size = 0;
#if ! defined(HAVE_PREAD)
  off_t seek_val;
  if(!use_gz)
  {
    while((seek_val= lseek(theFd, offset, SEEK_SET)) == (off_t)-1
          && errno == EINTR);
    if(seek_val == (off_t)-1)
    {
      return errno;
    }
  }
#endif
  off_t seek_val;
  if(use_gz)
  {
    while((seek_val= azseek(&azf, offset, SEEK_SET)) == (off_t)-1
          && errno == EINTR);
    if(seek_val == (off_t)-1)
    {
      return errno;
    }
  }

  int error;

  while (size > 0) {
    size_t bytes_read = 0;

#if  ! defined(HAVE_PREAD)
    if(use_gz)
      return_value = azread(&azf, buf, size, &error);
    else
      return_value = ::read(theFd, buf, size);
#else // UNIX
    if(!use_gz)
      return_value = ::pread(theFd, buf, size, offset);
    else
      return_value = azread(&azf, buf, size, &error);
#endif
    if (return_value == -1 && errno == EINTR) {
      DEBUG(ndbout_c("EINTR in read"));
      continue;
    } else if (!use_gz) {
      if (return_value == -1)
        return errno;
    }
    else if (return_value < 1 && azf.z_eof!=1)
    {
      if(my_errno==0 && errno==0 && error==0 && azf.z_err==Z_STREAM_END)
        break;
      DEBUG(ndbout_c("ERROR DURING %sRead: %d off: %d from %s",(use_gz)?"gz":"",size,offset,theFileName.c_str()));
      ndbout_c("ERROR IN PosixAsyncFile::readBuffer %d %d %d %d",
               my_errno, errno, azf.z_err, error);
      if(use_gz)
        return my_errno;
      return errno;
    }
    bytes_read = return_value;
    req->par.readWrite.pages[0].size += bytes_read;
    if(bytes_read == 0){
      if(req->action == Request::readPartial)
      {
	return 0;
      }
      DEBUG(ndbout_c("Read underflow %d %d\n %x\n%d %d",
		     size, offset, buf, bytes_read, return_value));
      return ERR_ReadUnderflow;
    }

    if(bytes_read != size){
      DEBUG(ndbout_c("Warning partial read %d != %d on %s",
		     bytes_read, size, theFileName.c_str()));
    }

    buf += bytes_read;
    size -= bytes_read;
    offset += bytes_read;
  }
  return 0;
}

void PosixAsyncFile::readvReq(Request *request)
{
#if ! defined(HAVE_PREAD)
  readReq(request);
  return;
#else
  int return_value;
  int length = 0;
  struct iovec iov[20]; // the parameter in the signal restricts this to 20 deep
  for(int i=0; i < request->par.readWrite.numberOfPages ; i++) {
    iov[i].iov_base= request->par.readWrite.pages[i].buf;
    iov[i].iov_len= request->par.readWrite.pages[i].size;
    length = length + iov[i].iov_len;
  }
  lseek( theFd, request->par.readWrite.pages[0].offset, SEEK_SET );
  return_value = ::readv(theFd, iov, request->par.readWrite.numberOfPages);
  if (return_value == -1) {
    request->error = errno;
    return;
  } else if (return_value != length) {
    request->error = 1011;
    return;
  }
#endif
}

int PosixAsyncFile::writeBuffer(const char *buf, size_t size, off_t offset,
                                size_t chunk_size)
{
  size_t bytes_to_write = chunk_size;
  int return_value;

  m_write_wo_sync += size;

#if ! defined(HAVE_PWRITE)
  off_t seek_val;
  while((seek_val= lseek(theFd, offset, SEEK_SET)) == (off_t)-1
	&& errno == EINTR);
  if(seek_val == (off_t)-1)
  {
    return errno;
  }
#endif

  while (size > 0) {
    if (size < bytes_to_write){
      // We are at the last chunk
      bytes_to_write = size;
    }
    size_t bytes_written = 0;

#if ! defined(HAVE_PWRITE)
    if(use_gz)
      return_value= azwrite(&azf, buf, bytes_to_write);
    else
      return_value = ::write(theFd, buf, bytes_to_write);
#else // UNIX
    if(use_gz)
      return_value= azwrite(&azf, buf, bytes_to_write);
    else
      return_value = ::pwrite(theFd, buf, bytes_to_write, offset);
#endif
    if (return_value == -1 && errno == EINTR) {
      bytes_written = 0;
      DEBUG(ndbout_c("EINTR in write"));
    } else if (return_value == -1 || return_value < 1){
      ndbout_c("ERROR IN PosixAsyncFile::writeBuffer %d %d %d",
               my_errno, errno, azf.z_err);
      if(use_gz)
        return my_errno;
      return errno;
    } else {
      bytes_written = return_value;

      if(bytes_written == 0){
        DEBUG(ndbout_c("no bytes written"));
	abort();
      }

      if(bytes_written != bytes_to_write){
	DEBUG(ndbout_c("Warning partial write %d != %d",
		 bytes_written, bytes_to_write));
      }
    }

    buf += bytes_written;
    size -= bytes_written;
    offset += bytes_written;
  }
  return 0;
}

void PosixAsyncFile::closeReq(Request *request)
{
  if (m_open_flags & (
      FsOpenReq::OM_WRITEONLY |
      FsOpenReq::OM_READWRITE |
      FsOpenReq::OM_APPEND )) {
    syncReq(request);
  }
  int r;
  if(use_gz)
    r= azclose(&azf);
  else
    r= ::close(theFd);
  use_gz= 0;
  Byte *a,*b;
  a= azf.inbuf;
  b= azf.outbuf;
  memset(&azf,0,sizeof(azf));
  azf.inbuf= a;
  azf.outbuf= b;
  azf.stream.opaque = (void*)&az_mempool;

  if (-1 == r) {
#ifndef DBUG_OFF
    if (theFd == -1) {
      DEBUG(ndbout_c("close on fd = -1"));
      abort();
    }
#endif
    request->error = errno;
  }
  theFd = -1;
}

bool PosixAsyncFile::isOpen(){
  return (theFd != -1);
}


void PosixAsyncFile::syncReq(Request *request)
{
  if(m_auto_sync_freq && m_write_wo_sync == 0){
    return;
  }
  if (-1 == ::fsync(theFd)){
    request->error = errno;
    return;
  }
  m_write_wo_sync = 0;
}

void PosixAsyncFile::appendReq(Request *request)
{
  const char * buf = request->par.append.buf;
  Uint32 size = request->par.append.size;

  m_write_wo_sync += size;

  while(size > 0){
    int n;
    if(use_gz)
      n= azwrite(&azf,buf,size);
    else
      n= write(theFd, buf, size);
    if(n == -1 && errno == EINTR){
      continue;
    }
    if(n == -1){
      if(use_gz)
        request->error = my_errno;
      else
        request->error = errno;
      return;
    }
    if(n == 0){
      DEBUG(ndbout_c("append with n=0"));
      abort();
    }
    size -= n;
    buf += n;
  }

  if(m_auto_sync_freq && m_write_wo_sync > m_auto_sync_freq){
    syncReq(request);
  }
}

void PosixAsyncFile::removeReq(Request *request)
{
  if (-1 == ::remove(theFileName.c_str())) {
    request->error = errno;

  }
}

void PosixAsyncFile::rmrfReq(Request *request, char *path, bool removePath)
{
  Uint32 path_len = strlen(path);
  Uint32 path_max_copy = PATH_MAX - path_len;
  char* path_add = &path[path_len];

  if(!request->par.rmrf.directory){
    // Remove file
    if(unlink((const char *)path) != 0 && errno != ENOENT)
      request->error = errno;
    return;
  }
  // Remove directory
  DIR* dirp = opendir((const char *)path);
  if(dirp == 0){
    if(errno != ENOENT)
      request->error = errno;
    return;
  }
  struct dirent * dp;
  while ((dp = readdir(dirp)) != NULL){
    if ((strcmp(".", dp->d_name) != 0) && (strcmp("..", dp->d_name) != 0)) {
      BaseString::snprintf(path_add, (size_t)path_max_copy, "%s%s",
	       DIR_SEPARATOR, dp->d_name);
      if(remove((const char*)path) == 0){
        path[path_len] = 0;
	continue;
      }

      rmrfReq(request, path, true);
      path[path_len] = 0;
      if(request->error != 0){
	closedir(dirp);
	return;
      }
    }
  }
  closedir(dirp);
  if(removePath && rmdir((const char *)path) != 0){
    request->error = errno;
  }
  return;
}

void PosixAsyncFile::endReq()
{
  // Thread is ended with return
  if (theWriteBufferUnaligned)
    ndbd_free(theWriteBufferUnaligned, theWriteBufferSize);

  if (azfBufferUnaligned)
    ndbd_free(azfBufferUnaligned, (AZ_BUFSIZE_READ*AZ_BUFSIZE_WRITE)
              +NDB_O_DIRECT_WRITE_ALIGNMENT-1);

  if(az_mempool.mem)
    ndbd_free(az_mempool.mem,az_mempool.size);

  az_mempool.mem = NULL;
  theWriteBufferUnaligned = NULL;
  azfBufferUnaligned = NULL;
}


void PosixAsyncFile::createDirectories()
{
  char* tmp;
  const char * name = theFileName.c_str();
  const char * base = theFileName.get_base_name();
  while((tmp = (char *)strstr(base, DIR_SEPARATOR)))
  {
    char t = tmp[0];
    tmp[0] = 0;
    mkdir(name, S_IRUSR | S_IWUSR | S_IXUSR | S_IXGRP | S_IRGRP);
    tmp[0] = t;
    base = tmp + sizeof(DIR_SEPARATOR);
  }
}
