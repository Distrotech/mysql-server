//Created by Max Veremayenko (verem@tg.kiev.ua)
//As standart for ukranian font i use ukrainian for window95

#include <global.h>  
#include "m_string.h"

uchar NEAR ctype_win1251ukr[257] = {
0,
32,32,32,32,32,32,32,32,32,40,40,40,40,40,32,32,
32,32,32,32,32,32,32,32,32,32,32,32,32,32,32,32,
72,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,
132,132,132,132,132,132,132,132,132,132,16,16,16,16,16,16,
16,129,129,129,129,129,129,1,1,1,1,1,1,1,1,1,
1,1,1,1,1,1,1,1,1,1,1,16,16,16,16,16,
16,130,130,130,130,130,130,2,2,2,2,2,2,2,2,2,
2,2,2,2,2,2,2,2,2,2,2,16,16,16,16,32,
(uchar)'\20',(uchar)'\20',(uchar)'\20',(uchar)'\20',(uchar)'\20',(uchar)'\20',(uchar)'\20',(uchar)'\20',
(uchar)'\20',(uchar)'\20',(uchar)'\20',(uchar)'\20',(uchar)'\20',(uchar)'\20',(uchar)'\20',(uchar)'\20',
(uchar)'\20',(uchar)'\20',(uchar)'\20',(uchar)'\20',(uchar)'\20',(uchar)'\20',(uchar)'\20',(uchar)'\20',
(uchar)'\20',(uchar)'\20',(uchar)'\20',(uchar)'\20',(uchar)'\20',(uchar)'\20',(uchar)'\20',(uchar)'\20',
(uchar)'\20',(uchar)'\20',(uchar)'\20',(uchar)'\20',(uchar)'\20',(uchar)'\1',(uchar)'\20',(uchar)'\20',
(uchar)'\1',(uchar)'\20',(uchar)'\1',(uchar)'\20',(uchar)'\20',(uchar)'\20',(uchar)'\20',(uchar)'\1',
(uchar)'\20',(uchar)'\20',(uchar)'\1',(uchar)'\2',(uchar)'\2',(uchar)'\20',(uchar)'\20',(uchar)'\20',
(uchar)'\2',(uchar)'\20',(uchar)'\2',(uchar)'\20',(uchar)'\20',(uchar)'\20',(uchar)'\20',(uchar)'\2',
(uchar)'\1',(uchar)'\1',(uchar)'\1',(uchar)'\1',(uchar)'\1',(uchar)'\1',(uchar)'\1',(uchar)'\1',
(uchar)'\1',(uchar)'\1',(uchar)'\1',(uchar)'\1',(uchar)'\1',(uchar)'\1',(uchar)'\1',(uchar)'\1',
(uchar)'\1',(uchar)'\1',(uchar)'\1',(uchar)'\1',(uchar)'\1',(uchar)'\1',(uchar)'\1',(uchar)'\1',
(uchar)'\1',(uchar)'\1',(uchar)'\1',(uchar)'\1',(uchar)'\1',(uchar)'\1',(uchar)'\1',(uchar)'\1',
(uchar)'\2',(uchar)'\2',(uchar)'\2',(uchar)'\2',(uchar)'\2',(uchar)'\2',(uchar)'\2',(uchar)'\2',
(uchar)'\2',(uchar)'\2',(uchar)'\2',(uchar)'\2',(uchar)'\2',(uchar)'\2',(uchar)'\2',(uchar)'\2',
(uchar)'\2',(uchar)'\2',(uchar)'\2',(uchar)'\2',(uchar)'\2',(uchar)'\2',(uchar)'\2',(uchar)'\2',
(uchar)'\2',(uchar)'\2',(uchar)'\2',(uchar)'\2',(uchar)'\2',(uchar)'\2',(uchar)'\2',(uchar)'\2',
};

