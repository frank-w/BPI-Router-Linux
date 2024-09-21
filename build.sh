#!/bin/bash
if [[ $UID -eq 0 ]];
then
  echo "do not run as root!"
  exit 1;
fi

numproc=$(grep ^processor /proc/cpuinfo  | wc -l)

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

DEFCONFIG=""
DTS=""
DTSI=""

case $board in
	"bpi-r2pro")
		ARCH=arm64
		CONFIGPATH=arch/$ARCH/configs
		DEFCONFIG=$CONFIGPATH/rk3568_bpi-r2p_defconfig
		DEFCONFIG=$CONFIGPATH/quartz64_defconfig
		if [[ "$boardversion" == "v00" ]];then
			DTS=arch/arm64/boot/dts/rockchip/rk3568-bpi-r2-pro-rtl8367.dts
		else
			DTS=arch/arm64/boot/dts/rockchip/rk3568-bpi-r2-pro.dts
		fi
		DTSI=arch/arm64/boot/dts/rockchip/rk356x.dtsi
		;;
	"bpi-r64")
		ARCH=arm64
		CONFIGPATH=arch/$ARCH/configs
		DEFCONFIG=$CONFIGPATH/mt7622_bpi-r64_defconfig
		if [[ "$boardversion" == "v0.1" ]];then
			DTS=arch/arm64/boot/dts/mediatek/mt7622-bananapi-bpi-r64-rtl8367.dts
		else
			DTS=arch/arm64/boot/dts/mediatek/mt7622-bananapi-bpi-r64.dts
		fi
		DTSI=arch/arm64/boot/dts/mediatek/mt7622.dtsi
		;;
	"bpi-r3")
		ARCH=arm64
		CONFIGPATH=arch/$ARCH/configs
		DEFCONFIG=$CONFIGPATH/mt7986a_bpi-r3_defconfig
		DTS=arch/arm64/boot/dts/mediatek/mt7986a-bananapi-bpi-r3.dts
		#DTS=arch/arm64/boot/dts/mediatek/mt7986a-bananapi-bpi-r3-sd.dts
		DTSI=arch/arm64/boot/dts/mediatek/mt7986a.dtsi
		;;
	"bpi-r4")
		ARCH=arm64
		CONFIGPATH=arch/$ARCH/configs
		DEFCONFIG=$CONFIGPATH/mt7988a_bpi-r4_defconfig
		DTS=arch/arm64/boot/dts/mediatek/mt7988a-bananapi-bpi-r4.dts
		DTSI=arch/arm64/boot/dts/mediatek/mt7988a.dtsi
		;;
	*) #bpir2
		ARCH=arm
		CONFIGPATH=arch/$ARCH/configs
		DEFCONFIG=$CONFIGPATH/mt7623n_evb_fwu_defconfig
		DTS=arch/arm/boot/dts/mediatek/mt7623n-bananapi-bpi-r2.dts
		DTSI=arch/arm/boot/dts/mediatek/mt7623.dtsi
		;;
esac
#echo "DTB:${DTS%.*}.dtb"
export ARCH=$ARCH;

#defconfig-override
if [[ "$1" =~ ^(importconfig|defconfig)$ && -n "$2" ]];
then
	if [[ -e "${CONFIGPATH}/${2}_defconfig" ]];then
		echo "using defconfig ${2}_defconfig"
		DEFCONFIG="${CONFIGPATH}/${2}_defconfig"
	else
		echo "defconfig ${2}_defconfig not found!"
	fi
fi

#Check Crosscompile
crosscompile=0
hostarch=$(uname -m) #r64:aarch64 r2: armv7l

if [[ ! $hostarch =~ aarch64|armv ]];then
	if [[ "$ARCH" == "arm64" ]]; then
		if [[ -z "$(which aarch64-linux-gnu-gcc)" ]];then echo "please install gcc-aarch64-linux-gnu";exit 1;fi
		export CROSS_COMPILE='ccache aarch64-linux-gnu-'
	elif [[ "$ARCH" == "arm" ]]; then
		if [[ -z "$(which arm-linux-gnueabihf-gcc)" ]];then echo "please install gcc-arm-linux-gnueabihf";exit 1;fi
		export CROSS_COMPILE='ccache arm-linux-gnueabihf-'
	fi

	crosscompile=1
