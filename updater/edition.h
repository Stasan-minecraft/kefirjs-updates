#pragma once
#ifdef KF_ATMOSPHERE_EDITION
#define KF_BASE_HASH "a6d37f1eabdc2964e2dba0984af17ebeea6de1167391118faed205d9d631d39a"
#define KF_BASE "20.5.0-ams-1.10.1"
#define KF_EDITION "Atmosphere edition"
#define KF_UPDATES "updates/atmosphere/native"
#define KF_PREVIEW_UPDATES KF_UPDATES "/full"
#define KF_BOOT_ART "bootloader/res/KEFIRIZ-boot.bmp"
#define KF_BOOT_ICON "bootloader/res/KEFIRIZ-CFW.bmp"
#define KF_OTA "config/kefirjs/atmosphere-ota"
#define KF_MINIMUM "2.0.0"
#else
#define KF_BASE_HASH "1fca4f0a1d2a214d7d92c0574827066e59bbfc4e230750a800654534f1820ac2"
#define KF_BASE "20.5.0-786"
#define KF_EDITION "Horizon edition 786"
#define KF_UPDATES "updates/native"
#define KF_PREVIEW_UPDATES KF_UPDATES
#define KF_BOOT_ART "bootloader/bootlogo_kefir.bmp"
#define KF_BOOT_ICON "bootloader/res/atmosphere.bmp"
#define KF_OTA "config/kefirjs/ota"
#define KF_MINIMUM "1.1.0"
#endif
