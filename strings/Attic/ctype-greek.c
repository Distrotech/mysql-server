/* SCV: The content of this file is freeware.
        Use it or abuse it. I couldn't care less */

/* This implements the ISO 8859-7 Greek character-set */
/* See the end of this file for a definition of the set */

#include <global.h>
#include "m_string.h"

/* some definitions first */
#define GREEK_TONOS                                             ((uchar)('\264'))	/* 180 */
#define GREEK_DIALYTIKA_TONOS                                   ((uchar)('\265'))	/* 181 */
#define GREEK_CAPITAL_LETTER_ALPHA_WITH_TONOS                   ((uchar)('\266'))	/* 182 */
#define GREEK_CAPITAL_LETTER_EPSILON_WITH_TONOS                 ((uchar)('\270'))	/* 184 */
#define GREEK_CAPITAL_LETTER_ETA_WITH_TONOS                     ((uchar)('\271'))	/* 185 */
#define GREEK_CAPITAL_LETTER_IOTA_WITH_TONOS                    ((uchar)('\272'))	/* 186 */
#define GREEK_CAPITAL_LETTER_OMICRON_WITH_TONOS                 ((uchar)('\274'))	/* 188 */
#define GREEK_CAPITAL_LETTER_UPSILON_WITH_TONOS                 ((uchar)('\276'))	/* 190 */
#define GREEK_CAPITAL_LETTER_OMEGA_WITH_TONOS                   ((uchar)('\277'))	/* 191 */
#define GREEK_SMALL_LETTER_IOTA_WITH_DIALYTIKA_AND_TONOS        ((uchar)('\300'))	/* 192 */
#define GREEK_CAPITAL_LETTER_ALPHA                              ((uchar)('\301'))	/* 193 */
#define GREEK_CAPITAL_LETTER_BETA                               ((uchar)('\302'))	/* 194 */
#define GREEK_CAPITAL_LETTER_GAMMA                              ((uchar)('\303'))	/* 195 */
#define GREEK_CAPITAL_LETTER_DELTA                              ((uchar)('\304'))	/* 196 */
#define GREEK_CAPITAL_LETTER_EPSILON                            ((uchar)('\305'))	/* 197 */
#define GREEK_CAPITAL_LETTER_ZETA                               ((uchar)('\306'))	/* 198 */
#define GREEK_CAPITAL_LETTER_ETA                                ((uchar)('\307'))	/* 199 */
#define GREEK_CAPITAL_LETTER_THETA                              ((uchar)('\310'))	/* 200 */
#define GREEK_CAPITAL_LETTER_IOTA                               ((uchar)('\311'))	/* 201 */
#define GREEK_CAPITAL_LETTER_KAPPA                              ((uchar)('\312'))	/* 202 */
#define GREEK_CAPITAL_LETTER_LAMDA                              ((uchar)('\313'))	/* 203 */
#define GREEK_CAPITAL_LETTER_MU                                 ((uchar)('\314'))	/* 204 */
#define GREEK_CAPITAL_LETTER_NU                                 ((uchar)('\315'))	/* 205 */
#define GREEK_CAPITAL_LETTER_XI                                 ((uchar)('\316'))	/* 206 */
#define GREEK_CAPITAL_LETTER_OMICRON                            ((uchar)('\317'))	/* 207 */
#define GREEK_CAPITAL_LETTER_PI                                 ((uchar)('\320'))	/* 208 */
#define GREEK_CAPITAL_LETTER_RHO                                ((uchar)('\321'))	/* 209 */
#define GREEK_CAPITAL_LETTER_SIGMA                              ((uchar)('\323'))	/* 211 */
#define GREEK_CAPITAL_LETTER_TAU                                ((uchar)('\324'))	/* 212 */
#define GREEK_CAPITAL_LETTER_UPSILON                            ((uchar)('\325'))	/* 213 */
#define GREEK_CAPITAL_LETTER_PHI                                ((uchar)('\326'))	/* 214 */
#define GREEK_CAPITAL_LETTER_CHI                                ((uchar)('\327'))	/* 215 */
#define GREEK_CAPITAL_LETTER_PSI                                ((uchar)('\330'))	/* 216 */
#define GREEK_CAPITAL_LETTER_OMEGA                              ((uchar)('\331'))	/* 217 */
#define GREEK_CAPITAL_LETTER_IOTA_WITH_DIALYTIKA                ((uchar)('\332'))	/* 218 */
#define GREEK_CAPITAL_LETTER_UPSILON_WITH_DIALYTIKA             ((uchar)('\333'))	/* 219 */
#define GREEK_SMALL_LETTER_ALPHA_WITH_TONOS                     ((uchar)('\334'))	/* 220 */
#define GREEK_SMALL_LETTER_EPSILON_WITH_TONOS                   ((uchar)('\335'))	/* 221 */
#define GREEK_SMALL_LETTER_ETA_WITH_TONOS                       ((uchar)('\336'))	/* 222 */
#define GREEK_SMALL_LETTER_IOTA_WITH_TONOS                      ((uchar)('\337'))	/* 223 */
#define GREEK_SMALL_LETTER_UPSILON_WITH_DIALYTIKA_AND_TONOS     ((uchar)('\340'))	/* 224 */
#define GREEK_SMALL_LETTER_ALPHA                                ((uchar)('\341'))	/* 225 */
#define GREEK_SMALL_LETTER_BETA                                 ((uchar)('\342'))	/* 226 */
#define GREEK_SMALL_LETTER_GAMMA                                ((uchar)('\343'))	/* 227 */
#define GREEK_SMALL_LETTER_DELTA                                ((uchar)('\344'))	/* 228 */
#define GREEK_SMALL_LETTER_EPSILON                              ((uchar)('\345'))	/* 229 */
#define GREEK_SMALL_LETTER_ZETA                                 ((uchar)('\346'))	/* 230 */
#define GREEK_SMALL_LETTER_ETA                                  ((uchar)('\347'))	/* 231 */
#define GREEK_SMALL_LETTER_THETA                                ((uchar)('\350'))	/* 232 */
#define GREEK_SMALL_LETTER_IOTA                                 ((uchar)('\351'))	/* 233 */
#define GREEK_SMALL_LETTER_KAPPA                                ((uchar)('\352'))	/* 234 */
#define GREEK_SMALL_LETTER_LAMDA                                ((uchar)('\353'))	/* 235 */
#define GREEK_SMALL_LETTER_MU                                   ((uchar)('\354'))	/* 236 */
#define GREEK_SMALL_LETTER_NU                                   ((uchar)('\355'))	/* 237 */
#define GREEK_SMALL_LETTER_XI                                   ((uchar)('\356'))	/* 238 */
#define GREEK_SMALL_LETTER_OMICRON                              ((uchar)('\357'))	/* 239 */
#define GREEK_SMALL_LETTER_PI                                   ((uchar)('\360'))	/* 240 */
#define GREEK_SMALL_LETTER_RHO                                  ((uchar)('\361'))	/* 241 */
#define GREEK_SMALL_LETTER_FINAL_SIGMA                          ((uchar)('\362'))	/* 242 */
#define GREEK_SMALL_LETTER_SIGMA                                ((uchar)('\363'))	/* 243 */
#define GREEK_SMALL_LETTER_TAU                                  ((uchar)('\364'))	/* 244 */
#define GREEK_SMALL_LETTER_UPSILON                              ((uchar)('\365'))	/* 245 */
#define GREEK_SMALL_LETTER_PHI                                  ((uchar)('\366'))	/* 246 */
#define GREEK_SMALL_LETTER_CHI                                  ((uchar)('\367'))	/* 247 */
#define GREEK_SMALL_LETTER_PSI                                  ((uchar)('\370'))	/* 248 */
#define GREEK_SMALL_LETTER_OMEGA                                ((uchar)('\371'))	/* 249 */
#define GREEK_SMALL_LETTER_IOTA_WITH_DIALYTIKA                  ((uchar)('\372'))	/* 250 */
#define GREEK_SMALL_LETTER_UPSILON_WITH_DIALYTIKA               ((uchar)('\373'))	/* 251 */
#define GREEK_SMALL_LETTER_OMICRON_WITH_TONOS                   ((uchar)('\374'))	/* 252 */
#define GREEK_SMALL_LETTER_UPSILON_WITH_TONOS                   ((uchar)('\375'))	/* 253 */
#define GREEK_SMALL_LETTER_OMEGA_WITH_TONOS                     ((uchar)('\376'))	/* 254 */

