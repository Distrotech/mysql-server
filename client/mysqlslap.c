/* Copyright (C) 2005 MySQL AB

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; version 2 of the License.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA

   original idea: Brian Aker via playing with ab for too many years
   coded by: Patrick Galbraith
*/


/*
  MySQL Slap

  A simple program designed to work as if multiple clients querying the database,
  then reporting the timing of each stage.

  MySQL slap runs three stages:
  1) Create schema,table, and optionally any SP or data you want to beign
     the test with. (single client)
  2) Load test (many clients)
  3) Cleanup (disconnection, drop table if specified, single client)

  Examples:

  Supply your own create and query SQL statements, with 50 clients 
  querying (200 selects for each):

    mysqlslap --delimiter=";" \
              --create="CREATE TABLE A (a int);INSERT INTO A VALUES (23)" \
              --query="SELECT * FROM A" --concurrency=50 --iterations=200

  Let the program build the query SQL statement with a table of two int
  columns, three varchar columns, five clients querying (20 times each),
  don't create the table or insert the data (using the previous test's
  schema and data):

    mysqlslap --concurrency=5 --iterations=20 \
              --number-int-cols=2 --number-char-cols=3 \
              --auto-generate-sql

  Tell the program to load the create, insert and query SQL statements from
  the specified files, where the create.sql file has multiple table creation
  statements delimited by ';' and multiple insert statements delimited by ';'.
  The --query file will have multiple queries delimited by ';', run all the 
  load statements, and then run all the queries in the query file
  with five clients (five times each):

    mysqlslap --concurrency=5 \
              --iterations=5 --query=query.sql --create=create.sql \
              --delimiter=";"

TODO:
  Add language for better tests
  String length for files and those put on the command line are not
    setup to handle binary data.
  More stats
  Break up tests and run them on multiple hosts at once.
  Allow output to be fed into a database directly.

*/

#define SLAP_VERSION "1.0"

#define HUGE_STRING_LENGTH 8196
#define RAND_STRING_SIZE 126

/* Types */
#define SELECT_TYPE 0
#define UPDATE_TYPE 1
#define INSERT_TYPE 2
#define UPDATE_TYPE_REQUIRES_PREFIX 3
#define CREATE_TABLE_TYPE 4
#define SELECT_TYPE_REQUIRES_PREFIX 5
#define DELETE_TYPE_REQUIRES_PREFIX 6

#include "client_priv.h"
#include <mysqld_error.h>
#include <my_dir.h>
#include <signal.h>
#include <stdarg.h>
#include <sslopt-vars.h>
#include <sys/types.h>
#ifndef __WIN__
#include <sys/wait.h>
#endif
#include <ctype.h>

#ifdef __WIN__
#define srandom  srand
#define random   rand
#define snprintf _snprintf
#endif

#ifdef HAVE_SMEM 
static char *shared_memory_base_name=0;
#endif

/* Global Thread counter */
uint thread_counter;
pthread_mutex_t counter_mutex;
pthread_cond_t count_threshhold;
uint master_wakeup;
pthread_mutex_t sleeper_mutex;
pthread_cond_t sleep_threshhold;

static char **defaults_argv;

char **primary_keys;
unsigned long long primary_keys_number_of;

static char *host= NULL, *opt_password= NULL, *user= NULL,
            *user_supplied_query= NULL,
            *user_supplied_pre_statements= NULL,
            *user_supplied_post_statements= NULL,
            *default_engine= NULL,
            *pre_system= NULL,
            *post_system= NULL,
            *opt_mysql_unix_port= NULL;

const char *delimiter= "\n";

const char *create_schema_string= "mysqlslap";

static my_bool opt_preserve= TRUE;
static my_bool debug_info_flag= 0, debug_check_flag= 0;
static my_bool opt_only_print= FALSE;
static my_bool opt_compress= FALSE, tty_password= FALSE,
               opt_silent= FALSE,
               auto_generate_sql_autoincrement= FALSE,
               auto_generate_sql_guid_primary= FALSE,
               auto_generate_sql= FALSE;
const char *auto_generate_sql_type= "mixed";

static unsigned long connect_flags= CLIENT_MULTI_RESULTS |
                                    CLIENT_MULTI_STATEMENTS;

static int verbose, delimiter_length;
static uint commit_rate;
static uint detach_rate;
const char *num_int_cols_opt;
const char *num_char_cols_opt;

/* Yes, we do set defaults here */
static unsigned int num_int_cols= 1;
static unsigned int num_char_cols= 1;
static unsigned int num_int_cols_index= 0; 
static unsigned int num_char_cols_index= 0;
static unsigned int iterations;
static uint my_end_arg= 0;
static char *default_charset= (char*) MYSQL_DEFAULT_CHARSET_NAME;
static ulonglong actual_queries= 0;
static ulonglong auto_actual_queries;
static ulonglong auto_generate_sql_unique_write_number;
static ulonglong auto_generate_sql_unique_query_number;
static unsigned int auto_generate_sql_secondary_indexes;
static ulonglong num_of_query;
static ulonglong auto_generate_sql_number;
const char *concurrency_str= NULL;
static char *create_string;
uint *concurrency;

const char *default_dbug_option="d:t:o,/tmp/mysqlslap.trace";
const char *opt_csv_str;
File csv_file;

static uint opt_protocol= 0;

static int get_options(int *argc,char ***argv);
static uint opt_mysql_port= 0;

static const char *load_default_groups[]= { "mysqlslap","client",0 };

typedef struct statement statement;

struct statement {
  char *string;
  size_t length;
  unsigned char type;
  char *option;
  size_t option_length;
  statement *next;
};

typedef struct option_string option_string;

struct option_string {
  char *string;
  size_t length;
  char *option;
  size_t option_length;
  option_string *next;
};

typedef struct stats stats;

struct stats {
  long int timing;
  uint users;
  unsigned long long rows;
};

typedef struct thread_context thread_context;

struct thread_context {
  statement *stmt;
  ulonglong limit;
};

typedef struct conclusions conclusions;

struct conclusions {
  char *engine;
  long int avg_timing;
  long int max_timing;
  long int min_timing;
  uint users;
  unsigned long long avg_rows;
  /* The following are not used yet */
  unsigned long long max_rows;
  unsigned long long min_rows;
};

static option_string *engine_options= NULL;
static statement *pre_statements= NULL; 
static statement *post_statements= NULL; 
static statement *create_statements= NULL, 
                 *query_statements= NULL;

/* Prototypes */
void print_conclusions(conclusions *con);
void print_conclusions_csv(conclusions *con);
void generate_stats(conclusions *con, option_string *eng, stats *sptr);
uint parse_comma(const char *string, uint **range);
uint parse_delimiter(const char *script, statement **stmt, char delm);
uint parse_option(const char *origin, option_string **stmt, char delm);
static int drop_schema(MYSQL *mysql, const char *db);
uint get_random_string(char *buf);
static statement *build_table_string(void);
static statement *build_insert_string(void);
static statement *build_update_string(void);
static statement * build_select_string(my_bool key);
static int generate_primary_key_list(MYSQL *mysql, option_string *engine_stmt);
static int drop_primary_key_list(void);
static int create_schema(MYSQL *mysql, const char *db, statement *stmt, 
              option_string *engine_stmt);
static int run_scheduler(stats *sptr, statement *stmts, uint concur, 
                         ulonglong limit);
pthread_handler_t run_task(void *p);
void statement_cleanup(statement *stmt);
void option_cleanup(option_string *stmt);
void concurrency_loop(MYSQL *mysql, uint current, option_string *eptr);
static int run_statements(MYSQL *mysql, statement *stmt);
int slap_connect(MYSQL *mysql);
static int run_query(MYSQL *mysql, const char *query, int len);

static const char ALPHANUMERICS[]=
  "0123456789ABCDEFGHIJKLMNOPQRSTWXYZabcdefghijklmnopqrstuvwxyz";

#define ALPHANUMERICS_SIZE (sizeof(ALPHANUMERICS)-1)


static long int timedif(struct timeval a, struct timeval b)
{
    register int us, s;
 
    us = a.tv_usec - b.tv_usec;
    us /= 1000;
    s = a.tv_sec - b.tv_sec;
    s *= 1000;
    return s + us;
}

#ifdef __WIN__
static int gettimeofday(struct timeval *tp, void *tzp)
{
  unsigned int ticks;
  ticks= GetTickCount();
  tp->tv_usec= ticks*1000;
  tp->tv_sec= ticks/1000;

  return 0;
}
#endif

