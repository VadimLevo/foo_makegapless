#include "foo_makegapless.h"
#include "make_gapless.h"
#include "resource.h"

DECLARE_COMPONENT_VERSION(
    "Gapless Mix",
    "1.0",
    "Makes gapless mix from selected tracks.\n"
    "Removes absolute-zero holes at joints, applies fade-in/out on cuts.\n"
);

VALIDATE_COMPONENT_FILENAME("foo_makegapless.dll");

// ============================================================
// Группа "Make Gapless" внутри Utilities
// ============================================================
static contextmenu_group_popup_factory g_makegapless_group(
    guid_makegapless_group,
    contextmenu_groups::utilities,
    "Make Gapless",
    0
);

// ============================================================
// Пункт "Image + CUE"
// ============================================================
static void run_image_cue(metadb_handle_list_cref tracks) {
    if (tracks.get_count() == 0) return;
    service_ptr_t<MakeGaplessProcess> proc =
        new service_impl_t<MakeGaplessProcess>(tracks, OutputMode::ImageWithCue);
    threaded_process::g_run_modeless(
        proc,
        threaded_process::flag_show_progress | threaded_process::flag_show_abort,
        core_api::get_main_window(),
        "Gapless Mix: Image + CUE");
}

// ============================================================
// Пункт "Tracks"
// ============================================================
static void run_tracks(metadb_handle_list_cref tracks) {
    if (tracks.get_count() == 0) return;
    service_ptr_t<MakeGaplessProcess> proc =
        new service_impl_t<MakeGaplessProcess>(tracks, OutputMode::TracksOnly);
    threaded_process::g_run_modeless(
        proc,
        threaded_process::flag_show_progress | threaded_process::flag_show_abort,
        core_api::get_main_window(),
        "Gapless Mix: Tracks");
}

FB2K_DECLARE_CONTEXT_MENU_ITEM(run_image_cue,
    "Image + CUE", guid_makegapless_image,
    guid_makegapless_group,
    "Join selected tracks into a single WAV image with a CUE sheet.",
    0.0);

FB2K_DECLARE_CONTEXT_MENU_ITEM(run_tracks,
    "Tracks", guid_makegapless_tracks,
    guid_makegapless_group,
    "Write each selected track as a separate WAV, trimmed at joints.",
    1.0);