uchar NEAR ctype_greek[257] = {
0,
32,32,32,32,32,32,32,32,32,40,40,40,40,40,32,32,
32,32,32,32,32,32,32,32,32,32,32,32,32,32,32,32,
72,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,
132,132,132,132,132,132,132,132,132,132,16,16,16,16,16,16,
16,129,129,129,129,129,129,1,1,1,1,1,1,1,1,1,
1,1,1,1,1,1,1,1,1,1,1,16,16,16,16,16,
16,130,130,130,130,130,130,2,2,2,2,2,2,2,2,2,
2,2,2,2,2,2,2,2,2,2,2,16,16,16,16,32,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
					/* 0 - 160 are the same as latin-1 */
/* 160 */ 010 + 0100,
/* 161 */ 020,
/* 162 */ 020,
/* 163 */ 020,
/* 164 */ 0,
/* 165 */ 0,
/* 166 */ 020,
/* 167 */ 020,
/* 168 */ 020,
/* 169 */ 020,
/* 170 */ 0,
/* 171 */ 020,
/* 172 */ 020,
/* 173 */ 020,
/* 174 */ 0,
/* 175 */ 020,
/* 176 */ 020,
/* 177 */ 020,
/* 178 */ 020,
/* 179 */ 020,
/* 180 */ 020,
/* 181 */ 020,
/* 182 */ 01,
/* 183 */ 020,
/* 184 */ 01,
/* 185 */ 01,
/* 186 */ 01,
/* 187 */ 020,
/* 188 */ 01,
/* 189 */ 020,
/* 190 */ 01,
/* 191 */ 01,
/* 192 */ 02,
/* 193 */ 01,
/* 194 */ 01,
/* 195 */ 01,
/* 196 */ 01,
/* 197 */ 01,
/* 198 */ 01,
/* 199 */ 01,
/* 200 */ 01,
/* 201 */ 01,
/* 202 */ 01,
/* 203 */ 01,
/* 204 */ 01,
/* 205 */ 01,
/* 206 */ 01,
/* 207 */ 01,
/* 208 */ 01,
/* 209 */ 01,
/* 210 */ 0,
/* 211 */ 01,
/* 212 */ 01,
/* 213 */ 01,
/* 214 */ 01,
/* 215 */ 01,
/* 216 */ 01,
/* 217 */ 01,
/* 218 */ 01,
/* 219 */ 01,
/* 220 */ 02,
/* 221 */ 02,
/* 222 */ 02,
/* 223 */ 02,
/* 224 */ 02,
/* 225 */ 02,
/* 226 */ 02,
/* 227 */ 02,
/* 228 */ 02,
/* 229 */ 02,
/* 230 */ 02,
/* 231 */ 02,
/* 232 */ 02,
/* 233 */ 02,
/* 234 */ 02,
/* 235 */ 02,
/* 236 */ 02,
/* 237 */ 02,
/* 238 */ 02,
/* 239 */ 02,
/* 240 */ 02,
/* 241 */ 02,
/* 242 */ 02,
/* 243 */ 02,
/* 244 */ 02,
/* 245 */ 02,
/* 246 */ 02,
/* 247 */ 02,
/* 248 */ 02,
/* 249 */ 02,
/* 250 */ 02,
/* 251 */ 02,
/* 252 */ 02,
/* 253 */ 02,
/* 254 */ 02,
/* 255 */ 0
};