int main(int argc, char **argv)
{
  MYSQL mysql;
  option_string *eptr;

  MY_INIT(argv[0]);

  load_defaults("my",load_default_groups,&argc,&argv);
  defaults_argv=argv;
  if (get_options(&argc,&argv))
  {
    free_defaults(defaults_argv);
    my_end(0);
    exit(1);
  }

  /* Seed the random number generator if we will be using it. */
  if (auto_generate_sql)
    srandom((uint)time(NULL));

  /* globals? Yes, so we only have to run strlen once */
  delimiter_length= strlen(delimiter);

  if (argc > 2)
  {
    fprintf(stderr,"%s: Too many arguments\n",my_progname);
    free_defaults(defaults_argv);
    my_end(0);
    exit(1);
  }
  mysql_init(&mysql);
  if (opt_compress)
    mysql_options(&mysql,MYSQL_OPT_COMPRESS,NullS);
#ifdef HAVE_OPENSSL
  if (opt_use_ssl)
    mysql_ssl_set(&mysql, opt_ssl_key, opt_ssl_cert, opt_ssl_ca,
                  opt_ssl_capath, opt_ssl_cipher);
#endif
  if (opt_protocol)
    mysql_options(&mysql,MYSQL_OPT_PROTOCOL,(char*)&opt_protocol);
#ifdef HAVE_SMEM
  if (shared_memory_base_name)
    mysql_options(&mysql,MYSQL_SHARED_MEMORY_BASE_NAME,shared_memory_base_name);
#endif
  mysql_options(&mysql, MYSQL_SET_CHARSET_NAME, default_charset);

  if (!opt_only_print) 
  {
    if (!(mysql_real_connect(&mysql, host, user, opt_password,
                             NULL, opt_mysql_port,
                             opt_mysql_unix_port, connect_flags)))
    {
      fprintf(stderr,"%s: Error when connecting to server: %s\n",
              my_progname,mysql_error(&mysql));
      free_defaults(defaults_argv);
      my_end(0);
      exit(1);
    }
  }

  VOID(pthread_mutex_init(&counter_mutex, NULL));
  VOID(pthread_cond_init(&count_threshhold, NULL));
  VOID(pthread_mutex_init(&sleeper_mutex, NULL));
  VOID(pthread_cond_init(&sleep_threshhold, NULL));

  /* Main iterations loop */
  eptr= engine_options;
  do
  {
    /* For the final stage we run whatever queries we were asked to run */
    uint *current;

    if (verbose >= 2)
      printf("Starting Concurrency Test\n");

    if (*concurrency)
    {
      for (current= concurrency; current && *current; current++)
        concurrency_loop(&mysql, *current, eptr);
    }
    else
    {
      uint infinite= 1;
      do {
        concurrency_loop(&mysql, infinite, eptr);
      }
      while (infinite++);
    }

    if (!opt_preserve)
      drop_schema(&mysql, create_schema_string);

  } while (eptr ? (eptr= eptr->next) : 0);

  VOID(pthread_mutex_destroy(&counter_mutex));
  VOID(pthread_cond_destroy(&count_threshhold));
  VOID(pthread_mutex_destroy(&sleeper_mutex));
  VOID(pthread_cond_destroy(&sleep_threshhold));

  if (!opt_only_print) 
    mysql_close(&mysql); /* Close & free connection */

  /* now free all the strings we created */
  if (opt_password)
    my_free(opt_password, MYF(0));

  my_free(concurrency, MYF(0));

  statement_cleanup(create_statements);
  statement_cleanup(query_statements);
  statement_cleanup(pre_statements);
  statement_cleanup(post_statements);
  option_cleanup(engine_options);

#ifdef HAVE_SMEM
  if (shared_memory_base_name)
    my_free(shared_memory_base_name, MYF(MY_ALLOW_ZERO_PTR));
#endif
  free_defaults(defaults_argv);
  my_end(my_end_arg);

  return 0;
}

void concurrency_loop(MYSQL *mysql, uint current, option_string *eptr)
{
  unsigned int x;
  stats *head_sptr;
  stats *sptr;
  conclusions conclusion;
  unsigned long long client_limit;

  head_sptr= (stats *)my_malloc(sizeof(stats) * iterations, 
                                MYF(MY_ZEROFILL|MY_FAE|MY_WME));

  bzero(&conclusion, sizeof(conclusions));

  if (auto_actual_queries)
    client_limit= auto_actual_queries;
  else if (num_of_query)
    client_limit=  num_of_query / current;
  else
    client_limit= actual_queries;

  for (x= 0, sptr= head_sptr; x < iterations; x++, sptr++)
  {
    /*
      We might not want to load any data, such as when we are calling
      a stored_procedure that doesn't use data, or we know we already have
      data in the table.
    */
    if (!opt_preserve)
      drop_schema(mysql, create_schema_string);

    /* First we create */
    if (create_statements)
      create_schema(mysql, create_schema_string, create_statements, eptr);

    /*
      If we generated GUID we need to build a list of them from creation that
      we can later use.
    */
    if (verbose >= 2)
      printf("Generating primary key list\n");
    if (auto_generate_sql_autoincrement || auto_generate_sql_guid_primary)
      generate_primary_key_list(mysql, eptr);

    if (commit_rate)
      run_query(mysql, "SET AUTOCOMMIT=0", strlen("SET AUTOCOMMIT=0"));

    if (pre_system)
      system(pre_system);

    /* 
      Pre statements are always run after all other logic so they can 
      correct/adjust any item that they want. 
    */
    if (pre_statements)
      run_statements(mysql, pre_statements);

    run_scheduler(sptr, query_statements, current, client_limit); 
    
    if (post_statements)
      run_statements(mysql, post_statements);

    if (post_system)
      system(post_system);

    /* We are finished with this run */
    if (auto_generate_sql_autoincrement || auto_generate_sql_guid_primary)
      drop_primary_key_list();
  }

  if (verbose >= 2)
    printf("Generating stats\n");

  generate_stats(&conclusion, eptr, head_sptr);

  if (!opt_silent)
    print_conclusions(&conclusion);
  if (opt_csv_str)
    print_conclusions_csv(&conclusion);

  my_free(head_sptr, MYF(0));

}


