use mysql;
set table_type=myisam;

CREATE TABLE db (
  Host char(60) binary DEFAULT '' NOT NULL,
  Db char(64) binary DEFAULT '' NOT NULL,
  User char(16) binary DEFAULT '' NOT NULL,
  Select_priv enum('N','Y') COLLATE utf8_general_ci DEFAULT 'N' NOT NULL,
  Insert_priv enum('N','Y') COLLATE utf8_general_ci DEFAULT 'N' NOT NULL,
  Update_priv enum('N','Y') COLLATE utf8_general_ci DEFAULT 'N' NOT NULL,
  Delete_priv enum('N','Y') COLLATE utf8_general_ci DEFAULT 'N' NOT NULL,
  Create_priv enum('N','Y') COLLATE utf8_general_ci DEFAULT 'N' NOT NULL,
  Drop_priv enum('N','Y') COLLATE utf8_general_ci DEFAULT 'N' NOT NULL,
  Grant_priv enum('N','Y') COLLATE utf8_general_ci DEFAULT 'N' NOT NULL,
  References_priv enum('N','Y') COLLATE utf8_general_ci DEFAULT 'N' NOT NULL,
  Index_priv enum('N','Y') COLLATE utf8_general_ci DEFAULT 'N' NOT NULL,
  Alter_priv enum('N','Y') COLLATE utf8_general_ci DEFAULT 'N' NOT NULL,
  Create_tmp_table_priv enum('N','Y') COLLATE utf8_general_ci DEFAULT 'N' NOT NULL,
  Lock_tables_priv enum('N','Y') COLLATE utf8_general_ci DEFAULT 'N' NOT NULL,
  Create_view_priv enum('N','Y') COLLATE utf8_general_ci DEFAULT 'N' NOT NULL,
  Show_view_priv enum('N','Y') COLLATE utf8_general_ci DEFAULT 'N' NOT NULL,
  Create_routine_priv enum('N','Y') COLLATE utf8_general_ci DEFAULT 'N' NOT NULL,
  Alter_routine_priv enum('N','Y') COLLATE utf8_general_ci DEFAULT 'N' NOT NULL,
  Execute_priv enum('N','Y') COLLATE utf8_general_ci DEFAULT 'N' NOT NULL,
  Event_priv enum('N','Y') COLLATE utf8_general_ci DEFAULT 'N' NOT NULL,
  PRIMARY KEY Host (Host,Db,User),
  KEY User (User)
) engine=MyISAM
CHARACTER SET utf8 COLLATE utf8_bin
comment='Database privileges';

  
INSERT INTO db VALUES ('%','test','','Y','Y','Y','Y','Y','Y','N','Y','Y','Y','Y','Y','Y','Y','Y','N','N','Y');
INSERT INTO db VALUES ('%','test\_%','','Y','Y','Y','Y','Y','Y','N','Y','Y','Y','Y','Y','Y','Y','Y','N','N','Y');


CREATE TABLE host (
  Host char(60) binary DEFAULT '' NOT NULL,
  Db char(64) binary DEFAULT '' NOT NULL,
  Select_priv enum('N','Y') COLLATE utf8_general_ci DEFAULT 'N' NOT NULL,
  Insert_priv enum('N','Y') COLLATE utf8_general_ci DEFAULT 'N' NOT NULL,
  Update_priv enum('N','Y') COLLATE utf8_general_ci DEFAULT 'N' NOT NULL,
  Delete_priv enum('N','Y') COLLATE utf8_general_ci DEFAULT 'N' NOT NULL,
  Create_priv enum('N','Y') COLLATE utf8_general_ci DEFAULT 'N' NOT NULL,
  Drop_priv enum('N','Y') COLLATE utf8_general_ci DEFAULT 'N' NOT NULL,
  Grant_priv enum('N','Y') COLLATE utf8_general_ci DEFAULT 'N' NOT NULL,
  References_priv enum('N','Y') COLLATE utf8_general_ci DEFAULT 'N' NOT NULL,
  Index_priv enum('N','Y') COLLATE utf8_general_ci DEFAULT 'N' NOT NULL,
  Alter_priv enum('N','Y') COLLATE utf8_general_ci DEFAULT 'N' NOT NULL,
  Create_tmp_table_priv enum('N','Y') COLLATE utf8_general_ci DEFAULT 'N' NOT NULL,
  Lock_tables_priv enum('N','Y') COLLATE utf8_general_ci DEFAULT 'N' NOT NULL,
  Create_view_priv enum('N','Y') COLLATE utf8_general_ci DEFAULT 'N' NOT NULL,
  Show_view_priv enum('N','Y') COLLATE utf8_general_ci DEFAULT 'N' NOT NULL,
  Create_routine_priv enum('N','Y') COLLATE utf8_general_ci DEFAULT 'N' NOT NULL,
  Alter_routine_priv enum('N','Y') COLLATE utf8_general_ci DEFAULT 'N' NOT NULL,
  Execute_priv enum('N','Y') COLLATE utf8_general_ci DEFAULT 'N' NOT NULL,
  PRIMARY KEY Host (Host,Db)
) engine=MyISAM
CHARACTER SET utf8 COLLATE utf8_bin
comment='Host privileges;  Merged with database privileges';