uchar NEAR to_lower_win1251ukr[] = {
'\000','\001','\002','\003','\004','\005','\006','\007',
'\010','\011','\012','\013','\014','\015','\016','\017',
'\020','\021','\022','\023','\024','\025','\026','\027',
'\030','\031','\032','\033','\034','\035','\036','\037',
' ',   '!',   '"',   '#',   '$',   '%',   '&',   '\'',  
'(',   ')',   '*',   '+',   ',',   '-',   '.',   '/',   
'0',   '1',   '2',   '3',   '4',   '5',   '6',   '7',   
'8',   '9',   ':',   ';',   '<',   '=',   '>',   '?',   
'@',   'a',   'b',   'c',   'd',   'e',   'f',   'g',   
'h',   'i',   'j',   'k',   'l',   'm',   'n',   'o',   
'p',   'q',   'r',   's',   't',   'u',   'v',   'w',   
'x',   'y',   'z',   '[',   '\\',  ']',   '^',   '_',   
' ',   'a',   'b',   'c',   'd',   'e',   'f',   'g',   
'h',   'i',   'j',   'k',   'l',   'm',   'n',   'o',   
'p',   'q',   'r',   's',   't',   'u',   'v',   'w',   
'x',   'y',   'z',   '{',   '|',   '}',   '~',   '\177',

(uchar)'\40',(uchar)'\40',(uchar)'\40',(uchar)'\40',(uchar)'\40',(uchar)'\40',(uchar)'\40',(uchar)'\40',
(uchar)'\40',(uchar)'\40',(uchar)'\40',(uchar)'\40',(uchar)'\40',(uchar)'\40',(uchar)'\40',(uchar)'\40',
(uchar)'\40',(uchar)'\40',(uchar)'\40',(uchar)'\40',(uchar)'\40',(uchar)'\40',(uchar)'\40',(uchar)'\40',
(uchar)'\40',(uchar)'\40',(uchar)'\40',(uchar)'\40',(uchar)'\40',(uchar)'\40',(uchar)'\40',(uchar)'\40',
(uchar)'\40',(uchar)'\40',(uchar)'\40',(uchar)'\40',(uchar)'\40',(uchar)'\245',(uchar)'\40',(uchar)'\40',
(uchar)'\250',(uchar)'\40',(uchar)'\252',(uchar)'\40',(uchar)'\40',(uchar)'\40',(uchar)'\40',(uchar)'\257',
(uchar)'\40',(uchar)'\40',(uchar)'\262',(uchar)'\262',(uchar)'\245',(uchar)'\40',(uchar)'\40',(uchar)'\40',
(uchar)'\250',(uchar)'\40',(uchar)'\252',(uchar)'\40',(uchar)'\40',(uchar)'\40',(uchar)'\40',(uchar)'\257',
(uchar)'\300',(uchar)'\301',(uchar)'\302',(uchar)'\303',(uchar)'\304',(uchar)'\305',(uchar)'\306',(uchar)'\307',
(uchar)'\310',(uchar)'\311',(uchar)'\312',(uchar)'\313',(uchar)'\314',(uchar)'\315',(uchar)'\316',(uchar)'\317',
(uchar)'\320',(uchar)'\321',(uchar)'\322',(uchar)'\323',(uchar)'\324',(uchar)'\325',(uchar)'\326',(uchar)'\327',
(uchar)'\330',(uchar)'\331',(uchar)'\332',(uchar)'\333',(uchar)'\334',(uchar)'\335',(uchar)'\336',(uchar)'\337',
(uchar)'\300',(uchar)'\301',(uchar)'\302',(uchar)'\303',(uchar)'\304',(uchar)'\305',(uchar)'\306',(uchar)'\307',
(uchar)'\310',(uchar)'\311',(uchar)'\312',(uchar)'\313',(uchar)'\314',(uchar)'\315',(uchar)'\316',(uchar)'\317',
(uchar)'\320',(uchar)'\321',(uchar)'\322',(uchar)'\323',(uchar)'\324',(uchar)'\325',(uchar)'\326',(uchar)'\327',
(uchar)'\330',(uchar)'\331',(uchar)'\332',(uchar)'\333',(uchar)'\334',(uchar)'\335',(uchar)'\336',(uchar)'\337',
};