static struct my_option my_long_options[] =
{
  {"help", '?', "Display this help and exit.", 0, 0, 0, GET_NO_ARG, NO_ARG,
    0, 0, 0, 0, 0, 0},
  {"auto-generate-sql", 'a',
    "Generate SQL where not supplied by file or command line.",
    (uchar**) &auto_generate_sql, (uchar**) &auto_generate_sql,
    0, GET_BOOL, NO_ARG, 0, 0, 0, 0, 0, 0},
  {"auto-generate-sql-add-autoincrement", OPT_SLAP_AUTO_GENERATE_ADD_AUTO,
    "Add an AUTO_INCREMENT column to auto-generated tables.",
    (uchar**) &auto_generate_sql_autoincrement, 
    (uchar**) &auto_generate_sql_autoincrement,
    0, GET_BOOL, NO_ARG, 0, 0, 0, 0, 0, 0},
  {"auto-generate-sql-execute-number", OPT_SLAP_AUTO_GENERATE_EXECUTE_QUERIES,
    "Set this number to generate a set number of queries to run.",
    (uchar**) &auto_actual_queries, (uchar**) &auto_actual_queries,
    0, GET_ULL, REQUIRED_ARG, 0, 0, 0, 0, 0, 0},
  {"auto-generate-sql-guid-primary", OPT_SLAP_AUTO_GENERATE_GUID_PRIMARY,
    "Add GUID based primary keys to auto-generated tables.",
    (uchar**) &auto_generate_sql_guid_primary, 
    (uchar**) &auto_generate_sql_guid_primary,
    0, GET_BOOL, NO_ARG, 0, 0, 0, 0, 0, 0},
  {"auto-generate-sql-load-type", OPT_SLAP_AUTO_GENERATE_SQL_LOAD_TYPE,
    "Specify test load type: mixed, update, write, key, or read; default is mixed.",
    (uchar**) &auto_generate_sql_type, (uchar**) &auto_generate_sql_type,
    0, GET_STR, REQUIRED_ARG, 0, 0, 0, 0, 0, 0},
  {"auto-generate-sql-secondary-indexes", 
    OPT_SLAP_AUTO_GENERATE_SECONDARY_INDEXES, 
    "Number of secondary indexes to add to auto-generated tables.",
    (uchar**) &auto_generate_sql_secondary_indexes, 
    (uchar**) &auto_generate_sql_secondary_indexes, 0,
    GET_UINT, REQUIRED_ARG, 0, 0, 0, 0, 0, 0},
  {"auto-generate-sql-unique-query-number", 
    OPT_SLAP_AUTO_GENERATE_UNIQUE_QUERY_NUM,
    "Number of unique queries to generate for automatic tests.",
    (uchar**) &auto_generate_sql_unique_query_number, 
    (uchar**) &auto_generate_sql_unique_query_number,
    0, GET_ULL, REQUIRED_ARG, 10, 0, 0, 0, 0, 0},
  {"auto-generate-sql-unique-write-number", 
    OPT_SLAP_AUTO_GENERATE_UNIQUE_WRITE_NUM,
    "Number of unique queries to generate for auto-generate-sql-write-number.",
    (uchar**) &auto_generate_sql_unique_write_number, 
    (uchar**) &auto_generate_sql_unique_write_number,
    0, GET_ULL, REQUIRED_ARG, 10, 0, 0, 0, 0, 0},
  {"auto-generate-sql-write-number", OPT_SLAP_AUTO_GENERATE_WRITE_NUM,
    "Number of row inserts to perform for each thread (default is 100).",
    (uchar**) &auto_generate_sql_number, (uchar**) &auto_generate_sql_number,
    0, GET_ULL, REQUIRED_ARG, 100, 0, 0, 0, 0, 0},
  {"commit", OPT_SLAP_COMMIT, "Commit records every X number of statements.",
    (uchar**) &commit_rate, (uchar**) &commit_rate, 0, GET_UINT, REQUIRED_ARG,
    0, 0, 0, 0, 0, 0},
  {"compress", 'C', "Use compression in server/client protocol.",
    (uchar**) &opt_compress, (uchar**) &opt_compress, 0, GET_BOOL, NO_ARG, 0, 0, 0,
    0, 0, 0},
  {"concurrency", 'c', "Number of clients to simulate for query to run.",
    (uchar**) &concurrency_str, (uchar**) &concurrency_str, 0, GET_STR,
    REQUIRED_ARG, 0, 0, 0, 0, 0, 0},
  {"create", OPT_SLAP_CREATE_STRING, "File or string to use create tables.",
    (uchar**) &create_string, (uchar**) &create_string, 0, GET_STR, REQUIRED_ARG,
    0, 0, 0, 0, 0, 0},
  {"create-schema", OPT_CREATE_SLAP_SCHEMA, "Schema to run tests in.",
    (uchar**) &create_schema_string, (uchar**) &create_schema_string, 0, GET_STR, 
    REQUIRED_ARG, 0, 0, 0, 0, 0, 0},
  {"csv", OPT_SLAP_CSV,
	"Generate CSV output to named file or to stdout if no file is named.",
    (uchar**) &opt_csv_str, (uchar**) &opt_csv_str, 0, GET_STR, 
    OPT_ARG, 0, 0, 0, 0, 0, 0},
#ifdef DBUG_OFF
  {"debug", '#', "This is a non-debug version. Catch this and exit.",
   0, 0, 0, GET_DISABLED, OPT_ARG, 0, 0, 0, 0, 0, 0},
#else
  {"debug", '#', "Output debug log. Often this is 'd:t:o,filename'.",
    (uchar**) &default_dbug_option, (uchar**) &default_dbug_option, 0, GET_STR,
    OPT_ARG, 0, 0, 0, 0, 0, 0},
#endif
  {"debug-check", OPT_DEBUG_CHECK, "Check memory and open file usage at exit.",
   (uchar**) &debug_check_flag, (uchar**) &debug_check_flag, 0,
   GET_BOOL, NO_ARG, 0, 0, 0, 0, 0, 0},
  {"debug-info", 'T', "Print some debug info at exit.", (uchar**) &debug_info_flag,
   (uchar**) &debug_info_flag, 0, GET_BOOL, NO_ARG, 0, 0, 0, 0, 0, 0},
  {"delimiter", 'F',
    "Delimiter to use in SQL statements supplied in file or command line.",
    (uchar**) &delimiter, (uchar**) &delimiter, 0, GET_STR, REQUIRED_ARG,
    0, 0, 0, 0, 0, 0},
  {"detach", OPT_SLAP_DETACH,
    "Detach (close and reopen) connections after X number of requests.",
    (uchar**) &detach_rate, (uchar**) &detach_rate, 0, GET_UINT, REQUIRED_ARG, 
    0, 0, 0, 0, 0, 0},
  {"engine", 'e', "Storage engine to use for creating the table.",
    (uchar**) &default_engine, (uchar**) &default_engine, 0,
    GET_STR, REQUIRED_ARG, 0, 0, 0, 0, 0, 0},
  {"host", 'h', "Connect to host.", (uchar**) &host, (uchar**) &host, 0, GET_STR,
    REQUIRED_ARG, 0, 0, 0, 0, 0, 0},
  {"iterations", 'i', "Number of times to run the tests.", (uchar**) &iterations,
    (uchar**) &iterations, 0, GET_UINT, REQUIRED_ARG, 1, 0, 0, 0, 0, 0},
  {"number-char-cols", 'x', 
    "Number of VARCHAR columns to create in table if specifying --auto-generate-sql.",
    (uchar**) &num_char_cols_opt, (uchar**) &num_char_cols_opt, 0, GET_STR, REQUIRED_ARG,
    0, 0, 0, 0, 0, 0},
  {"number-int-cols", 'y', 
    "Number of INT columns to create in table if specifying --auto-generate-sql.",
    (uchar**) &num_int_cols_opt, (uchar**) &num_int_cols_opt, 0, GET_STR, REQUIRED_ARG, 
    0, 0, 0, 0, 0, 0},
  {"number-of-queries", OPT_MYSQL_NUMBER_OF_QUERY, 
    "Limit each client to this number of queries (this is not exact).",
    (uchar**) &num_of_query, (uchar**) &num_of_query, 0,
    GET_ULL, REQUIRED_ARG, 0, 0, 0, 0, 0, 0},
  {"only-print", OPT_MYSQL_ONLY_PRINT,
    "This causes mysqlslap to not connect to the databases, but instead print "
      "out what it would have done instead.",
    (uchar**) &opt_only_print, (uchar**) &opt_only_print, 0, GET_BOOL,  NO_ARG,
    0, 0, 0, 0, 0, 0},
  {"password", 'p',
    "Password to use when connecting to server. If password is not given it's "
      "asked from the tty.", 0, 0, 0, GET_STR, OPT_ARG, 0, 0, 0, 0, 0, 0},
#ifdef __WIN__
  {"pipe", 'W', "Use named pipes to connect to server.", 0, 0, 0, GET_NO_ARG,
    NO_ARG, 0, 0, 0, 0, 0, 0},
#endif
  {"port", 'P', "Port number to use for connection.", (uchar**) &opt_mysql_port,
    (uchar**) &opt_mysql_port, 0, GET_UINT, REQUIRED_ARG, MYSQL_PORT, 0, 0, 0, 0,
    0},
  {"post-query", OPT_SLAP_POST_QUERY,
    "Query to run or file containing query to execute after tests have completed.",
    (uchar**) &user_supplied_post_statements, 
    (uchar**) &user_supplied_post_statements,
    0, GET_STR, REQUIRED_ARG, 0, 0, 0, 0, 0, 0},
  {"post-system", OPT_SLAP_POST_SYSTEM,
    "system() string to execute after tests have completed.",
    (uchar**) &post_system, 
    (uchar**) &post_system,
    0, GET_STR, REQUIRED_ARG, 0, 0, 0, 0, 0, 0},
  {"pre-query", OPT_SLAP_PRE_QUERY, 
    "Query to run or file containing query to execute before running tests.",
    (uchar**) &user_supplied_pre_statements, 
    (uchar**) &user_supplied_pre_statements,
    0, GET_STR, REQUIRED_ARG, 0, 0, 0, 0, 0, 0},
  {"pre-system", OPT_SLAP_PRE_SYSTEM, 
    "system() string to execute before running tests.",
    (uchar**) &pre_system, 
    (uchar**) &pre_system,
    0, GET_STR, REQUIRED_ARG, 0, 0, 0, 0, 0, 0},
  {"protocol", OPT_MYSQL_PROTOCOL,
    "The protocol of connection (tcp,socket,pipe,memory).",
    0, 0, 0, GET_STR,  REQUIRED_ARG, 0, 0, 0, 0, 0, 0},
  {"query", 'q', "Query to run or file containing query to run.",
    (uchar**) &user_supplied_query, (uchar**) &user_supplied_query,
    0, GET_STR, REQUIRED_ARG, 0, 0, 0, 0, 0, 0},
#ifdef HAVE_SMEM
  {"shared-memory-base-name", OPT_SHARED_MEMORY_BASE_NAME,
    "Base name of shared memory.", (uchar**) &shared_memory_base_name,
    (uchar**) &shared_memory_base_name, 0, GET_STR_ALLOC, REQUIRED_ARG,
    0, 0, 0, 0, 0, 0},
#endif
  {"silent", 's', "Run program in silent mode - no output.",
    (uchar**) &opt_silent, (uchar**) &opt_silent, 0, GET_BOOL,  NO_ARG,
    0, 0, 0, 0, 0, 0},
  {"socket", 'S', "Socket file to use for connection.",
    (uchar**) &opt_mysql_unix_port, (uchar**) &opt_mysql_unix_port, 0, GET_STR,
    REQUIRED_ARG, 0, 0, 0, 0, 0, 0},
#include <sslopt-longopts.h>
#ifndef DONT_ALLOW_USER_CHANGE
  {"user", 'u', "User for login if not current user.", (uchar**) &user,
    (uchar**) &user, 0, GET_STR, REQUIRED_ARG, 0, 0, 0, 0, 0, 0},
#endif
  {"verbose", 'v',
    "More verbose output; you can use this multiple times to get even more "
      "verbose output.", (uchar**) &verbose, (uchar**) &verbose, 0, 
      GET_NO_ARG, NO_ARG, 0, 0, 0, 0, 0, 0},
  {"version", 'V', "Output version information and exit.", 0, 0, 0, GET_NO_ARG,
    NO_ARG, 0, 0, 0, 0, 0, 0},
  {0, 0, 0, 0, 0, 0, GET_NO_ARG, NO_ARG, 0, 0, 0, 0, 0, 0}
};


#include <help_start.h>

static void print_version(void)
{
  printf("%s  Ver %s Distrib %s, for %s (%s)\n",my_progname, SLAP_VERSION,
         MYSQL_SERVER_VERSION,SYSTEM_TYPE,MACHINE_TYPE);
}


