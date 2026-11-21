#!/system/bin/sh

RVPATH=/data/adb/rvhc/${MODDIR##*/}.apk
NM_BIN=/data/adb/modules/nomount/bin/nm
. "$MODDIR/config"

ch_desc() {
	sed -i "s|^description=.*|description=${1}|" "$MODDIR/module.prop"
}

ch_desc_err() {
	ch_desc "⚠️ Needs reflash: '${1}'"
}

pmex() {
	OP=$(pm "$@" 2>&1 </dev/null)
	RET=$?
	echo "$OP"
	return $RET
}

get_app_version() {
	VERSION=$(dumpsys package "$PKG_NAME" 2>&1 | grep -m1 versionName=) VERSION="${VERSION#*=}"
	echo "$VERSION"
}

get_basepath() {
	BASEPATH=$(pmex path "$PKG_NAME")
	SVCL=$?

	BASEPATH=${BASEPATH##*:} BASEPATH=${BASEPATH%/*}
	echo "$BASEPATH"
	return $SVCL
}

has_nomount() {
	[ -x "$NM_BIN" ] &&
		[ ! -f "/data/adb/modules/nomount/disable" ] &&
		[ ! -f "/data/adb/modules/nomount/remove" ] &&
		[ ! -f "/data/adb/nomount/disable" ] &&
		"$NM_BIN" version >/dev/null 2>&1
}

umount_all() {
	su -M -c grep -F "$PKG_NAME" /proc/mounts | while read -r line; do
		mp=${line#* } mp=${mp%% *} mp=${mp%%\\*}
		su -M -c umount -l "${mp}"
	done
	if [ -x "$NM_BIN" ]; then
		"$NM_BIN" rule list 2>/dev/null | grep -F "$PKG_NAME" | while read -r line; do
			vp=${line%% ->*}
			"$NM_BIN" rule del "$vp" >/dev/null 2>&1 || :
		done
	fi
	am force-stop "$PKG_NAME" || :
}

get_mounts() {
	su -M -c grep -F "$PKG_NAME" /proc/mounts || :
	if [ -x "$NM_BIN" ]; then
		"$NM_BIN" rule list 2>/dev/null | grep -F "$PKG_NAME" || :
	fi
}

mount_rv() {
	if [ ! -d "${1}/lib" ]; then
		ch_desc_err "Your installation got broken. Dont report this, consider using rvmm-zygisk-mount."
		return 1
	fi
	VERSION=$(get_app_version)
	if [ "$VERSION" != "$PKG_VER" ] && [ "$VERSION" ]; then
		ch_desc_err "Version mismatch (installed:$VERSION, module:$PKG_VER)"
		return 1
	fi
	umount_all
	if ! OP=$(chcon u:object_r:apk_data_file:s0 "$RVPATH" 2>&1); then
		ch_desc_err "Error chcon: '$OP'"
		return 1
	fi
	if has_nomount; then
		"$NM_BIN" rule del "${1}/base.apk" >/dev/null 2>&1
		if ! "$NM_BIN" rule add "${1}/base.apk" "$RVPATH" >/dev/null 2>&1; then
			mount -o bind "$RVPATH" "${1}/base.apk"
		fi
	else
		mount -o bind "$RVPATH" "${1}/base.apk"
	fi
	am force-stop "$PKG_NAME"
	cp -f "$MODDIR/module.prop.orig" "$MODDIR/module.prop"
	return 0
}

mount_rv_now() {
	if ! BASEPATH=$(get_basepath); then
		ch_desc_err "App not installed: '$BASEPATH'"
		return 1
	fi
	mount_rv "$BASEPATH"
}