uchar NEAR to_lower_greek[]={
'\000','\001','\002','\003','\004','\005','\006','\007',
'\010','\011','\012','\013','\014','\015','\016','\017',
'\020','\021','\022','\023','\024','\025','\026','\027',
'\030','\031','\032','\033','\034','\035','\036','\037',
' ',   '!',   '"',   '#',   '$',   '%',	  '&',	 '\'',
'(',   ')',   '*',   '+',   ',',   '-',	  '.',	 '/',
'0',   '1',   '2',   '3',   '4',   '5',	  '6',	 '7',
'8',   '9',   ':',   ';',   '<',   '=',	  '>',	 '?',
'@',   'a',   'b',   'c',   'd',   'e',	  'f',	 'g',
'h',   'i',   'j',   'k',   'l',   'm',	  'n',	 'o',
'p',   'q',   'r',   's',   't',   'u',	  'v',	 'w',
'x',   'y',   'z',   '[',   '\\',  ']',	  '^',	 '_',
'`',   'a',   'b',   'c',   'd',   'e',	  'f',	 'g',
'h',   'i',   'j',   'k',   'l',   'm',	  'n',	 'o',
'p',   'q',   'r',   's',   't',   'u',	  'v',	 'w',
'x',   'y',   'z',   '{',   '|',   '}',	  '~',	 '\177',

/* 128 */	(uchar)('\200'),
/* 129 */	(uchar)('\201'),
/* 130 */	(uchar)('\202'),
/* 131 */	(uchar)('\203'),
/* 132 */	(uchar)('\204'),
/* 133 */	(uchar)('\205'),
/* 134 */	(uchar)('\206'),
/* 135 */	(uchar)('\207'),
/* 136 */	(uchar)('\210'),
/* 137 */	(uchar)('\211'),
/* 138 */	(uchar)('\212'),
/* 139 */	(uchar)('\213'),
/* 140 */	(uchar)('\214'),
/* 141 */	(uchar)('\215'),
/* 142 */	(uchar)('\216'),
/* 143 */	(uchar)('\217'),
/* 144 */	(uchar)('\220'),
/* 145 */	(uchar)('\221'),
/* 146 */	(uchar)('\222'),
/* 147 */	(uchar)('\223'),
/* 148 */	(uchar)('\224'),
/* 149 */	(uchar)('\225'),
/* 150 */	(uchar)('\226'),
/* 151 */	(uchar)('\227'),
/* 152 */	(uchar)('\230'),
/* 153 */	(uchar)('\231'),
/* 154 */	(uchar)('\232'),
/* 155 */	(uchar)('\233'),
/* 156 */	(uchar)('\234'),
/* 157 */	(uchar)('\235'),
/* 158 */	(uchar)('\236'),
/* 159 */	(uchar)('\237'),
/* 160 */	(uchar)('\240'),
/* 161 */	(uchar)('\241'),
/* 162 */	(uchar)('\242'),
/* 163 */	(uchar)('\243'),
/* 164 */	(uchar)('\244'),
/* 165 */	(uchar)('\245'),
/* 166 */	(uchar)('\246'),
/* 167 */	(uchar)('\247'),
/* 168 */	(uchar)('\250'),
/* 169 */	(uchar)('\251'),
/* 170 */	(uchar)('\252'),
/* 171 */	(uchar)('\253'),
/* 172 */	(uchar)('\254'),
/* 173 */	(uchar)('\255'),
/* 174 */	(uchar)('\256'),
/* 175 */	(uchar)('\257'),
/* 176 */	(uchar)('\260'),
/* 177 */	(uchar)('\261'),
/* 178 */	(uchar)('\262'),
/* 179 */	(uchar)('\263'),
/* 180 */	GREEK_TONOS,
/* 181 */	GREEK_DIALYTIKA_TONOS,
/* 182 */	GREEK_SMALL_LETTER_ALPHA_WITH_TONOS,
/* 183 */	(uchar)('\267'),
/* 184 */	GREEK_SMALL_LETTER_EPSILON_WITH_TONOS,
/* 185 */	GREEK_SMALL_LETTER_ETA_WITH_TONOS,
/* 186 */	GREEK_SMALL_LETTER_IOTA_WITH_TONOS,
/* 187 */	(uchar)('\273'),
/* 188 */	GREEK_SMALL_LETTER_OMICRON_WITH_TONOS,
/* 189 */	(uchar)('\275'),
/* 190 */	GREEK_SMALL_LETTER_UPSILON_WITH_TONOS,
/* 191 */	GREEK_SMALL_LETTER_OMEGA_WITH_TONOS,
/* 192 */	GREEK_SMALL_LETTER_IOTA_WITH_DIALYTIKA_AND_TONOS,
/* 193 */	GREEK_SMALL_LETTER_ALPHA,
/* 194 */	GREEK_SMALL_LETTER_BETA,
/* 195 */	GREEK_SMALL_LETTER_GAMMA,
/* 196 */	GREEK_SMALL_LETTER_DELTA,
/* 197 */	GREEK_SMALL_LETTER_EPSILON,
/* 198 */	GREEK_SMALL_LETTER_ZETA,
/* 199 */	GREEK_SMALL_LETTER_ETA,
/* 200 */	GREEK_SMALL_LETTER_THETA,
/* 201 */	GREEK_SMALL_LETTER_IOTA,
/* 202 */	GREEK_SMALL_LETTER_KAPPA,
/* 203 */	GREEK_SMALL_LETTER_LAMDA,
/* 204 */	GREEK_SMALL_LETTER_MU,
/* 205 */	GREEK_SMALL_LETTER_NU,
/* 206 */	GREEK_SMALL_LETTER_XI,
/* 207 */	GREEK_SMALL_LETTER_OMICRON,
/* 208 */	GREEK_SMALL_LETTER_PI,
/* 209 */	GREEK_SMALL_LETTER_RHO,
/* 210 */	(uchar)('\322'),
/* 211 */	GREEK_SMALL_LETTER_SIGMA,
/* 212 */	GREEK_SMALL_LETTER_TAU,
/* 213 */	GREEK_SMALL_LETTER_UPSILON,
/* 214 */	GREEK_SMALL_LETTER_PHI,
/* 215 */	GREEK_SMALL_LETTER_CHI,
/* 216 */	GREEK_SMALL_LETTER_PSI,
/* 217 */	GREEK_SMALL_LETTER_OMEGA,
/* 218 */	GREEK_SMALL_LETTER_IOTA_WITH_DIALYTIKA,
/* 219 */	GREEK_SMALL_LETTER_UPSILON_WITH_DIALYTIKA,
/* 220 */	GREEK_SMALL_LETTER_ALPHA_WITH_TONOS,
/* 221 */	GREEK_SMALL_LETTER_EPSILON_WITH_TONOS,
/* 222 */	GREEK_SMALL_LETTER_ETA_WITH_TONOS,
/* 223 */	GREEK_SMALL_LETTER_IOTA_WITH_TONOS,
/* 224 */	GREEK_SMALL_LETTER_UPSILON_WITH_DIALYTIKA_AND_TONOS,
/* 225 */	GREEK_SMALL_LETTER_ALPHA,
/* 226 */	GREEK_SMALL_LETTER_BETA,
/* 227 */	GREEK_SMALL_LETTER_GAMMA,
/* 228 */	GREEK_SMALL_LETTER_DELTA,
/* 229 */	GREEK_SMALL_LETTER_EPSILON,
/* 230 */	GREEK_SMALL_LETTER_ZETA,
/* 231 */	GREEK_SMALL_LETTER_ETA,
/* 232 */	GREEK_SMALL_LETTER_THETA,
/* 233 */	GREEK_SMALL_LETTER_IOTA,
/* 234 */	GREEK_SMALL_LETTER_KAPPA,
/* 235 */	GREEK_SMALL_LETTER_LAMDA,
/* 236 */	GREEK_SMALL_LETTER_MU,
/* 237 */	GREEK_SMALL_LETTER_NU,
/* 238 */	GREEK_SMALL_LETTER_XI,
/* 239 */	GREEK_SMALL_LETTER_OMICRON,
/* 240 */	GREEK_SMALL_LETTER_PI,
/* 241 */	GREEK_SMALL_LETTER_RHO,
/* 242 */	GREEK_SMALL_LETTER_FINAL_SIGMA,
/* 243 */	GREEK_SMALL_LETTER_SIGMA,
/* 244 */	GREEK_SMALL_LETTER_TAU,
/* 245 */	GREEK_SMALL_LETTER_UPSILON,
/* 246 */	GREEK_SMALL_LETTER_PHI,
/* 247 */	GREEK_SMALL_LETTER_CHI,
/* 248 */	GREEK_SMALL_LETTER_PSI,
/* 249 */	GREEK_SMALL_LETTER_OMEGA,
/* 250 */	GREEK_SMALL_LETTER_IOTA_WITH_DIALYTIKA,
/* 251 */	GREEK_SMALL_LETTER_UPSILON_WITH_DIALYTIKA,
/* 252 */	GREEK_SMALL_LETTER_OMICRON_WITH_TONOS,
/* 253 */	GREEK_SMALL_LETTER_UPSILON_WITH_TONOS,
/* 254 */	GREEK_SMALL_LETTER_OMEGA_WITH_TONOS,
/* 255 */	(uchar)('\377')
};