static void usage(void)
{
  print_version();
  puts("Copyright (C) 2005 MySQL AB");
  puts("This software comes with ABSOLUTELY NO WARRANTY. This is free software,\nand you are welcome to modify and redistribute it under the GPL license\n");
  puts("Run a query multiple times against the server\n");
  printf("Usage: %s [OPTIONS]\n",my_progname);
  print_defaults("my",load_default_groups);
  my_print_help(my_long_options);
}

#include <help_end.h>

static my_bool
get_one_option(int optid, const struct my_option *opt __attribute__((unused)),
               char *argument)
{
  DBUG_ENTER("get_one_option");
  switch(optid) {
#ifdef __NETWARE__
  case OPT_AUTO_CLOSE:
    setscreenmode(SCR_AUTOCLOSE_ON_EXIT);
    break;
#endif
  case 'v':
    verbose++;
    break;
  case 'p':
    if (argument == disabled_my_option)
      argument= (char*) "";			/* Don't require password */
    if (argument)
    {
      char *start= argument;
      my_free(opt_password, MYF(MY_ALLOW_ZERO_PTR));
      opt_password= my_strdup(argument,MYF(MY_FAE));
      while (*argument) *argument++= 'x';		/* Destroy argument */
      if (*start)
        start[1]= 0;				/* Cut length of argument */
      tty_password= 0;
    }
    else
      tty_password= 1;
    break;
  case 'W':
#ifdef __WIN__
    opt_protocol= MYSQL_PROTOCOL_PIPE;
#endif
    break;
  case OPT_MYSQL_PROTOCOL:
    opt_protocol= find_type_or_exit(argument, &sql_protocol_typelib,
                                    opt->name);
    break;
  case '#':
    DBUG_PUSH(argument ? argument : default_dbug_option);
    debug_check_flag= 1;
    break;
#include <sslopt-case.h>
  case 'V':
    print_version();
    exit(0);
    break;
  case '?':
  case 'I':					/* Info */
    usage();
    exit(0);
  }
  DBUG_RETURN(0);
}


uint
get_random_string(char *buf)
{
  char *buf_ptr= buf;
  int x;
  DBUG_ENTER("get_random_string");
  for (x= RAND_STRING_SIZE; x > 0; x--)
    *buf_ptr++= ALPHANUMERICS[random() % ALPHANUMERICS_SIZE];
  DBUG_RETURN(buf_ptr - buf);
}


/*
  build_table_string

  This function builds a create table query if the user opts to not supply
  a file or string containing a create table statement
*/
static statement *
build_table_string(void)
{
  char       buf[HUGE_STRING_LENGTH];
  unsigned int        col_count;
  statement *ptr;
  DYNAMIC_STRING table_string;
  DBUG_ENTER("build_table_string");

  DBUG_PRINT("info", ("num int cols %u num char cols %u",
                      num_int_cols, num_char_cols));

  init_dynamic_string(&table_string, "", 1024, 1024);

  dynstr_append(&table_string, "CREATE TABLE `t1` (");

  if (auto_generate_sql_autoincrement)
  {
    dynstr_append(&table_string, "id serial");

    if (num_int_cols || num_char_cols)
      dynstr_append(&table_string, ",");
  }

  if (auto_generate_sql_guid_primary)
  {
    dynstr_append(&table_string, "id varchar(32) primary key");

    if (num_int_cols || num_char_cols || auto_generate_sql_guid_primary)
      dynstr_append(&table_string, ",");
  }

  if (auto_generate_sql_secondary_indexes)
  {
    unsigned int count;

    for (count= 0; count < auto_generate_sql_secondary_indexes; count++)
    {
      if (count) /* Except for the first pass we add a comma */
        dynstr_append(&table_string, ",");

      if (snprintf(buf, HUGE_STRING_LENGTH, "id%d varchar(32) unique key", count) 
          > HUGE_STRING_LENGTH)
      {
        fprintf(stderr, "Memory Allocation error in create table\n");
        exit(1);
      }
      dynstr_append(&table_string, buf);
    }

    if (num_int_cols || num_char_cols)
      dynstr_append(&table_string, ",");
  }

  if (num_int_cols)
    for (col_count= 1; col_count <= num_int_cols; col_count++)
    {
      if (num_int_cols_index)
      {
        if (snprintf(buf, HUGE_STRING_LENGTH, "intcol%d INT(32), INDEX(intcol%d)", 
                     col_count, col_count) > HUGE_STRING_LENGTH)
        {
          fprintf(stderr, "Memory Allocation error in create table\n");
          exit(1);
        }
      }
      else
      {
        if (snprintf(buf, HUGE_STRING_LENGTH, "intcol%d INT(32) ", col_count) 
            > HUGE_STRING_LENGTH)
        {
          fprintf(stderr, "Memory Allocation error in create table\n");
          exit(1);
        }
      }
      dynstr_append(&table_string, buf);

      if (col_count < num_int_cols || num_char_cols > 0)
        dynstr_append(&table_string, ",");
    }

  if (num_char_cols)
    for (col_count= 1; col_count <= num_char_cols; col_count++)
    {
      if (num_char_cols_index)
      {
        if (snprintf(buf, HUGE_STRING_LENGTH, 
                     "charcol%d VARCHAR(128), INDEX(charcol%d) ", 
                     col_count, col_count) > HUGE_STRING_LENGTH)
        {
          fprintf(stderr, "Memory Allocation error in creating table\n");
          exit(1);
        }
      }
      else
      {
        if (snprintf(buf, HUGE_STRING_LENGTH, "charcol%d VARCHAR(128)", 
                     col_count) > HUGE_STRING_LENGTH)
        {
          fprintf(stderr, "Memory Allocation error in creating table\n");
          exit(1);
        }
      }
      dynstr_append(&table_string, buf);

      if (col_count < num_char_cols)
        dynstr_append(&table_string, ",");
    }

  dynstr_append(&table_string, ")");
  ptr= (statement *)my_malloc(sizeof(statement), 
                              MYF(MY_ZEROFILL|MY_FAE|MY_WME));
  ptr->string = (char *)my_malloc(table_string.length+1,
                                  MYF(MY_ZEROFILL|MY_FAE|MY_WME));
  ptr->length= table_string.length+1;
  ptr->type= CREATE_TABLE_TYPE;
  strmov(ptr->string, table_string.str);
  dynstr_free(&table_string);
  DBUG_RETURN(ptr);
}

/*
  build_update_string()

  This function builds insert statements when the user opts to not supply
  an insert file or string containing insert data
*/
static statement *
build_update_string(void)
{
  char       buf[HUGE_STRING_LENGTH];
  unsigned int        col_count;
  statement *ptr;
  DYNAMIC_STRING update_string;
  DBUG_ENTER("build_update_string");

  init_dynamic_string(&update_string, "", 1024, 1024);

  dynstr_append(&update_string, "UPDATE t1 SET ");

  if (num_int_cols)
    for (col_count= 1; col_count <= num_int_cols; col_count++)
    {
      if (snprintf(buf, HUGE_STRING_LENGTH, "intcol%d = %ld", col_count, 
                   random()) > HUGE_STRING_LENGTH)
      {
        fprintf(stderr, "Memory Allocation error in creating update\n");
        exit(1);
      }
      dynstr_append(&update_string, buf);

      if (col_count < num_int_cols || num_char_cols > 0)
        dynstr_append_mem(&update_string, ",", 1);
    }

  if (num_char_cols)
    for (col_count= 1; col_count <= num_char_cols; col_count++)
    {
      char rand_buffer[RAND_STRING_SIZE];
      int buf_len= get_random_string(rand_buffer);

      if (snprintf(buf, HUGE_STRING_LENGTH, "charcol%d = '%.*s'", col_count, 
                   buf_len, rand_buffer) 
          > HUGE_STRING_LENGTH)
      {
        fprintf(stderr, "Memory Allocation error in creating update\n");
        exit(1);
      }
      dynstr_append(&update_string, buf);

      if (col_count < num_char_cols)
        dynstr_append_mem(&update_string, ",", 1);
    }

  if (auto_generate_sql_autoincrement || auto_generate_sql_guid_primary)
    dynstr_append(&update_string, " WHERE id = ");


  ptr= (statement *)my_malloc(sizeof(statement), 
                              MYF(MY_ZEROFILL|MY_FAE|MY_WME));

  ptr->string= (char *)my_malloc(update_string.length + 1,
                                  MYF(MY_ZEROFILL|MY_FAE|MY_WME));
  ptr->length= update_string.length+1;
  if (auto_generate_sql_autoincrement || auto_generate_sql_guid_primary)
    ptr->type= UPDATE_TYPE_REQUIRES_PREFIX ;
  else
    ptr->type= UPDATE_TYPE;
  strmov(ptr->string, update_string.str);
  dynstr_free(&update_string);
  DBUG_RETURN(ptr);
}