CREATE TABLE user (
  Host char(60) binary DEFAULT '' NOT NULL,
  User char(16) binary DEFAULT '' NOT NULL,
  Password char(41) character set latin1 collate latin1_bin DEFAULT '' NOT NULL,
  Select_priv enum('N','Y') COLLATE utf8_general_ci DEFAULT 'N' NOT NULL,
  Insert_priv enum('N','Y') COLLATE utf8_general_ci DEFAULT 'N' NOT NULL,
  Update_priv enum('N','Y') COLLATE utf8_general_ci DEFAULT 'N' NOT NULL,
  Delete_priv enum('N','Y') COLLATE utf8_general_ci DEFAULT 'N' NOT NULL,
  Create_priv enum('N','Y') COLLATE utf8_general_ci DEFAULT 'N' NOT NULL,
  Drop_priv enum('N','Y') COLLATE utf8_general_ci DEFAULT 'N' NOT NULL,
  Reload_priv enum('N','Y') COLLATE utf8_general_ci DEFAULT 'N' NOT NULL,
  Shutdown_priv enum('N','Y') COLLATE utf8_general_ci DEFAULT 'N' NOT NULL,
  Process_priv enum('N','Y') COLLATE utf8_general_ci DEFAULT 'N' NOT NULL,
  File_priv enum('N','Y') COLLATE utf8_general_ci DEFAULT 'N' NOT NULL,
  Grant_priv enum('N','Y') COLLATE utf8_general_ci DEFAULT 'N' NOT NULL,
  References_priv enum('N','Y') COLLATE utf8_general_ci DEFAULT 'N' NOT NULL,
  Index_priv enum('N','Y') COLLATE utf8_general_ci DEFAULT 'N' NOT NULL,
  Alter_priv enum('N','Y') COLLATE utf8_general_ci DEFAULT 'N' NOT NULL,
  Show_db_priv enum('N','Y') COLLATE utf8_general_ci DEFAULT 'N' NOT NULL,
  Super_priv enum('N','Y') COLLATE utf8_general_ci DEFAULT 'N' NOT NULL,
  Create_tmp_table_priv enum('N','Y') COLLATE utf8_general_ci DEFAULT 'N' NOT NULL,
  Lock_tables_priv enum('N','Y') COLLATE utf8_general_ci DEFAULT 'N' NOT NULL,
  Execute_priv enum('N','Y') COLLATE utf8_general_ci DEFAULT 'N' NOT NULL,
  Repl_slave_priv enum('N','Y') COLLATE utf8_general_ci DEFAULT 'N' NOT NULL,
  Repl_client_priv enum('N','Y') COLLATE utf8_general_ci DEFAULT 'N' NOT NULL,
  Create_view_priv enum('N','Y') COLLATE utf8_general_ci DEFAULT 'N' NOT NULL,
  Show_view_priv enum('N','Y') COLLATE utf8_general_ci DEFAULT 'N' NOT NULL,
  Create_routine_priv enum('N','Y') COLLATE utf8_general_ci DEFAULT 'N' NOT NULL,
  Alter_routine_priv enum('N','Y') COLLATE utf8_general_ci DEFAULT 'N' NOT NULL,
  Create_user_priv enum('N','Y') COLLATE utf8_general_ci DEFAULT 'N' NOT NULL,
  Event_priv enum('N','Y') COLLATE utf8_general_ci DEFAULT 'N' NOT NULL,
  ssl_type enum('','ANY','X509', 'SPECIFIED') COLLATE utf8_general_ci DEFAULT '' NOT NULL,
  ssl_cipher BLOB NOT NULL,
  x509_issuer BLOB NOT NULL,
  x509_subject BLOB NOT NULL,
  max_questions int(11) unsigned DEFAULT 0  NOT NULL,
  max_updates int(11) unsigned DEFAULT 0  NOT NULL,
  max_connections int(11) unsigned DEFAULT 0  NOT NULL,
  max_user_connections int(11) unsigned DEFAULT 0  NOT NULL,
  PRIMARY KEY Host (Host,User)
) engine=MyISAM
CHARACTER SET utf8 COLLATE utf8_bin
comment='Users and global privileges';


INSERT INTO user VALUES ('localhost'   ,'root','','Y','Y','Y','Y','Y','Y','Y','Y','Y','Y','Y','Y','Y','Y','Y','Y','Y','Y','Y','Y','Y','Y','Y','Y','Y','Y','Y','','','','',0,0,0,0);
INSERT INTO user VALUES ('@HOSTNAME@%' ,'root','','Y','Y','Y','Y','Y','Y','Y','Y','Y','Y','Y','Y','Y','Y','Y','Y','Y','Y','Y','Y','Y','Y','Y','Y','Y','Y','Y','','','','',0,0,0,0);
REPLACE INTO user VALUES ('127.0.0.1'  ,'root','','Y','Y','Y','Y','Y','Y','Y','Y','Y','Y','Y','Y','Y','Y','Y','Y','Y','Y','Y','Y','Y','Y','Y','Y','Y','Y','Y','','','','',0,0,0,0);
INSERT  INTO user (host,user) VALUES ('localhost','');
INSERT  INTO user (host,user) VALUES ('@HOSTNAME@%','');