uchar NEAR to_upper_greek[]={
'\000','\001','\002','\003','\004','\005','\006','\007',
'\010','\011','\012','\013','\014','\015','\016','\017',
'\020','\021','\022','\023','\024','\025','\026','\027',
'\030','\031','\032','\033','\034','\035','\036','\037',
' ',   '!',   '"',   '#',   '$',   '%',	  '&',	 '\'',
'(',   ')',   '*',   '+',   ',',   '-',	  '.',	 '/',
'0',   '1',   '2',   '3',   '4',   '5',	  '6',	 '7',
'8',   '9',   ':',   ';',   '<',   '=',	  '>',	 '?',
'@',   'A',   'B',   'C',   'D',   'E',	  'F',	 'G',
'H',   'I',   'J',   'K',   'L',   'M',	  'N',	 'O',
'P',   'Q',   'R',   'S',   'T',   'U',	  'V',	 'W',
'X',   'Y',   'Z',   '[',   '\\',  ']',	  '^',	 '_',
'`',   'A',   'B',   'C',   'D',   'E',	  'F',	 'G',
'H',   'I',   'J',   'K',   'L',   'M',	  'N',	 'O',
'P',   'Q',   'R',   'S',   'T',   'U',	  'V',	 'W',
'X',   'Y',   'Z',   '{',   '|',   '}',	  '~',	 '\177',

/* 128 */	(uchar)('\200'),
/* 129 */	(uchar)('\201'),
/* 130 */	(uchar)('\202'),
/* 131 */	(uchar)('\203'),
/* 132 */	(uchar)('\204'),
/* 133 */	(uchar)('\205'),
/* 134 */	(uchar)('\206'),
/* 135 */	(uchar)('\207'),
/* 136 */	(uchar)('\210'),
/* 137 */	(uchar)('\211'),
/* 138 */	(uchar)('\212'),
/* 139 */	(uchar)('\213'),
/* 140 */	(uchar)('\214'),
/* 141 */	(uchar)('\215'),
/* 142 */	(uchar)('\216'),
/* 143 */	(uchar)('\217'),
/* 144 */	(uchar)('\220'),
/* 145 */	(uchar)('\221'),
/* 146 */	(uchar)('\222'),
/* 147 */	(uchar)('\223'),
/* 148 */	(uchar)('\224'),
/* 149 */	(uchar)('\225'),
/* 150 */	(uchar)('\226'),
/* 151 */	(uchar)('\227'),
/* 152 */	(uchar)('\230'),
/* 153 */	(uchar)('\231'),
/* 154 */	(uchar)('\232'),
/* 155 */	(uchar)('\233'),
/* 156 */	(uchar)('\234'),
/* 157 */	(uchar)('\235'),
/* 158 */	(uchar)('\236'),
/* 159 */	(uchar)('\237'),
/* 160 */	(uchar)('\240'),
/* 161 */	(uchar)('\241'),
/* 162 */	(uchar)('\242'),
/* 163 */	(uchar)('\243'),
/* 164 */	(uchar)('\244'),
/* 165 */	(uchar)('\245'),
/* 166 */	(uchar)('\246'),
/* 167 */	(uchar)('\247'),
/* 168 */	(uchar)('\250'),
/* 169 */	(uchar)('\251'),
/* 170 */	(uchar)('\252'),
/* 171 */	(uchar)('\253'),
/* 172 */	(uchar)('\254'),
/* 173 */	(uchar)('\255'),
/* 174 */	(uchar)('\256'),
/* 175 */	(uchar)('\257'),
/* 176 */	(uchar)('\260'),
/* 177 */	(uchar)('\261'),
/* 178 */	(uchar)('\262'),
/* 179 */	(uchar)('\263'),
/* 180 */	GREEK_TONOS,
/* 181 */	GREEK_DIALYTIKA_TONOS,
/* 182 */	GREEK_CAPITAL_LETTER_ALPHA_WITH_TONOS,
/* 183 */	(uchar)('\267'),
/* 184 */	GREEK_CAPITAL_LETTER_EPSILON_WITH_TONOS,
/* 185 */	GREEK_CAPITAL_LETTER_ETA_WITH_TONOS,
/* 186 */	GREEK_CAPITAL_LETTER_IOTA_WITH_TONOS,
/* 187 */	(uchar)('\273'),
/* 188 */	GREEK_CAPITAL_LETTER_OMICRON_WITH_TONOS,
/* 189 */	(uchar)('\275'),
/* 190 */	GREEK_CAPITAL_LETTER_UPSILON_WITH_TONOS,
/* 191 */	GREEK_CAPITAL_LETTER_OMEGA_WITH_TONOS,
/* 192 */	GREEK_CAPITAL_LETTER_IOTA_WITH_DIALYTIKA,
/* 193 */	GREEK_CAPITAL_LETTER_ALPHA,
/* 194 */	GREEK_CAPITAL_LETTER_BETA,
/* 195 */	GREEK_CAPITAL_LETTER_GAMMA,
/* 196 */	GREEK_CAPITAL_LETTER_DELTA,
/* 197 */	GREEK_CAPITAL_LETTER_EPSILON,
/* 198 */	GREEK_CAPITAL_LETTER_ZETA,
/* 199 */	GREEK_CAPITAL_LETTER_ETA,
/* 200 */	GREEK_CAPITAL_LETTER_THETA,
/* 201 */	GREEK_CAPITAL_LETTER_IOTA,
/* 202 */	GREEK_CAPITAL_LETTER_KAPPA,
/* 203 */	GREEK_CAPITAL_LETTER_LAMDA,
/* 204 */	GREEK_CAPITAL_LETTER_MU,
/* 205 */	GREEK_CAPITAL_LETTER_NU,
/* 206 */	GREEK_CAPITAL_LETTER_XI,
/* 207 */	GREEK_CAPITAL_LETTER_OMICRON,
/* 208 */	GREEK_CAPITAL_LETTER_PI,
/* 209 */	GREEK_CAPITAL_LETTER_RHO,
/* 210 */	(uchar)('\322'),
/* 211 */	GREEK_CAPITAL_LETTER_SIGMA,
/* 212 */	GREEK_CAPITAL_LETTER_TAU,
/* 213 */	GREEK_CAPITAL_LETTER_UPSILON,
/* 214 */	GREEK_CAPITAL_LETTER_PHI,
/* 215 */	GREEK_CAPITAL_LETTER_CHI,
/* 216 */	GREEK_CAPITAL_LETTER_PSI,
/* 217 */	GREEK_CAPITAL_LETTER_OMEGA,
/* 218 */	GREEK_CAPITAL_LETTER_IOTA_WITH_DIALYTIKA,
/* 219 */	GREEK_CAPITAL_LETTER_UPSILON_WITH_DIALYTIKA,
/* 220 */	GREEK_CAPITAL_LETTER_ALPHA,
/* 221 */	GREEK_CAPITAL_LETTER_EPSILON,
/* 222 */	GREEK_CAPITAL_LETTER_ETA,
/* 223 */	GREEK_CAPITAL_LETTER_IOTA,
/* 224 */	GREEK_CAPITAL_LETTER_UPSILON_WITH_DIALYTIKA,
/* 225 */	GREEK_CAPITAL_LETTER_ALPHA,
/* 226 */	GREEK_CAPITAL_LETTER_BETA,
/* 227 */	GREEK_CAPITAL_LETTER_GAMMA,
/* 228 */	GREEK_CAPITAL_LETTER_DELTA,
/* 229 */	GREEK_CAPITAL_LETTER_EPSILON,
/* 230 */	GREEK_CAPITAL_LETTER_ZETA,
/* 231 */	GREEK_CAPITAL_LETTER_ETA,
/* 232 */	GREEK_CAPITAL_LETTER_THETA,
/* 233 */	GREEK_CAPITAL_LETTER_IOTA,
/* 234 */	GREEK_CAPITAL_LETTER_KAPPA,
/* 235 */	GREEK_CAPITAL_LETTER_LAMDA,
/* 236 */	GREEK_CAPITAL_LETTER_MU,
/* 237 */	GREEK_CAPITAL_LETTER_NU,
/* 238 */	GREEK_CAPITAL_LETTER_XI,
/* 239 */	GREEK_CAPITAL_LETTER_OMICRON,
/* 240 */	GREEK_CAPITAL_LETTER_PI,
/* 241 */	GREEK_CAPITAL_LETTER_RHO,
/* 242 */	GREEK_CAPITAL_LETTER_SIGMA,
/* 243 */	GREEK_CAPITAL_LETTER_SIGMA,
/* 244 */	GREEK_CAPITAL_LETTER_TAU,
/* 245 */	GREEK_CAPITAL_LETTER_UPSILON,
/* 246 */	GREEK_CAPITAL_LETTER_PHI,
/* 247 */	GREEK_CAPITAL_LETTER_CHI,
/* 248 */	GREEK_CAPITAL_LETTER_PSI,
/* 249 */	GREEK_CAPITAL_LETTER_OMEGA,
/* 250 */	GREEK_CAPITAL_LETTER_IOTA_WITH_DIALYTIKA,
/* 251 */	GREEK_CAPITAL_LETTER_UPSILON_WITH_DIALYTIKA,
/* 252 */	GREEK_CAPITAL_LETTER_OMICRON,
/* 253 */	GREEK_CAPITAL_LETTER_UPSILON,
/* 254 */	GREEK_CAPITAL_LETTER_OMEGA,
/* 255 */	(uchar)('\377')
};