uchar NEAR to_upper_win1251ukr[] = {
'\000','\001','\002','\003','\004','\005','\006','\007',
'\010','\011','\012','\013','\014','\015','\016','\017',
'\020','\021','\022','\023','\024','\025','\026','\027',
'\030','\031','\032','\033','\034','\035','\036','\037',
' ',   '!',   '"',   '#',   '$',   '%',   '&',   '\'',  
'(',   ')',   '*',   '+',   ',',   '-',   '.',   '/',   
'0',   '1',   '2',   '3',   '4',   '5',   '6',   '7',   
'8',   '9',   ':',   ';',   '<',   '=',   '>',   '?',   
'@',   'A',   'B',   'C',   'D',   'E',   'F',   'G',   
'H',   'I',   'J',   'K',   'L',   'M',   'N',   'O',   
'P',   'Q',   'R',   'S',   'T',   'U',   'V',   'W',   
'X',   'Y',   'Z',   '[',   '\\',  ']',   '^',   '_',   
' ',   'A',   'B',   'C',   'D',   'E',   'F',   'G',   
'H',   'I',   'J',   'K',   'L',   'M',   'N',   'O',   
'P',   'Q',   'R',   'S',   'T',   'U',   'V',   'W',   
'X',   'Y',   'Z',   '{',   '|',   '}',   '~',   '\177',

(uchar)'\40',(uchar)'\40',(uchar)'\40',(uchar)'\40',(uchar)'\40',(uchar)'\40',(uchar)'\40',(uchar)'\40',
(uchar)'\40',(uchar)'\40',(uchar)'\40',(uchar)'\40',(uchar)'\40',(uchar)'\40',(uchar)'\40',(uchar)'\40',
(uchar)'\40',(uchar)'\40',(uchar)'\40',(uchar)'\40',(uchar)'\40',(uchar)'\40',(uchar)'\40',(uchar)'\40',
(uchar)'\40',(uchar)'\40',(uchar)'\40',(uchar)'\40',(uchar)'\40',(uchar)'\40',(uchar)'\40',(uchar)'\40',
(uchar)'\40',(uchar)'\40',(uchar)'\40',(uchar)'\40',(uchar)'\40',(uchar)'\264',(uchar)'\40',(uchar)'\40',
(uchar)'\270',(uchar)'\40',(uchar)'\272',(uchar)'\40',(uchar)'\40',(uchar)'\40',(uchar)'\40',(uchar)'\277',
(uchar)'\40',(uchar)'\40',(uchar)'\263',(uchar)'\263',(uchar)'\264',(uchar)'\40',(uchar)'\40',(uchar)'\40',
(uchar)'\270',(uchar)'\40',(uchar)'\272',(uchar)'\40',(uchar)'\40',(uchar)'\40',(uchar)'\40',(uchar)'\277',
(uchar)'\340',(uchar)'\341',(uchar)'\342',(uchar)'\343',(uchar)'\344',(uchar)'\345',(uchar)'\346',(uchar)'\347',
(uchar)'\350',(uchar)'\351',(uchar)'\352',(uchar)'\353',(uchar)'\354',(uchar)'\355',(uchar)'\356',(uchar)'\357',
(uchar)'\360',(uchar)'\361',(uchar)'\362',(uchar)'\363',(uchar)'\364',(uchar)'\365',(uchar)'\366',(uchar)'\367',
(uchar)'\370',(uchar)'\371',(uchar)'\372',(uchar)'\373',(uchar)'\374',(uchar)'\375',(uchar)'\376',(uchar)'\377',
(uchar)'\340',(uchar)'\341',(uchar)'\342',(uchar)'\343',(uchar)'\344',(uchar)'\345',(uchar)'\346',(uchar)'\347',
(uchar)'\350',(uchar)'\351',(uchar)'\352',(uchar)'\353',(uchar)'\354',(uchar)'\355',(uchar)'\356',(uchar)'\357',
(uchar)'\360',(uchar)'\361',(uchar)'\362',(uchar)'\363',(uchar)'\364',(uchar)'\365',(uchar)'\366',(uchar)'\367',
(uchar)'\370',(uchar)'\371',(uchar)'\372',(uchar)'\373',(uchar)'\374',(uchar)'\375',(uchar)'\376',(uchar)'\377',
};