CREATE TABLE func (
  name char(64) binary DEFAULT '' NOT NULL,
  ret tinyint(1) DEFAULT '0' NOT NULL,
  dl char(128) DEFAULT '' NOT NULL,
  type enum ('function','aggregate') COLLATE utf8_general_ci NOT NULL,
  PRIMARY KEY (name)
) engine=MyISAM
CHARACTER SET utf8 COLLATE utf8_bin
comment='User defined functions';


CREATE TABLE plugin (
  name char(64) binary DEFAULT '' NOT NULL,
  dl char(128) DEFAULT '' NOT NULL,
  PRIMARY KEY (name)
) engine=MyISAM
CHARACTER SET utf8 COLLATE utf8_bin
comment='MySQL plugins';


CREATE TABLE tables_priv (
  Host char(60) binary DEFAULT '' NOT NULL,
  Db char(64) binary DEFAULT '' NOT NULL,
  User char(16) binary DEFAULT '' NOT NULL,
  Table_name char(64) binary DEFAULT '' NOT NULL,
  Grantor char(77) DEFAULT '' NOT NULL,
  Timestamp timestamp(14),
  Table_priv set('Select','Insert','Update','Delete','Create','Drop','Grant','References','Index','Alter','Create View','Show view') COLLATE utf8_general_ci DEFAULT '' NOT NULL,
  Column_priv set('Select','Insert','Update','References') COLLATE utf8_general_ci DEFAULT '' NOT NULL,
  PRIMARY KEY (Host,Db,User,Table_name),KEY Grantor (Grantor)
) engine=MyISAM
CHARACTER SET utf8 COLLATE utf8_bin
comment='Table privileges';


CREATE TABLE columns_priv (
  Host char(60) binary DEFAULT '' NOT NULL,
  Db char(64) binary DEFAULT '' NOT NULL,
  User char(16) binary DEFAULT '' NOT NULL,
  Table_name char(64) binary DEFAULT '' NOT NULL,
  Column_name char(64) binary DEFAULT '' NOT NULL,
  Timestamp timestamp(14),
  Column_priv set('Select','Insert','Update','References') COLLATE utf8_general_ci DEFAULT '' NOT NULL,
  PRIMARY KEY (Host,Db,User,Table_name,Column_name)
) engine=MyISAM
CHARACTER SET utf8 COLLATE utf8_bin
comment='Column privileges';


CREATE TABLE help_topic (
  help_topic_id int unsigned not null,
  name char(64) not null,
  help_category_id smallint unsigned not null,
  description text not null,
  example text not null,
  url char(128) not null,
  primary key (help_topic_id),
  unique index (name)
) engine=MyISAM
CHARACTER SET utf8
comment='help topics';

  
CREATE TABLE help_category (
  help_category_id smallint unsigned not null,
  name char(64) not null,
  parent_category_id smallint unsigned null,
  url char(128) not null,
  primary key (help_category_id),unique index (name)
) engine=MyISAM
CHARACTER SET utf8
comment='help categories';


CREATE TABLE help_keyword (
  help_keyword_id int unsigned not null,
  name char(64) not null,
  primary key (help_keyword_id),unique index (name)
) engine=MyISAM
CHARACTER SET utf8
comment='help keywords';


CREATE TABLE help_relation (
  help_topic_id int unsigned not null references help_topic,
  help_keyword_id  int unsigned not null references help_keyword,
  primary key (help_keyword_id, help_topic_id)
) engine=MyISAM
CHARACTER SET utf8
comment='keyword-topic relation';


CREATE TABLE time_zone_name (
  Name char(64) NOT NULL,
  Time_zone_id int unsigned NOT NULL,
  PRIMARY KEY Name (Name)
) engine=MyISAM
CHARACTER SET utf8
comment='Time zone names';

  
INSERT INTO time_zone_name (Name, Time_Zone_id) VALUES
  ('MET', 1), ('UTC', 2), ('Universal', 2),
  ('Europe/Moscow',3), ('leap/Europe/Moscow',4),
  ('Japan', 5);


CREATE TABLE time_zone (
  Time_zone_id int unsigned NOT NULL auto_increment,
  Use_leap_seconds enum('Y','N') COLLATE utf8_general_ci DEFAULT 'N' NOT NULL,
  PRIMARY KEY TzId (Time_zone_id)
) engine=MyISAM
CHARACTER SET utf8
comment='Time zones';

  
INSERT INTO time_zone (Time_zone_id, Use_leap_seconds)
  VALUES (1,'N'), (2,'N'), (3,'N'), (4,'Y'), (5,'N');