uchar NEAR sort_order_greek[]={
'\000','\001','\002','\003','\004','\005','\006','\007',
'\010','\011','\012','\013','\014','\015','\016','\017',
'\020','\021','\022','\023','\024','\025','\026','\027',
'\030','\031','\032','\033','\034','\035','\036','\037',
' ',   '!',   '"',   '#',   '$',   '%',	  '&',	 '\'',
'(',   ')',   '*',   '+',   ',',   '-',	  '.',	 '/',
'0',   '1',   '2',   '3',   '4',   '5',	  '6',	 '7',
'8',   '9',   ':',   ';',   '<',   '=',	  '>',	 '?',
'@',   'A',   'B',   'C',   'D',   'E',	  'F',	 'G',
'H',   'I',   'J',   'K',   'L',   'M',	  'N',	 'O',
'P',   'Q',   'R',   'S',   'T',   'U',	  'V',	 'W',
'X',   'Y',   'Z',   '[',   '\\',  ']',	  '^',	 '_',
'`',   'A',   'B',   'C',   'D',   'E',	  'F',	 'G',
'H',   'I',   'J',   'K',   'L',   'M',	  'N',	 'O',
'P',   'Q',   'R',   'S',   'T',   'U',	  'V',	 'W',
'X',   'Y',   'Z',   '{',   '|',   '}',	  '~',	 '\177',

(uchar) '\200',(uchar) '\201',(uchar) '\202',(uchar) '\203',(uchar) '\204',(uchar) '\205',(uchar) '\206',(uchar) '\207',
(uchar) '\210',(uchar) '\211',(uchar) '\212',(uchar) '\213',(uchar) '\214',(uchar) '\215',(uchar) '\216',(uchar) '\217',
(uchar) '\220',(uchar) '\221',(uchar) '\222',(uchar) '\223',(uchar) '\224',(uchar) '\225',(uchar) '\226',(uchar) '\227',
(uchar) '\230',(uchar) '\231',(uchar) '\232',(uchar) '\233',(uchar) '\234',(uchar) '\235',(uchar) '\236',(uchar) '\237',
(uchar) '\240',(uchar) '\241',(uchar) '\242',(uchar) '\243',(uchar) '\244',(uchar) '\245',(uchar) '\246',(uchar) '\247',
(uchar) '\250',(uchar) '\251',(uchar) '\252',(uchar) '\253',(uchar) '\254',(uchar) '\255',(uchar) '\256',(uchar) '\257',
(uchar) '\260',(uchar) '\261',(uchar) '\262',(uchar) '\263',
					/* 0 - 180 are the same as latin-1 */
/* 180 */	GREEK_TONOS,
/* 181 */	GREEK_DIALYTIKA_TONOS,
/* 182 */	GREEK_CAPITAL_LETTER_ALPHA,
/* 183 */	(uchar)('\267'),
/* 184 */	GREEK_CAPITAL_LETTER_EPSILON,
/* 185 */	GREEK_CAPITAL_LETTER_ETA,
/* 186 */	GREEK_CAPITAL_LETTER_IOTA,
/* 187 */	(uchar)('\273'),
/* 188 */	GREEK_CAPITAL_LETTER_OMICRON,
/* 189 */	(uchar)('\275'),
/* 190 */	GREEK_CAPITAL_LETTER_UPSILON,
/* 191 */	GREEK_CAPITAL_LETTER_OMEGA,
/* 192 */	GREEK_CAPITAL_LETTER_IOTA,
/* 193 */	GREEK_CAPITAL_LETTER_ALPHA,
/* 194 */	GREEK_CAPITAL_LETTER_BETA,
/* 195 */	GREEK_CAPITAL_LETTER_GAMMA,
/* 196 */	GREEK_CAPITAL_LETTER_DELTA,
/* 197 */	GREEK_CAPITAL_LETTER_EPSILON,
/* 198 */	GREEK_CAPITAL_LETTER_ZETA,
/* 199 */	GREEK_CAPITAL_LETTER_ETA,
/* 200 */	GREEK_CAPITAL_LETTER_THETA,
/* 201 */	GREEK_CAPITAL_LETTER_IOTA,
/* 202 */	GREEK_CAPITAL_LETTER_KAPPA,
/* 203 */	GREEK_CAPITAL_LETTER_LAMDA,
/* 204 */	GREEK_CAPITAL_LETTER_MU,
/* 205 */	GREEK_CAPITAL_LETTER_NU,
/* 206 */	GREEK_CAPITAL_LETTER_XI,
/* 207 */	GREEK_CAPITAL_LETTER_OMICRON,
/* 208 */	GREEK_CAPITAL_LETTER_PI,
/* 209 */	GREEK_CAPITAL_LETTER_RHO,
/* 210 */	(uchar)('\322'),
/* 211 */	GREEK_CAPITAL_LETTER_SIGMA,
/* 212 */	GREEK_CAPITAL_LETTER_TAU,
/* 213 */	GREEK_CAPITAL_LETTER_UPSILON,
/* 214 */	GREEK_CAPITAL_LETTER_PHI,
/* 215 */	GREEK_CAPITAL_LETTER_CHI,
/* 216 */	GREEK_CAPITAL_LETTER_PSI,
/* 217 */	GREEK_CAPITAL_LETTER_OMEGA,
/* 218 */	GREEK_CAPITAL_LETTER_IOTA,
/* 219 */	GREEK_CAPITAL_LETTER_UPSILON,
/* 220 */	GREEK_CAPITAL_LETTER_ALPHA,
/* 221 */	GREEK_CAPITAL_LETTER_EPSILON,
/* 222 */	GREEK_CAPITAL_LETTER_ETA,
/* 223 */	GREEK_CAPITAL_LETTER_IOTA,
/* 224 */	GREEK_CAPITAL_LETTER_UPSILON,
/* 225 */	GREEK_CAPITAL_LETTER_ALPHA,
/* 226 */	GREEK_CAPITAL_LETTER_BETA,
/* 227 */	GREEK_CAPITAL_LETTER_GAMMA,
/* 228 */	GREEK_CAPITAL_LETTER_DELTA,
/* 229 */	GREEK_CAPITAL_LETTER_EPSILON,
/* 230 */	GREEK_CAPITAL_LETTER_ZETA,
/* 231 */	GREEK_CAPITAL_LETTER_ETA,
/* 232 */	GREEK_CAPITAL_LETTER_THETA,
/* 233 */	GREEK_CAPITAL_LETTER_IOTA,
/* 234 */	GREEK_CAPITAL_LETTER_KAPPA,
/* 235 */	GREEK_CAPITAL_LETTER_LAMDA,
/* 236 */	GREEK_CAPITAL_LETTER_MU,
/* 237 */	GREEK_CAPITAL_LETTER_NU,
/* 238 */	GREEK_CAPITAL_LETTER_XI,
/* 239 */	GREEK_CAPITAL_LETTER_OMICRON,
/* 240 */	GREEK_CAPITAL_LETTER_PI,
/* 241 */	GREEK_CAPITAL_LETTER_RHO,
/* 242 */	GREEK_CAPITAL_LETTER_SIGMA,
/* 243 */	GREEK_CAPITAL_LETTER_SIGMA,
/* 244 */	GREEK_CAPITAL_LETTER_TAU,
/* 245 */	GREEK_CAPITAL_LETTER_UPSILON,
/* 246 */	GREEK_CAPITAL_LETTER_PHI,
/* 247 */	GREEK_CAPITAL_LETTER_CHI,
/* 248 */	GREEK_CAPITAL_LETTER_PSI,
/* 249 */	GREEK_CAPITAL_LETTER_OMEGA,
/* 250 */	GREEK_CAPITAL_LETTER_IOTA,
/* 251 */	GREEK_CAPITAL_LETTER_UPSILON,
/* 252 */	GREEK_CAPITAL_LETTER_OMICRON,
/* 253 */	GREEK_CAPITAL_LETTER_UPSILON,
/* 254 */	GREEK_CAPITAL_LETTER_OMEGA,
(uchar) '\377'
};