fi;

#if [[ "$builddir" != "" && ! "$1" =~ ^(updatesrc|uenv|defconfig|dts.?|[u]?mount)$ ]];
if [[ "$builddir" != "" ]];
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
			if [[ "$1" == "importconfig" ]];
			then
				echo "mounting tmpfs for building..."
				sudo mount -t tmpfs -o size=$ramdisksize none $builddir
			fi
		fi
	fi

	#if builddir is empty then run make mrproper to clean up sourcedir
	if [[ ! "$(ls -A $builddir)" ]];then make mrproper;fi

	if ! [[ "$1" =~ "updatesrc" ]];then
		DOTCONFIG="$builddir/$DOTCONFIG"
		export KBUILD_OUTPUT=$builddir
	fi
fi

function edit()
{
	file=$1
	if [[ -z "$EDITOR" ]];then EDITOR=/usr/bin/editor;fi #use alternatives setting
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
			NEEDED_PKGS+=" u-boot-tools bc gcc libc6-dev libncurses-dev ccache libssl-dev"
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
	if [[ -d $uenv_base ]];then
		if [[ "$board" == "bpi-r2" || "$board" == "bpi-r64" ]];then
			#r2/r64 uboot: ${bpi}/${board}/${service}/${bootenv}
			uenv=${uenv_base}/bananapi/$board/linux/uEnv.txt
		else
			uenv=${uenv_base}/uEnv.txt
		fi
		if [[ ! -e $uenv ]];then
			mkdir -p $(dirname ${uenv})
		fi
	fi
	echo $uenv
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

	gitbranch=$(echo $kernbranch|sed 's/^[456]\.[0-9]\+//'|sed 's/-rc$//')

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

function changelog() {
	get_version >/dev/null
	lasttag=$(curl -s "https://api.github.com/repos/frank-w/BPI-R2-4.14/releases?per_page=100" | jq -r '[.[] | select(.tag_name|contains("'${kernbranch}'"))][0]' | grep "tag_name" | sed 's/.*: "\(.*\)",/\1/')
	echo "changes since $lasttag:"
	git log --pretty=format:"%h %ad %s %d" --date=short $lasttag..HEAD
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
	LOCALVERSION="${gitbranch}" board="$board" KDEB_COMPRESS=gzip make bindeb-pkg
	ls ../*.deb
}


function upload {
	get_version

	DTBFILE=${DTS%.*}.dtb
	echo "using ${DTBFILE}..."
	bindir="";
	if [[ -n "$builddir" ]];then bindir="$builddir/"; fi

	b=${board//bpi/}
	if [[ "${gitbranch}" =~ "${b}" ]]; then
		b=""
	fi

	imagename="${kernver}${gitbranch}${b}"
	read -e -i $imagename -p "Kernel-filename: " input
	imagename="${input:-$imagename}"

	echo "Name: $imagename"

	if [[ "$board" == "bpi-r2" ]];then
		imagename="uImage_$imagename"
	else
		read -e -i y -p "upload fit? " fitupload
		if [[ "$fitupload" == "y" ]];
		then
			imagename="${imagename}.itb"
			echo "uploading fit as $imagename"
		else
			dtbname="${kernver}${b}${gitbranch}.dtb"
			read -e -i $dtbname -p "dtb-filename: " input
			dtbname="${input:-$dtbname}"
			echo "DTB Name: $dtbname"
		fi
	fi

	echo "DTB Name: $dtbname"
	echo "uploading to ${uploadserver}:${uploaddir}..."

	if [[ "$board" == "bpi-r2" ]];then
		scp uImage ${uploaduser}@${uploadserver}:${uploaddir}/${imagename}
	else
		if [[ "$fitupload" == "y" ]];
		then
			scp ${board}.itb ${uploaduser}@${uploadserver}:${uploaddir}/${imagename}
		else
			scp uImage_nodt ${uploaduser}@${uploadserver}:${uploaddir}/${imagename}
			scp ${bindir}${DTBFILE} ${uploaduser}@${uploadserver}:${uploaddir}/${dtbname}
		fi
	fi
}

function install
{
	get_version

	DTBFILE=${DTS%.*}.dtb
	echo "using ${DTBFILE}..."
	bindir="";
	if [[ -n "$builddir" ]];then bindir="$builddir/"; fi

	imagename="${kernver}${gitbranch}"
	if [[ "$board" == "bpi-r2pro" ]];then
		$0 mount
	fi
	read -e -i $imagename -p "Image-filename: " input
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
		imginput=n
		read -p "Press [enter] to copy data to SD-Card..."
		if  [[ -d /media/$USER/BPI-BOOT ]]; then

			targetdir=/media/$USER/BPI-BOOT
			if [[ "$board" == "bpi-r2" || "$board" == "bpi-r64" ]];then
				targetdir=/media/$USER/BPI-BOOT/bananapi/$board/linux
			fi
			mkdir -p $targetdir
			kernelfile=$targetdir/$imagename

			dtinput=n
			ndtinput=y
			imginput=y
			if [[ -e ${board}.its ]];then
				read -e -i "y" -p "install FIT kernel (itb) [yn]? " itbinput
				if [[ "$itbinput" == "y" ]];then
					itbname=${imagename}.itb
					itbfile=$targetdir/$itbname
					if [[ -e $itbfile ]];then
						echo "backup of kernel: $itbfile.bak"
						cp $itbfile $itbfile.bak
					fi
					echo "copy new kernel"
					cp ./${board}.itb $itbfile
					if [[ $? -ne 0 ]];then exit 1;fi
					ndtinput=n
					imginput=n
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

			if [[ "$board" == "bpi-r2pro" ]];then
				targetdir=/media/$USER/BPI-BOOT/extlinux
				read -e -i "$imginput" -p "install img kernel (img.gz) [yn]? " imginput
				if [[ "$imginput" == "y" ]];then
					dtbname=${imgname}.dtb
					imgname=${imagename}.gz
					imgfile=$targetdir/$imgname
					if [[ -e ${imgfile} ]];then
						echo "backup of kernel: $imgfile.bak"
						cp ${imgfile} ${imgfile}.bak
					fi
					echo "copy new kernel"
					set -x
					cp ${bindir}arch/arm64/boot/Image.gz $imgfile
					cp ${bindir}${DTBFILE} ${targetdir}/${dtbname}
					set +x
					if [[ $? -ne 0 ]];then exit 1;fi
					ndtinput=n
				fi
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
				cp ${bindir}${DTBFILE} $dtbfile
			fi

			if [[ "$dtinput" == "y" ]] || [[ "$ndtinput" == "y" ]] || [[ "$itbinput" == "y" ]]  || [[ "$imginput" == "y" ]];then
				echo "copy modules (root needed because of ext-fs permission)"
				export INSTALL_MOD_PATH=/media/$USER/BPI-ROOT/;
				echo "INSTALL_MOD_PATH: $INSTALL_MOD_PATH"
				sudo make ARCH=$ARCH INSTALL_MOD_PATH=$INSTALL_MOD_PATH KBUILD_OUTPUT=$KBUILD_OUTPUT modules_install

				echo "uImage:"
				if [[ "$itbinput" == "y" ]];then
					echo "${itbfile}"
				fi
				if [[ "$dtinput" == "y" ]];then
					echo "${kernelfile}"
				fi
				if [[ "$ndtinput" == "y" ]];then
					echo "${kernelfile}${ndtsuffix}"
					echo "DTB: ${dtbfile}"
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
			echo "run 'watch -n 1 grep -e Dirty: /proc/meminfo' to show progress"
			sync

			uenv=$(getuenvpath)
			echo "using bootfile $uenv"
			openuenv=n

			if [[ -e "$uenv" ]];then
				echo "by default this kernel-/dtb-file will be loaded (kernel-var in uEnv.txt):"
				if [[ "$itbinput" == "y" ]];then
					curkernel=$(grep '^fit=' $uenv|tail -1| sed 's/fit=//')
				else
					curkernel=$(grep '^kernel=' $uenv|tail -1| sed 's/kernel=//')
					curfdt=$(grep '^fdt=' $uenv|tail -1| sed 's/fdt=//')
				fi
				echo "kernel: " $curkernel
				echo "dtb: " $curfdt
				if [[ "$curkernel" == "${imagename}" || "$curkernel" == "${imagename}${ndtsuffix}" || "$curkernel" == "${itbname}" ]];then
					echo "no change needed!"
					openuenv=n
				else
					echo "change needed to boot new kernel (kernel=${imagename}/fit=${itbname})!"
					openuenv=y
				fi
			else
				echo "no bootconfig...change needed to boot new kernel (kernel=${imagename}/fit=${itbname})!"
				openuenv=y
			fi

			read -e -i "$openuenv" -p "open boot config file (uEnv/extlinux.conf) [yn]? " input
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

function install_modules {
	echo "cleaning up modules"
	rm -r $(pwd)/mod/* 2> /dev/null
	mkdir $(pwd)/mod
	echo "building and installing modules"
#	export LOCALVERSION="${gitbranch}"
#	#MAKEFLAGS="V=1"
#	make ${MAKEFLAGS} ${CFLAGS} modules
	build
	INSTALL_MOD_PATH=$(pwd)/mod make modules_install
}

function mod2initrd {
	size=$(du -s mod | awk '{ print $1}') #65580 working
	echo $size
	if [[ $size -eq 0 ]];
	then
		install_modules
	fi
	#first reset initrd
	echo "reset initramfs..."
	git checkout utils/buildroot/rootfs_${board}.cpio.gz
	ls -lh utils/buildroot/rootfs_${board}.cpio.gz
	(
		echo "adding modules to initramfs..."
		cd mod
		#need to create dirs and common files first
		find . -not -iname '*.ko' > mod.lst
		if [[ "$ownmodules" != "" ]];then
			find . | grep $ownmodules >> mod.lst
		fi
		cat mod.lst | cpio -H newc -o | gzip >> ../utils/buildroot/rootfs_${board}.cpio.gz
		#to show whats inside cpio:
		#gunzip -c < ../utils/buildroot/rootfs_${board}.cpio.gz | while LANG=C cpio -itv 2>/dev/null; do :; done| grep lib/modules
		ls -lh ../utils/buildroot/rootfs_${board}.cpio.gz
	)
	initrdsize=$(ls -l --block-size=M utils/buildroot/rootfs_bpi-r2.cpio.gz | awk '{print $5}' | sed 's/M$//')
	if [[ $initrdsize -lt 100 ]]; #lower than 40MB leaves 10MB for kernel uImage
	then
		echo "re-building kernel with initramfs... ($DOTCONFIG)"
		OWNCONFIGS="CONFIG_INITRAMFS_SOURCE=\"utils/buildroot/rootfs_${board}.cpio.gz\" CONFIG_INITRAMFS_FORCE=y"
		build
		installchoice
	else
		echo "kernel with initrd may exceed uboot limit of ~100MB (Bad Data CRC)"
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
	if [[ "$board" == "bpi-r2" ]];then
		debarch=armhf
	else
		debarch=arm64
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
		export DTC_FLAGS="-@"
		make ${MAKEFLAGS} ${CFLAGS} ${OWNCONFIGS} 2>&3
		ret=$?
		exec 3>&-

		if [[ $ret == 0 ]]; then
			if [[ -z "${uimagearch}" ]];then uimagearch=arm;fi

			DTBFILE=${DTS%.*}.dtb
			case "$board" in
				"bpi-r2pro")
					IMAGE=arch/arm64/boot/Image
					LADDR=0x7f000001
					ENTRY=0x7f000001
				;;
				"bpi-r64")
					IMAGE=arch/arm64/boot/Image
					LADDR=40080000
					ENTRY=40080000
				;;
				"bpi-r3")
					IMAGE=arch/arm64/boot/Image
					LADDR=40080000
					ENTRY=40080000
					if [[ -e mt7986a-bananapi-bpi-r3-nor.dts ]];then
						echo "compiling r3 dt overlays"
						#dtc -O dtb -o bpi-r3-nor.dtbo mt7986a-bananapi-bpi-r3-nor.dts
						#dtc -O dtb -o bpi-r3-nand.dtbo mt7986a-bananapi-bpi-r3-nand.dts
					fi
				;;
				"bpi-r4")
					IMAGE=arch/arm64/boot/Image
					LADDR=40080000
					ENTRY=40080000
				;;
				"bpi-r2")
					IMAGE=arch/arm/boot/zImage
					LADDR=80008000
					ENTRY=80008000
				;;
			esac

			if [[ "$builddir" != "" ]];
			then
				cp $builddir/${IMAGE%.*}* ${IMAGE%/*}
				DTBBASE=${DTBFILE%.*}
				if [[ -e $builddir/${DTBBASE}.dtb ]];then
					cp $builddir/${DTBBASE}*.dtb ${DTBFILE%/*}
					cp $builddir/$DTBFILE $board.dtb
				fi
				if [[ -e $builddir/${DTBBASE}-sd.dtbo ]];then
					cp $builddir/${DTBBASE}-*.dtbo ${DTBFILE%/*}
				fi
			elif [[ -e $DTBFILE ]];then
				cp $DTBFILE $board.dtb
			fi

			#if [[ "$board" == "bpi-r2pro" ]];then
			#	#skipping mkimage causes no choice, but uImage is not bootable on r2pro
			#	mkimage -A ${uimagearch} -O linux -T kernel -C none -a $LADDR -e $ENTRY -n "Linux Kernel $kernver$gitbranch" -d $IMAGE ./uImage_nodt
			if [[ -e ${board}.its ]];then #"$board" == "bpi-r64" || "$board" == "bpi-r3" ]];then
				mkimage -A ${uimagearch} -O linux -T kernel -C none -a $LADDR -e $ENTRY -n "Linux Kernel $kernver$gitbranch" -d $IMAGE ./uImage_nodt
				sed "s/%version%/$kernver$gitbranch/" ${board}.its > ${board}.its.tmp
				mkimage -f ${board}.its.tmp ${board}.itb
				cp ${board}.itb ${board}-$kernver$gitbranch.itb
				rm ${board}.its.tmp
			else
				cat $IMAGE $DTBFILE > arch/arm/boot/zImage-dtb
				mkimage -A arm -O linux -T kernel -C none -a $LADDR -e $ENTRY -n "Linux Kernel $kernver$gitbranch" -d arch/arm/boot/zImage-dtb ./uImage

				echo "build uImage without appended DTB..."
				export DTC_FLAGS="-@ --space 32768"
				make ${CFLAGS} CONFIG_ARM_APPENDED_DTB=n &>/dev/null #output/errors can be ignored because they are printed before
				ret=$?
				if [[ $ret == 0 ]]; then
					mkimage -A arm -O linux -T kernel -C none -a $LADDR -e $ENTRY -n "Linux Kernel $kernver$gitbranch" -d $IMAGE ./uImage_nodt
				fi
			fi

			if [[ "$builddir" != "" ]];
			then
				if [[ -e uImage ]];then
					cp {,$builddir/}uImage
				fi
				cp {,$builddir/}uImage_nodt
				if [[ -e ${board}.itb ]];then
					cp {,$builddir/}${board}.itb
				fi
			fi
		fi
	else
		echo "No Configfile found, Please Configure Kernel"
	fi
	return $ret
}

function prepare_SD {
	SD=../SD
	cd $(dirname $0)
	mkdir -p ../SD >/dev/null 2>/dev/null

	echo "cleanup..."
	for toDel in "$SD/BPI-BOOT/" "$SD/BPI-ROOT/"; do
		rm -r ${toDel} 2>/dev/null
	done

	echo "copy..."

	bindir="";
	if [[ -n "$builddir" ]];then bindir="$builddir/"; fi

	export INSTALL_MOD_PATH=$SD/BPI-ROOT/;
	echo "INSTALL_MOD_PATH: $INSTALL_MOD_PATH"
	kerndir="$SD/BPI-BOOT"
	fdtdir=$kerndir
	if [[ "$board" == "bpi-r2pro" ]];then
		mkdir -p $kerndir/extlinux/
		cp ${bindir}arch/arm64/boot/Image.gz $kerndir/extlinux/
		cp ./$board.dtb $kerndir/extlinux/$board.dtb
	else
		if [[ "$board" =~ bpi-r2|bpi-r64 ]];then
			kerndir=$SD/BPI-BOOT/bananapi/$board/linux
			fdtdir="$kerndir/dtb"
		fi
		for createDir in "$kerndir" "$fdtdir" "$SD/BPI-ROOT/lib/modules" "$SD/BPI-ROOT/lib/firmware"; do
			mkdir -p ${createDir} >/dev/null 2>/dev/null
		done
		if [[ -e ./uImage ]];then
			cp ./uImage $kerndir/uImage
		fi
		if [[ -e ./uImage_nodt ]];then
			cp ./uImage_nodt $kerndir/uImage_nodt
			cp ./$board.dtb $fdtdir/$board.dtb
		fi
	fi
	if [[ -e ./$board.itb ]];then
		cp ./$board.itb $kerndir/$board.itb
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
	fi
}

function installchoice
{
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
	CFLAGS=-j${numproc}

	#echo $action

	case "$action" in
		"checkdep")
			echo "checking depencies..."
			check_dep "build";
			if [[ $? -eq 0 ]];then echo "OK";else echo "failed";fi
		;;
		"changelog")
			echo "print changelog from last release of branch"
			changelog
		;;
		"changelog_file")
			echo "write changelog from last release of branch to file"
			changelog > changelog.txt
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

		"mount")
			mount | grep "BPI-BOOT" > /dev/null
			if [[ $? -ne 0 ]];then
				udisksctl mount -b /dev/disk/by-label/BPI-BOOT
			fi
			mount | grep "BPI-ROOT" > /dev/null
			if [[ $? -ne 0 ]];then
				udisksctl mount -b /dev/disk/by-label/BPI-ROOT
			fi
			;;

		"umount")
			echo "umount SD Media"
			dev=$(mount | grep BPI-ROOT | head -1 | sed -e 's/[0-9] .*$/?/' | sort -u)
			echo "$dev"
			if [[ ! -z "$dev" ]];then
				umount $dev
			fi
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
			edit $DEFCONFIG
			;;
		"deb")
			echo "deb-package (currently testing-state)"
			deb
			;;
		"dtsi")
			edit $DTSI
			;;

		"dts")
			edit $DTS
			;;
		"dtbs_check")
			make dtbs_check 2>&1 | tee -a dtbs_check.log
		;;

		"importmylconfig")
			echo "import myl config"
			make mt7623n_myl_defconfig
			;;


		"importconfig")
			echo "import a defconfig file"
			if [[ -e "${DEFCONFIG}" ]];then
				DEFCONF="${DEFCONFIG##*/}"
				echo "import $DEFCONF ($DEFCONFIG)"
				make ${DEFCONF}
				if [[ -n "$disable" ]];then
					disable_option "$disable"
				fi
			else
				echo "file ${DEFCONFIG} not found"
			fi
			;;

		"ic")
			echo "menu for multiple conf-files...currently in developement"
			files=();
			i=1;
			if [[ "$board" == "bpi-r2pro" ]];then
				p=arch/arm64/configs/
				ff=rk3568_bpi-r2p_defconfig
			elif [[ "$board" == "bpi-r64" ]];then
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
			echo "building with ${numproc} threads"
			time build
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
				make
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
		"install_modules")
				install_modules
			;;
		"mod2initrd")
				mod2initrd
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
			installchoice
 			;;
	esac
fi