/*
  build_insert_string()

  This function builds insert statements when the user opts to not supply
  an insert file or string containing insert data
*/
static statement *
build_insert_string(void)
{
  char       buf[HUGE_STRING_LENGTH];
  unsigned int        col_count;
  statement *ptr;
  DYNAMIC_STRING insert_string;
  DBUG_ENTER("build_insert_string");

  init_dynamic_string(&insert_string, "", 1024, 1024);

  dynstr_append(&insert_string, "INSERT INTO t1 VALUES (");

  if (auto_generate_sql_autoincrement)
  {
    dynstr_append(&insert_string, "NULL");

    if (num_int_cols || num_char_cols)
      dynstr_append(&insert_string, ",");
  }

  if (auto_generate_sql_guid_primary)
  {
    dynstr_append(&insert_string, "uuid()");

    if (num_int_cols || num_char_cols)
      dynstr_append(&insert_string, ",");
  }

  if (auto_generate_sql_secondary_indexes)
  {
    unsigned int count;

    for (count= 0; count < auto_generate_sql_secondary_indexes; count++)
    {
      if (count) /* Except for the first pass we add a comma */
        dynstr_append(&insert_string, ",");

      dynstr_append(&insert_string, "uuid()");
    }

    if (num_int_cols || num_char_cols)
      dynstr_append(&insert_string, ",");
  }

  if (num_int_cols)
    for (col_count= 1; col_count <= num_int_cols; col_count++)
    {
      if (snprintf(buf, HUGE_STRING_LENGTH, "%ld", random()) > HUGE_STRING_LENGTH)
      {
        fprintf(stderr, "Memory Allocation error in creating insert\n");
        exit(1);
      }
      dynstr_append(&insert_string, buf);

      if (col_count < num_int_cols || num_char_cols > 0)
        dynstr_append_mem(&insert_string, ",", 1);
    }

  if (num_char_cols)
    for (col_count= 1; col_count <= num_char_cols; col_count++)
    {
      int buf_len= get_random_string(buf);
      dynstr_append_mem(&insert_string, "'", 1);
      dynstr_append_mem(&insert_string, buf, buf_len);
      dynstr_append_mem(&insert_string, "'", 1);

      if (col_count < num_char_cols)
        dynstr_append_mem(&insert_string, ",", 1);
    }

  dynstr_append_mem(&insert_string, ")", 1);

  ptr= (statement *)my_malloc(sizeof(statement),
                              MYF(MY_ZEROFILL|MY_FAE|MY_WME));
  ptr->string= (char *)my_malloc(insert_string.length + 1,
                              MYF(MY_ZEROFILL|MY_FAE|MY_WME));
  ptr->length= insert_string.length+1;
  ptr->type= INSERT_TYPE;
  strmov(ptr->string, insert_string.str);
  dynstr_free(&insert_string);
  DBUG_RETURN(ptr);
}


/*
  build_select_string()

  This function builds a query if the user opts to not supply a query
  statement or file containing a query statement
*/
static statement *
build_select_string(my_bool key)
{
  char       buf[HUGE_STRING_LENGTH];
  unsigned int        col_count;
  statement *ptr;
  static DYNAMIC_STRING query_string;
  DBUG_ENTER("build_select_string");

  init_dynamic_string(&query_string, "", 1024, 1024);

  dynstr_append_mem(&query_string, "SELECT ", 7);
  for (col_count= 1; col_count <= num_int_cols; col_count++)
  {
    if (snprintf(buf, HUGE_STRING_LENGTH, "intcol%d", col_count) 
        > HUGE_STRING_LENGTH)
    {
      fprintf(stderr, "Memory Allocation error in creating select\n");
      exit(1);
    }
    dynstr_append(&query_string, buf);

    if (col_count < num_int_cols || num_char_cols > 0)
      dynstr_append_mem(&query_string, ",", 1);

  }
  for (col_count= 1; col_count <= num_char_cols; col_count++)
  {
    if (snprintf(buf, HUGE_STRING_LENGTH, "charcol%d", col_count)
        > HUGE_STRING_LENGTH)
    {
      fprintf(stderr, "Memory Allocation error in creating select\n");
      exit(1);
    }
    dynstr_append(&query_string, buf);

    if (col_count < num_char_cols)
      dynstr_append_mem(&query_string, ",", 1);

  }
  dynstr_append(&query_string, " FROM t1");

  if ((key) && 
      (auto_generate_sql_autoincrement || auto_generate_sql_guid_primary))
    dynstr_append(&query_string, " WHERE id = ");

  ptr= (statement *)my_malloc(sizeof(statement),
                              MYF(MY_ZEROFILL|MY_FAE|MY_WME));
  ptr->string= (char *)my_malloc(query_string.length + 1,
                              MYF(MY_ZEROFILL|MY_FAE|MY_WME));
  ptr->length= query_string.length+1;
  if ((key) && 
      (auto_generate_sql_autoincrement || auto_generate_sql_guid_primary))
    ptr->type= SELECT_TYPE_REQUIRES_PREFIX;
  else
    ptr->type= SELECT_TYPE;
  strmov(ptr->string, query_string.str);
  dynstr_free(&query_string);
  DBUG_RETURN(ptr);
}

