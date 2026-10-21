#pragma once
#include "foo_makegapless.h"
#include <vector>

enum class OutputMode { ImageWithCue, TracksOnly };

struct HoleRange {
    t_size offset = 0;
    t_size length = 0;
};

struct TrackAnalysis {
    metadb_handle_ptr track;
    t_size total_samples = 0;
    unsigned srate = 0;
    unsigned channels = 0;
    unsigned channel_mask = 0;
    unsigned bps = 0;
    bool is_float = false;
    bool supported = false;

    pfc::list_t<HoleRange> lead_holes;
    pfc::list_t<HoleRange> trail_holes;
};

class MakeGaplessProcess : public threaded_process_callback {
public:
    MakeGaplessProcess(metadb_handle_list_cref tracks, OutputMode mode);
    void run(threaded_process_status& p_status, abort_callback& p_abort) override;
    void on_done(HWND p_wnd, bool p_was_aborted) override;

private:
    bool analyze_track(metadb_handle_ptr track, abort_callback& p_abort, TrackAnalysis& out);

    // ИЗМЕНЕНО: float -> audio_sample
    void find_holes(const audio_sample* data, t_size frames, unsigned nch,
        t_size base_offset, t_size min_sil_frames,
        pfc::list_t<HoleRange>& out) const;

    // ИЗМЕНЕНО: std::vector<float> -> std::vector<audio_sample>
    bool render_track(metadb_handle_ptr track, const TrackAnalysis& ta,
        pfc::list_t<HoleRange> const& active_holes,
        std::vector<audio_sample>& out_buf, abort_callback& abort);

    bool resolve_output_dir(pfc::string8& out_dir, abort_callback& abort);
    void copy_tags_and_art(metadb_handle_ptr source, const char* dest_path,
        const char* new_title, abort_callback& abort);

    metadb_handle_list m_tracks;
    OutputMode m_mode;
    pfc::list_t<TrackAnalysis> m_analysis;
    pfc::list_t<pfc::string8> m_output_paths;
    pfc::string8 m_image_path;
    pfc::string8 m_cue_path;
    bool m_failed = false;
};