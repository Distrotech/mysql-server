/* Copyright Abandoned 1996 TCX DataKonsult AB & Monty Program KB & Detron HB
   This file is public domain and comes with NO WARRANTY of any kind */

#include <global.h>
#include "m_string.h"

#ifndef _WINDOWS
uchar NEAR ctype[257] =
{
  0,				/* For standard library */
  32,48,48,48,48,48,48,32,32,40,40,40,40,40,48,48,
  48,48,48,48,48,48,48,48,48,48,32,48,48,48,48,48,
  72,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,
  132,132,132,132,132,132,132,132,132,132,16,16,16,16,16,16,
  16,129,129,129,129,129,129,1,1,1,1,1,1,1,1,1,
  1,1,1,1,1,1,1,1,1,1,1,16,16,16,16,16,
  16,130,130,130,130,130,130,2,2,2,2,2,2,2,2,2,
  2,2,2,2,2,2,2,2,2,2,2,16,16,16,16,48,

  2,2,2,2,2,2,2,2,2,2,2,2,2,2,1,1,
  1,2,1,2,2,2,2,2,2,1,1,16,16,16,16,16,
  2,2,2,2,2,1,16,16,16,16,16,16,16,16,16,16,
  16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,
  16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,
  16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,
  16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,
  16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,32,
};

uchar NEAR to_lower[]=
{
  '\000','\001','\002','\003','\004','\005','\006','\007',
  '\010','\011','\012','\013','\014','\015','\016','\017',
  '\020','\021','\022','\023','\024','\025','\026','\027',
  '\030','\031','\032','\033','\034','\035','\036','\037',
  ' ',	 '!',	'"',   '#',   '$',   '%',   '&',   '\'',
  '(',	 ')',	'*',   '+',   ',',   '-',   '.',   '/',
  '0',	 '1',	'2',   '3',   '4',   '5',   '6',   '7',
  '8',	 '9',	':',   ';',   '<',   '=',   '>',   '?',
  '@',	 'a',	'b',   'c',   'd',   'e',   'f',   'g',
  'h',	 'i',	'j',   'k',   'l',   'm',   'n',   'o',
  'p',	 'q',	'r',   's',   't',   'u',   'v',   'w',
  'x',	 'y',	'z',   '[',   '\\',  ']',   '^',   '_',
  '`',	 'a',	'b',   'c',   'd',   'e',   'f',   'g',
  'h',	 'i',	'j',   'k',   'l',   'm',   'n',   'o',
  'p',	 'q',	'r',   's',   't',   'u',   'v',   'w',
  'x',	 'y',	'z',   '{',   '|',   '}',   '~',   '\177',
  135,	 129,	130,   131,   132,   133,   134,   135,
  136,	 137,	138,   139,   140,   141,   132,   134,
  130,	 145,	145,   147,   148,   149,   150,   151,
  152,	 148,	129,   155,   156,   157,   158,   159,
  160,	 161,	162,   163,   164,   164,   166,   167,
  168,	 169,	170,   171,   172,   173,   174,   175,
  176,	 177,	178,   179,   180,   181,   182,   183,
  184,	 185,	186,   187,   188,   189,   190,   191,
  192,	 193,	194,   195,   196,   197,   198,   199,
  200,	 201,	202,   203,   204,   205,   206,   207,
  208,	 209,	210,   211,   212,   213,   214,   215,
  216,	 217,	218,   219,   220,   221,   222,   223,
  224,	 225,	226,   227,   228,   229,   230,   231,
  232,	 233,	234,   235,   236,   237,   238,   239,
  240,	 241,	242,   243,   244,   245,   246,   247,
  248,	 249,	250,   251,   252,   253,   254,   255,
};