CREATE TABLE time_zone_transition (
  Time_zone_id int unsigned NOT NULL,
  Transition_time bigint signed NOT NULL,
  Transition_type_id int unsigned NOT NULL,
  PRIMARY KEY TzIdTranTime (Time_zone_id, Transition_time)
) engine=MyISAM
CHARACTER SET utf8
comment='Time zone transitions';

  
INSERT INTO time_zone_transition
  (Time_zone_id, Transition_time, Transition_type_id)
VALUES
  (1, -1693706400, 0) ,(1, -1680483600, 1)
 ,(1, -1663455600, 2) ,(1, -1650150000, 3)
 ,(1, -1632006000, 2) ,(1, -1618700400, 3)
 ,(1, -938905200, 2) ,(1, -857257200, 3)
 ,(1, -844556400, 2) ,(1, -828226800, 3)
 ,(1, -812502000, 2) ,(1, -796777200, 3)
 ,(1, 228877200, 2) ,(1, 243997200, 3)
 ,(1, 260326800, 2) ,(1, 276051600, 3)
 ,(1, 291776400, 2) ,(1, 307501200, 3)
 ,(1, 323830800, 2) ,(1, 338950800, 3)
 ,(1, 354675600, 2) ,(1, 370400400, 3)
 ,(1, 386125200, 2) ,(1, 401850000, 3)
 ,(1, 417574800, 2) ,(1, 433299600, 3)
 ,(1, 449024400, 2) ,(1, 465354000, 3)
 ,(1, 481078800, 2) ,(1, 496803600, 3)
 ,(1, 512528400, 2) ,(1, 528253200, 3)
 ,(1, 543978000, 2) ,(1, 559702800, 3)
 ,(1, 575427600, 2) ,(1, 591152400, 3)
 ,(1, 606877200, 2) ,(1, 622602000, 3)
 ,(1, 638326800, 2) ,(1, 654656400, 3)
 ,(1, 670381200, 2) ,(1, 686106000, 3)
 ,(1, 701830800, 2) ,(1, 717555600, 3)
 ,(1, 733280400, 2) ,(1, 749005200, 3)
 ,(1, 764730000, 2) ,(1, 780454800, 3)
 ,(1, 796179600, 2) ,(1, 811904400, 3)
 ,(1, 828234000, 2) ,(1, 846378000, 3)
 ,(1, 859683600, 2) ,(1, 877827600, 3)
 ,(1, 891133200, 2) ,(1, 909277200, 3)
 ,(1, 922582800, 2) ,(1, 941331600, 3)
 ,(1, 954032400, 2) ,(1, 972781200, 3)
 ,(1, 985482000, 2) ,(1, 1004230800, 3)
 ,(1, 1017536400, 2) ,(1, 1035680400, 3)
 ,(1, 1048986000, 2) ,(1, 1067130000, 3)
 ,(1, 1080435600, 2) ,(1, 1099184400, 3)
 ,(1, 1111885200, 2) ,(1, 1130634000, 3)
 ,(1, 1143334800, 2) ,(1, 1162083600, 3)
 ,(1, 1174784400, 2) ,(1, 1193533200, 3)
 ,(1, 1206838800, 2) ,(1, 1224982800, 3)
 ,(1, 1238288400, 2) ,(1, 1256432400, 3)
 ,(1, 1269738000, 2) ,(1, 1288486800, 3)
 ,(1, 1301187600, 2) ,(1, 1319936400, 3)
 ,(1, 1332637200, 2) ,(1, 1351386000, 3)
 ,(1, 1364691600, 2) ,(1, 1382835600, 3)
 ,(1, 1396141200, 2) ,(1, 1414285200, 3)
 ,(1, 1427590800, 2) ,(1, 1445734800, 3)
 ,(1, 1459040400, 2) ,(1, 1477789200, 3)
 ,(1, 1490490000, 2) ,(1, 1509238800, 3)
 ,(1, 1521939600, 2) ,(1, 1540688400, 3)
 ,(1, 1553994000, 2) ,(1, 1572138000, 3)
 ,(1, 1585443600, 2) ,(1, 1603587600, 3)
 ,(1, 1616893200, 2) ,(1, 1635642000, 3)
 ,(1, 1648342800, 2) ,(1, 1667091600, 3)
 ,(1, 1679792400, 2) ,(1, 1698541200, 3)
 ,(1, 1711846800, 2) ,(1, 1729990800, 3)
 ,(1, 1743296400, 2) ,(1, 1761440400, 3)
 ,(1, 1774746000, 2) ,(1, 1792890000, 3)
 ,(1, 1806195600, 2) ,(1, 1824944400, 3)
 ,(1, 1837645200, 2) ,(1, 1856394000, 3)
 ,(1, 1869094800, 2) ,(1, 1887843600, 3)
 ,(1, 1901149200, 2) ,(1, 1919293200, 3)
 ,(1, 1932598800, 2) ,(1, 1950742800, 3)
 ,(1, 1964048400, 2) ,(1, 1982797200, 3)
 ,(1, 1995498000, 2) ,(1, 2014246800, 3)
 ,(1, 2026947600, 2) ,(1, 2045696400, 3)
 ,(1, 2058397200, 2) ,(1, 2077146000, 3)
 ,(1, 2090451600, 2) ,(1, 2108595600, 3)
 ,(1, 2121901200, 2) ,(1, 2140045200, 3)
 ,(3, -1688265000, 2) ,(3, -1656819048, 1)
 ,(3, -1641353448, 2) ,(3, -1627965048, 3)
 ,(3, -1618716648, 1) ,(3, -1596429048, 3)
 ,(3, -1593829848, 5) ,(3, -1589860800, 4)
 ,(3, -1542427200, 5) ,(3, -1539493200, 6)
 ,(3, -1525323600, 5) ,(3, -1522728000, 4)
 ,(3, -1491188400, 7) ,(3, -1247536800, 4)
 ,(3, 354920400, 5) ,(3, 370728000, 4)
 ,(3, 386456400, 5) ,(3, 402264000, 4)
 ,(3, 417992400, 5) ,(3, 433800000, 4)
 ,(3, 449614800, 5) ,(3, 465346800, 8)
 ,(3, 481071600, 9) ,(3, 496796400, 8)
 ,(3, 512521200, 9) ,(3, 528246000, 8)
 ,(3, 543970800, 9) ,(3, 559695600, 8)
 ,(3, 575420400, 9) ,(3, 591145200, 8)
 ,(3, 606870000, 9) ,(3, 622594800, 8)
 ,(3, 638319600, 9) ,(3, 654649200, 8)
 ,(3, 670374000, 10) ,(3, 686102400, 11)
 ,(3, 695779200, 8) ,(3, 701812800, 5)
 ,(3, 717534000, 4) ,(3, 733273200, 9)
 ,(3, 748998000, 8) ,(3, 764722800, 9)
 ,(3, 780447600, 8) ,(3, 796172400, 9)
 ,(3, 811897200, 8) ,(3, 828226800, 9)
 ,(3, 846370800, 8) ,(3, 859676400, 9)
 ,(3, 877820400, 8) ,(3, 891126000, 9)
 ,(3, 909270000, 8) ,(3, 922575600, 9)
 ,(3, 941324400, 8) ,(3, 954025200, 9)
 ,(3, 972774000, 8) ,(3, 985474800, 9)
 ,(3, 1004223600, 8) ,(3, 1017529200, 9)
 ,(3, 1035673200, 8) ,(3, 1048978800, 9)
 ,(3, 1067122800, 8) ,(3, 1080428400, 9)
 ,(3, 1099177200, 8) ,(3, 1111878000, 9)
 ,(3, 1130626800, 8) ,(3, 1143327600, 9)
 ,(3, 1162076400, 8) ,(3, 1174777200, 9)
 ,(3, 1193526000, 8) ,(3, 1206831600, 9)
 ,(3, 1224975600, 8) ,(3, 1238281200, 9)
 ,(3, 1256425200, 8) ,(3, 1269730800, 9)
 ,(3, 1288479600, 8) ,(3, 1301180400, 9)
 ,(3, 1319929200, 8) ,(3, 1332630000, 9)
 ,(3, 1351378800, 8) ,(3, 1364684400, 9)
 ,(3, 1382828400, 8) ,(3, 1396134000, 9)
 ,(3, 1414278000, 8) ,(3, 1427583600, 9)
 ,(3, 1445727600, 8) ,(3, 1459033200, 9)
 ,(3, 1477782000, 8) ,(3, 1490482800, 9)
 ,(3, 1509231600, 8) ,(3, 1521932400, 9)
 ,(3, 1540681200, 8) ,(3, 1553986800, 9)
 ,(3, 1572130800, 8) ,(3, 1585436400, 9)
 ,(3, 1603580400, 8) ,(3, 1616886000, 9)
 ,(3, 1635634800, 8) ,(3, 1648335600, 9)
 ,(3, 1667084400, 8) ,(3, 1679785200, 9)
 ,(3, 1698534000, 8) ,(3, 1711839600, 9)
 ,(3, 1729983600, 8) ,(3, 1743289200, 9)
 ,(3, 1761433200, 8) ,(3, 1774738800, 9)
 ,(3, 1792882800, 8) ,(3, 1806188400, 9)
 ,(3, 1824937200, 8) ,(3, 1837638000, 9)
 ,(3, 1856386800, 8) ,(3, 1869087600, 9)
 ,(3, 1887836400, 8) ,(3, 1901142000, 9)
 ,(3, 1919286000, 8) ,(3, 1932591600, 9)
 ,(3, 1950735600, 8) ,(3, 1964041200, 9)
 ,(3, 1982790000, 8) ,(3, 1995490800, 9)
 ,(3, 2014239600, 8) ,(3, 2026940400, 9)
 ,(3, 2045689200, 8) ,(3, 2058390000, 9)
 ,(3, 2077138800, 8) ,(3, 2090444400, 9)
 ,(3, 2108588400, 8) ,(3, 2121894000, 9)
 ,(3, 2140038000, 8)
 ,(4, -1688265000, 2) ,(4, -1656819048, 1)
 ,(4, -1641353448, 2) ,(4, -1627965048, 3)
 ,(4, -1618716648, 1) ,(4, -1596429048, 3)
 ,(4, -1593829848, 5) ,(4, -1589860800, 4)
 ,(4, -1542427200, 5) ,(4, -1539493200, 6)
 ,(4, -1525323600, 5) ,(4, -1522728000, 4)
 ,(4, -1491188400, 7) ,(4, -1247536800, 4)
 ,(4, 354920409, 5) ,(4, 370728010, 4)
 ,(4, 386456410, 5) ,(4, 402264011, 4)
 ,(4, 417992411, 5) ,(4, 433800012, 4)
 ,(4, 449614812, 5) ,(4, 465346812, 8)
 ,(4, 481071612, 9) ,(4, 496796413, 8)
 ,(4, 512521213, 9) ,(4, 528246013, 8)
 ,(4, 543970813, 9) ,(4, 559695613, 8)
 ,(4, 575420414, 9) ,(4, 591145214, 8)
 ,(4, 606870014, 9) ,(4, 622594814, 8)
 ,(4, 638319615, 9) ,(4, 654649215, 8)
 ,(4, 670374016, 10) ,(4, 686102416, 11)
 ,(4, 695779216, 8) ,(4, 701812816, 5)
 ,(4, 717534017, 4) ,(4, 733273217, 9)
 ,(4, 748998018, 8) ,(4, 764722818, 9)
 ,(4, 780447619, 8) ,(4, 796172419, 9)
 ,(4, 811897219, 8) ,(4, 828226820, 9)
 ,(4, 846370820, 8) ,(4, 859676420, 9)
 ,(4, 877820421, 8) ,(4, 891126021, 9)
 ,(4, 909270021, 8) ,(4, 922575622, 9)
 ,(4, 941324422, 8) ,(4, 954025222, 9)
 ,(4, 972774022, 8) ,(4, 985474822, 9)
 ,(4, 1004223622, 8) ,(4, 1017529222, 9)
 ,(4, 1035673222, 8) ,(4, 1048978822, 9)
 ,(4, 1067122822, 8) ,(4, 1080428422, 9)
 ,(4, 1099177222, 8) ,(4, 1111878022, 9)
 ,(4, 1130626822, 8) ,(4, 1143327622, 9)
 ,(4, 1162076422, 8) ,(4, 1174777222, 9)
 ,(4, 1193526022, 8) ,(4, 1206831622, 9)
 ,(4, 1224975622, 8) ,(4, 1238281222, 9)
 ,(4, 1256425222, 8) ,(4, 1269730822, 9)
 ,(4, 1288479622, 8) ,(4, 1301180422, 9)
 ,(4, 1319929222, 8) ,(4, 1332630022, 9)
 ,(4, 1351378822, 8) ,(4, 1364684422, 9)
 ,(4, 1382828422, 8) ,(4, 1396134022, 9)
 ,(4, 1414278022, 8) ,(4, 1427583622, 9)
 ,(4, 1445727622, 8) ,(4, 1459033222, 9)
 ,(4, 1477782022, 8) ,(4, 1490482822, 9)
 ,(4, 1509231622, 8) ,(4, 1521932422, 9)
 ,(4, 1540681222, 8) ,(4, 1553986822, 9)
 ,(4, 1572130822, 8) ,(4, 1585436422, 9)
 ,(4, 1603580422, 8) ,(4, 1616886022, 9)
 ,(4, 1635634822, 8) ,(4, 1648335622, 9)
 ,(4, 1667084422, 8) ,(4, 1679785222, 9)
 ,(4, 1698534022, 8) ,(4, 1711839622, 9)
 ,(4, 1729983622, 8) ,(4, 1743289222, 9)
 ,(4, 1761433222, 8) ,(4, 1774738822, 9)
 ,(4, 1792882822, 8) ,(4, 1806188422, 9)
 ,(4, 1824937222, 8) ,(4, 1837638022, 9)
 ,(4, 1856386822, 8) ,(4, 1869087622, 9)
 ,(4, 1887836422, 8) ,(4, 1901142022, 9)
 ,(4, 1919286022, 8) ,(4, 1932591622, 9)
 ,(4, 1950735622, 8) ,(4, 1964041222, 9)
 ,(4, 1982790022, 8) ,(4, 1995490822, 9)
 ,(4, 2014239622, 8) ,(4, 2026940422, 9)
 ,(4, 2045689222, 8) ,(4, 2058390022, 9)
 ,(4, 2077138822, 8) ,(4, 2090444422, 9)
 ,(4, 2108588422, 8) ,(4, 2121894022, 9)
 ,(4, 2140038022, 8)
 ,(5, -1009875600, 1);


