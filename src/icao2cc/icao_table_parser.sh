#!/bin/bash

sed '/^$/d; s/,/ /g; s/\[.*\]//g;' test.txt | awk '
{
	if ($0 ~ /^ /) {
		name = $1;
		for (i = 2; $i != ""; i++)
			name = name " " $i;
	} else if ($0 ~ /^\tISO 3166-1 alpha-2/) {
		parse_cc=1;
	} else if ($0 ~ /^\tICAO airport code/) {
		parse_cc = 0;
		parse_icao = 1;
	} else if ($0 ~ /^E\./) {
		parse_icao = 0;
	}
	if ($0 ~ /^[A-Z]/) {
		if (parse_cc) {
			cc = $1;
		} else if (parse_icao) {
			for (i = 1; $i != ""; i++) {
				printf("\t{ .icao = \"%s\", .cc = \"%s\" },\t/* %s */\n",
				    $i, cc, name);
			}
		}
	}
}' | grep -v Antarctica | sort
