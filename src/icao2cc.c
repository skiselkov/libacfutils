/*
 * CDDL HEADER START
 *
 * This file and its contents are supplied under the terms of the
 * Common Development and Distribution License ("CDDL"), version 1.0.
 * You may only use this file in accordance with the terms of version
 * 1.0 of the CDDL.
 *
 * A full copy of the text of the CDDL should have accompanied this
 * source.  A copy of the CDDL is also available via the Internet at
 * http://www.illumos.org/license/CDDL.
 *
 * CDDL HEADER END
*/
/*
 * Copyright 2017 Saso Kiselkov. All rights reserved.
 */

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include <acfutils/icao2cc.h>

typedef struct {
	const char *icao;
	const char *cc;
	const char *name;
} icao2cc_t;

/*
 * Although we'd love to have this table be uniquely keyed by ICAO code,
 * unfortunately not every place has its own unique ICAO prefix. However,
 * they're usually small enough that we don't need to worry too much.
 */
static icao2cc_t icao2cc_table[] = {
	{ .icao = "AG", .cc = "SB" },	/* Solomon Islands */
	{ .icao = "AN", .cc = "NR" },	/* Nauru */
	{ .icao = "AY", .cc = "PG" },	/* Papua New Guinea */
	{ .icao = "BG", .cc = "GL" },	/* Greenland */
	{ .icao = "BI", .cc = "IS" },	/* Iceland */
	{ .icao = "BK", .cc = "XK" },	/* Kosovo */
	{ .icao = "C", .cc = "CA" },	/* Canada */
	{ .icao = "DA", .cc = "DZ" },	/* Algeria */
	{ .icao = "DB", .cc = "BJ" },	/* Benin */
	{ .icao = "DF", .cc = "BF" },	/* Burkina Faso */
	{ .icao = "DG", .cc = "GH" },	/* Ghana */
	{ .icao = "DI", .cc = "CI" },	/* Ivory Coast */
	{ .icao = "DN", .cc = "NG" },	/* Nigeria */
	{ .icao = "DR", .cc = "NE" },	/* Niger */
	{ .icao = "DT", .cc = "TN" },	/* Tunisia */
	{ .icao = "DX", .cc = "TG" },	/* Togo */
	{ .icao = "EB", .cc = "BE" },	/* Belgium */
	{ .icao = "ED", .cc = "DE" },	/* Germany */
	{ .icao = "EE", .cc = "EE" },	/* Estonia */
	{ .icao = "EF", .cc = "FI" },	/* Finland */
	{ .icao = "EG", .cc = "GB" },	/* United Kingdom */
	{ .icao = "EG", .cc = "GS" },	/* South Georgia and the */
					/* South Sandwich Islands */
	{ .icao = "EH", .cc = "NL" },	/* Netherlands */
	{ .icao = "EI", .cc = "IE" },	/* Ireland */
	{ .icao = "EK", .cc = "DK" },	/* Denmark */
	{ .icao = "EL", .cc = "LU" },	/* Luxembourg */
	{ .icao = "EN", .cc = "MH" },	/* Marshall Islands */
	{ .icao = "EN", .cc = "NO" },	/* Norway */
	{ .icao = "EP", .cc = "PL" },	/* Poland */
	{ .icao = "ES", .cc = "SE" },	/* Sweden */
	{ .icao = "ET", .cc = "DE" },	/* Germany */
	{ .icao = "EV", .cc = "LV" },	/* Latvia */
	{ .icao = "EY", .cc = "LT" },	/* Lithuania */
	{ .icao = "FA", .cc = "ZA" },	/* South Africa */
	{ .icao = "FB", .cc = "BW" },	/* Botswana */
	{ .icao = "FC", .cc = "CG" },	/* Republic of the Congo */
	{ .icao = "FD", .cc = "SZ" },	/* Swaziland */
	{ .icao = "FE", .cc = "CF" },	/* Central African Republic */
	{ .icao = "FG", .cc = "GQ" },	/* Equatorial Guinea */
	{ .icao = "FH", .cc = "SH" },	/* Saint Helena Ascension and */
					/* Tristan da Cunha */
	{ .icao = "FI", .cc = "MU" },	/* Mauritius */
	{ .icao = "FJ", .cc = "IO" },	/* British Indian Ocean Territory */
	{ .icao = "FK", .cc = "CM" },	/* Cameroon */
	{ .icao = "FL", .cc = "ZM" },	/* Zambia */
	{ .icao = "FMC", .cc = "KM" },	/* Comoros */
	{ .icao = "FM", .cc = "YT" },	/* Mayotte */
	{ .icao = "FME", .cc = "RE" },	/* Réunion */
	{ .icao = "FMM", .cc = "MG" },	/* Madagascar */
	{ .icao = "FMN", .cc = "MG" },	/* Madagascar */
	{ .icao = "FMS", .cc = "MG" },	/* Madagascar */
	{ .icao = "FN", .cc = "AO" },	/* Angola */
	{ .icao = "FO", .cc = "GA" },	/* Gabon */
	{ .icao = "FP", .cc = "ST" },	/* São Tomé and Príncipe */
	{ .icao = "FQ", .cc = "MZ" },	/* Mozambique */
	{ .icao = "FS", .cc = "SC" },	/* Seychelles */
	{ .icao = "FT", .cc = "TD" },	/* Chad */
	{ .icao = "FV", .cc = "ZW" },	/* Zimbabwe */
	{ .icao = "FW", .cc = "MW" },	/* Malawi */
	{ .icao = "FX", .cc = "LS" },	/* Lesotho */
	{ .icao = "FY", .cc = "NA" },	/* Namibia */
	{ .icao = "FZ", .cc = "CD" },	/* Democratic Republic of the Congo */
	{ .icao = "GA", .cc = "ML" },	/* Mali */
	{ .icao = "GB", .cc = "GM" },	/* Gambia */
	{ .icao = "GC", .cc = "ES" },	/* Spain */
	{ .icao = "GE", .cc = "ES" },	/* Spain */
	{ .icao = "GF", .cc = "SL" },	/* Sierra Leone */
	{ .icao = "GG", .cc = "GW" },	/* Guinea-Bissau */
	{ .icao = "GL", .cc = "LR" },	/* Liberia */
	{ .icao = "GM", .cc = "MA" },	/* Morocco */
	{ .icao = "GO", .cc = "SN" },	/* Senegal */
	{ .icao = "GQ", .cc = "MR" },	/* Mauritania */
	{ .icao = "GS", .cc = "EH" },	/* Western Sahara */
	{ .icao = "GU", .cc = "GN" },	/* Guinea */
	{ .icao = "GV", .cc = "CV" },	/* Cape Verde */
	{ .icao = "HA", .cc = "ET" },	/* Ethiopia */
	{ .icao = "HB", .cc = "BI" },	/* Burundi */
	{ .icao = "HC", .cc = "SO" },	/* Somalia */
	{ .icao = "HD", .cc = "DJ" },	/* Djibouti */
	{ .icao = "HE", .cc = "EG" },	/* Egypt */
	{ .icao = "HH", .cc = "ER" },	/* Eritrea */
	{ .icao = "HK", .cc = "KE" },	/* Kenya */
	{ .icao = "HL", .cc = "LY" },	/* Libya */
	{ .icao = "HR", .cc = "RW" },	/* Rwanda */
	{ .icao = "HS", .cc = "SD" },	/* Sudan */
	{ .icao = "HS", .cc = "SS" },	/* South Sudan */
	{ .icao = "HT", .cc = "TZ" },	/* Tanzania */
	{ .icao = "HU", .cc = "UG" },	/* Uganda */
	{ .icao = "K", .cc = "US" },	/* United States */
	{ .icao = "LA", .cc = "AL" },	/* Albania */
	{ .icao = "LB", .cc = "BG" },	/* Bulgaria */
	{ .icao = "LC", .cc = "CY" },	/* Cyprus */
	{ .icao = "LD", .cc = "HR" },	/* Croatia */
	{ .icao = "LE", .cc = "ES" },	/* Spain */
	{ .icao = "LF", .cc = "FR" },	/* France */
	{ .icao = "LF", .cc = "PM" },	/* Saint Pierre and Miquelon */
	{ .icao = "LG", .cc = "GR" },	/* Greece */
	{ .icao = "LH", .cc = "HU" },	/* Hungary */
	{ .icao = "LI", .cc = "IT" },	/* Italy */
	{ .icao = "LJ", .cc = "SI" },	/* Slovenia */
	{ .icao = "LK", .cc = "CZ" },	/* Czech Republic */
	{ .icao = "LL", .cc = "IL" },	/* Israel */
	{ .icao = "LM", .cc = "MT" },	/* Malta */
	{ .icao = "LN", .cc = "MC" },	/* Monaco */
	{ .icao = "LO", .cc = "AT" },	/* Austria */
	{ .icao = "LP", .cc = "PT" },	/* Portugal */
	{ .icao = "LQ", .cc = "BA" },	/* Bosnia and Herzegovina */
	{ .icao = "LR", .cc = "RO" },	/* Romania */
	{ .icao = "LS", .cc = "CH" },	/* Switzerland */
	{ .icao = "LT", .cc = "TR" },	/* Turkey */
	{ .icao = "LU", .cc = "MD" },	/* Moldova */
	{ .icao = "LV", .cc = "PS" },	/* Palestine */
	{ .icao = "LW", .cc = "MK" },	/* Macedonia */
	{ .icao = "LX", .cc = "GI" },	/* Gibraltar */
	{ .icao = "LY", .cc = "ME" },	/* Montenegro */
	{ .icao = "LY", .cc = "RS" },	/* Serbia */
	{ .icao = "LZ", .cc = "SK" },	/* Slovakia */
	{ .icao = "MB", .cc = "TC" },	/* Turks and Caicos Islands */
	{ .icao = "MD", .cc = "DO" },	/* Dominican Republic */
	{ .icao = "MG", .cc = "GT" },	/* Guatemala */
	{ .icao = "MH", .cc = "HN" },	/* Honduras */
	{ .icao = "MI", .cc = "VI" },	/* United States Virgin Islands */
	{ .icao = "MK", .cc = "JM" },	/* Jamaica */
	{ .icao = "MM", .cc = "MX" },	/* Mexico */
	{ .icao = "MN", .cc = "NI" },	/* Nicaragua */
	{ .icao = "MP", .cc = "PA" },	/* Panama */
	{ .icao = "MR", .cc = "CR" },	/* Costa Rica */
	{ .icao = "MS", .cc = "SV" },	/* El Salvador */
	{ .icao = "MT", .cc = "HT" },	/* Haiti */
	{ .icao = "MU", .cc = "CU" },	/* Cuba */
	{ .icao = "MW", .cc = "KY" },	/* Cayman Islands */
	{ .icao = "MY", .cc = "BS" },	/* Bahamas */
	{ .icao = "MZ", .cc = "BZ" },	/* Belize */
	{ .icao = "NC", .cc = "CK" },	/* Cook Islands */
	{ .icao = "NE", .cc = "CL" },	/* Chile */
	{ .icao = "NF", .cc = "FJ" },	/* Fiji */
	{ .icao = "NFT", .cc = "TO" },	/* Tonga */
	{ .icao = "NG", .cc = "KI" },	/* Kiribati */
	{ .icao = "NGF", .cc = "TV" },	/* Tuvalu */
	{ .icao = "NI", .cc = "NU" },	/* Niue */
	{ .icao = "NL", .cc = "WF" },	/* Wallis and Futuna */
	{ .icao = "NS", .cc = "AS" },	/* American Samoa */
	{ .icao = "NS", .cc = "WS" },	/* Samoa */
	{ .icao = "NT", .cc = "PF" },	/* French Polynesia */
	{ .icao = "NV", .cc = "VU" },	/* Vanuatu */
	{ .icao = "NW", .cc = "NC" },	/* New Caledonia */
	{ .icao = "NZ", .cc = "NZ" },	/* New Zealand */
	{ .icao = "OA", .cc = "AF" },	/* Afghanistan */
	{ .icao = "OB", .cc = "BH" },	/* Bahrain */
	{ .icao = "OE", .cc = "SA" },	/* Saudi Arabia */
	{ .icao = "OI", .cc = "IR" },	/* Iran */
	{ .icao = "OJ", .cc = "JO" },	/* Jordan */
	{ .icao = "OJ", .cc = "PS" },	/* Palestine */
	{ .icao = "OK", .cc = "KW" },	/* Kuwait */
	{ .icao = "OL", .cc = "LB" },	/* Lebanon */
	{ .icao = "OM", .cc = "AE" },	/* United Arab Emirates */
	{ .icao = "OO", .cc = "OM" },	/* Oman */
	{ .icao = "OP", .cc = "PK" },	/* Pakistan */
	{ .icao = "OR", .cc = "IQ" },	/* Iraq */
	{ .icao = "OS", .cc = "SY" },	/* Syria */
	{ .icao = "OT", .cc = "QA" },	/* Qatar */
	{ .icao = "OY", .cc = "YE" },	/* Yemen */
	{ .icao = "PA", .cc = "US" },	/* United States */
	{ .icao = "PB", .cc = "US" },	/* United States */
	{ .icao = "PF", .cc = "US" },	/* United States */
	{ .icao = "PG", .cc = "GU" },	/* Guam */
	{ .icao = "PG", .cc = "MP" },	/* Northern Mariana Islands */
	{ .icao = "PH", .cc = "US" },	/* United States */
	{ .icao = "PJ", .cc = "US" },	/* United States */
	{ .icao = "PK", .cc = "MH" },	/* Marshall Islands */
	{ .icao = "PL", .cc = "NZ" },	/* New Zealand */
	{ .icao = "PL", .cc = "US" },	/* United States */
	{ .icao = "PM", .cc = "US" },	/* United States */
	{ .icao = "PO", .cc = "US" },	/* United States */
	{ .icao = "PP", .cc = "US" },	/* United States */
	{ .icao = "PT", .cc = "FM" },	/* Federated States of Micronesia */
	{ .icao = "PT", .cc = "PW" },	/* Palau */
	{ .icao = "PW", .cc = "US" },	/* United States */
	{ .icao = "RC", .cc = "TW" },	/* Taiwan */
	{ .icao = "RJ", .cc = "JP" },	/* Japan */
	{ .icao = "RK", .cc = "KR" },	/* South Korea */
	{ .icao = "RO", .cc = "JP" },	/* Japan */
	{ .icao = "RP", .cc = "PH" },	/* Philippines */
	{ .icao = "SA", .cc = "AR" },	/* Argentina */
	{ .icao = "SB", .cc = "BR" },	/* Brazil */
	{ .icao = "SC", .cc = "CL" },	/* Chile */
	{ .icao = "SD", .cc = "BR" },	/* Brazil */
	{ .icao = "SE", .cc = "EC" },	/* Ecuador */
	{ .icao = "SF", .cc = "FK" },	/* Falkland Islands */
	{ .icao = "SG", .cc = "PY" },	/* Paraguay */
	{ .icao = "SK", .cc = "CO" },	/* Colombia */
	{ .icao = "SL", .cc = "BO" },	/* Bolivia */
	{ .icao = "SM", .cc = "SR" },	/* Suriname */
	{ .icao = "SN", .cc = "BR" },	/* Brazil */
	{ .icao = "SO", .cc = "GF" },	/* French Guiana */
	{ .icao = "SP", .cc = "PE" },	/* Peru */
	{ .icao = "SS", .cc = "BR" },	/* Brazil */
	{ .icao = "SU", .cc = "UY" },	/* Uruguay */
	{ .icao = "SV", .cc = "VE" },	/* Venezuela */
	{ .icao = "SW", .cc = "BR" },	/* Brazil */
	{ .icao = "SY", .cc = "GY" },	/* Guyana */
	{ .icao = "TA", .cc = "AG" },	/* Antigua and Barbuda */
	{ .icao = "TB", .cc = "BB" },	/* Barbados */
	{ .icao = "TD", .cc = "DM" },	/* Dominica */
	{ .icao = "TF", .cc = "BL" },	/* Saint Barthélemy */
	{ .icao = "TF", .cc = "GP" },	/* Guadeloupe */
	{ .icao = "TF", .cc = "MF" },	/* Saint Martin */
	{ .icao = "TF", .cc = "MQ" },	/* Martinique */
	{ .icao = "TG", .cc = "GD" },	/* Grenada */
	{ .icao = "TI", .cc = "VI" },	/* United States Virgin Islands */
	{ .icao = "TJ", .cc = "PR" },	/* Puerto Rico */
	{ .icao = "TK", .cc = "KN" },	/* Saint Kitts and Nevis */
	{ .icao = "TL", .cc = "LC" },	/* Saint Lucia */
	{ .icao = "TN", .cc = "AW" },	/* Aruba */
	{ .icao = "TN", .cc = "BQ" },	/* Caribbean Netherlands */
	{ .icao = "TN", .cc = "CW" },	/* Curaçao */
	{ .icao = "TN", .cc = "SX" },	/* Sint Maarten */
	{ .icao = "TQ", .cc = "AI" },	/* Anguilla */
	{ .icao = "TR", .cc = "MS" },	/* Montserrat */
	{ .icao = "TT", .cc = "TT" },	/* Trinidad and Tobago */
	{ .icao = "TU", .cc = "VG" },	/* British Virgin Islands */
	{ .icao = "TV", .cc = "VC" },	/* Saint Vincent and the Grenadines */
	{ .icao = "TX", .cc = "BM" },	/* Bermuda */
	{ .icao = "UA", .cc = "KZ" },	/* Kazakhstan */
	{ .icao = "UB", .cc = "AZ" },	/* Azerbaijan */
	{ .icao = "UC", .cc = "KG" },	/* Kyrgyzstan */
	{ .icao = "UD", .cc = "AM" },	/* Armenia */
	{ .icao = "UE", .cc = "RU" },	/* Russia */
	{ .icao = "UG", .cc = "GE" },	/* Georgia */
	{ .icao = "UH", .cc = "RU" },	/* Russia */
	{ .icao = "UI", .cc = "RU" },	/* Russia */
	{ .icao = "UK", .cc = "UA" },	/* Ukraine */
	{ .icao = "UL", .cc = "RU" },	/* Russia */
	{ .icao = "UM", .cc = "BY" },	/* Belarus */
	{ .icao = "UN", .cc = "RU" },	/* Russia */
	{ .icao = "UO", .cc = "RU" },	/* Russia */
	{ .icao = "UR", .cc = "RU" },	/* Russia */
	{ .icao = "US", .cc = "RU" },	/* Russia */
	{ .icao = "UT", .cc = "TJ" },	/* Tajikistan */
	{ .icao = "UT", .cc = "TM" },	/* Turkmenistan */
	{ .icao = "UT", .cc = "UZ" },	/* Uzbekistan */
	{ .icao = "UU", .cc = "RU" },	/* Russia */
	{ .icao = "UW", .cc = "RU" },	/* Russia */
	{ .icao = "VA", .cc = "IN" },	/* India */
	{ .icao = "VB", .cc = "MM" },	/* Myanmar */
	{ .icao = "VC", .cc = "LK" },	/* Sri Lanka */
	{ .icao = "VD", .cc = "KH" },	/* Cambodia */
	{ .icao = "VE", .cc = "IN" },	/* India */
	{ .icao = "VG", .cc = "BD" },	/* Bangladesh */
	{ .icao = "VH", .cc = "HK" },	/* Hong Kong */
	{ .icao = "VI", .cc = "IN" },	/* India */
	{ .icao = "VL", .cc = "LA" },	/* Laos */
	{ .icao = "VM", .cc = "MO" },	/* Macau */
	{ .icao = "VN", .cc = "NP" },	/* Nepal */
	{ .icao = "VO", .cc = "IN" },	/* India */
	{ .icao = "VQ", .cc = "BT" },	/* Bhutan */
	{ .icao = "VR", .cc = "MV" },	/* Maldives */
	{ .icao = "VT", .cc = "TH" },	/* Thailand */
	{ .icao = "VV", .cc = "VN" },	/* Vietnam */
	{ .icao = "VY", .cc = "MM" },	/* Myanmar */
	{ .icao = "WA", .cc = "ID" },	/* Indonesia */
	{ .icao = "WB", .cc = "BN" },	/* Brunei */
	{ .icao = "WB", .cc = "MY" },	/* Malaysia */
	{ .icao = "WI", .cc = "ID" },	/* Indonesia */
	{ .icao = "WM", .cc = "MY" },	/* Malaysia */
	{ .icao = "WP", .cc = "TL" },	/* Timor-Leste */
	{ .icao = "WS", .cc = "SG" },	/* Singapore */
	{ .icao = "Y", .cc = "AU" },	/* Australia */
	{ .icao = "YP", .cc = "CX" },	/* Christmas Island */
	{ .icao = "ZB", .cc = "CN" },	/* China */
	{ .icao = "ZG", .cc = "CN" },	/* China */
	{ .icao = "ZH", .cc = "CN" },	/* China */
	{ .icao = "ZJ", .cc = "CN" },	/* China */
	{ .icao = "ZK", .cc = "KP" },	/* North Korea */
	{ .icao = "ZL", .cc = "CN" },	/* China */
	{ .icao = "ZM", .cc = "MN" },	/* Mongolia */
	{ .icao = "ZP", .cc = "CN" },	/* China */
	{ .icao = "ZS", .cc = "CN" },	/* China */
	{ .icao = "ZT", .cc = "CN" },	/* China */
	{ .icao = "ZU", .cc = "CN" },	/* China */
	{ .icao = "ZW", .cc = "CN" },	/* China */
	{ .icao = "ZY", .cc = "CN" },	/* China */
	{ .icao = NULL, .cc = NULL, .name = NULL}	/* Last entry */
};

/*
 * Converts an ICAO code to a country code. This performs a simple prefix
 * match using the icao2cc_table.
 */
const char *
icao2cc(const char *icao)
{
	/*
	 * Doing a linear search is not particularly elegant, but the size
	 * of the ICAO table is fixed and small, so it probably doesn't
	 * matter anyway.
	 */
	for (int i = 0; icao2cc_table[i].icao != NULL; i++) {
		if (strstr(icao, icao2cc_table[i].icao) == icao)
			return (icao2cc_table[i].cc);
	}

	return (NULL);
}