CREATE TABLE time_zone_transition_type (
  Time_zone_id int unsigned NOT NULL,
  Transition_type_id int unsigned NOT NULL,
  Offset int signed DEFAULT 0 NOT NULL,
  Is_DST tinyint unsigned DEFAULT 0 NOT NULL,
  Abbreviation char(8) DEFAULT '' NOT NULL,
  PRIMARY KEY TzIdTrTId (Time_zone_id, Transition_type_id)
) engine=MyISAM
CHARACTER SET utf8
comment='Time zone transition types';

  
INSERT INTO time_zone_transition_type (
  Time_zone_id,Transition_type_id, Offset, Is_DST, Abbreviation) VALUES
  (1, 0, 7200, 1, 'MEST') ,(1, 1, 3600, 0, 'MET')
 ,(1, 2, 7200, 1, 'MEST') ,(1, 3, 3600, 0, 'MET')
 ,(2, 0, 0, 0, 'UTC')
 ,(3, 0, 9000, 0, 'MMT') ,(3, 1, 12648, 1, 'MST')
 ,(3, 2, 9048, 0, 'MMT') ,(3, 3, 16248, 1, 'MDST')
 ,(3, 4, 10800, 0, 'MSK') ,(3, 5, 14400, 1, 'MSD')
 ,(3, 6, 18000, 1, 'MSD') ,(3, 7, 7200, 0, 'EET')
 ,(3, 8, 10800, 0, 'MSK') ,(3, 9, 14400, 1, 'MSD')
 ,(3, 10, 10800, 1, 'EEST') ,(3, 11, 7200, 0, 'EET')
 ,(4, 0, 9000, 0, 'MMT') ,(4, 1, 12648, 1, 'MST')
 ,(4, 2, 9048, 0, 'MMT') ,(4, 3, 16248, 1, 'MDST')
 ,(4, 4, 10800, 0, 'MSK') ,(4, 5, 14400, 1, 'MSD')
 ,(4, 6, 18000, 1, 'MSD') ,(4, 7, 7200, 0, 'EET')
 ,(4, 8, 10800, 0, 'MSK') ,(4, 9, 14400, 1, 'MSD')
 ,(4, 10, 10800, 1, 'EEST') ,(4, 11, 7200, 0, 'EET')
 ,(5, 0, 32400, 0, 'CJT') ,(5, 1, 32400, 0, 'JST');


