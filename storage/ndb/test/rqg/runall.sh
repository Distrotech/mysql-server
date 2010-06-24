#!/bin/sh

set -e

: ${load:=1}
: ${loops:=100}
: ${queries:=1000}

pre="spj"
opre="$pre.$$"

: ${RQG_HOME:=/net/fimafeng09/export/home/tmp/oleja/mysql/randgen/randgen-2.2.0}

: ${data:= --spec=simple.zz}
: ${grammar:=spj_test.yy}

##: ${grammar:=$RQG_HOME/conf/optimizer_no_subquery.yy}   ## a pretty sensible grammar:
##: ${grammar:=$RQG_HOME/conf/optimizer_no_subquery_portable.yy}
##: ${grammar:=$RQG_HOME/conf/subquery_drizzle.yy}
##: ${grammar:=$RQG_HOME/conf/subquery_materialization.yy}
##: ${grammar:=$RQG_HOME/conf/subquery_semijoin.yy}
##: ${grammar:=$RQG_HOME/conf/subquery_semijoin_nested.yy}
##: ${grammar:=$RQG_HOME/conf/outer_join.yy}
##: ${grammar:=$RQG_HOME/conf/outer_join_portable.yy}

## Really simple (or: 'stupid') grammars: Have modified these, used to have nondeterministic behaviour
##: ${grammar:=$RQG_HOME/conf/subquery.yy}
##: ${grammar:=$RQG_HOME/conf/subquery-5.1.yy}

##: ${grammar:=$RQG_HOME/conf/optimizer_subquery.yy}

gensql=${RQG_HOME}/gensql.pl
gendata=${RQG_HOME}/gendata.pl
ecp="set engine_condition_pushdown=off;"

dsn=dbi:mysql:host=loki43:port=4401:user=root:database=${pre}_myisam
mysqltest="$MYSQLINSTALL/bin/mysqltest -uroot"
mysql="$MYSQLINSTALL/bin/mysql --host=loki43 --port=4401"

# Create database with a case sensitive collation to ensure a deterministic 
# resultset when 'LIMIT' is specified:
charset_spec="character set latin1 collate latin1_bin"
#charset_spec="default character set utf8 default collate utf8_bin"

export RQG_HOME
if [ "$load" ]
then
	$mysql -uroot -e "drop database if exists ${pre}_myisam; drop database if exists ${pre}_ndb"
	$mysql -uroot -e "create database ${pre}_myisam ${charset_spec}; create database ${pre}_ndb ${charset_spec}"
	${gendata} --dsn=$dsn ${data}
cat > /tmp/sproc.$$ <<EOF
DROP PROCEDURE IF EXISTS copydb;
delimiter |;
CREATE PROCEDURE copydb(dstdb varchar(64), srcdb varchar(64),
                        dstengine varchar(64))
BEGIN

  declare tabname varchar(255);
  declare done integer default 0;
  declare c cursor for 
  SELECT table_name
  FROM INFORMATION_SCHEMA.TABLES where table_schema = srcdb;
  declare continue handler for not found set done = 1;

  open c;
  
  repeat
    fetch c into tabname;
    if not done then
       set @ddl = CONCAT('CREATE TABLE ', dstdb, '.', tabname, 
                         ' LIKE ', srcdb, '.', tabname);
       select @ddl;
       PREPARE stmt from @ddl;
       EXECUTE stmt;
       set @ddl = CONCAT('ALTER TABLE ', dstdb, '.', tabname, 
                         ' engine = ', dstengine);
       PREPARE stmt from @ddl;
       EXECUTE stmt;

       ## Add some composite unique indexes
       if tabname > 'O' then
          set @ddl = CONCAT('CREATE UNIQUE INDEX ix1 ON ', dstdb, '.', tabname, 
                            '(col_int,col_int_unique)');
          PREPARE stmt from @ddl;
          EXECUTE stmt;
          set @ddl = CONCAT('CREATE UNIQUE INDEX ix2 ON ', dstdb, '.', tabname, 
                            '(col_int_key,col_int_unique)');
          PREPARE stmt from @ddl;
          EXECUTE stmt;

          ## Drop the original indexes covered by the indexes created above
          set @ddl = CONCAT('DROP INDEX col_int_key ON ', dstdb, '.', tabname);
          PREPARE stmt from @ddl;
          EXECUTE stmt;
          set @ddl = CONCAT('DROP INDEX col_int_unique ON ', dstdb, '.', tabname);
          PREPARE stmt from @ddl;
          EXECUTE stmt;
       elseif tabname > 'H' then
          set @ddl = CONCAT('CREATE UNIQUE INDEX ix3 ON ', dstdb, '.', tabname, 
                            '(col_int,col_int_key,col_int_unique)');
          PREPARE stmt from @ddl;
          EXECUTE stmt;

          ## Drop the original indexes covered by the indexes created above
          set @ddl = CONCAT('DROP INDEX col_int_key ON ', dstdb, '.', tabname);
          PREPARE stmt from @ddl;
          EXECUTE stmt;
          set @ddl = CONCAT('DROP INDEX col_int_unique ON ', dstdb, '.', tabname);
          PREPARE stmt from @ddl;
          EXECUTE stmt;
       end if;

       set @ddl = CONCAT('INSERT INTO ', dstdb, '.', tabname, 
                         ' SELECT * FROM ', srcdb, '.', tabname);
       PREPARE stmt from @ddl;
       EXECUTE stmt;
    end if;
  until done end repeat;
  close c;
