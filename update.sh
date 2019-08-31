#!/bin/bash
k=$(make kernelversion)
#4.4.140
kn=$(echo $k|awk -F. '{print $1"."$2"."($3+1)}')
#4.4.141
kf=$(echo $k|awk -F. '{print $1"."$2"."$3"-"($3+1)}')
#4.4.140-141

wget https://cdn.kernel.org/pub/linux/kernel/v4.x/incr/patch-$kf.xz
unxz patch-$kf.xz
patch -p1 --dry-run < patch-$kf
ret=$?
echo "patch-ret:$ret"
if [[ $ret -eq 0 ]];
then
	patch -p1 < patch-$kf
#	git status | grep -v ../u-boot | grep -v patch
	untracked=$(git ls-files --others --exclude-standard | grep -v patch)
	if [[ -z "$untracked" ]]; then
		echo "no untracked"
		git add -u
		git reset -- update.sh
		git commit -m "Update to $(make kernelversion)"
	else
		echo -e "please add untracked files:\n$untracked"
		git add $untracked
		git add -u
		git reset -- update.sh
		#git commit -m "Update to $(make kernelversion)"
	fi
else
	echo -e "please call patch manually\npatch -p1 < patch-$kf"
fi
