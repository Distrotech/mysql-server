/* Copyright (C) 2000 MySQL AB & MySQL Finland AB & TCX DataKonsult AB

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA */

#ifndef _MINI_CLIENT_H
#define _MINI_CLIENT_H

MYSQL *
mysql_real_connect(MYSQL *mysql,const char *host, const char *user,
		   const char *passwd, const char *db,
		   uint port, const char *unix_socket,ulong client_flag,
		   uint net_read_timeout);

my_bool simple_command(MYSQL *mysql,enum enum_server_command command,
		       const char *arg, unsigned long length, 
		       my_bool skip_check);
void mysql_close(MYSQL *mysql);
MYSQL *mysql_init(MYSQL *mysql);
void mysql_debug(const char *debug);
ulong net_safe_read(MYSQL *mysql);
const char *mysql_error(MYSQL *mysql);
unsigned int  mysql_errno(MYSQL *mysql);
my_bool mysql_reconnect(MYSQL* mysql);
int mysql_send_query(MYSQL* mysql, const char* query, uint length);
my_bool mysql_read_query_result(MYSQL *mysql);
int mysql_real_query(MYSQL *mysql, const char *q, unsigned long length);
MYSQL_RES * mysql_store_result(MYSQL *mysql);
void mysql_free_result(MYSQL_RES *result);
void mysql_data_seek(MYSQL_RES *result, my_ulonglong row);
my_ulonglong mysql_num_rows(MYSQL_RES *res);
unsigned int mysql_num_fields(MYSQL_RES *res);
MYSQL_ROW STDCALL mysql_fetch_row(MYSQL_RES *res);
int mysql_select_db(MYSQL *mysql, const char *db);
void end_server(MYSQL *mysql); 

#endif