/* let's clean after ourselves */
#undef GREEK_TONOS
#undef GREEK_DIALYTIKA_TONOS
#undef GREEK_CAPITAL_LETTER_ALPHA_WITH_TONOS
#undef GREEK_CAPITAL_LETTER_EPSILON_WITH_TONOS
#undef GREEK_CAPITAL_LETTER_ETA_WITH_TONOS
#undef GREEK_CAPITAL_LETTER_IOTA_WITH_TONOS
#undef GREEK_CAPITAL_LETTER_OMICRON_WITH_TONOS
#undef GREEK_CAPITAL_LETTER_UPSILON_WITH_TONOS
#undef GREEK_CAPITAL_LETTER_OMEGA_WITH_TONOS
#undef GREEK_SMALL_LETTER_IOTA_WITH_DIALYTIKA_AND_TONOS
#undef GREEK_CAPITAL_LETTER_ALPHA
#undef GREEK_CAPITAL_LETTER_BETA
#undef GREEK_CAPITAL_LETTER_GAMMA
#undef GREEK_CAPITAL_LETTER_DELTA
#undef GREEK_CAPITAL_LETTER_EPSILON
#undef GREEK_CAPITAL_LETTER_ZETA
#undef GREEK_CAPITAL_LETTER_ETA
#undef GREEK_CAPITAL_LETTER_THETA
#undef GREEK_CAPITAL_LETTER_IOTA
#undef GREEK_CAPITAL_LETTER_KAPPA
#undef GREEK_CAPITAL_LETTER_LAMDA
#undef GREEK_CAPITAL_LETTER_MU
#undef GREEK_CAPITAL_LETTER_NU
#undef GREEK_CAPITAL_LETTER_XI
#undef GREEK_CAPITAL_LETTER_OMICRON
#undef GREEK_CAPITAL_LETTER_PI
#undef GREEK_CAPITAL_LETTER_RHO
#undef GREEK_CAPITAL_LETTER_SIGMA
#undef GREEK_CAPITAL_LETTER_TAU
#undef GREEK_CAPITAL_LETTER_UPSILON
#undef GREEK_CAPITAL_LETTER_PHI
#undef GREEK_CAPITAL_LETTER_CHI
#undef GREEK_CAPITAL_LETTER_PSI
#undef GREEK_CAPITAL_LETTER_OMEGA
#undef GREEK_CAPITAL_LETTER_IOTA_WITH_DIALYTIKA
#undef GREEK_CAPITAL_LETTER_UPSILON_WITH_DIALYTIKA
#undef GREEK_SMALL_LETTER_ALPHA_WITH_TONOS
#undef GREEK_SMALL_LETTER_EPSILON_WITH_TONOS
#undef GREEK_SMALL_LETTER_ETA_WITH_TONOS
#undef GREEK_SMALL_LETTER_IOTA_WITH_TONOS
#undef GREEK_SMALL_LETTER_UPSILON_WITH_DIALYTIKA_AND_TONOS     ((uchar)('\340'))	/* 224 */
#undef GREEK_SMALL_LETTER_ALPHA
#undef GREEK_SMALL_LETTER_BETA
#undef GREEK_SMALL_LETTER_GAMMA
#undef GREEK_SMALL_LETTER_DELTA
#undef GREEK_SMALL_LETTER_EPSILON
#undef GREEK_SMALL_LETTER_ZETA
#undef GREEK_SMALL_LETTER_ETA
#undef GREEK_SMALL_LETTER_THETA
#undef GREEK_SMALL_LETTER_IOTA
#undef GREEK_SMALL_LETTER_KAPPA
#undef GREEK_SMALL_LETTER_LAMDA
#undef GREEK_SMALL_LETTER_MU
#undef GREEK_SMALL_LETTER_NU
#undef GREEK_SMALL_LETTER_XI
#undef GREEK_SMALL_LETTER_OMICRON
#undef GREEK_SMALL_LETTER_PI
#undef GREEK_SMALL_LETTER_RHO
#undef GREEK_SMALL_LETTER_FINAL_SIGMA
#undef GREEK_SMALL_LETTER_SIGMA
#undef GREEK_SMALL_LETTER_TAU
#undef GREEK_SMALL_LETTER_UPSILON
#undef GREEK_SMALL_LETTER_PHI
#undef GREEK_SMALL_LETTER_CHI
#undef GREEK_SMALL_LETTER_PSI
#undef GREEK_SMALL_LETTER_OMEGA
#undef GREEK_SMALL_LETTER_IOTA_WITH_DIALYTIKA
#undef GREEK_SMALL_LETTER_UPSILON_WITH_DIALYTIKA
#undef GREEK_SMALL_LETTER_OMICRON_WITH_TONOS
#undef GREEK_SMALL_LETTER_UPSILON_WITH_TONOS
#undef GREEK_SMALL_LETTER_OMEGA_WITH_TONOS