END
\G

DROP PROCEDURE IF EXISTS alterengine\G
CREATE PROCEDURE alterengine (db varchar(64), newengine varchar(64))
BEGIN

  declare tabname varchar(255);
  declare done integer default 0;
  declare c cursor for 
  SELECT table_name
  FROM INFORMATION_SCHEMA.TABLES where table_schema = db;
  declare continue handler for not found set done = 1;

  open c;
  
  repeat
    fetch c into tabname;
    if not done then
       set @ddl = CONCAT('ALTER TABLE ', db, '.', tabname, 
                         ' engine = ', newengine);
       select @ddl;
       PREPARE stmt from @ddl;
       EXECUTE stmt;
    end if;
  until done end repeat;
  close c;
END
\G


CALL copydb('${pre}_ndb', '${pre}_myisam', 'ndb')\G
##CALL alterengine('${pre}_ndb', 'ndb')\G
DROP PROCEDURE copydb\G
DROP PROCEDURE alterengine\G
EOF
	$mysql -uroot test < /tmp/sproc.$$
	rm -f /tmp/sproc.$$
fi

check_query(){
    file=$1
    no=$2
    line=`expr $no \* 3` || true
    line=`expr $line + 3 + 2 + 1 + 1` || true
    sql=`head -n $line $file | tail -n 1`

    tmp=${opre}.$no.sql
    cat > $tmp <<EOF
--disable_warnings
--disable_query_log
--eval set ndb_join_pushdown='\$NDB_JOIN_PUSHDOWN';
$ecp
--echo kalle
--sorted_result
--error 0,1048,1054,1242
$sql
--exit
EOF

    NDB_JOIN_PUSHDOWN=off
    export NDB_JOIN_PUSHDOWN
    for t in 1 2 3 4 5
    do
	$mysqltest ${pre}_myisam < $tmp >> ${opre}.$no.myisam.$i.txt
    done

    for t in 1 2 3 4 5
    do
	$mysqltest ${pre}_ndb < $tmp >> ${opre}.$no.ndb.$i.txt
    done

    NDB_JOIN_PUSHDOWN=on
    export NDB_JOIN_PUSHDOWN
    for t in 1 2 3 4 5
    do
	$mysqltest ${pre}_ndb < $tmp >> ${opre}.$no.ndbpush.$i.txt
    done

    cnt=`md5sum ${opre}.$no.*.txt | awk '{ print $1;}' | sort | uniq | wc -l`
    if [ $cnt -ne 1 ]
    then
	echo -n "$no "
	echo $sql >> ${opre}.failing.sql
    fi

    rm $tmp ${opre}.$no.*.txt
}

locate_query (){
    file=$1
    rows=`cat $file | wc -l`
    queries=`expr $rows - 4`
    queries=`expr $queries / 3`
    q=0
    echo -n "checking queries..."
    while [ $q -ne $queries ]
    do
	check_query $file $q
	q=`expr $q + 1`
    done
    echo
}

run_all() {
    file=$1

    NDB_JOIN_PUSHDOWN=off
    export NDB_JOIN_PUSHDOWN
    echo "- run myisam"
    $mysqltest ${pre}_myisam < $file > ${opre}_myisam.out
    md5_myisam=`md5sum ${opre}_myisam.out | awk '{ print $1;}'`

    echo "- run ndb without push"
    NDB_JOIN_PUSHDOWN=off
    export NDB_JOIN_PUSHDOWN
    $mysqltest ${pre}_ndb < $file > ${opre}_ndb.out
    md5_ndb=`md5sum ${opre}_ndb.out | awk '{ print $1;}'`

    echo "- run ndb with push"
    NDB_JOIN_PUSHDOWN=on
    export NDB_JOIN_PUSHDOWN
    $mysqltest ${pre}_ndb < $file > ${opre}_ndbpush.out
    md5_ndbpush=`md5sum ${opre}_ndbpush.out | awk '{ print $1;}'`

    if [ "$md5_myisam" != "$md5_ndb" ] || [ "$md5_myisam" != "$md5_ndbpush" ]
    then
	echo "md5 missmatch: $md5_myisam $md5_ndb $md5_ndbpush"
	echo "locating failing query(s)"
	locate_query $file
    fi

    rm ${opre}_myisam.out ${opre}_ndb.out ${opre}_ndbpush.out
}

i=0
while [ $i -ne $loops ]
do
    i=`expr $i + 1`

    echo "** loop $i"
    us="$seed" || true
    if [ -z "$us" ]
    then
    	us=`date '+%N'`
    fi

    echo "- generating sql seed: $us"

    (
	echo "--disable_warnings"
	echo "--disable_query_log"
	echo "--eval set ndb_join_pushdown='\$NDB_JOIN_PUSHDOWN';"
	echo "$ecp"
	${gensql} --seed=$us --queries=$queries --dsn=$dsn --grammar=$grammar|
	awk '{ print "--sorted_result"; print "--error 0,1242"; print; }'
	echo "--exit"
    ) > ${opre}_test.sql

    run_all ${opre}_test.sql
    rm ${opre}_test.sql
    echo
done