uchar NEAR to_upper[]=
{
  '\000','\001','\002','\003','\004','\005','\006','\007',
  '\010','\011','\012','\013','\014','\015','\016','\017',
  '\020','\021','\022','\023','\024','\025','\026','\027',
  '\030','\031','\032','\033','\034','\035','\036','\037',
  ' ',	 '!',	'"',   '#',   '$',   '%',   '&',   '\'',
  '(',	 ')',	'*',   '+',   ',',   '-',   '.',   '/',
  '0',	 '1',	'2',   '3',   '4',   '5',   '6',   '7',
  '8',	 '9',	':',   ';',   '<',   '=',   '>',   '?',
  '@',	 'A',	'B',   'C',   'D',   'E',   'F',   'G',
  'H',	 'I',	'J',   'K',   'L',   'M',   'N',   'O',
  'P',	 'Q',	'R',   'S',   'T',   'U',   'V',   'W',
  'X',	 'Y',	'Z',   '[',   '\\',  ']',   '^',   '_',
  '`',	 'A',	'B',   'C',   'D',   'E',   'F',   'G',
  'H',	 'I',	'J',   'K',   'L',   'M',   'N',   'O',
  'P',	 'Q',	'R',   'S',   'T',   'U',   'V',   'W',
  'X',	 'Y',	'Z',   '{',   '|',   '}',   '~',   '\177',

  128,	 154,	144,	65,   142,    65,   143,   128,
   69,	  69,	 69,	73,    73,    73,   142,   143,
  144,	 146,	146,	79,   153,    79,    85,    85,
   89,	 153,	154,   155,   156,   157,   158,   159,
   65,	  73,	 79,	85,   165,   165,   166,   167,
  168,	 169,	170,   171,   172,   173,   174,   175,
  176,	 177,	178,   179,   180,   181,   182,   183,
  184,	 185,	186,   187,   188,   189,   190,   191,
  192,	 193,	194,   195,   196,   197,   198,   199,
  200,	 201,	202,   203,   204,   205,   206,   207,
  208,	 209,	210,   211,   212,   213,   214,   215,
  216,	 217,	218,   219,   220,   221,   222,   223,
  224,	 225,	226,   227,   228,   229,   230,   231,
  232,	 233,	234,   235,   236,   237,   238,   239,
  240,	 241,	242,   243,   244,   245,   246,   247,
  248,	 249,	250,   251,   252,   253,   254,   255,
};

uchar NEAR sort_order[]=
{
  '\000','\001','\002','\003','\004','\005','\006','\007',
  '\010','\011','\012','\013','\014','\015','\016','\017',
  '\020','\021','\022','\023','\024','\025','\026','\027',
  '\030','\031','\032','\033','\034','\035','\036','\037',
  ' ',	 '!',	'"',   '#',   '$',   '%',   '&',   '\'',
  '(',	 ')',	'*',   '+',   ',',   '-',   '.',   '/',
  '0',	 '1',	'2',   '3',   '4',   '5',   '6',   '7',
  '8',	 '9',	':',   ';',   '<',   '=',   '>',   '?',
  '@',	 'A',	'B',   'C',   'D',   'E',   'F',   'G',
  'H',	 'I',	'J',   'K',   'L',   'M',   'N',   'O',
  'P',	 'Q',	'R',   'S',   'T',   'U',   'V',   'W',
  'X',	 'Y',	'Z',   '[',   '\\',  ']',   '^',   '_',
  '`',	 'A',	'B',   'C',   'D',   'E',   'F',   'G',
  'H',	 'I',	'J',   'K',   'L',   'M',   'N',   'O',
  'P',	 'Q',	'R',   'S',   'T',   'U',   'V',   'W',
  'X',	 'Y',	'Z',   '{',   '|',   '}',   '~',   '\177',
   67,	  89,	 69,	65,    92,    65,    91,    67,
   69,	  69,	 69,	73,    73,    73,    92,    91,
   69,	  92,	 92,	79,    93,    79,    85,    85,
   89,	  93,	 89,	36,    36,    36,    36,    36,
   65,	  73,	 79,	85,    78,    78,   166,   167,
   63,	 169,	170,   171,   172,    33,    34,    34,
  176,	 177,	178,   179,   180,   181,   182,   183,
  184,	 185,	186,   187,   188,   189,   190,   191,
  192,	 193,	194,   195,   196,   197,   198,   199,
  200,	 201,	202,   203,   204,   205,   206,   207,
  208,	 209,	210,   211,   212,   213,   214,   215,
  216,	 217,	218,   219,   220,   221,   222,   223,
  224,	 225,	226,   227,   228,   229,   230,   231,
  232,	 233,	234,   235,   236,   237,   238,   239,
  240,	 241,	242,   243,   244,   245,   246,   247,
  248,	 249,	250,   251,   252,   253,   254,   255,
};

#endif