CREATE TABLE time_zone_leap_second (
  Transition_time bigint signed NOT NULL,
  Correction int signed NOT NULL,
  PRIMARY KEY TranTime (Transition_time)
) engine=MyISAM
CHARACTER SET utf8
comment='Leap seconds information for time zones';

  
INSERT INTO time_zone_leap_second (
  Transition_time, Correction) VALUES
  (78796800, 1) ,(94694401, 2) ,(126230402, 3)
 ,(157766403, 4) ,(189302404, 5) ,(220924805, 6)
 ,(252460806, 7) ,(283996807, 8) ,(315532808, 9)
 ,(362793609, 10) ,(394329610, 11) ,(425865611, 12)
 ,(489024012, 13) ,(567993613, 14) ,(631152014, 15)
 ,(662688015, 16) ,(709948816, 17) ,(741484817, 18)
 ,(773020818, 19) ,(820454419, 20) ,(867715220, 21)
 ,(915148821, 22);


CREATE TABLE procs_priv (
  Host char(60) binary DEFAULT '' NOT NULL,
  Db char(64) binary DEFAULT '' NOT NULL,
  User char(16) binary DEFAULT '' NOT NULL,
  Routine_name char(64) binary DEFAULT '' NOT NULL,
  Routine_type enum('FUNCTION','PROCEDURE') NOT NULL,
  Grantor char(77) DEFAULT '' NOT NULL,
  Proc_priv set('Execute','Alter Routine','Grant') COLLATE utf8_general_ci DEFAULT '' NOT NULL,
  Timestamp timestamp(14),
  PRIMARY KEY (Host,Db,User,Routine_name,Routine_type),
  KEY Grantor (Grantor)
) engine=MyISAM
CHARACTER SET utf8 COLLATE utf8_bin
comment='Procedure privileges';