uchar NEAR sort_order_win1251ukr[] = {
'\000','\001','\002','\003','\004','\005','\006','\007',
'\010','\011','\012','\013','\014','\015','\016','\017',
'\020','\021','\022','\023','\024','\025','\026','\027',
'\030','\031','\032','\033','\034','\035','\036','\037',
' ',   '!',   '"',   '#',   '$',   '%',   '&',   '\'',  
'(',   ')',   '*',   '+',   ',',   '-',   '.',   '/',   
'0',   '1',   '2',   '3',   '4',   '5',   '6',   '7',   
'8',   '9',   ':',   ';',   '<',   '=',   '>',   '?',   
'@',   'A',   'B',   'C',   'D',   'E',   'F',   'G',   
'H',   'I',   'J',   'K',   'L',   'M',   'N',   'O',   
'P',   'Q',   'R',   'S',   'T',   'U',   'V',   'W',   
'X',   'Y',   'Z',   '[',   '\\',  ']',   '^',   '_',   
' ',   'A',   'B',   'C',   'D',   'E',   'F',   'G',   
'H',   'I',   'J',   'K',   'L',   'M',   'N',   'O',   
'P',   'Q',   'R',   'S',   'T',   'U',   'V',   'W',   
'X',   'Y',   'Z',   '{',   '|',   '}',   '~',   '\177',

(uchar)'\245',(uchar)'\246',(uchar)'\247',(uchar)'\250',(uchar)'\251',(uchar)'\252',(uchar)'\253',(uchar)'\254',
(uchar)'\255',(uchar)'\256',(uchar)'\257',(uchar)'\260',(uchar)'\261',(uchar)'\262',(uchar)'\263',(uchar)'\264',
(uchar)'\265',(uchar)'\266',(uchar)'\267',(uchar)'\270',(uchar)'\271',(uchar)'\272',(uchar)'\273',(uchar)'\274',
(uchar)'\275',(uchar)'\276',(uchar)'\277',(uchar)'\300',(uchar)'\301',(uchar)'\302',(uchar)'\303',(uchar)'\304',
(uchar)'\305',(uchar)'\306',(uchar)'\307',(uchar)'\310',(uchar)'\311',(uchar)'\204',(uchar)'\312',(uchar)'\313',
(uchar)'\210',(uchar)'\314',(uchar)'\207',(uchar)'\315',(uchar)'\316',(uchar)'\317',(uchar)'\320',(uchar)'\215',
(uchar)'\321',(uchar)'\322',(uchar)'\214',(uchar)'\214',(uchar)'\204',(uchar)'\323',(uchar)'\324',(uchar)'\325',
(uchar)'\210',(uchar)'\326',(uchar)'\207',(uchar)'\327',(uchar)'\330',(uchar)'\331',(uchar)'\332',(uchar)'\215',
(uchar)'\200',(uchar)'\201',(uchar)'\202',(uchar)'\203',(uchar)'\205',(uchar)'\206',(uchar)'\211',(uchar)'\212',
(uchar)'\213',(uchar)'\216',(uchar)'\217',(uchar)'\220',(uchar)'\221',(uchar)'\222',(uchar)'\223',(uchar)'\224',
(uchar)'\225',(uchar)'\226',(uchar)'\227',(uchar)'\230',(uchar)'\231',(uchar)'\232',(uchar)'\233',(uchar)'\234',
(uchar)'\235',(uchar)'\236',(uchar)'\237',(uchar)'\240',(uchar)'\241',(uchar)'\242',(uchar)'\243',(uchar)'\244',
(uchar)'\200',(uchar)'\201',(uchar)'\202',(uchar)'\203',(uchar)'\205',(uchar)'\206',(uchar)'\211',(uchar)'\212',
(uchar)'\213',(uchar)'\216',(uchar)'\217',(uchar)'\220',(uchar)'\221',(uchar)'\222',(uchar)'\223',(uchar)'\224',
(uchar)'\225',(uchar)'\226',(uchar)'\227',(uchar)'\230',(uchar)'\231',(uchar)'\232',(uchar)'\233',(uchar)'\234',
(uchar)'\235',(uchar)'\236',(uchar)'\237',(uchar)'\240',(uchar)'\241',(uchar)'\242',(uchar)'\243',(uchar)'\244',

};