static int
get_options(int *argc,char ***argv)
{
  int ho_error;
  char *tmp_string;
  MY_STAT sbuf;  /* Stat information for the data file */

  DBUG_ENTER("get_options");
  if ((ho_error= handle_options(argc, argv, my_long_options, get_one_option)))
    exit(ho_error);
  if (debug_info_flag)
    my_end_arg= MY_CHECK_ERROR | MY_GIVE_INFO;
  if (debug_check_flag)
    my_end_arg= MY_CHECK_ERROR;

  if (!user)
    user= (char *)"root";

  /* If something is created we clean it up, otherwise we leave schemas alone */
  if (create_string || auto_generate_sql)
    opt_preserve= FALSE;

  if (auto_generate_sql && (create_string || user_supplied_query))
  {
      fprintf(stderr,
              "%s: Can't use --auto-generate-sql when create and query strings are specified!\n",
              my_progname);
      exit(1);
  }

  if (auto_generate_sql && auto_generate_sql_guid_primary && 
      auto_generate_sql_autoincrement)
  {
      fprintf(stderr,
              "%s: Either auto-generate-sql-guid-primary or auto-generate-sql-add-autoincrement can be used!\n",
              my_progname);
      exit(1);
  }

  /* 
    We are testing to make sure that if someone specified a key search
    that we actually added a key!
  */
  if (auto_generate_sql && auto_generate_sql_type[0] == 'k')
    if ( auto_generate_sql_autoincrement == FALSE &&
         auto_generate_sql_guid_primary == FALSE)
    {
      fprintf(stderr,
              "%s: Can't perform key test without a primary key!\n",
              my_progname);
      exit(1);
    }



  if (auto_generate_sql && num_of_query && auto_actual_queries)
  {
      fprintf(stderr,
              "%s: Either auto-generate-sql-execute-number or number-of-queries can be used!\n",
              my_progname);
      exit(1);
  }

  parse_comma(concurrency_str ? concurrency_str : "1", &concurrency);

  if (opt_csv_str)
  {
    opt_silent= TRUE;
    
    if (opt_csv_str[0] == '-')
    {
      csv_file= fileno(stdout);
    }
    else
    {
      if ((csv_file= my_open(opt_csv_str, O_CREAT|O_WRONLY|O_APPEND, MYF(0)))
          == -1)
      {
        fprintf(stderr,"%s: Could not open csv file: %sn\n",
                my_progname, opt_csv_str);
        exit(1);
      }
    }
  }

  if (opt_only_print)
    opt_silent= TRUE;

  if (num_int_cols_opt)
  {
    option_string *str;
    parse_option(num_int_cols_opt, &str, ',');
    num_int_cols= atoi(str->string);
    if (str->option)
      num_int_cols_index= atoi(str->option);
    option_cleanup(str);
  }

  if (num_char_cols_opt)
  {
    option_string *str;
    parse_option(num_char_cols_opt, &str, ',');
    num_char_cols= atoi(str->string);
    if (str->option)
      num_char_cols_index= atoi(str->option);
    else
      num_char_cols_index= 0;
    option_cleanup(str);
  }


  if (auto_generate_sql)
  {
    unsigned long long x= 0;
    statement *ptr_statement;

    if (verbose >= 2)
      printf("Building Create Statements for Auto\n");

    create_statements= build_table_string();
    /* 
      Pre-populate table 
    */
    for (ptr_statement= create_statements, x= 0; 
         x < auto_generate_sql_unique_write_number; 
         x++, ptr_statement= ptr_statement->next)
    {
      ptr_statement->next= build_insert_string();
    }

    if (verbose >= 2)
      printf("Building Query Statements for Auto\n");

    if (auto_generate_sql_type[0] == 'r')
    {
      if (verbose >= 2)
        printf("Generating SELECT Statements for Auto\n");

      query_statements= build_select_string(FALSE);
      for (ptr_statement= query_statements, x= 0; 
           x < auto_generate_sql_unique_query_number; 
           x++, ptr_statement= ptr_statement->next)
      {
        ptr_statement->next= build_select_string(FALSE);
      }
    }
    else if (auto_generate_sql_type[0] == 'k')
    {
      if (verbose >= 2)
        printf("Generating SELECT for keys Statements for Auto\n");

      query_statements= build_select_string(TRUE);
      for (ptr_statement= query_statements, x= 0; 
           x < auto_generate_sql_unique_query_number; 
           x++, ptr_statement= ptr_statement->next)
      {
        ptr_statement->next= build_select_string(TRUE);
      }
    }
    else if (auto_generate_sql_type[0] == 'w')
    {
      /*
        We generate a number of strings in case the engine is 
        Archive (since strings which were identical one after another
        would be too easily optimized).
      */
      if (verbose >= 2)
        printf("Generating INSERT Statements for Auto\n");
      query_statements= build_insert_string();
      for (ptr_statement= query_statements, x= 0; 
           x < auto_generate_sql_unique_query_number; 
           x++, ptr_statement= ptr_statement->next)
      {
        ptr_statement->next= build_insert_string();
      }
    }
    else if (auto_generate_sql_type[0] == 'u')
    {
      query_statements= build_update_string();
      for (ptr_statement= query_statements, x= 0; 
           x < auto_generate_sql_unique_query_number; 
           x++, ptr_statement= ptr_statement->next)
      {
          ptr_statement->next= build_update_string();
      }
    }
    else /* Mixed mode is default */
    {
      int coin= 0;

      query_statements= build_insert_string();
      /* 
        This logic should be extended to do a more mixed load,
        at the moment it results in "every other".
      */
      for (ptr_statement= query_statements, x= 0; 
           x < auto_generate_sql_unique_query_number; 
           x++, ptr_statement= ptr_statement->next)
      {
        if (coin)
        {
          ptr_statement->next= build_insert_string();
          coin= 0;
        }
        else
        {
          ptr_statement->next= build_select_string(TRUE);
          coin= 1;
        }
      }
    }
  }
  else
  {
    if (create_string && my_stat(create_string, &sbuf, MYF(0)))
    {
      File data_file;
      if (!MY_S_ISREG(sbuf.st_mode))
      {
        fprintf(stderr,"%s: Create file was not a regular file\n",
                my_progname);
        exit(1);
      }
      if ((data_file= my_open(create_string, O_RDWR, MYF(0))) == -1)
      {
        fprintf(stderr,"%s: Could not open create file\n", my_progname);
        exit(1);
      }
      tmp_string= (char *)my_malloc(sbuf.st_size + 1,
                              MYF(MY_ZEROFILL|MY_FAE|MY_WME));
      my_read(data_file, (uchar*) tmp_string, sbuf.st_size, MYF(0));
      tmp_string[sbuf.st_size]= '\0';
      my_close(data_file,MYF(0));
      parse_delimiter(tmp_string, &create_statements, delimiter[0]);
      my_free(tmp_string, MYF(0));
    }
    else if (create_string)
    {
        parse_delimiter(create_string, &create_statements, delimiter[0]);
    }

    if (user_supplied_query && my_stat(user_supplied_query, &sbuf, MYF(0)))
    {
      File data_file;
      if (!MY_S_ISREG(sbuf.st_mode))
      {
        fprintf(stderr,"%s: User query supplied file was not a regular file\n",
                my_progname);
        exit(1);
      }
      if ((data_file= my_open(user_supplied_query, O_RDWR, MYF(0))) == -1)
      {
        fprintf(stderr,"%s: Could not open query supplied file\n", my_progname);
        exit(1);
      }
      tmp_string= (char *)my_malloc(sbuf.st_size + 1,
                                    MYF(MY_ZEROFILL|MY_FAE|MY_WME));
      my_read(data_file, (uchar*) tmp_string, sbuf.st_size, MYF(0));
      tmp_string[sbuf.st_size]= '\0';
      my_close(data_file,MYF(0));
      if (user_supplied_query)
        actual_queries= parse_delimiter(tmp_string, &query_statements,
                                        delimiter[0]);
      my_free(tmp_string, MYF(0));
    } 
    else if (user_supplied_query)
    {
        actual_queries= parse_delimiter(user_supplied_query, &query_statements,
                                        delimiter[0]);
    }
  }

  if (user_supplied_pre_statements && my_stat(user_supplied_pre_statements, &sbuf, MYF(0)))
  {
    File data_file;
    if (!MY_S_ISREG(sbuf.st_mode))
    {
      fprintf(stderr,"%s: User query supplied file was not a regular file\n",
              my_progname);
      exit(1);
    }
    if ((data_file= my_open(user_supplied_pre_statements, O_RDWR, MYF(0))) == -1)
    {
      fprintf(stderr,"%s: Could not open query supplied file\n", my_progname);
      exit(1);
    }
    tmp_string= (char *)my_malloc(sbuf.st_size + 1,
                                  MYF(MY_ZEROFILL|MY_FAE|MY_WME));
    my_read(data_file, (uchar*) tmp_string, sbuf.st_size, MYF(0));
    tmp_string[sbuf.st_size]= '\0';
    my_close(data_file,MYF(0));
    if (user_supplied_pre_statements)
      (void)parse_delimiter(tmp_string, &pre_statements,
                            delimiter[0]);
    my_free(tmp_string, MYF(0));
  } 
  else if (user_supplied_pre_statements)
  {
    (void)parse_delimiter(user_supplied_pre_statements,
                          &pre_statements,
                          delimiter[0]);
  }

  if (user_supplied_post_statements && my_stat(user_supplied_post_statements, &sbuf, MYF(0)))
  {
    File data_file;
    if (!MY_S_ISREG(sbuf.st_mode))
    {
      fprintf(stderr,"%s: User query supplied file was not a regular file\n",
              my_progname);
      exit(1);
    }
    if ((data_file= my_open(user_supplied_post_statements, O_RDWR, MYF(0))) == -1)
    {
      fprintf(stderr,"%s: Could not open query supplied file\n", my_progname);
      exit(1);
    }
    tmp_string= (char *)my_malloc(sbuf.st_size + 1,
                                  MYF(MY_ZEROFILL|MY_FAE|MY_WME));
    my_read(data_file, (uchar*) tmp_string, sbuf.st_size, MYF(0));
    tmp_string[sbuf.st_size]= '\0';
    my_close(data_file,MYF(0));
    if (user_supplied_post_statements)
      (void)parse_delimiter(tmp_string, &post_statements,
                            delimiter[0]);
    my_free(tmp_string, MYF(0));
  } 
  else if (user_supplied_post_statements)
  {
    (void)parse_delimiter(user_supplied_post_statements, &post_statements,
                          delimiter[0]);
  }

  if (verbose >= 2)
    printf("Parsing engines to use.\n");

  if (default_engine)
    parse_option(default_engine, &engine_options, ',');

  if (tty_password)
    opt_password= get_tty_password(NullS);
  DBUG_RETURN(0);
}


static int run_query(MYSQL *mysql, const char *query, int len)
{
  if (opt_only_print)
  {
    printf("%.*s;\n", len, query);
    return 0;
  }

  if (verbose >= 3)
    printf("%.*s;\n", len, query);
  return mysql_real_query(mysql, query, len);
}


static int
generate_primary_key_list(MYSQL *mysql, option_string *engine_stmt)
{
  MYSQL_RES *result;
  MYSQL_ROW row;
  unsigned long long counter;
  DBUG_ENTER("generate_primary_key_list");

  /* 
    Blackhole is a special case, this allows us to test the upper end 
    of the server during load runs.
  */
  if (opt_only_print || (engine_stmt && 
                         strstr(engine_stmt->string, "blackhole")))
  {
    primary_keys_number_of= 1;
    primary_keys= (char **)my_malloc((uint)(sizeof(char *) * 
                                            primary_keys_number_of), 
                                    MYF(MY_ZEROFILL|MY_FAE|MY_WME));
    /* Yes, we strdup a const string to simplify the interface */
    primary_keys[0]= my_strdup("796c4422-1d94-102a-9d6d-00e0812d", MYF(0)); 
  }
  else
  {
    if (run_query(mysql, "SELECT id from t1", strlen("SELECT id from t1")))
    {
      fprintf(stderr,"%s: Cannot select GUID primary keys. (%s)\n", my_progname,
              mysql_error(mysql));
      exit(1);
    }

    result= mysql_store_result(mysql);
    primary_keys_number_of= mysql_num_rows(result);

    /* So why check this? Blackhole :) */
    if (primary_keys_number_of)
    {
      /*
        We create the structure and loop and create the items.
      */
      primary_keys= (char **)my_malloc((uint)(sizeof(char *) * 
                                              primary_keys_number_of), 
                                       MYF(MY_ZEROFILL|MY_FAE|MY_WME));
      row= mysql_fetch_row(result);
      for (counter= 0; counter < primary_keys_number_of; 
           counter++, row= mysql_fetch_row(result))
        primary_keys[counter]= my_strdup(row[0], MYF(0));
    }

    mysql_free_result(result);
  }

  DBUG_RETURN(0);
}

static int
drop_primary_key_list(void)
{
  unsigned long long counter;

  if (primary_keys_number_of)
  {
    for (counter= 0; counter < primary_keys_number_of; counter++)
      my_free(primary_keys[counter], MYF(0));

    my_free(primary_keys, MYF(0));
  }

  return 0;
}

