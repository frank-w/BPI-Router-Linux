#!/bin/bash
if [[ $UID -eq 0 ]];
then
  echo "do not run as root!"
  exit 1;
fi

. build.conf

r64newswver=1.0
if [[ -z "$board" ]];then board="bpi-r2";fi

kernver=$(make kernelversion | grep -v 'make')
#echo $kernver

DOTCONFIG=".config"

clr_red=$'\e[1;31m'
clr_green=$'\e[1;32m'
clr_yellow=$'\e[1;33m'
clr_blue=$'\e[1;34m'
clr_reset=$'\e[0m'

#Check Crosscompile
crosscompile=0
hostarch=$(uname -m) #r64:aarch64 r2: armv7l

if [[ ! $hostarch =~ aarch64|armv ]];then
	if [[ "$board" == "bpi-r64" ]];then
		if [[ -z "$(which aarch64-linux-gnu-gcc)" ]];then echo "please install gcc-aarch64-linux-gnu";exit 1;fi
		export ARCH=arm64;export CROSS_COMPILE='ccache aarch64-linux-gnu-'
	else
		if [[ -z "$(which arm-linux-gnueabihf-gcc)" ]];then echo "please install gcc-arm-linux-gnueabihf";exit 1;fi
		export ARCH=arm;export CROSS_COMPILE='ccache arm-linux-gnueabihf-'
	fi
#	CCVER=$(arm-linux-gnueabihf-gcc --version |grep arm| sed -e 's/^.* \([0-9]\.[0-9-]\).*$/\1/')
#	if [[ $CCVER =~ ^7 ]]; then
#		echo "arm-linux-gnueabihf-gcc version 7 currently not supported";exit 1;
#	fi

	crosscompile=1
fi;

if [[ "$builddir" != "" && ! "$1" =~ ^(updatesrc|uenv|defconfig|dts*)$ ]];
then
	if [[ "$1" == "importconfig" ]];
	then
		KBUILD_OUTPUT= make mrproper
	fi
	if [[ ! "$builddir" =~ ^/ ]] || [[ "$builddir" == "/" ]];then
		#make it absolute
		builddir=$(realpath $(pwd)"/"$builddir)