CREATE TABLE proc (
  db                char(64) collate utf8_bin DEFAULT '' NOT NULL,
  name              char(64) DEFAULT '' NOT NULL,
  type              enum('FUNCTION','PROCEDURE') NOT NULL,
  specific_name     char(64) DEFAULT '' NOT NULL,
  language          enum('SQL') DEFAULT 'SQL' NOT NULL,
  sql_data_access   enum('CONTAINS_SQL',
		     'NO_SQL',
		     'READS_SQL_DATA',
		     'MODIFIES_SQL_DATA'
                    ) DEFAULT 'CONTAINS_SQL' NOT NULL,
  is_deterministic  enum('YES','NO') DEFAULT 'NO' NOT NULL,
  security_type     enum('INVOKER','DEFINER') DEFAULT 'DEFINER' NOT NULL,
  param_list        blob DEFAULT '' NOT NULL,
  returns           char(64) DEFAULT '' NOT NULL,
  body              longblob DEFAULT '' NOT NULL,
  definer           char(77) collate utf8_bin DEFAULT '' NOT NULL,
  created           timestamp,
  modified          timestamp,
  sql_mode          set(
                        'REAL_AS_FLOAT',
                        'PIPES_AS_CONCAT',
                        'ANSI_QUOTES',
                        'IGNORE_SPACE',
                        'NOT_USED',
                        'ONLY_FULL_GROUP_BY',
                        'NO_UNSIGNED_SUBTRACTION',
                        'NO_DIR_IN_CREATE',
                        'POSTGRESQL',
                        'ORACLE',
                        'MSSQL',
                        'DB2',
                        'MAXDB',
                        'NO_KEY_OPTIONS',
                        'NO_TABLE_OPTIONS',
                        'NO_FIELD_OPTIONS',
                        'MYSQL323',
                        'MYSQL40',
                        'ANSI',
                        'NO_AUTO_VALUE_ON_ZERO',
                        'NO_BACKSLASH_ESCAPES',
                        'STRICT_TRANS_TABLES',
                        'STRICT_ALL_TABLES',
                        'NO_ZERO_IN_DATE',
                        'NO_ZERO_DATE',
                        'INVALID_DATES',
                        'ERROR_FOR_DIVISION_BY_ZERO',
                        'TRADITIONAL',
                        'NO_AUTO_CREATE_USER',
                        'HIGH_NOT_PRECEDENCE'
                    ) DEFAULT '' NOT NULL,
  comment           char(64) collate utf8_bin DEFAULT '' NOT NULL,
  PRIMARY KEY (db,name,type)
) character set utf8 comment='Stored Procedures';