static int
create_schema(MYSQL *mysql, const char *db, statement *stmt, 
              option_string *engine_stmt)
{
  char query[HUGE_STRING_LENGTH];
  statement *ptr;
  statement *after_create;
  int len;
  ulonglong count;
  DBUG_ENTER("create_schema");

  len= snprintf(query, HUGE_STRING_LENGTH, "CREATE SCHEMA `%s`", db);

  if (verbose >= 2)
    printf("Loading Pre-data\n");

  if (run_query(mysql, query, len))
  {
    fprintf(stderr,"%s: Cannot create schema %s : %s\n", my_progname, db,
            mysql_error(mysql));
    exit(1);
  }

  if (opt_only_print)
  {
    printf("use %s;\n", db);
  }
  else
  {
    if (verbose >= 3)
      printf("%s;\n", query);

    if (mysql_select_db(mysql,  db))
    {
      fprintf(stderr,"%s: Cannot select schema '%s': %s\n",my_progname, db,
              mysql_error(mysql));
      exit(1);
    }
  }

  if (engine_stmt)
  {
    len= snprintf(query, HUGE_STRING_LENGTH, "set storage_engine=`%s`",
                  engine_stmt->string);
    if (run_query(mysql, query, len))
    {
      fprintf(stderr,"%s: Cannot set default engine: %s\n", my_progname,
              mysql_error(mysql));
      exit(1);
    }
  }

  count= 0;
  after_create= stmt;

limit_not_met:
  for (ptr= after_create; ptr && ptr->length; ptr= ptr->next, count++)
  {
    if (auto_generate_sql && ( auto_generate_sql_number == count))
      break;

    if (engine_stmt && engine_stmt->option && ptr->type == CREATE_TABLE_TYPE)
    {
      char buffer[HUGE_STRING_LENGTH];

      snprintf(buffer, HUGE_STRING_LENGTH, "%s %s", ptr->string, 
               engine_stmt->option);
      if (run_query(mysql, buffer, strlen(buffer)))
      {
        fprintf(stderr,"%s: Cannot run query %.*s ERROR : %s\n",
                my_progname, (uint)ptr->length, ptr->string, mysql_error(mysql));
        exit(1);
      }
    }
    else
    {
      if (run_query(mysql, ptr->string, ptr->length))
      {
        fprintf(stderr,"%s: Cannot run query %.*s ERROR : %s\n",
                my_progname, (uint)ptr->length, ptr->string, mysql_error(mysql));
        exit(1);
      }
    }
  }

  if (auto_generate_sql && (auto_generate_sql_number > count ))
  {
    /* Special case for auto create, we don't want to create tables twice */
    after_create= stmt->next;
    goto limit_not_met;
  }

  DBUG_RETURN(0);
}

static int
drop_schema(MYSQL *mysql, const char *db)
{
  char query[HUGE_STRING_LENGTH];
  int len;
  DBUG_ENTER("drop_schema");
  len= snprintf(query, HUGE_STRING_LENGTH, "DROP SCHEMA IF EXISTS `%s`", db);

  if (run_query(mysql, query, len))
  {
    fprintf(stderr,"%s: Cannot drop database '%s' ERROR : %s\n",
            my_progname, db, mysql_error(mysql));
    exit(1);
  }



  DBUG_RETURN(0);
}

static int
run_statements(MYSQL *mysql, statement *stmt) 
{
  statement *ptr;
  MYSQL_RES *result;
  DBUG_ENTER("run_statements");

  for (ptr= stmt; ptr && ptr->length; ptr= ptr->next)
  {
    if (run_query(mysql, ptr->string, ptr->length))
    {
      fprintf(stderr,"%s: Cannot run query %.*s ERROR : %s\n",
              my_progname, (uint)ptr->length, ptr->string, mysql_error(mysql));
      exit(1);
    }
    if (mysql_field_count(mysql))
    {
      result= mysql_store_result(mysql);
      mysql_free_result(result);
    }
  }

  DBUG_RETURN(0);
}

static int
run_scheduler(stats *sptr, statement *stmts, uint concur, ulonglong limit)
{
  uint x;
  struct timeval start_time, end_time;
  thread_context con;
  pthread_t mainthread;            /* Thread descriptor */
  pthread_attr_t attr;          /* Thread attributes */
  DBUG_ENTER("run_scheduler");

  con.stmt= stmts;
  con.limit= limit;

  pthread_attr_init(&attr);
  pthread_attr_setdetachstate(&attr,
		  PTHREAD_CREATE_DETACHED);

  pthread_mutex_lock(&counter_mutex);
  thread_counter= 0;

  pthread_mutex_lock(&sleeper_mutex);
  master_wakeup= 1;
  pthread_mutex_unlock(&sleeper_mutex);
  for (x= 0; x < concur; x++)
  {
    /* now you create the thread */
    if (pthread_create(&mainthread, &attr, run_task, 
                       (void *)&con) != 0)
    {
      fprintf(stderr,"%s: Could not create thread\n",
              my_progname);
      exit(0);
    }
    thread_counter++;
  }
  pthread_mutex_unlock(&counter_mutex);
  pthread_attr_destroy(&attr);

  pthread_mutex_lock(&sleeper_mutex);
  master_wakeup= 0;
  pthread_mutex_unlock(&sleeper_mutex);
  pthread_cond_broadcast(&sleep_threshhold);

  gettimeofday(&start_time, NULL);

  /*
    We loop until we know that all children have cleaned up.
  */
  pthread_mutex_lock(&counter_mutex);
  while (thread_counter)
  {
    struct timespec abstime;

    set_timespec(abstime, 3);
    pthread_cond_timedwait(&count_threshhold, &counter_mutex, &abstime);
  }
  pthread_mutex_unlock(&counter_mutex);

  gettimeofday(&end_time, NULL);


  sptr->timing= timedif(end_time, start_time);
  sptr->users= concur;
  sptr->rows= limit;

  DBUG_RETURN(0);
}


pthread_handler_t run_task(void *p)
{
  ulonglong counter= 0, queries;
  ulonglong detach_counter;
  unsigned int commit_counter;
  MYSQL *mysql;
  MYSQL_RES *result;
  MYSQL_ROW row;
  statement *ptr;
  thread_context *con= (thread_context *)p;

  DBUG_ENTER("run_task");
  DBUG_PRINT("info", ("task script \"%s\"", con->stmt ? con->stmt->string : ""));

  pthread_mutex_lock(&sleeper_mutex);
  while (master_wakeup)
  {
    pthread_cond_wait(&sleep_threshhold, &sleeper_mutex);
  }
  pthread_mutex_unlock(&sleeper_mutex);

  if (!(mysql= mysql_init(NULL)))
  {
    fprintf(stderr,"%s: mysql_init() failed ERROR : %s\n",
            my_progname, mysql_error(mysql));
    exit(0);
  }

  if (mysql_thread_init())
  {
    fprintf(stderr,"%s: mysql_thread_init() failed ERROR : %s\n",
            my_progname, mysql_error(mysql));
    exit(0);
  }

  DBUG_PRINT("info", ("trying to connect to host %s as user %s", host, user));

  if (!opt_only_print)
  {
    if (slap_connect(mysql))
      goto end;
  }

  DBUG_PRINT("info", ("connected."));
  if (verbose >= 3)
    printf("connected!\n");
  queries= 0;

  commit_counter= 0;
  if (commit_rate)
    run_query(mysql, "SET AUTOCOMMIT=0", strlen("SET AUTOCOMMIT=0"));

limit_not_met:
    for (ptr= con->stmt, detach_counter= 0; 
         ptr && ptr->length; 
         ptr= ptr->next, detach_counter++)
    {
      if (!opt_only_print && detach_rate && !(detach_counter % detach_rate))
      {
        mysql_close(mysql);

        if (!(mysql= mysql_init(NULL)))
        {
          fprintf(stderr,"%s: mysql_init() failed ERROR : %s\n",
                  my_progname, mysql_error(mysql));
          exit(0);
        }

        if (slap_connect(mysql))
          goto end;
      }

      /* 
        We have to execute differently based on query type. This should become a function.
      */
      if ((ptr->type == UPDATE_TYPE_REQUIRES_PREFIX) ||
          (ptr->type == SELECT_TYPE_REQUIRES_PREFIX))
      {
        int length;
        unsigned int key_val;
        char *key;
        char buffer[HUGE_STRING_LENGTH];

        /* 
          This should only happen if some sort of new engine was
          implemented that didn't properly handle UPDATEs.

          Just in case someone runs this under an experimental engine we don't
          want a crash so the if() is placed here.
        */
        DBUG_ASSERT(primary_keys_number_of);
        if (primary_keys_number_of)
        {
          key_val= (unsigned int)(random() % primary_keys_number_of);
          key= primary_keys[key_val];

          DBUG_ASSERT(key);

          length= snprintf(buffer, HUGE_STRING_LENGTH, "%.*s '%s'", 
                           (int)ptr->length, ptr->string, key);

          if (run_query(mysql, buffer, length))
          {
            fprintf(stderr,"%s: Cannot run query %.*s ERROR : %s\n",
                    my_progname, (uint)length, buffer, mysql_error(mysql));
            exit(0);
          }
        }
      }
      else
      {
        if (run_query(mysql, ptr->string, ptr->length))
        {
          fprintf(stderr,"%s: Cannot run query %.*s ERROR : %s\n",
                  my_progname, (uint)ptr->length, ptr->string, mysql_error(mysql));
          exit(0);
        }
      }

      do
      {
        if (mysql_field_count(mysql))
        {
          result= mysql_store_result(mysql);
          while ((row = mysql_fetch_row(result)))
            counter++;
          mysql_free_result(result);
        }
      } while(mysql_next_result(mysql) == 0);
      queries++;

      if (commit_rate && (++commit_counter == commit_rate))
      {
        commit_counter= 0;
        run_query(mysql, "COMMIT", strlen("COMMIT"));
      }

      if (con->limit && queries == con->limit)
        goto end;
    }

    if (con->limit && queries < con->limit)
      goto limit_not_met;

end:
  if (commit_rate)
    run_query(mysql, "COMMIT", strlen("COMMIT"));

  if (!opt_only_print) 
    mysql_close(mysql);

  my_thread_end();

  pthread_mutex_lock(&counter_mutex);
  thread_counter--;
  pthread_cond_signal(&count_threshhold);
  pthread_mutex_unlock(&counter_mutex);

  DBUG_RETURN(0);
}

