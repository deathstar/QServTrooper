#!/bin/sh

# colors and such
   c_bold=`tput bold`
  c_reset=`tput sgr0`

  c_black=`tput setaf 0`
    c_red=`tput setaf 1`
  c_green=`tput setaf 2`
 c_yellow=`tput setaf 3`
   c_blue=`tput setaf 4`
c_magenta=`tput setaf 5`
   c_cyan=`tput setaf 6`
  c_white=`tput setaf 7`

  c_bblack=$c_bold`tput setaf 0`
    c_bred=$c_bold`tput setaf 1`
  c_bgreen=$c_bold`tput setaf 2`
 c_byellow=$c_bold`tput setaf 3`
   c_bblue=$c_bold`tput setaf 4`
c_bmagenta=$c_bold`tput setaf 5`
   c_bcyan=$c_bold`tput setaf 6`
  c_bwhite=$c_bold`tput setaf 7`

  b_black=`tput setab 0`
    b_red=`tput setab 1`
  b_green=`tput setab 2`
 b_yellow=`tput setab 3`
   b_blue=`tput setab 4`
b_magenta=`tput setab 5`
   b_cyan=`tput setab 6`
  b_white=`tput setab 7`

echo "${c_byellow}${b_blue}Configuring QServ...${c_reset}"


rm -f config.h config.mk


echo "${c_bblue}Checking for zlib...${c_reset}"
tfl=`mktemp -t config.XXXXXX`
mv -f $tfl ${tfl}.c
tfl=${tfl}.c

cat > $tfl <<EOF
extern void deflate();
int main() { deflate(); }
EOF

if gcc $tfl -lz -o /dev/null > /dev/null 2>&1; then
HAVE_LIBZ=true
else
HAVE_LIBZ=false
fi

cat > $tfl <<EOF
#include <zlib.h>
int main() {}
EOF

if gcc $tfl -o /dev/null > /dev/null 2>&1; then
HAVE_ZLIB_H=true
else
HAVE_ZLIB_H=false
fi

if test $HAVE_LIBZ = true; then
	if test $HAVE_ZLIB_H = true; then
		echo "${c_bgreen}Found${c_reset}"
	else
		echo "${c_bred}Not found${c_reset}"
		echo "Please install zlib development files."
		echo "Debian/Ubuntu users: ${c_cyan}sudo apt-get install zlib1g-dev${c_reset}"
		exit 1
	fi
else
	echo "${c_bred}Not found${c_reset}"
	echo "Please install zlib library and development files"
	echo "Debian/Ubuntu users can do ${c_cyan}sudo apt-get install zlib1g zlib1g-dev${c_reset}"
	exit 1
fi



echo "${c_bblue}Checking whether -lrt is needed...${c_reset}"

cat > $tfl <<EOF
extern void clock_gettime();
int main() { clock_gettime(); }
EOF

if gcc $tfl -o /dev/null > /dev/null 2>&1; then
NEED_LIBRT=false
else
	if gcc $tfl -lrt -o /dev/null > /dev/null 2>&1; then
	NEED_LIBRT=true
	else
	NEED_LIBRT=error
	fi
fi

case $NEED_LIBRT in
	true)
		echo "${c_bgreen}Needed.${c_reset}"
		echo "LDFLAGS+=-lrt" >> config.mk
		;;
	false) 
		echo "${c_bgreen}Not needed.${c_reset}"
		;;
	error)
		echo "${c_bred}Cannot figure out. Leaving without...${c_reset}"
		;;
esac

echo "${c_bblue}Checking for GeoIP...${c_reset}"

cat > $tfl <<EOF
extern void GeoIP_new();
int main() { GeoIP_new(); }
EOF

if gcc $tfl -lGeoIP -o /dev/null > /dev/null 2>&1; then
HAVE_LIBGEOIP=true
else
HAVE_LIBGEOIP=false
fi

cat > $tfl <<EOF
#include <GeoIP.h>
int main() {}
EOF

if gcc $tfl -o /dev/null > /dev/null 2>&1; then
HAVE_GEOIP_H=true
else
HAVE_GEOIP_H=false
fi

if test $HAVE_LIBGEOIP = true; then
	if test $HAVE_GEOIP_H = true; then
		echo "${c_bgreen}Found.${c_reset}"
		echo "#define HAVE_GEOIP 1" >> config.h
		echo "LDFLAGS+=-lGeoIP" >> config.mk
	else
		echo "${c_bred}Not found.${c_reset}"
		echo "Please install GeoIP development files."
		echo "Debian/Ubuntu users can do ${c_cyan}sudo apt-get install libgeoip-dev${c_reset}"
	fi
else
	echo "${c_bred}Not found.${c_reset}"
	echo "Please install GeoIP library and development files."
	echo "Debian/Ubuntu users can do ${c_cyan}sudo apt-get install libgeoip1 libgeoip-dev${c_reset}"
	echo "Frogmod will be built without GeoIP support."
fi


echo "${c_bblue}Checking for /proc...${c_reset}"

if [ -d /proc ]; then
HAVE_PROC=true
else
HAVE_PROC=false
fi

if test $HAVE_PROC = true; then
	echo "${c_bgreen}Found.${c_reset}"
	echo "#define HAVE_PROC 1" >> config.h
else
	echo "${c_bred}Not found.${c_reset}"
	echo "The memory usage will not be displayed."
fi