#if 0
ISO 8859-7 (Latin/Greek Alphabet)
Dec Hex ISO/IEC 10646-1:1993(E) Character Name
 32 20	SPACE
 33 21	EXCLAMATION MARK
 34 22	QUOTATION MARK
 35 23	NUMBER SIGN
 36 24	DOLLAR SIGN
 37 25	PERCENT SIGN
 38 26	AMPERSAND
 39 27	APOSTROPHE
 40 28	LEFT PARENTHESIS
 41 29	RIGHT PARENTHESIS
 42 2A	ASTERISK
 43 2B	PLUS SIGN
 44 2C	COMMA
 45 2D	HYPHEN-MINUS
 46 2E	FULL STOP
 47 2F	SOLIDUS
 48 30	DIGIT ZERO
 49 31	DIGIT ONE
 50 32	DIGIT TWO
 51 33	DIGIT THREE
 52 34	DIGIT FOUR
 53 35	DIGIT FIVE
 54 36	DIGIT SIX
 55 37	DIGIT SEVEN
 56 38	DIGIT EIGHT
 57 39	DIGIT NINE
 58 3A	COLON
 59 3B	SEMICOLON
 60 3C	LESS-THAN SIGN
 61 3D	EQUALS SIGN
 62 3E	GREATER-THAN SIGN
 63 3F	QUESTION MARK
 64 40	COMMERCIAL AT
 65 41	LATIN CAPITAL LETTER A
 66 42	LATIN CAPITAL LETTER B
 67 43	LATIN CAPITAL LETTER C
 68 44	LATIN CAPITAL LETTER D
 69 45	LATIN CAPITAL LETTER E
 70 46	LATIN CAPITAL LETTER F
 71 47	LATIN CAPITAL LETTER G
 72 48	LATIN CAPITAL LETTER H
 73 49	LATIN CAPITAL LETTER I
 74 4A	LATIN CAPITAL LETTER J
 75 4B	LATIN CAPITAL LETTER K
 76 4C	LATIN CAPITAL LETTER L
 77 4D	LATIN CAPITAL LETTER M
 78 4E	LATIN CAPITAL LETTER N
 79 4F	LATIN CAPITAL LETTER O
 80 50	LATIN CAPITAL LETTER P
 81 51	LATIN CAPITAL LETTER Q
 82 52	LATIN CAPITAL LETTER R
 83 53	LATIN CAPITAL LETTER S
 84 54	LATIN CAPITAL LETTER T
 85 55	LATIN CAPITAL LETTER U
 86 56	LATIN CAPITAL LETTER V
 87 57	LATIN CAPITAL LETTER W
 88 58	LATIN CAPITAL LETTER X
 89 59	LATIN CAPITAL LETTER Y
 90 5A	LATIN CAPITAL LETTER Z
 91 5B	LEFT SQUARE BRACKET
 92 5C	REVERSE SOLIDUS
 93 5D	RIGHT SQUARE BRACKET
 94 5E	CIRCUMFLEX ACCENT
 95 5F	LOW LINE
 96 60	GRAVE ACCENT
 97 61	LATIN SMALL LETTER A
 98 62	LATIN SMALL LETTER B
 99 63	LATIN SMALL LETTER C