uint
parse_option(const char *origin, option_string **stmt, char delm)
{
  char *retstr;
  char *ptr= (char *)origin;
  option_string **sptr= stmt;
  option_string *tmp;
  size_t length= strlen(origin);
  uint count= 0; /* We know that there is always one */

  for (tmp= *sptr= (option_string *)my_malloc(sizeof(option_string),
                                          MYF(MY_ZEROFILL|MY_FAE|MY_WME));
       (retstr= strchr(ptr, delm)); 
       tmp->next=  (option_string *)my_malloc(sizeof(option_string),
                                          MYF(MY_ZEROFILL|MY_FAE|MY_WME)),
       tmp= tmp->next)
  {
    char buffer[HUGE_STRING_LENGTH];
    char *buffer_ptr;

    count++;
    strncpy(buffer, ptr, (size_t)(retstr - ptr));
    if ((buffer_ptr= strchr(buffer, ':')))
    {
      char *option_ptr;

      tmp->length= (size_t)(buffer_ptr - buffer);
      tmp->string= my_strndup(ptr, (uint)tmp->length, MYF(MY_FAE));

      option_ptr= ptr + 1 + tmp->length;

      /* Move past the : and the first string */
      tmp->option_length= (size_t)(retstr - option_ptr);
      tmp->option= my_strndup(option_ptr, (uint)tmp->option_length,
                              MYF(MY_FAE));
    }
    else
    {
      tmp->string= my_strndup(ptr, (size_t)(retstr - ptr), MYF(MY_FAE));
      tmp->length= (size_t)(retstr - ptr);
    }

    ptr+= retstr - ptr + 1;
    if (isspace(*ptr))
      ptr++;
    count++;
  }

  if (ptr != origin+length)
  {
    char *origin_ptr;

    if ((origin_ptr= strchr(ptr, ':')))
    {
      char *option_ptr;

      tmp->length= (size_t)(origin_ptr - ptr);
      tmp->string= my_strndup(origin, tmp->length, MYF(MY_FAE));

      option_ptr= (char *)ptr + 1 + tmp->length;

      /* Move past the : and the first string */
      tmp->option_length= (size_t)((ptr + length) - option_ptr);
      tmp->option= my_strndup(option_ptr, tmp->option_length,
                              MYF(MY_FAE));
    }
    else
    {
      tmp->length= (size_t)((ptr + length) - ptr);
      tmp->string= my_strndup(ptr, tmp->length, MYF(MY_FAE));
    }

    count++;
  }

  return count;
}


uint
parse_delimiter(const char *script, statement **stmt, char delm)
{
  char *retstr;
  char *ptr= (char *)script;
  statement **sptr= stmt;
  statement *tmp;
  uint length= strlen(script);
  uint count= 0; /* We know that there is always one */

  for (tmp= *sptr= (statement *)my_malloc(sizeof(statement),
                                          MYF(MY_ZEROFILL|MY_FAE|MY_WME));
       (retstr= strchr(ptr, delm)); 
       tmp->next=  (statement *)my_malloc(sizeof(statement),
                                          MYF(MY_ZEROFILL|MY_FAE|MY_WME)),
       tmp= tmp->next)
  {
    count++;
    tmp->string= my_strndup(ptr, (uint)(retstr - ptr), MYF(MY_FAE));
    tmp->length= (size_t)(retstr - ptr);
    ptr+= retstr - ptr + 1;
    if (isspace(*ptr))
      ptr++;
  }

  if (ptr != script+length)
  {
    tmp->string= my_strndup(ptr, (uint)((script + length) - ptr), 
                                       MYF(MY_FAE));
    tmp->length= (size_t)((script + length) - ptr);
    count++;
  }

  return count;
}


uint
parse_comma(const char *string, uint **range)
{
  uint count= 1,x; /* We know that there is always one */
  char *retstr;
  char *ptr= (char *)string;
  uint *nptr;

  for (;*ptr; ptr++)
    if (*ptr == ',') count++;
  
  /* One extra spot for the NULL */
  nptr= *range= (uint *)my_malloc(sizeof(uint) * (count + 1), 
                                  MYF(MY_ZEROFILL|MY_FAE|MY_WME));

  ptr= (char *)string;
  x= 0;
  while ((retstr= strchr(ptr,',')))
  {
    nptr[x++]= atoi(ptr);
    ptr+= retstr - ptr + 1;
  }
  nptr[x++]= atoi(ptr);

  return count;
}

void
print_conclusions(conclusions *con)
{
  printf("Benchmark\n");
  if (con->engine)
    printf("\tRunning for engine %s\n", con->engine);
  printf("\tAverage number of seconds to run all queries: %ld.%03ld seconds\n",
                    con->avg_timing / 1000, con->avg_timing % 1000);
  printf("\tMinimum number of seconds to run all queries: %ld.%03ld seconds\n",
                    con->min_timing / 1000, con->min_timing % 1000);
  printf("\tMaximum number of seconds to run all queries: %ld.%03ld seconds\n",
                    con->max_timing / 1000, con->max_timing % 1000);
  printf("\tNumber of clients running queries: %d\n", con->users);
  printf("\tAverage number of queries per client: %llu\n", con->avg_rows); 
  printf("\n");
}

void
print_conclusions_csv(conclusions *con)
{
  char buffer[HUGE_STRING_LENGTH];
  const char *ptr= auto_generate_sql_type ? auto_generate_sql_type : "query";
  snprintf(buffer, HUGE_STRING_LENGTH, 
           "%s,%s,%ld.%03ld,%ld.%03ld,%ld.%03ld,%d,%llu\n",
           con->engine ? con->engine : "", /* Storage engine we ran against */
           ptr, /* Load type */
           con->avg_timing / 1000, con->avg_timing % 1000, /* Time to load */
           con->min_timing / 1000, con->min_timing % 1000, /* Min time */
           con->max_timing / 1000, con->max_timing % 1000, /* Max time */
           con->users, /* Children used */
           con->avg_rows  /* Queries run */
          );
  my_write(csv_file, (uchar*) buffer, (uint)strlen(buffer), MYF(0));
}

void
generate_stats(conclusions *con, option_string *eng, stats *sptr)
{
  stats *ptr;
  unsigned int x;

  con->min_timing= sptr->timing; 
  con->max_timing= sptr->timing;
  con->min_rows= sptr->rows;
  con->max_rows= sptr->rows;
  
  /* At the moment we assume uniform */
  con->users= sptr->users;
  con->avg_rows= sptr->rows;
  
  /* With no next, we know it is the last element that was malloced */
  for (ptr= sptr, x= 0; x < iterations; ptr++, x++)
  {
    con->avg_timing+= ptr->timing;

    if (ptr->timing > con->max_timing)
      con->max_timing= ptr->timing;
    if (ptr->timing < con->min_timing)
      con->min_timing= ptr->timing;
  }
  con->avg_timing= con->avg_timing/iterations;

  if (eng && eng->string)
    con->engine= eng->string;
  else
    con->engine= NULL;
}

void
option_cleanup(option_string *stmt)
{
  option_string *ptr, *nptr;
  if (!stmt)
    return;

  for (ptr= stmt; ptr; ptr= nptr)
  {
    nptr= ptr->next;
    if (ptr->string)
      my_free(ptr->string, MYF(0)); 
    if (ptr->option)
      my_free(ptr->option, MYF(0)); 
    my_free(ptr, MYF(0));
  }
}

void
statement_cleanup(statement *stmt)
{
  statement *ptr, *nptr;
  if (!stmt)
    return;

  for (ptr= stmt; ptr; ptr= nptr)
  {
    nptr= ptr->next;
    if (ptr->string)
      my_free(ptr->string, MYF(0)); 
    my_free(ptr, MYF(0));
  }
}


int 
slap_connect(MYSQL *mysql)
{
  /* Connect to server */
  static ulong connection_retry_sleep= 100000; /* Microseconds */
  int x, connect_error= 1;
  for (x= 0; x < 10; x++)
  {
    if (mysql_real_connect(mysql, host, user, opt_password,
                           create_schema_string,
                           opt_mysql_port,
                           opt_mysql_unix_port,
                           connect_flags))
    {
      /* Connect suceeded */
      connect_error= 0;
      break;
    }
    my_sleep(connection_retry_sleep);
  }
  if (connect_error)
  {
    fprintf(stderr,"%s: Error when connecting to server: %d %s\n",
            my_progname, mysql_errno(mysql), mysql_error(mysql));
    return 1;
  }

  return 0;
}
