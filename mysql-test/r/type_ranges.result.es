drop table if exists t1,t2,t3;
SET SQL_WARNINGS=1;
CREATE TABLE t1 (
auto int(5) unsigned NOT NULL auto_increment,
string char(10) default "hello",
tiny tinyint(4) DEFAULT '0' NOT NULL ,
short smallint(6) DEFAULT '1' NOT NULL ,
medium mediumint(8) DEFAULT '0' NOT NULL,
long_int int(11) DEFAULT '0' NOT NULL,
longlong bigint(13) DEFAULT '0' NOT NULL,
real_float float(13,1) DEFAULT 0.0 NOT NULL,
real_double double(16,4),
utiny tinyint(3) unsigned DEFAULT '0' NOT NULL,
ushort smallint(5) unsigned zerofill DEFAULT '00000' NOT NULL,
umedium mediumint(8) unsigned DEFAULT '0' NOT NULL,
ulong int(11) unsigned DEFAULT '0' NOT NULL,
ulonglong bigint(13) unsigned DEFAULT '0' NOT NULL,
time_stamp timestamp,
date_field date,	
time_field time,	
date_time datetime,
blob_col blob,
tinyblob_col tinyblob,
mediumblob_col mediumblob  not null,
longblob_col longblob  not null,
options enum('one','two','tree') not null,
flags set('one','two','tree') not null,
PRIMARY KEY (auto),
KEY (utiny),
KEY (tiny),
KEY (short),
KEY any_name (medium),
KEY (longlong),
KEY (real_float),
KEY (ushort),
KEY (umedium),
KEY (ulong),
KEY (ulonglong,ulong),
KEY (options,flags)
);
show full fields from t1;
Field	Type	Collation	Null	Key	Default	Extra	Privileges	Comment
auto	int(5) unsigned	NULL		PRI	NULL	auto_increment		
string	varchar(10)	latin1_swedish_ci	YES		hello			
tiny	tinyint(4)	NULL		MUL	0			
short	smallint(6)	NULL		MUL	1			
medium	mediumint(8)	NULL		MUL	0			
long_int	int(11)	NULL			0			
longlong	bigint(13)	NULL		MUL	0			
real_float	float(13,1)	NULL		MUL	0.0			
real_double	double(16,4)	NULL	YES		NULL			
utiny	tinyint(3) unsigned	NULL		MUL	0			
ushort	smallint(5) unsigned zerofill	NULL		MUL	00000			
umedium	mediumint(8) unsigned	NULL		MUL	0			
ulong	int(11) unsigned	NULL		MUL	0			
ulonglong	bigint(13) unsigned	NULL		MUL	0			
time_stamp	timestamp	NULL	YES		CURRENT_TIMESTAMP			
date_field	date	NULL	YES		NULL			
time_field	time	NULL	YES		NULL			
date_time	datetime	NULL	YES		NULL			
blob_col	blob	NULL	YES		NULL			
tinyblob_col	tinyblob	NULL	YES		NULL			
mediumblob_col	mediumblob	NULL						
longblob_col	longblob	NULL						
options	enum('one','two','tree')	latin1_swedish_ci		MUL	one			
flags	set('one','two','tree')	latin1_swedish_ci						
show keys from t1;
Table	Non_unique	Key_name	Seq_in_index	Column_name	Collation	Cardinality	Sub_part	Packed	Null	Index_type	Comment
t1	0	PRIMARY	1	auto	A	0	NULL	NULL		BTREE	
t1	1	utiny	1	utiny	A	NULL	NULL	NULL		BTREE	
t1	1	tiny	1	tiny	A	NULL	NULL	NULL		BTREE	
t1	1	short	1	short	A	NULL	NULL	NULL		BTREE	
t1	1	any_name	1	medium	A	NULL	NULL	NULL		BTREE	
t1	1	longlong	1	longlong	A	NULL	NULL	NULL		BTREE	
t1	1	real_float	1	real_float	A	NULL	NULL	NULL		BTREE	
t1	1	ushort	1	ushort	A	NULL	NULL	NULL		BTREE	
t1	1	umedium	1	umedium	A	NULL	NULL	NULL		BTREE	
t1	1	ulong	1	ulong	A	NULL	NULL	NULL		BTREE	
t1	1	ulonglong	1	ulonglong	A	NULL	NULL	NULL		BTREE	
t1	1	ulonglong	2	ulong	A	NULL	NULL	NULL		BTREE	
t1	1	options	1	options	A	NULL	NULL	NULL		BTREE	
t1	1	options	2	flags	A	NULL	NULL	NULL		BTREE	
CREATE UNIQUE INDEX test on t1 ( auto ) ;
CREATE INDEX test2 on t1 ( ulonglong,ulong) ;
CREATE INDEX test3 on t1 ( medium ) ;
DROP INDEX test ON t1;
insert into t1 values (10, 1,1,1,1,1,1,1,1,1,1,1,1,1,NULL,0,0,0,1,1,1,1,'one','one');
insert into t1 values (NULL,2,2,2,2,2,2,2,2,2,2,2,2,2,NULL,NULL,NULL,NULL,NULL,NULL,2,2,'two','two,one');
insert into t1 values (0,1/3,3,3,3,3,3,3,3,3,3,3,3,3,NULL,'19970303','10:10:10','19970303101010','','','','3',3,3);
insert into t1 values (0,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,NULL,19970807,080706,19970403090807,-1,-1,-1,'-1',-1,-1);
Warnings:
Warning	1264	Data truncated; out of range for column 'utiny' at row 1
Warning	1264	Data truncated; out of range for column 'ushort' at row 1
Warning	1264	Data truncated; out of range for column 'umedium' at row 1
Warning	1264	Data truncated; out of range for column 'ulong' at row 1
Warning	1265	Data truncated for column 'options' at row 1
Warning	1265	Data truncated for column 'flags' at row 1
insert into t1 values (0,-4294967295,-4294967295,-4294967295,-4294967295,-4294967295,-4294967295,-4294967295,-4294967295,-4294967295,-4294967295,-4294967295,-4294967295,-4294967295,NULL,0,0,0,-4294967295,-4294967295,-4294967295,'-4294967295',0,"one,two,tree");
Warnings:
Warning	1265	Data truncated for column 'string' at row 1
Warning	1264	Data truncated; out of range for column 'tiny' at row 1
Warning	1264	Data truncated; out of range for column 'short' at row 1
Warning	1264	Data truncated; out of range for column 'medium' at row 1
Warning	1264	Data truncated; out of range for column 'long_int' at row 1
Warning	1264	Data truncated; out of range for column 'utiny' at row 1
Warning	1264	Data truncated; out of range for column 'ushort' at row 1
Warning	1264	Data truncated; out of range for column 'umedium' at row 1
Warning	1264	Data truncated; out of range for column 'ulong' at row 1
Warning	1265	Data truncated for column 'options' at row 1
insert into t1 values (0,4294967295,4294967295,4294967295,4294967295,4294967295,4294967295,4294967295,4294967295,4294967295,4294967295,4294967295,4294967295,4294967295,NULL,0,0,0,4294967295,4294967295,4294967295,'4294967295',0,0);
Warnings:
Warning	1264	Data truncated; out of range for column 'tiny' at row 1
Warning	1264	Data truncated; out of range for column 'short' at row 1
Warning	1264	Data truncated; out of range for column 'medium' at row 1
Warning	1264	Data truncated; out of range for column 'long_int' at row 1
Warning	1264	Data truncated; out of range for column 'utiny' at row 1
Warning	1264	Data truncated; out of range for column 'ushort' at row 1
Warning	1264	Data truncated; out of range for column 'umedium' at row 1
Warning	1265	Data truncated for column 'options' at row 1
insert into t1 (tiny) values (1);
select auto,string,tiny,short,medium,long_int,longlong,real_float,real_double,utiny,ushort,umedium,ulong,ulonglong,mod(floor(time_stamp/1000000),1000000)-mod(curdate(),1000000),date_field,time_field,date_time,blob_col,tinyblob_col,mediumblob_col,longblob_col from t1;
auto	string	tiny	short	medium	long_int	longlong	real_float	real_double	utiny	ushort	umedium	ulong	ulonglong	mod(floor(time_stamp/1000000),1000000)-mod(curdate(),1000000)	date_field	time_field	date_time	blob_col	tinyblob_col	mediumblob_col	longblob_col
10	1	1	1	1	1	1	1.0	1.0000	1	00001	1	1	1	0	0000-00-00	00:00:00	0000-00-00 00:00:00	1	1	1	1
11	2	2	2	2	2	2	2.0	2.0000	2	00002	2	2	2	0	NULL	NULL	NULL	NULL	NULL	2	2
12	0.33	3	3	3	3	3	3.0	3.0000	3	00003	3	3	3	0	1997-03-03	10:10:10	1997-03-03 10:10:10				3
13	-1	-1	-1	-1	-1	-1	-1.0	-1.0000	0	00000	0	0	18446744073709551615	0	1997-08-07	08:07:06	1997-04-03 09:08:07	-1	-1	-1	-1
14	-429496729	-128	-32768	-8388608	-2147483648	-4294967295	-4294967296.0	-4294967295.0000	0	00000	0	0	18446744069414584321	0	0000-00-00	00:00:00	0000-00-00 00:00:00	-4294967295	-4294967295	-4294967295	-4294967295
15	4294967295	127	32767	8388607	2147483647	4294967295	4294967296.0	4294967295.0000	255	65535	16777215	4294967295	4294967295	0	0000-00-00	00:00:00	0000-00-00 00:00:00	4294967295	4294967295	4294967295	4294967295
16	hello	1	1	0	0	0	0.0	NULL	0	00000	0	0	0	0	NULL	NULL	NULL	NULL	NULL		
ALTER TABLE t1
add new_field char(10) default "new" not null,
change blob_col new_blob_col varchar(20),
change date_field date_field char(10),
alter column string set default "new default",
alter short drop default,
DROP INDEX utiny,
DROP INDEX ushort,
DROP PRIMARY KEY,
DROP FOREIGN KEY any_name,
ADD INDEX (auto);
LOCK TABLES t1 WRITE;
ALTER TABLE t1 
RENAME as t2,
DROP longblob_col;
UNLOCK TABLES;
ALTER TABLE t2 rename as t3;
LOCK TABLES t3 WRITE ;
ALTER TABLE t3 rename as t1;
UNLOCK TABLES;
select auto,new_field,new_blob_col,date_field from t1 ;
auto	new_field	new_blob_col	date_field
10	new	1	0000-00-00
11	new	NULL	NULL
12	new		1997-03-03
13	new	-1	1997-08-07
14	new	-4294967295	0000-00-00
15	new	4294967295	0000-00-00
16	new	NULL	NULL
CREATE TABLE t2 (
auto int(5) unsigned NOT NULL auto_increment,
string char(20),
mediumblob_col mediumblob not null,
new_field char(2),
PRIMARY KEY (auto)
);
INSERT INTO t2 (string,mediumblob_col,new_field) SELECT string,mediumblob_col,new_field from t1 where auto > 10;
Warnings:
Warning	1265	Data truncated for column 'new_field' at row 2
Warning	1265	Data truncated for column 'new_field' at row 3
Warning	1265	Data truncated for column 'new_field' at row 4
Warning	1265	Data truncated for column 'new_field' at row 5
Warning	1265	Data truncated for column 'new_field' at row 6
Warning	1265	Data truncated for column 'new_field' at row 7
select * from t2;
auto	string	mediumblob_col	new_field
1	2	2	ne
2	0.33		ne
3	-1	-1	ne
4	-429496729	-4294967295	ne
5	4294967295	4294967295	ne
6	hello		ne
select distinct flags from t1;
flags