CREATE PROCEDURE create_log_tables() BEGIN DECLARE is_csv_enabled int DEFAULT 0; SELECT @@have_csv = 'YES' INTO is_csv_enabled; IF (is_csv_enabled) THEN CREATE TABLE general_log (event_time TIMESTAMP NOT NULL, user_host MEDIUMTEXT, thread_id INTEGER, server_id INTEGER, command_type VARCHAR(64), argument MEDIUMTEXT) engine=CSV CHARACTER SET utf8 comment='General log'; CREATE TABLE slow_log (start_time TIMESTAMP NOT NULL, user_host MEDIUMTEXT NOT NULL, query_time TIME NOT NULL, lock_time TIME NOT NULL, rows_sent INTEGER NOT NULL, rows_examined INTEGER NOT NULL, db VARCHAR(512), last_insert_id INTEGER, insert_id INTEGER, server_id INTEGER, sql_text MEDIUMTEXT NOT NULL) engine=CSV CHARACTER SET utf8 comment='Slow log'; END IF; END;
CALL create_log_tables();
DROP PROCEDURE create_log_tables;

CREATE TABLE event (
  db char(64) CHARACTER SET utf8 COLLATE utf8_bin NOT NULL default '',
  name char(64) CHARACTER SET utf8 COLLATE utf8_bin NOT NULL default '',
  body longblob NOT NULL,
  definer char(77) CHARACTER SET utf8 COLLATE utf8_bin NOT NULL default '',
  execute_at DATETIME default NULL,
  interval_value int(11) default NULL,
  interval_field ENUM('YEAR','QUARTER','MONTH','DAY','HOUR','MINUTE','WEEK',
                       'SECOND','MICROSECOND', 'YEAR_MONTH','DAY_HOUR',
                       'DAY_MINUTE','DAY_SECOND',
                       'HOUR_MINUTE','HOUR_SECOND',
                       'MINUTE_SECOND','DAY_MICROSECOND',
                       'HOUR_MICROSECOND','MINUTE_MICROSECOND',
                       'SECOND_MICROSECOND') default NULL,
  created TIMESTAMP NOT NULL,
  modified TIMESTAMP NOT NULL,
  last_executed DATETIME default NULL,
  starts DATETIME default NULL,
  ends DATETIME default NULL,
  status ENUM('ENABLED','DISABLED') NOT NULL default 'ENABLED',
  on_completion ENUM('DROP','PRESERVE') NOT NULL default 'DROP',
  comment varchar(64) CHARACTER SET utf8 COLLATE utf8_bin NOT NULL default '',
  PRIMARY KEY  (definer, db, name)
) ENGINE=MyISAM DEFAULT CHARSET=utf8 COMMENT 'Events';

CREATE DATABASE IF NOT EXISTS cluster_replication;
CREATE TABLE IF NOT EXISTS cluster_replication.binlog_index (Position BIGINT UNSIGNED NOT NULL, File VARCHAR(255) NOT NULL, epoch BIGINT UNSIGNED NOT NULL, inserts BIGINT UNSIGNED NOT NULL, updates BIGINT UNSIGNED NOT NULL, deletes BIGINT UNSIGNED NOT NULL, schemaops BIGINT UNSIGNED NOT NULL, PRIMARY KEY(epoch)) ENGINE=MYISAM;