100 64	LATIN SMALL LETTER D
101 65	LATIN SMALL LETTER E
102 66	LATIN SMALL LETTER F
103 67	LATIN SMALL LETTER G
104 68	LATIN SMALL LETTER H
105 69	LATIN SMALL LETTER I
106 6A	LATIN SMALL LETTER J
107 6B	LATIN SMALL LETTER K
108 6C	LATIN SMALL LETTER L
109 6D	LATIN SMALL LETTER M
110 6E	LATIN SMALL LETTER N
111 6F	LATIN SMALL LETTER O
112 70	LATIN SMALL LETTER P
113 71	LATIN SMALL LETTER Q
114 72	LATIN SMALL LETTER R
115 73	LATIN SMALL LETTER S
116 74	LATIN SMALL LETTER T
117 75	LATIN SMALL LETTER U
118 76	LATIN SMALL LETTER V
119 77	LATIN SMALL LETTER W
120 78	LATIN SMALL LETTER X
121 79	LATIN SMALL LETTER Y
122 7A	LATIN SMALL LETTER Z
123 7B	LEFT CURLY BRACKET
124 7C	VERTICAL LINE
125 7D	RIGHT CURLY BRACKET
126 7E	TILDE
160 A0	NO-BREAK SPACE
161 A1	LEFT SINGLE QUOTATION MARK
162 A2	RIGHT SINGLE QUOTATION MARK
163 A3	POUND SIGN
166 A6	BROKEN BAR
167 A7	SECTION SIGN
168 A8	DIAERESIS
169 A9	COPYRIGHT SIGN
171 AB	LEFT-POINTING DOUBLE ANGLE QUOTATION MARK
172 AC	NOT SIGN
173 AD	SOFT HYPHEN
175 AF	HORIZONTAL BAR
176 B0	DEGREE SIGN
177 B1	PLUS-MINUS SIGN
178 B2	SUPERSCRIPT TWO
179 B3	SUPERSCRIPT THREE
180 B4	GREEK TONOS
181 B5	GREEK DIALYTIKA TONOS
182 B6	GREEK CAPITAL LETTER ALPHA WITH TONOS
183 B7	MIDDLE DOT
184 B8	GREEK CAPITAL LETTER EPSILON WITH TONOS
185 B9	GREEK CAPITAL LETTER ETA WITH TONOS
186 BA	GREEK CAPITAL LETTER IOTA WITH TONOS
187 BB	RIGHT-POINTING DOUBLE ANGLE QUOTATION MARK
188 BC	GREEK CAPITAL LETTER OMICRON WITH TONOS
189 BD	VULGAR FRACTION ONE HALF
190 BE	GREEK CAPITAL LETTER UPSILON WITH TONOS
191 BF	GREEK CAPITAL LETTER OMEGA WITH TONOS
192 C0	GREEK SMALL LETTER IOTA WITH DIALYTIKA AND TONOS
193 C1	GREEK CAPITAL LETTER ALPHA
194 C2	GREEK CAPITAL LETTER BETA
195 C3	GREEK CAPITAL LETTER GAMMA
196 C4	GREEK CAPITAL LETTER DELTA
197 C5	GREEK CAPITAL LETTER EPSILON
198 C6	GREEK CAPITAL LETTER ZETA
199 C7	GREEK CAPITAL LETTER ETA
200 C8	GREEK CAPITAL LETTER THETA
201 C9	GREEK CAPITAL LETTER IOTA
202 CA	GREEK CAPITAL LETTER KAPPA
203 CB	GREEK CAPITAL LETTER LAMDA
204 CC	GREEK CAPITAL LETTER MU
205 CD	GREEK CAPITAL LETTER NU
206 CE	GREEK CAPITAL LETTER XI
207 CF	GREEK CAPITAL LETTER OMICRON
208 D0	GREEK CAPITAL LETTER PI
209 D1	GREEK CAPITAL LETTER RHO
211 D3	GREEK CAPITAL LETTER SIGMA
212 D4	GREEK CAPITAL LETTER TAU
213 D5	GREEK CAPITAL LETTER UPSILON
214 D6	GREEK CAPITAL LETTER PHI
215 D7	GREEK CAPITAL LETTER CHI
216 D8	GREEK CAPITAL LETTER PSI
217 D9	GREEK CAPITAL LETTER OMEGA
218 DA	GREEK CAPITAL LETTER IOTA WITH DIALYTIKA
219 DB	GREEK CAPITAL LETTER UPSILON WITH DIALYTIKA
220 DC	GREEK SMALL LETTER ALPHA WITH TONOS
221 DD	GREEK SMALL LETTER EPSILON WITH TONOS
222 DE	GREEK SMALL LETTER ETA WITH TONOS
223 DF	GREEK SMALL LETTER IOTA WITH TONOS
224 E0	GREEK SMALL LETTER UPSILON WITH DIALYTIKA AND TONOS
225 E1	GREEK SMALL LETTER ALPHA
226 E2	GREEK SMALL LETTER BETA
227 E3	GREEK SMALL LETTER GAMMA
228 E4	GREEK SMALL LETTER DELTA
229 E5	GREEK SMALL LETTER EPSILON
230 E6	GREEK SMALL LETTER ZETA
231 E7	GREEK SMALL LETTER ETA
232 E8	GREEK SMALL LETTER THETA
233 E9	GREEK SMALL LETTER IOTA
234 EA	GREEK SMALL LETTER KAPPA
235 EB	GREEK SMALL LETTER LAMDA
236 EC	GREEK SMALL LETTER MU
237 ED	GREEK SMALL LETTER NU
238 EE	GREEK SMALL LETTER XI
239 EF	GREEK SMALL LETTER OMICRON
240 F0	GREEK SMALL LETTER PI
241 F1	GREEK SMALL LETTER RHO
242 F2	GREEK SMALL LETTER FINAL SIGMA
243 F3	GREEK SMALL LETTER SIGMA
244 F4	GREEK SMALL LETTER TAU
245 F5	GREEK SMALL LETTER UPSILON
246 F6	GREEK SMALL LETTER PHI
247 F7	GREEK SMALL LETTER CHI
248 F8	GREEK SMALL LETTER PSI
249 F9	GREEK SMALL LETTER OMEGA
250 FA	GREEK SMALL LETTER IOTA WITH DIALYTIKA
251 FB	GREEK SMALL LETTER UPSILON WITH DIALYTIKA
252 FC	GREEK SMALL LETTER OMICRON WITH TONOS
253 FD	GREEK SMALL LETTER UPSILON WITH TONOS
254 FE	GREEK SMALL LETTER OMEGA WITH TONOS
#endif
