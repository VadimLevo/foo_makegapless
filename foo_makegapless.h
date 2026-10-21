#pragma once

#include <SDK/foobar2000.h>
#include <helpers/input_helpers.h>
#include <helpers/writer_wav.h>

// ============================================================
// GUID'ы
// ============================================================
static const GUID guid_makegapless_group =
{ 0xa1b2c3d4, 0x0001, 0x4e5f,{ 0x90, 0x01, 0x6d, 0x61, 0x6b, 0x65, 0x67, 0x01 } };

static const GUID guid_makegapless_image =
{ 0xa1b2c3d4, 0x0002, 0x4e5f,{ 0x90, 0x01, 0x6d, 0x61, 0x6b, 0x65, 0x67, 0x02 } };

static const GUID guid_makegapless_tracks =
{ 0xa1b2c3d4, 0x0003, 0x4e5f,{ 0x90, 0x01, 0x6d, 0x61, 0x6b, 0x65, 0x67, 0x03 } };

static const GUID guid_makegapless_prefs =
{ 0xa1b2c3d4, 0x0004, 0x4e5f,{ 0x90, 0x01, 0x6d, 0x61, 0x6b, 0x65, 0x67, 0x04 } };

static const GUID guid_makegapless_advconfig =
{ 0xa1b2c3d4, 0x0005, 0x4e5f,{ 0x90, 0x01, 0x6d, 0x61, 0x6b, 0x65, 0x67, 0x05 } };

// ============================================================
// Конфигурация (сохраняется в foobar2000.cfg)
// ============================================================
extern cfg_int  cfg_scan_depth_sec;
extern cfg_int  cfg_min_silence_ms;
extern cfg_int  cfg_fade_ms;

extern cfg_bool cfg_use_source_dir;
extern cfg_string cfg_custom_dir;
extern cfg_bool cfg_create_subfolder;
extern cfg_string cfg_subfolder_name;
extern cfg_string cfg_image_name;
extern cfg_string cfg_cue_name;
extern cfg_bool cfg_add_to_playlist;