one,two,tree
one
one,two
select flags from t1 where find_in_set("two",flags)>0;
flags
one,two,tree
one,two,tree
one,two
one,two
select flags from t1 where find_in_set("unknown",flags)>0;
flags
select options,flags from t1 where options="ONE" and flags="ONE";
options	flags
one	one
select options,flags from t1 where options="one" and flags="one";
options	flags
one	one
drop table t2;
create table t2 select * from t1;
Warnings:
Warning	1265	Data truncated for column 'options' at row 4
Warning	1265	Data truncated for column 'options' at row 5
Warning	1265	Data truncated for column 'options' at row 6
update t2 set string="changed" where auto=16;
show full columns from t1;
Field	Type	Collation	Null	Key	Default	Extra	Privileges	Comment
auto	int(5) unsigned	NULL		MUL	NULL	auto_increment		
string	varchar(10)	latin1_swedish_ci	YES		new defaul			
tiny	tinyint(4)	NULL		MUL	0			
short	smallint(6)	NULL		MUL	0			
medium	mediumint(8)	NULL		MUL	0			
long_int	int(11)	NULL			0			
longlong	bigint(13)	NULL		MUL	0			
real_float	float(13,1)	NULL		MUL	0.0			
real_double	double(16,4)	NULL	YES		NULL			
utiny	tinyint(3) unsigned	NULL			0			
ushort	smallint(5) unsigned zerofill	NULL			00000			
umedium	mediumint(8) unsigned	NULL		MUL	0			
ulong	int(11) unsigned	NULL		MUL	0			
ulonglong	bigint(13) unsigned	NULL		MUL	0			
time_stamp	timestamp	NULL	YES		CURRENT_TIMESTAMP			
date_field	varchar(10)	latin1_swedish_ci	YES		NULL			
time_field	time	NULL	YES		NULL			
date_time	datetime	NULL	YES		NULL			
new_blob_col	varchar(20)	latin1_swedish_ci	YES		NULL			
tinyblob_col	tinyblob	NULL	YES		NULL			
mediumblob_col	mediumblob	NULL						
options	enum('one','two','tree')	latin1_swedish_ci		MUL	one			
flags	set('one','two','tree')	latin1_swedish_ci						
new_field	varchar(10)	latin1_swedish_ci			new			
show full columns from t2;
Field	Type	Collation	Null	Key	Default	Extra	Privileges	Comment
auto	int(5) unsigned	NULL			0			
string	varchar(10)	latin1_swedish_ci	YES		new defaul			
tiny	tinyint(4)	NULL			0			
short	smallint(6)	NULL			0			
medium	mediumint(8)	NULL			0			
long_int	int(11)	NULL			0			
longlong	bigint(13)	NULL			0			
real_float	float(13,1)	NULL			0.0			
real_double	double(16,4)	NULL	YES		NULL			
utiny	tinyint(3) unsigned	NULL			0			
ushort	smallint(5) unsigned zerofill	NULL			00000			
umedium	mediumint(8) unsigned	NULL			0			
ulong	int(11) unsigned	NULL			0			
ulonglong	bigint(13) unsigned	NULL			0			
time_stamp	timestamp	NULL	YES		0000-00-00 00:00:00			
date_field	varchar(10)	latin1_swedish_ci	YES		NULL			
time_field	time	NULL	YES		NULL			
date_time	datetime	NULL	YES		NULL			
new_blob_col	varchar(20)	latin1_swedish_ci	YES		NULL			
tinyblob_col	tinyblob	NULL	YES		NULL			
mediumblob_col	mediumblob	NULL						
options	enum('one','two','tree')	latin1_swedish_ci			one			
flags	set('one','two','tree')	latin1_swedish_ci						
new_field	varchar(10)	latin1_swedish_ci			new			
select t1.auto,t2.auto from t1,t2 where t1.auto=t2.auto and ((t1.string<>t2.string and (t1.string is not null or t2.string is not null)) or (t1.tiny<>t2.tiny and (t1.tiny is not null or t2.tiny is not null)) or (t1.short<>t2.short and (t1.short is not null or t2.short is not null)) or (t1.medium<>t2.medium and (t1.medium is not null or t2.medium is not null)) or (t1.long_int<>t2.long_int and (t1.long_int is not null or t2.long_int is not null)) or (t1.longlong<>t2.longlong and (t1.longlong is not null or t2.longlong is not null)) or (t1.real_float<>t2.real_float and (t1.real_float is not null or t2.real_float is not null)) or (t1.real_double<>t2.real_double and (t1.real_double is not null or t2.real_double is not null)) or (t1.utiny<>t2.utiny and (t1.utiny is not null or t2.utiny is not null)) or (t1.ushort<>t2.ushort and (t1.ushort is not null or t2.ushort is not null)) or (t1.umedium<>t2.umedium and (t1.umedium is not null or t2.umedium is not null)) or (t1.ulong<>t2.ulong and (t1.ulong is not null or t2.ulong is not null)) or (t1.ulonglong<>t2.ulonglong and (t1.ulonglong is not null or t2.ulonglong is not null)) or (t1.time_stamp<>t2.time_stamp and (t1.time_stamp is not null or t2.time_stamp is not null)) or (t1.date_field<>t2.date_field and (t1.date_field is not null or t2.date_field is not null)) or (t1.time_field<>t2.time_field and (t1.time_field is not null or t2.time_field is not null)) or (t1.date_time<>t2.date_time and (t1.date_time is not null or t2.date_time is not null)) or (t1.new_blob_col<>t2.new_blob_col and (t1.new_blob_col is not null or t2.new_blob_col is not null)) or (t1.tinyblob_col<>t2.tinyblob_col and (t1.tinyblob_col is not null or t2.tinyblob_col is not null)) or (t1.mediumblob_col<>t2.mediumblob_col and (t1.mediumblob_col is not null or t2.mediumblob_col is not null)) or (t1.options<>t2.options and (t1.options is not null or t2.options is not null)) or (t1.flags<>t2.flags and (t1.flags is not null or t2.flags is not null)) or (t1.new_field<>t2.new_field and (t1.new_field is not null or t2.new_field is not null)));
auto	auto
16	16
select t1.auto,t2.auto from t1,t2 where t1.auto=t2.auto and not (t1.string<=>t2.string and t1.tiny<=>t2.tiny and t1.short<=>t2.short and t1.medium<=>t2.medium and t1.long_int<=>t2.long_int and t1.longlong<=>t2.longlong and t1.real_float<=>t2.real_float and t1.real_double<=>t2.real_double and t1.utiny<=>t2.utiny and t1.ushort<=>t2.ushort and t1.umedium<=>t2.umedium and t1.ulong<=>t2.ulong and t1.ulonglong<=>t2.ulonglong and t1.time_stamp<=>t2.time_stamp and t1.date_field<=>t2.date_field and t1.time_field<=>t2.time_field and t1.date_time<=>t2.date_time and t1.new_blob_col<=>t2.new_blob_col and t1.tinyblob_col<=>t2.tinyblob_col and t1.mediumblob_col<=>t2.mediumblob_col and t1.options<=>t2.options and t1.flags<=>t2.flags and t1.new_field<=>t2.new_field);
auto	auto
16	16
drop table t2;
create table t2 (primary key (auto)) select auto+1 as auto,1 as t1, "a" as t2, repeat("a",256) as t3, binary repeat("b",256) as t4 from t1;
show full columns from t2;
Field	Type	Collation	Null	Key	Default	Extra	Privileges	Comment
auto	bigint(17) unsigned	NULL		PRI	0			
t1	bigint(1)	NULL			0			
t2	char(1)	latin1_swedish_ci						
t3	longtext	latin1_swedish_ci						
t4	longblob	NULL						
select * from t2;
auto	t1	t2	t3	t4
11	1	a	aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa	bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb
12	1	a	aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa	bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb
13	1	a	aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa	bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb
14	1	a	aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa	bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb
15	1	a	aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa	bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb
16	1	a	aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa	bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb
17	1	a	aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa	bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb
drop table t1,t2;
create table t1 (c int);
insert into t1 values(1),(2);
create table t2 select * from t1;
create table t3 select * from t1, t2;
ERROR 42S21: Duplicate column name 'c'
create table t3 select t1.c AS c1, t2.c AS c2,1 as "const" from t1, t2;
show full columns from t3;
Field	Type	Collation	Null	Key	Default	Extra	Privileges	Comment
c1	int(11)	NULL	YES		NULL			
c2	int(11)	NULL	YES		NULL			
const	bigint(1)	NULL			0			
drop table t1,t2,t3;
create table t1 ( myfield INT NOT NULL, UNIQUE INDEX (myfield), unique (myfield), index(myfield));
drop table t1;
create table t1 ( id integer unsigned not null primary key );
create table t2 ( id integer unsigned not null primary key );
insert into t1 values (1), (2);
insert into t2 values (1);
select  t1.id as id_A,  t2.id as id_B from t1 left join t2 using ( id );
id_A	id_B
1	1
2	NULL
create table t3 (id_A integer unsigned not null, id_B integer unsigned null  );
insert into t3 select t1.id as id_A,  t2.id as id_B from t1 left join t2 using ( id );
select * from t3;
id_A	id_B
1	1
2	NULL
drop table t3;
create table t3 select t1.id as id_A,  t2.id as id_B from t1 left join t2 using ( id );
select * from t3;
id_A	id_B
1	1
2	NULL
drop table t1,t2,t3;