#		echo "absolute: $builddir"
	fi

	mkdir -p $builddir

	if [[ "$ramdisksize" != "" ]];
	then
		mount | grep '\s'$builddir'\s' &>/dev/null #$?=0 found;1 not found
		if [[ $? -ne 0 ]];then
			#make sure directory is clean for mounting
			if [[ "$(ls -A $builddir)" ]];then
				rm -rf $builddir/*
			fi
			echo "mounting tmpfs for building..."
			sudo mount -t tmpfs -o size=$ramdisksize none $builddir
		fi
	fi

	#if builddir is empty then run make mrproper to clean up sourcedir
	if [[ ! "$(ls -A $builddir)" ]];then make mrproper;fi

	DOTCONFIG="$builddir/$DOTCONFIG"
	export KBUILD_OUTPUT=$builddir
fi

function edit()
{
	file=$1
	EDITOR=nano
	if [[ -e "$file" ]];then
		if [[ -w "$file" ]];then
			$EDITOR "$file"
		else
			echo "file $file not writable by user using sudo..."
			sudo $EDITOR "$file"
		fi
	else
		echo "file $file not found"
	fi
}

#Check Dependencies

function check_dep()
{
	PACKAGE_Error=0

	grep -i 'ubuntu\|debian' /etc/issue &>/dev/null
	if [[ $? -ne 0 ]];then
		echo "depency-check currently only on debian/ubuntu..."
		return 0;
	fi
	PACKAGES=$(dpkg -l | awk '{print $2}')

	NEEDED_PKGS="make"

	#echo "needed: $NEEDED_PKGS"

	if [[ $# -ge 1 ]];
	then
		if [[ $@ =~ "build" ]];then
			NEEDED_PKGS+=" u-boot-tools bc gcc libc6-dev libncurses5-dev ccache libssl-dev"
		fi
		if [[ $@ =~ "deb" ]];then
			NEEDED_PKGS+=" fakeroot"
		fi
	fi

	echo "needed: $NEEDED_PKGS"
	for package in $NEEDED_PKGS; do
		#TESTPKG=$(dpkg -l |grep "\s${package}")
		TESTPKG=$(echo "$PACKAGES" |grep "^${package}")
		if [[ -z "${TESTPKG}" ]];then echo "please install ${package}";PACKAGE_Error=1;fi
	done
	if [ ${PACKAGE_Error} == 1 ]; then return 1; fi
	return 0;
}

function getuenvpath {
	if [[ $crosscompile -ne 0 ]];then
		uenv_base=/media/${USER}/BPI-BOOT/
	else
		uenv_base=/boot/
	fi

	uenv=${uenv_base}/bananapi/$board/linux/uEnv.txt
	if [[ ! -e $uenv ]];then
		uenv=${uenv_base}/uEnv.txt
		if [[ ! -e $uenv ]];then
			uenv="";
		fi
	fi
	echo "$uenv";
}

function get_version()
{
	echo "generate branch vars..."
	#kernbranch=$(git rev-parse --abbrev-ref HEAD)

	echo "getting git branch: "
	#find branches with actual commit and filter out detached head
	commit=$(git log -n 1 --pretty='%h')
	branches=$(git branch --contains $commit | grep -v '(HEAD')
	echo "$branches"

	kernbranch=$(echo "$branches" | grep '^*') #look for marked branch (local)
	if [[ "$kernbranch" == "" ]];then #no marked branch (travis)
		kernbranch=$(echo "$branches" | head -1) #use first one
	fi
	#kernbranch=$(git branch --contains $(git log -n 1 --pretty='%h') | grep '^*' | grep -v '(HEAD' | head -1 | sed 's/^..//')
	kernbranch=$(echo "$kernbranch" | sed 's/^..//')
	kernbranch=${kernbranch//frank-w_/}
	#if no branch is selected (e.g. bisect)
	if [[ "${kernbranch}" =~ ^\(.* ]]; then
		kernbranch="-$commit";
	fi

	gitbranch=$(echo $kernbranch|sed 's/^[45]\.[0-9]\+//'|sed 's/-rc$//')

	echo "kernbranch:$kernbranch,gitbranch:$gitbranch"
}

function get_r64_switch
{
	grep 'RTL8367S_GSW=y' $DOTCONFIG &>/dev/null
	if [[ $? -eq 0 ]];then
		echo "rtl8367"
	else
		grep 'MT753X_GSW=y' $DOTCONFIG &>/dev/null
		if [[ $? -eq 0 ]];then
			echo "mt7531"
		else
			echo ""
		fi
	fi
}

function increase_kernel {
        #echo $kernver
        old_IFS=$IFS
        IFS='.'
        read -ra KV <<< "$kernver"
        IFS=','
        newkernver=${KV[0]}"."${KV[1]}"."$(( ${KV[2]} +1 ))
        echo $newkernver
}

function disable_option() {
	echo "disable $1"
	if [[ "$1" == "" ]];then echo "option missing ($1)";return;fi
	#CFG=RTL8367S_GSW;
	CFG=${1^^};
	CFG=${CFG//CONFIG_/}
	#echo "CFG:$CFG"
	#grep -i 'mt753x\|rtl8367' .config
	grep $CFG .config
	if [[ $? -eq 0 ]]; then
		sed -i 's:CONFIG_'$CFG'=y:# CONFIG_'$CFG' is not set:g' $DOTCONFIG
		grep $CFG $DOTCONFIG
	else
		echo "option CONFIG_$CFG not found in .config"
	fi
}

function update_kernel_source {
        changedfiles=$(git diff --name-only)
        if [[ -z "$changedfiles" ]]; then
        git fetch stable
        ret=$?
        if [[ $ret -eq 0 ]];then
                newkernver=$(increase_kernel)
		kernmajorver=$(make kernelversion | sed -e 's/\.[0-9]\+$//')
		maxkernver=$(git for-each-ref --sort=-taggerdate --count=1  refs/tags/v${kernmajorver}.*| sed 's/^.*refs\/tags\/v//')
                echo "newkernver:$newkernver (max:$maxkernver)"
                git merge --no-edit v$newkernver
        elif [[ $ret -eq 128 ]];then
                #repo not found
                git remote add stable https://git.kernel.org/pub/scm/linux/kernel/git/stable/linux-stable.git
        fi
        else
                echo "please first commit/stash modified files: $changedfiles"
        fi
}

function pack {
	get_version
	#if [[ "$board" == "bpi-r64" ]];then
	#	switch=$(get_r64_switch)"_"
	#fi
	prepare_SD
	echo "pack..."
	olddir=$(pwd)
	cd ../SD
	fname=${board}_${switch}${kernver}${gitbranch}.tar.gz
	tar -cz --owner=root --group=root -f $fname BPI-BOOT BPI-ROOT
	md5sum $fname > $fname.md5
	ls -lh $(pwd)"/"$fname
	cd $olddir
}

function pack_debs {
	get_version
	echo "pack linux-headers, linux-image, linux-libc-dev debs..."
    echo "LOCALVERSION=${gitbranch} board=$board ARCH=$ARCH CROSS_COMPILE=$CROSS_COMPILE"
	LOCALVERSION="${gitbranch}" board="$board" make bindeb-pkg
	ls ../*.deb
}


function upload {
	get_version
	#if [[ "$board" == "bpi-r64" ]];then
	#	switch="_"$(get_r64_switch)
	#fi
	imagename="uImage_${kernver}${gitbranch}${board//bpi/}${switch}"
	read -e -i $imagename -p "Kernel-filename: " input
	imagename="${input:-$imagename}"

	echo "Name: $imagename"

	if [[ "$board" == "bpi-r64" ]];then
		dtbname="${kernver}${board//bpi/}${gitbranch}${switch}.dtb"
		read -e -i $dtbname -p "dtb-filename: " input
		dtbname="${input:-$dtbname}"

		echo "DTB Name: $dtbname"
	fi

	echo "uploading to ${uploadserver}:${uploaddir}..."
	if [[ "$board" == "bpi-r64" ]];then
		scp uImage_nodt ${uploaduser}@${uploadserver}:${uploaddir}/${imagename}
		scp bpi-r64.dtb ${uploaduser}@${uploadserver}:${uploaddir}/${dtbname}
	else
		scp uImage ${uploaduser}@${uploadserver}:${uploaddir}/${imagename}
	fi
}

function install
{
	get_version
	imagename="uImage_${kernver}${gitbranch}"
	read -e -i $imagename -p "uImage-filename: " input
	imagename="${input:-$imagename}"

	echo "Name: $imagename"

	if [[ $crosscompile -eq 0 ]]; then
		kerneldir="/boot/bananapi/$board/linux"
		kernelfile="$kerneldir/$imagename"
		if [[ -d "$kerneldir" ]];then
			if [[ -e "$kernelfile" ]];then
				echo "backup of kernel: $kernelfile.bak"
				sudo cp "$kernelfile" "$kernelfile.bak"
			fi
			echo "installing new kernel..."
			sudo cp ./uImage "$kernelfile"
			sudo make modules_install
		else
			echo "Kernel directory not found...is /boot mounted?"
		fi
	else
		itbinput=n
		read -p "Press [enter] to copy data to SD-Card..."
		if  [[ -d /media/$USER/BPI-BOOT ]]; then
			targetdir=/media/$USER/BPI-BOOT/bananapi/$board/linux
			mkdir -p $targetdir
			kernelfile=$targetdir/$imagename

			dtinput=n
			ndtinput=y
			if [[ "$board" == "bpi-r64" ]];then
				read -e -i "y" -p "install FIT kernel (itb) [yn]? " itbinput
				if [[ "$itbinput" == "y" ]];then
					itbname=${imagename//uImage_/}.itb
					itbfile=$targetdir/$itbname
					if [[ -e $itbfile ]];then
						echo "backup of kernel: $kernelfile.bak"
						cp $itbfile $itbfile.bak
					fi
					echo "copy new kernel"
					cp ./${board}.itb $itbfile
					if [[ $? -ne 0 ]];then exit 1;fi
					ndtinput=n
				fi
			else
				read -e -i "y" -p "install kernel with DT [yn]? " dtinput
				if [[ "$dtinput" == "y" ]];then
					if [[ -e $kernelfile ]];then
						echo "backup of kernel: $kernelfile.bak"
						cp $kernelfile $kernelfile.bak
					fi
					echo "copy new kernel"
					cp ./uImage $kernelfile
					if [[ $? -ne 0 ]];then exit 1;fi
				fi

				ndt="n"
				if [ "$dtinput" != "y" ];then ndt="y"; fi
				read -e -i "$ndt" -p "install kernel with separate DT [yn]? " ndtinput
				ndtsuffix="_nodt"
			fi

			if [[ "$ndtinput" == "y" ]];then
				if [[ -e ${kernelfile}${ndtsuffix} ]];then
					echo "backup of kernel: $kernelfile.bak"
					cp ${kernelfile}${ndtsuffix} ${kernelfile}${ndtsuffix}.bak
				fi
				echo "copy new nodt kernel"
				cp ./uImage_nodt ${kernelfile}${ndtsuffix}
				if [[ $? -ne 0 ]];then exit 1;fi
				mkdir -p $targetdir/dtb
				dtbname=${kernver}${gitbranch}.dtb
				read -e -i $dtbname -p "dtb-filename: " input
				dtbname="${input:-$dtbname}"
				echo "DTB Name: $dtbname"

				dtbfile=$targetdir/dtb/${dtbname}
				if [[ -e $dtbfile ]];then
					echo "backup of dtb: $dtbfile.bak"
					cp $dtbfile $dtbfile.bak
				fi
				echo "copy new dtb"
				cp ./$board.dtb $dtbfile
			fi

			if [[ "$dtinput" == "y" ]] || [[ "$ndtinput" == "y" ]] || [[ "$itbinput" == "y" ]];then
				echo "copy modules (root needed because of ext-fs permission)"
				export INSTALL_MOD_PATH=/media/$USER/BPI-ROOT/;
				echo "INSTALL_MOD_PATH: $INSTALL_MOD_PATH"
				sudo make ARCH=$ARCH INSTALL_MOD_PATH=$INSTALL_MOD_PATH KBUILD_OUTPUT=$KBUILD_OUTPUT modules_install

				echo "uImage:"
				if [[ "$dtinput" == "y" ]];then
					echo "${kernelfile}"
				fi
				if [[ "$ndtinput" == "y" ]];then
					echo "${kernelfile}${ndtsuffix}"
					echo "DTB: ${dtbfile}"
				fi
				uenv=$(getuenvpath)
				echo $uenv
				echo "by default this kernel-/dtb-file will be loaded (kernel-var in uEnv.txt):"
				#grep '^kernel=' /media/${USER}/BPI-BOOT/bananapi/bpi-r2/linux/uEnv.txt|tail -1
				curkernel=$(grep '^kernel=' $uenv|tail -1| sed 's/kernel=//')
				curfdt=$(grep '^fdt=' $uenv|tail -1| sed 's/fdt=//')
				echo "kernel: " $curkernel
				echo "dtb: " $curfdt

				openuenv=n
				if [[ "$curkernel" == "${imagename}" || "$curkernel" == "${imagename}${ndtsuffix}" ]];then
					echo "no change needed!"
					openuenv=n
				else
					echo "change needed to boot new kernel (kernel=${imagename})!"
					openuenv=y
				fi

				kernelname=$(ls -1t $INSTALL_MOD_PATH"/lib/modules" | head -n 1)
				EXTRA_MODULE_PATH=$INSTALL_MOD_PATH"/lib/modules/"$kernelname"/kernel/extras"
				#echo $kernelname" - "${EXTRA_MODULE_PATH}
				CRYPTODEV="utils/cryptodev/cryptodev-linux/cryptodev.ko"
				if [ -e "${CRYPTODEV}" ]; then
					echo Copy CryptoDev
					sudo mkdir -p "${EXTRA_MODULE_PATH}"
					sudo cp "${CRYPTODEV}" "${EXTRA_MODULE_PATH}"
					#Build Module Dependencies
					sudo /sbin/depmod -b $INSTALL_MOD_PATH ${kernelname}
				fi

				#sudo cp -r ../mod/lib/modules /media/$USER/BPI-ROOT/lib/
				if [[ -n "$(grep 'CONFIG_MT76=' $DOTCONFIG)" ]];then
					echo "MT76 set,don't forget the firmware-files...";
				fi
			else
				echo "install of modules skipped because no kernel was installed";
			fi

			echo "syncing sd-card...this will take a while"
			sync

			read -e -i "$openuenv" -p "open uenv-file [yn]? " input
			if [[ "$input" == "y" ]];then
				$0 uenv
			fi
			read -e -i "y" -p "umount SD card [yn]? " input
			if [[ "$input" == "y" ]];then
				$0 umount
			fi
		else
			echo "SD-Card not found!"
		fi
	fi
}

function deb {
#set -x
	check_dep "deb"
	if [[ $? -ne 0 ]];then exit 1;fi
	get_version
	ver=${kernver}-${board}${gitbranch}
	uimagename=uImage_${kernver}${gitbranch}
	echo "deb package ${ver}"
	prepare_SD
	boardv=${board:4}
	targetdir=debian/bananapi-$boardv-image

#    cd ../SD
#    fname=bpi-r2_${kernver}_${gitbranch}.tar.gz
#    tar -cz --owner=root --group=root -f $fname BPI-BOOT BPI-ROOT

	rm -rf $targetdir/boot/bananapi/$board/linux/*
	rm -rf $targetdir/lib/modules/*
	mkdir -p $targetdir/boot/bananapi/$board/linux/dtb/
	mkdir -p $targetdir/lib/modules/
	mkdir -p $targetdir/DEBIAN/

	#sudo mount --bind ../SD/BPI-ROOT/lib/modules debian/bananapi-r2-image/lib/modules/
	if [[ -e ./uImage || -e ./uImage_nodt ]] && [[ -d ../SD/BPI-ROOT/lib/modules/${ver} ]]; then
	if [[ -e ./uImage ]];then
		cp ./uImage $targetdir/boot/bananapi/$board/linux/${uimagename}
	fi
	if [[ -e ./uImage_nodt && -e ./$board.dtb ]];then
		cp ./uImage_nodt $targetdir/boot/bananapi/$board/linux/${uimagename}_nodt
		cp ./$board.dtb $targetdir/boot/bananapi/$board/linux/dtb/$board-${kernver}${gitbranch}.dtb
	fi
#    pwd
	cp -r ../SD/BPI-ROOT/lib/modules/${ver} $targetdir/lib/modules/
	#rm debian/bananapi-r2-image/lib/modules/${ver}/{build,source}
	#mkdir debian/bananapi-r2-image/lib/modules/${ver}/kernel/extras
	#cp cryptodev-linux/cryptodev.ko debian/bananapi-r2-image/lib/modules/${ver}/kernel/extra
	cat > $targetdir/DEBIAN/preinst << EOF
#!/bin/bash
clr_red=\$'\e[1;31m'
clr_green=\$'\e[1;32m'
clr_yellow=\$'\e[1;33m'
clr_blue=\$'\e[1;34m'
clr_reset=\$'\e[0m'
m=\$(mount | grep '/boot[^/]')
if [[ -z "\$m" ]];
then
	echo "\${clr_red}/boot needs to be mountpoint for /dev/mmcblk0p1\${clr_reset}";
	exit 1;
fi
kernelfile=/boot/bananapi/$board/linux/${uimagename}
if [[ -e "\${kernelfile}" ]];then
	echo "\${clr_red}\${kernelfile} already exists\${clr_reset}"
	echo "\${clr_red}please remove/rename it or uninstall previous installed kernel-package\${clr_reset}"
	exit 2;
fi
EOF
	chmod +x $targetdir/DEBIAN/preinst
	cat > $targetdir/DEBIAN/postinst << EOF
#!/bin/bash
clr_red=\$'\e[1;31m'
clr_green=\$'\e[1;32m'
clr_yellow=\$'\e[1;33m'
clr_blue=\$'\e[1;34m'
clr_reset=\$'\e[0m'
case "\$1" in
	configure)
	#install|upgrade)
		echo "kernel=${uimagename}">>/boot/bananapi/$board/linux/uEnv.txt

		#check for non-dsa-kernel (4.4.x)
		kernver=\$(uname -r)
		if [[ "\${kernver:0:3}" == "4.4" ]];
		then
			echo "\${clr_yellow}you are upgrading from kernel 4.4.\${clr_reset}";
			echo "\${clr_yellow}Please make sure your network-config (/etc/network/interfaces) matches dsa-driver\${clr_reset}";
			echo "\${clr_yellow}(bring cpu-ports ethx up, ip-configuration to wan/lanx)\${clr_reset}";
		fi
	;;
	*) echo "unhandled \$1 in postinst-script"
esac
EOF
	chmod +x $targetdir/DEBIAN/postinst
	cat > $targetdir/DEBIAN/postrm << EOF
#!/bin/bash
case "\$1" in
	abort-install)
		echo "installation aborted"
	;;
	remove|purge)
		if [[ -e /boot/bananapi/$board/linux/uEnv.txt ]];then
			cp /boot/bananapi/$board/linux/uEnv.txt /boot/bananapi/$board/linux/uEnv.txt.bak
			grep -v  ${uimagename} /boot/bananapi/$board/linux/uEnv.txt.bak > /boot/bananapi/$board/linux/uEnv.txt
		else
			cp /boot/uEnv.txt /boot/uEnv.txt.bak
			grep -v  ${uimagename} /boot/uEnv.txt.bak > /boot/uEnv.txt
		fi
	;;
esac
EOF
	chmod +x $targetdir/DEBIAN/postrm
	if [[ "$board" == "bpi-r64" ]];then
		debarch=arm64
	else
		debarch=armhf
	fi

    cat > $targetdir/DEBIAN/control << EOF
Package: bananapi-$boardv-image-${kernbranch}
Version: ${kernver}-1
Section: custom
Priority: optional
Architecture: $debarch
Multi-Arch: no
Essential: no
Maintainer: Frank Wunderlich
Description: ${BOARD^^} linux image ${ver}
EOF
    cd debian
    fakeroot dpkg-deb --build bananapi-$boardv-image ../debian
    cd ..
    ls -lh debian/*.deb
    debfile=debian/bananapi-$boardv-image-${kernbranch,,}_${kernver}-1_$debarch.deb
    dpkg -c $debfile

	dpkg -I $debfile
  else
    echo "First build kernel ${ver}"
    echo "eg: ./build"
  fi
}

function build {
	echo Cleanup Kernel Build
	rm arch/arm/boot/zImage 2>/dev/null
	rm arch/arm/boot/zImage-dtb 2>/dev/null
	rm arch/arm64/boot/Image 2>/dev/null
	rm ./uImage* 2>/dev/null
	rm ${board}.dtb 2>/dev/null

	check_dep "build"
	if [[ $? -ne 0 ]];then exit 1;fi

	get_version

	if [ -e $DOTCONFIG ]; then
		exec 3> >(tee build.log)
		export LOCALVERSION="${gitbranch}"
		#MAKEFLAGS="V=1"
		make ${MAKEFLAGS} ${CFLAGS} 2>&3
		ret=$?
		exec 3>&-

		if [[ $ret == 0 ]]; then
			if [[ "$board" == "bpi-r64" ]];then
				if [[ "$builddir" != "" ]];
				then
					cp $builddir/arch/arm64/boot/Image arch/arm64/boot/Image
					cp $builddir/arch/arm64/boot/dts/mediatek/mt7622-bananapi-bpi-r64.dtb arch/arm64/boot/dts/mediatek/mt7622-bananapi-bpi-r64.dtb
				fi
				#how to create zImage?? make zImage does not work here
				if [[ -z "${uimagearch}" ]];then uimagearch=arm;fi
				mkimage -A ${uimagearch} -O linux -T kernel -C none -a 40080000 -e 40080000 -n "Linux Kernel $kernver$gitbranch" -d arch/arm64/boot/Image ./uImage_nodt
				cp arch/arm64/boot/dts/mediatek/mt7622-bananapi-bpi-r64.dtb $board.dtb
				sed "s/%version%/$kernver$gitbranch/" ${board}.its > ${board}.its.tmp
				mkimage -f ${board}.its.tmp ${board}.itb
				cp ${board}.itb ${board}-$kernver$gitbranch.itb
				rm ${board}.its.tmp
			else
				if [[ "$builddir" != "" ]];
				then
					cp $builddir/arch/arm/boot/zImage arch/arm/boot/zImage
					cp $builddir/arch/arm/boot/dts/mt7623n-bananapi-bpi-r2.dtb arch/arm/boot/dts/mt7623n-bananapi-bpi-r2.dtb
				fi
				cat arch/arm/boot/zImage arch/arm/boot/dts/mt7623n-bananapi-bpi-r2.dtb > arch/arm/boot/zImage-dtb
				mkimage -A arm -O linux -T kernel -C none -a 80008000 -e 80008000 -n "Linux Kernel $kernver$gitbranch" -d arch/arm/boot/zImage-dtb ./uImage

				echo "build uImage without appended DTB..."
				export DTC_FLAGS=-@
				make ${CFLAGS} CONFIG_ARM_APPENDED_DTB=n &>/dev/null #output/errors can be ignored because they are printed before
				ret=$?
				if [[ $ret == 0 ]]; then
					cp arch/arm/boot/dts/mt7623n-bananapi-bpi-r2.dtb $board.dtb
					mkimage -A arm -O linux -T kernel -C none -a 80008000 -e 80008000 -n "Linux Kernel $kernver$gitbranch" -d arch/arm/boot/zImage ./uImage_nodt
				fi
			fi
		fi
	else
		echo "No Configfile found, Please Configure Kernel"
	fi
}

function prepare_SD {
	SD=../SD
	cd $(dirname $0)
	mkdir -p ../SD >/dev/null 2>/dev/null

	echo "cleanup..."
	for toDel in "$SD/BPI-BOOT/" "$SD/BPI-ROOT/"; do
		rm -r ${toDel} 2>/dev/null
	done

	for createDir in "$SD/BPI-BOOT/bananapi/$board/linux/dtb" "$SD/BPI-ROOT/lib/modules" "$SD/BPI-ROOT/lib/firmware"; do
		mkdir -p ${createDir} >/dev/null 2>/dev/null
	done

	echo "copy..."
	export INSTALL_MOD_PATH=$SD/BPI-ROOT/;
	echo "INSTALL_MOD_PATH: $INSTALL_MOD_PATH"
	if [[ -e ./uImage ]];then
		cp ./uImage $SD/BPI-BOOT/bananapi/$board/linux/uImage
	fi
	if [[ -e ./uImage_nodt ]];then
    		cp ./uImage_nodt $SD/BPI-BOOT/bananapi/$board/linux/uImage_nodt
		cp ./$board.dtb $SD/BPI-BOOT/bananapi/$board/linux/dtb/$board.dtb
	fi
	make modules_install

	#Add CryptoDev Module if exists or Blacklist
	CRYPTODEV="utils/cryptodev/cryptodev-linux/cryptodev.ko"
	mkdir -p "${INSTALL_MOD_PATH}/etc/modules-load.d"
	mkdir -p "${INSTALL_MOD_PATH}/etc/modprobe.d"

	LOCALVERSION=$(find ../SD/BPI-ROOT/lib/modules/* -maxdepth 0 -type d |rev|cut -d"/" -f1 | rev)
	EXTRA_MODUL_PATH="${SD}/BPI-ROOT/lib/modules/${LOCALVERSION}/kernel/extras"
	if [ -e "${CRYPTODEV}" ]; then
		echo Copy CryptoDev
		mkdir -p "${EXTRA_MODUL_PATH}"
		cp "${CRYPTODEV}" "${EXTRA_MODUL_PATH}"
		#Load Cryptodev on BOOT
		echo  "cryptodev" >${INSTALL_MOD_PATH}/etc/modules-load.d/cryptodev.conf

		#Build Module Dependencies
		/sbin/depmod -b "${SD}/BPI-ROOT/" ${LOCALVERSION}
	else
		#Blacklist Cryptodev Module
		echo "blacklist cryptodev" >${INSTALL_MOD_PATH}/etc/modprobe.d/cryptodev.conf
	fi

	if [[ -n "$(grep 'CONFIG_MT76=' $DOTCONFIG)" ]];then
		echo "MT76 set, including the firmware-files...";
		cp drivers/net/wireless/mediatek/mt76/firmware/* $SD/BPI-ROOT/lib/firmware/
	fi

	if [[ "$board" == "bpi-r64" ]];then
		mkdir -p $SD/BPI-ROOT/lib/firmware/mediatek
		cp utils/firmware/mediatek/mt7622* $SD/BPI-ROOT/lib/firmware/mediatek/
	else
		if [[ -e "utils/wmt/src/wmt_loader" ]];then
			mkdir -p "$SD/BPI-ROOT/etc/firmware" "$SD/BPI-ROOT/usr/bin" "$SD/BPI-ROOT/system/etc/firmware"

			cp utils/wmt/config/* $SD/BPI-ROOT/system/etc/firmware/
			cp utils/wmt/src/{wmt_loader,wmt_loopback,stp_uart_launcher} $SD/BPI-ROOT/usr/bin/
			cp -r utils/wmt/firmware/* $SD/BPI-ROOT/etc/firmware/
		else
			echo "WMT-Tools not available"
		fi
	fi
}

function release
{
	lc=$(git log -n 1 --pretty=format:'%s')
	reltag="Release_${kernver}"
	if [[ ${lc} =~ ^Merge ]];
	then
		echo Merge;
	else
		echo "normal commit";
		lc=${lc//[^a-zA-Z0-9]/_}
		reltag="${reltag}_${lc}"
	fi
	echo "RelTag:"$reltag
	if [[ -z "$(git tag -l $reltag)" ]]; then
	git tag $reltag
	git push origin $reltag
	else
		echo "Tag already used, please use another"
	fi
}

#Test if the Kernel is there
if [ -n "$kernver" ]; then
	action=$1
	file=$2
	LANG=C
	CFLAGS=-j$(grep ^processor /proc/cpuinfo  | wc -l)

	#echo $action

	case "$action" in
		"checkdep")
			echo "checking depencies..."
			check_dep "build";
			if [[ $? -eq 0 ]];then echo "OK";else echo "failed";fi
		;;
		"reset")
			echo "Reset Git"
			##Reset Git
			git reset --hard HEAD
			#call self and Import Config
			$0 importconfig
			;;

		"update")
			echo "Update Git Repo"
			git pull
			;;

		"updatesrc")
			echo "Update kernel source"
			update_kernel_source
			;;

		"umount")
			echo "umount SD Media"
			umount /media/$USER/BPI-BOOT
			umount /media/$USER/BPI-ROOT
			;;

		"uenv")
			echo "edit uEnv.txt on sd-card"
			uenv=$(getuenvpath)
			if [[ -n "$uenv" ]];then
				edit $uenv
			else
				echo "uenv.txt not found"
			fi
			;;

		"lskernel")
			echo "list kernels on sd-card"
			ls -lh /media/$USER/BPI-BOOT/bananapi/$board/linux/
			echo "available DTBs:"
			ls -lh /media/$USER/BPI-BOOT/bananapi/$board/linux/dtb
			;;

		"defconfig")
			echo "edit def config"
			if [[ "$board" == "bpi-r64" ]];then
				edit arch/arm64/configs/mt7622_bpi-r64_defconfig
			else
				edit arch/arm/configs/mt7623n_evb_fwu_defconfig
			fi
			;;
		"deb")
			echo "deb-package (currently testing-state)"
			deb
			;;
		"dtsi")
			if [[ "$board" == "bpi-r64" ]];then
				echo "edit mt7622.dtsi"
				edit arch/arm64/boot/dts/mediatek/mt7622.dtsi
			else
				echo "edit mt7623.dtsi"
				edit arch/arm/boot/dts/mt7623.dtsi
			fi
			;;

		"dts")
			if [[ "$board" == "bpi-r64" ]];then
				echo "edit mt7622n-bpi-r64.dts"
				edit arch/arm64/boot/dts/mediatek/mt7622-bananapi-bpi-r64.dts
			else
				echo "edit mt7623n-bpi-r2.dts"
				edit arch/arm/boot/dts/mt7623n-bananapi-bpi-r2.dts
			fi
			;;

		"importmylconfig")
			echo "import myl config"
			make mt7623n_myl_defconfig
			;;


		"importconfig")
			echo "import a defconfig file"
			if [[ "$board" == "bpi-r64" ]];then
				p=arm64
				echo "Import r64 config"
				f=mt7622_bpi-r64_defconfig
			else
				p=arm
				if [[ -z "$file" ]];then
					echo "Import fwu config"
					f=mt7623n_evb_fwu_defconfig
				else
					echo "Import config: $f"
					f=mt7623n_${file}_defconfig
				fi
			fi
			if [[ -e "arch/${p}/configs/${f}" ]];then
				make ${f}
				if [[ -n "$disable" ]];then
					disable_option "$disable"
				fi
			else
				echo "file not found"
			fi
			;;

		"ic")
			echo "menu for multiple conf-files...currently in developement"
			files=();
			i=1;
			if [[ "$board" == "bpi-r64" ]];then
				p=arch/arm64/configs/
				ff="mt7622*defconfig"
			else
				p=arch/arm/configs/
				ff="mt7623n*defconfig"
			fi
			for f in $(cd $p; ls $ff)
			do
				echo "[$i] $f"
				files+=($f)
				i=$(($i+1))
			done
			read -n1 -p "choice [1..${#files[@]}]:" choice
			echo
			set -x
			make ${files[$(($choice-1))]}
			set +x
		;;
	  	"config")
			echo "change kernel-configuration (menuconfig)"
			make menuconfig
			;;

		"pack")
			echo "Pack Kernel to Archive"
			pack
			;;

		"pack_debs")
			echo "Pack Kernel to linux-*.deb"
			pack_debs
			;;

		"install")
			echo "Install Kernel to SD Card"
			install
			;;

		"upload")
			echo "Upload Kernel to TFTP-Server"
			upload
			;;

		"build")
			echo "Build Kernel"
			build
			#$0 cryptodev
			;;
		"clean")
			echo clean
			make clean
			;;
		"spidev")
			echo "Build SPIDEV-Test"
			(
				cd tools/spi;
				make #CROSS_COMPILE=arm-linux-gnueabihf-
			)
			;;

		"cryptodev")
			echo "Build CryptoDev"
			cdbuildscript=utils/cryptodev/build.sh
			if [[ -e $cdbuildscript ]];then
				$cdbuildscript
			fi
			;;

		"utils")
			echo "Build utils"
			( cd utils; make )
			;;

		"release")
			echo "create release tag for travis-ci build"
			release
			;;
		"all-pack")
			echo "update repo, create kernel & build archive"
			$0 update
			$0 importconfig
			$0 build
			#$0 cryptodev
			$0 pack
			;;

		"help")
			echo "print help"
			sed -n -e '/case "$action" in/,/esac/{//!p}'  $0 | grep -A1 '")$' | sed -e 's/echo "\(.*\)"/\1/'
			;;
		*)
			if [[ -n "$action" ]];then
				echo "unknown command $action";
				exit 1;
			fi;
			$0 build
			#$0 cryptodev
			if [ -e "./uImage" ] || [ -e "./uImage_nodt" ]; then
				echo "==========================================="
				echo "1) pack"
				if [[ $crosscompile -eq 0 ]];then
					echo "2) install to System"
				else
					echo "2) install to SD-Card"
				fi;
				echo "3) deb-package"
				echo "4) upload"
				read -n1 -p "choice [1234]:" choice
				echo
				if [[ "$choice" == "1" ]]; then
					$0 pack
				elif [[ "$choice" == "2" ]];then
					$0 install
				elif [[ "$choice" == "3" ]];then
					$0 deb
				elif [[ "$choice" == "4" ]];then
					$0 upload
				else
					echo "wrong option: $choice"
				fi
			fi
 			;;
	esac
fi
