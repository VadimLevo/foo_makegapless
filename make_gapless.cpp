#include "make_gapless.h"
#include <helpers/input_helpers.h>
#include <helpers/writer_wav.h>
#include <helpers/cue_creator.h>
#include <SDK/album_art.h>
#include <cmath>
#include <vector>
#include <algorithm>
#include <string>

struct Hole {
    t_size offset;
    t_size length;
};

// ============================================================
// find_holes
// ============================================================
static void find_holes_impl(const audio_sample* data, t_size frames, unsigned nch,
    t_size base_offset, t_size min_sil_frames,
    std::vector<Hole>& out) {
    t_size run_start = 0;
    bool in_run = false;
    for (t_size f = 0; f < frames; f++) {
        bool is_zero = true;
        const audio_sample* fr = data + f * nch;
        for (unsigned c = 0; c < nch; c++) {
            if (fr[c] != 0.0) { is_zero = false; break; }
        }
        if (is_zero) {
            if (!in_run) { in_run = true; run_start = f; }
        }
        else {
            if (in_run) {
                t_size len = f - run_start;
                if (len >= min_sil_frames) {
                    out.push_back({ base_offset + run_start, len });
                }
                in_run = false;
            }
        }
    }
    if (in_run) {
        t_size len = frames - run_start;
        if (len >= min_sil_frames) {
            out.push_back({ base_offset + run_start, len });
        }
    }
}

void MakeGaplessProcess::find_holes(const audio_sample* data, t_size frames, unsigned nch,
    t_size base_offset, t_size min_sil_frames,
    pfc::list_t<HoleRange>& out) const {
    std::vector<Hole> tmp;
    find_holes_impl(data, frames, nch, base_offset, min_sil_frames, tmp);
    for (auto& h : tmp) {
        HoleRange hr; hr.offset = h.offset; hr.length = h.length;
        out.add_item(hr);
    }
}

// ============================================================
// analyze_track
// ============================================================
bool MakeGaplessProcess::analyze_track(metadb_handle_ptr track,
    abort_callback& p_abort,
    TrackAnalysis& out) {
    out.track = track;
    out.supported = false;

    file_info_impl info;
    if (!track->get_info(info)) return false;

    const char* enc_ptr = info.info_get("encoding");
    pfc::string8 enc = enc_ptr ? enc_ptr : "";
    if (enc == "lossy") return false;

    out.bps = (unsigned)info.info_get_int("bitspersample");
    out.srate = (unsigned)info.info_get_int("samplerate");
    out.channels = (unsigned)info.info_get_int("channels");

    input_helper helper;
    helper.open(nullptr, track->get_location(), input_flag_no_looping, p_abort, false, false);

    audio_chunk_impl chunk;
    if (!helper.run(chunk, p_abort)) {
        helper.close();
        return false;
    }
    out.srate = chunk.get_srate();
    out.channels = chunk.get_channels();
    out.channel_mask = chunk.get_channel_config();

    const char* fs_ptr = info.info_get("float");
    pfc::string8 fs = fs_ptr ? fs_ptr : "";
    out.is_float = (fs == "1" || fs == "true");

    if (out.bps == 0) out.bps = out.is_float ? 32 : 16;
    if (out.srate == 0) out.srate = 44100;
    if (out.channels == 0) out.channels = 2;

    const t_size scan_frames = (t_size)cfg_scan_depth_sec * (t_size)out.srate;
    const t_size min_sil = (t_size)cfg_min_silence_ms * (t_size)out.srate / 1000;

    // LEAD
    {
        std::vector<audio_sample> lead;
        lead.reserve(scan_frames * out.channels);
        t_size pos = 0;
        do {
            const t_size cn = chunk.get_sample_count();
            if (cn == 0) break;
            const audio_sample* src = chunk.get_data();
            t_size from = 0, to = cn;
            if (pos + cn > scan_frames) to = scan_frames - pos;
            if (to > from) {
                for (t_size f = from; f < to; f++) {
                    const audio_sample* fr = src + f * out.channels;
                    for (unsigned c = 0; c < out.channels; c++) lead.push_back(fr[c]);
                }
            }
            pos += cn;
            if (pos >= scan_frames) break;
        } while (helper.run(chunk, p_abort));

        std::vector<Hole> tmp;
        find_holes_impl(lead.data(), lead.size() / out.channels, out.channels,
            0, min_sil, tmp);
        for (auto& h : tmp) {
            HoleRange hr; hr.offset = h.offset; hr.length = h.length;
            out.lead_holes.add_item(hr);
        }
    }

    // TOTAL + TAIL
    helper.close();
    helper.open(nullptr, track->get_location(), input_flag_no_looping, p_abort, false, false);

    t_size total = 0;
    std::vector<audio_sample> tail;
    tail.reserve(scan_frames * out.channels);

    while (helper.run(chunk, p_abort)) {
        p_abort.check();
        const t_size cn = chunk.get_sample_count();
        if (cn == 0) continue;
        const audio_sample* src = chunk.get_data();
        for (t_size f = 0; f < cn; f++) {
            const audio_sample* fr = src + f * out.channels;
            for (unsigned c = 0; c < out.channels; c++) tail.push_back(fr[c]);
        }
        if (tail.size() / out.channels > scan_frames) {
            t_size excess = tail.size() / out.channels - scan_frames;
            tail.erase(tail.begin(), tail.begin() + excess * out.channels);
        }
        total += cn;
    }
    helper.close();
    out.total_samples = total;
    out.supported = (total > 0 && out.srate > 0 && out.channels > 0);

    if (out.supported) {
        t_size frames_in_tail = tail.size() / out.channels;
        t_size base = (total > frames_in_tail) ? (total - frames_in_tail) : 0;
        std::vector<Hole> tmp;
        find_holes_impl(tail.data(), frames_in_tail, out.channels,
            base, min_sil, tmp);
        for (auto& h : tmp) {
            HoleRange hr; hr.offset = h.offset; hr.length = h.length;
            out.trail_holes.add_item(hr);
        }
    }
    return out.supported;
}

// ============================================================
// render_track
// ============================================================
bool MakeGaplessProcess::render_track(metadb_handle_ptr track,
    const TrackAnalysis& ta,
    pfc::list_t<HoleRange> const& active_holes,
    std::vector<audio_sample>& out_buf,
    abort_callback& abort) {
    out_buf.clear();
    const unsigned nch = ta.channels;
    const unsigned bps = ta.bps;
    const t_size fade_frames = (t_size)cfg_fade_ms * ta.srate / 1000;

    std::vector<Hole> holes;
    for (t_size i = 0; i < active_holes.get_count(); i++) {
        Hole h;
        h.offset = active_holes[i].offset;
        h.length = active_holes[i].length;
        holes.push_back(h);
    }
    std::sort(holes.begin(), holes.end(), [](const Hole& a, const Hole& b) {
        return a.offset < b.offset;
        });

    std::vector<Hole> merged;
    for (const auto& h : holes) {
        if (merged.empty()) {
            merged.push_back(h);
        }
        else {
            Hole& last = merged.back();
            if (h.offset <= last.offset + last.length) {
                t_size new_end = (std::max)(last.offset + last.length, h.offset + h.length);
                last.length = new_end - last.offset;
            }
            else {
                merged.push_back(h);
            }
        }
    }
    holes = merged;

    input_helper helper;
    helper.open(nullptr, track->get_location(), input_flag_no_looping, abort, false, false);

    std::vector<audio_sample> full;
    try {
        if (ta.total_samples < 500000000) {
            full.reserve(ta.total_samples * nch);
        }
    }
    catch (const std::bad_alloc&) {
        FB2K_console_formatter() << "Gapless Mix: out of memory reserving buffer for " << track->get_path();
        helper.close();
        return false;
    }

    audio_chunk_impl chunk;
    try {
        while (helper.run(chunk, abort)) {
            abort.check();
            const t_size cn = chunk.get_sample_count();
            if (cn == 0) continue;
            const audio_sample* src = chunk.get_data();
            for (t_size f = 0; f < cn; f++) {
                const audio_sample* fr = src + f * nch;
                for (unsigned c = 0; c < nch; c++) full.push_back(fr[c]);
            }
        }
    }
    catch (const std::bad_alloc&) {
        FB2K_console_formatter() << "Gapless Mix: out of memory reading " << track->get_path();
        helper.close();
        return false;
    }
    helper.close();

    if (full.empty()) return false;

    out_buf.reserve(full.size());

    audio_sample eps;
    if (ta.is_float) eps = 1e-6;
    else {
        unsigned shift = (bps >= 32) ? 31 : (bps - 1);
        eps = 1.0 / (double)((1ULL << shift) - 1);
    }

    t_size i = 0;
    t_size hole_idx = 0;
    const t_size total_frames = full.size() / nch;

    while (i < total_frames) {
        if (hole_idx < holes.size() && i >= holes[hole_idx].offset) {
            const Hole& h = holes[hole_idx];
            if (i < h.offset + h.length) {
                bool have_last = !out_buf.empty();
                bool have_next = (h.offset + h.length) < total_frames;

                if (fade_frames > 0 && have_last && have_next) {
                    if (out_buf.size() < (size_t)nch) {
                        i = h.offset + h.length;
                        hole_idx++;
                        continue;
                    }

                    const audio_sample* last_before = &out_buf[(out_buf.size() / nch - 1) * nch];
                    const audio_sample* first_after = &full[(h.offset + h.length) * nch];

                    bool do_fade = false;
                    for (unsigned c = 0; c < nch; c++) {
                        if (fabs(last_before[c] - first_after[c]) > eps) {
                            do_fade = true;
                            break;
                        }
                    }
                    if (do_fade) {
                        t_size out_frames = out_buf.size() / nch;
                        t_size fcount = (std::min)(fade_frames, out_frames);
                        audio_sample* p = out_buf.data() + (out_frames - fcount) * nch;
                        for (t_size f = 0; f < fcount; f++) {
                            audio_sample g = (audio_sample)(1.0 - (double)f / (double)fcount);
                            for (unsigned c = 0; c < nch; c++) p[f * nch + c] *= g;
                        }

                        t_size space_after = total_frames - (h.offset + h.length);
                        t_size fcount2 = (std::min)(fade_frames, space_after);
                        if (fcount2 > 0) {
                            audio_sample* q = &full[(h.offset + h.length) * nch];
                            for (t_size f = 0; f < fcount2; f++) {
                                audio_sample g = (audio_sample)((double)f / (double)fcount2);
                                for (unsigned c = 0; c < nch; c++) q[f * nch + c] *= g;
                            }
                        }
                    }
                }
                i = h.offset + h.length;
            }
            hole_idx++;
            continue;
        }

        const audio_sample* fr = &full[i * nch];
        for (unsigned c = 0; c < nch; c++) out_buf.push_back(fr[c]);
        i++;
    }

    return !out_buf.empty();
}

// ============================================================
// resolve_output_dir
// ============================================================
bool MakeGaplessProcess::resolve_output_dir(pfc::string8& out_dir, abort_callback& abort) {
    if (cfg_use_source_dir) {
        pfc::string8 src = m_tracks[0]->get_path();
        t_size pos = src.find_last('\\');
        if (pos == pfc::infinite_size) return false;
        src.truncate(pos);
        out_dir = src;
    }
    else {
        out_dir = cfg_custom_dir.get_ptr();
        if (out_dir.is_empty()) return false;
    }
    if (cfg_create_subfolder) {
        out_dir << "\\" << cfg_subfolder_name.get_ptr();
    }
    try { filesystem::g_create_directory(out_dir, abort); }
    catch (const exception_io_already_exists&) {}
    catch (...) { return false; }
    return true;
}

// ============================================================
// copy_tags_and_art
// ============================================================
void MakeGaplessProcess::copy_tags_and_art(metadb_handle_ptr source,
    const char* dest_path,
    const char* new_title,
    abort_callback& abort) {
    file_info_impl info;
    if (!source->get_info(info)) return;

    replaygain_info rg = info.get_replaygain();
    rg.reset();
    info.set_replaygain(rg);

    info.meta_remove_field("LENGTH");
    info.meta_remove_field("TLEN");
    if (new_title && *new_title) info.meta_set("TITLE", new_title);

    try {
        service_ptr_t<input_info_writer> w;
        input_entry::g_open_for_info_write(w, nullptr, dest_path, abort);
        w->set_info(0, info, abort);
        w->commit(abort);
    }
    catch (const std::exception& e) {
        FB2K_console_formatter() << "Gapless Mix: tag write failed for " << dest_path << " - " << e.what();
    }
    catch (...) {}

    try {
        album_art_extractor_instance_ptr src_art =
            album_art_extractor::g_open(nullptr, source->get_path(), abort);
        album_art_editor_instance_ptr dst_art =
            album_art_editor::g_open(nullptr, dest_path, abort);

        static const GUID* const art_types[] = {
            &album_art_ids::cover_front, &album_art_ids::cover_back,
            &album_art_ids::disc, &album_art_ids::icon, &album_art_ids::artist
        };
        bool modified = false;
        for (const GUID* g : art_types) {
            try {
                album_art_data_ptr data = src_art->query(*g, abort);
                if (data.is_valid()) { dst_art->set(*g, data, abort); modified = true; }
            }
            catch (...) {}
        }
        if (modified) dst_art->commit(abort);
    }
    catch (...) {}
}

// ============================================================
// ctor
// ============================================================
MakeGaplessProcess::MakeGaplessProcess(metadb_handle_list_cref tracks, OutputMode mode)
    : m_mode(mode) {
    m_tracks.remove_all();
    m_tracks.add_items(tracks);
}

// ============================================================
// run()
// ============================================================
void MakeGaplessProcess::run(threaded_process_status& status, abort_callback& abort) {
    const t_size n = m_tracks.get_count();
    if (n == 0) return;

    status.set_title("Gapless Mix: analyzing...");
    m_analysis.remove_all();

    for (t_size i = 0; i < n; i++) {
        abort.check();
        status.set_progress(i, n * 2);
        status.set_item_path(m_tracks[i]->get_path());

        TrackAnalysis ta;
        try {
            analyze_track(m_tracks[i], abort, ta);
        }
        catch (...) {
            ta.supported = false;
        }
        m_analysis.add_item(ta);
    }

    for (t_size i = 0; i < n; i++) {
        if (!m_analysis[i].supported) {
            popup_message::g_show("Unsupported or lossy track format.",
                "Gapless Mix", popup_message::icon_error);
            return;
        }
        if (i > 0) {
            if (m_analysis[i].srate != m_analysis[0].srate ||
                m_analysis[i].channels != m_analysis[0].channels ||
                m_analysis[i].bps != m_analysis[0].bps ||
                m_analysis[i].is_float != m_analysis[0].is_float) {
                popup_message::g_show(
                    "Selected tracks have different audio formats. Aborting.",
                    "Gapless Mix", popup_message::icon_error);
                return;
            }
        }
    }

    pfc::string8 out_dir;
    if (!resolve_output_dir(out_dir, abort)) {
        popup_message::g_show("Invalid output directory.",
            "Gapless Mix", popup_message::icon_error);
        return;
    }

    const TrackAnalysis& t0 = m_analysis[0];

    if (m_mode == OutputMode::ImageWithCue) {
        pfc::string8 img_name = cfg_image_name.get_ptr();
        pfc::string8 cue_name = cfg_cue_name.get_ptr();

        m_image_path = out_dir; m_image_path << "\\" << img_name << ".wav";
        m_cue_path = out_dir; m_cue_path << "\\" << cue_name << ".cue";

        CWavWriter writer;
        bool writer_open = false;
        bool any_written = false;
        t_size written_frames = 0;

        pfc::list_t<double> cue_secs;
        pfc::list_t<pfc::string8> titles;
        pfc::list_t<metadb_handle_ptr> cue_tracks;

        for (t_size i = 0; i < n; i++) {
            abort.check();
            status.set_progress(n + i, n * 2);
            status.set_item_path(m_tracks[i]->get_path());

            const TrackAnalysis& ta = m_analysis[i];

            pfc::list_t<HoleRange> active;
            if (i > 0)
                for (t_size h = 0; h < ta.lead_holes.get_count(); h++)
                    active.add_item(ta.lead_holes[h]);
            if (i < n - 1)
                for (t_size h = 0; h < ta.trail_holes.get_count(); h++)
                    active.add_item(ta.trail_holes[h]);

            std::vector<audio_sample> buf;
            bool ok = false;
            try {
                ok = render_track(m_tracks[i], ta, active, buf, abort);
            }
            catch (...) {
                ok = false;
            }
            if (!ok || buf.empty()) {
                FB2K_console_formatter() << "Gapless Mix: skipping empty/unreadable track " << m_tracks[i]->get_path();
                m_failed = true;
                continue;
            }

            cue_secs.add_item((double)written_frames / (double)t0.srate);
            cue_tracks.add_item(m_tracks[i]);

            pfc::string8 title;
            {
                file_info_impl fi;
                if (m_tracks[i]->get_info(fi)) {
                    const char* t = fi.meta_get("TITLE", 0);
                    if (t) title = t;
                }
                if (title.is_empty()) title = pfc::string_filename_ext(m_tracks[i]->get_path());
            }
            titles.add_item(title);

            const t_size frames_here = buf.size() / ta.channels;

            audio_chunk_impl tmp;
            tmp.set_data(buf.data(), frames_here, ta.channels, ta.srate, ta.channel_mask);

            try {
                if (!writer_open) {
                    wavWriterSetup_t setup;
                    setup.initialize2(tmp, ta.bps, ta.bps, ta.is_float, false);
                    writer.open(m_image_path, setup, abort);
                    writer_open = true;
                }
                writer.write(tmp, abort);
                written_frames += frames_here;
                any_written = true;
            }
            catch (const std::exception& e) {
                FB2K_console_formatter() << "Gapless Mix: WAV write error - " << e.what();
                m_failed = true;
                break;
            }
            catch (...) {
                m_failed = true;
                break;
            }
        }

        if (writer_open) {
            try { writer.finalize(abort); }
            catch (...) { m_failed = true; }
        }

        if (any_written) {
            if (!m_image_path.is_empty()) m_output_paths.add_item(m_image_path);

            cue_creator::t_entry_list cue_list;
            for (t_size i = 0; i < cue_secs.get_count(); i++) {
                cue_creator::t_entry e;
                e.m_track_number = (unsigned)(i + 1);
                e.m_file = pfc::string_filename_ext(m_image_path);
                e.m_fileType = "WAVE";
                e.set_simple_index(cue_secs[i]);

                file_info_impl track_info;
                if (cue_tracks[i]->get_info(track_info)) {
                    e.m_infos.copy(track_info);
                }
                e.m_infos.meta_set("TITLE", titles[i]);
                cue_list.add_item(e);
            }

            pfc::string8 cue_text = cue_creator::create(cue_list);
            try {
                {
                    service_ptr_t<file> f;
                    filesystem::g_open_write_new(f, m_cue_path, abort);
                    f->write(cue_text.get_ptr(), cue_text.length(), abort);
                }
                m_output_paths.add_item(m_cue_path);
            }
            catch (const std::exception& e) {
                FB2K_console_formatter() << "Gapless Mix: CUE write failed - " << e.what();
                m_failed = true;
            }
            catch (...) {
                m_failed = true;
            }

            copy_tags_and_art(m_tracks[0], m_image_path, nullptr, abort);
        }
    }
    else {
        t_size track_num = 1;
        for (t_size i = 0; i < n; i++) {
            abort.check();
            status.set_progress(n + i, n * 2);
            status.set_item_path(m_tracks[i]->get_path());

            const TrackAnalysis& ta = m_analysis[i];

            pfc::list_t<HoleRange> active;
            if (i > 0)
                for (t_size h = 0; h < ta.lead_holes.get_count(); h++)
                    active.add_item(ta.lead_holes[h]);
            if (i < n - 1)
                for (t_size h = 0; h < ta.trail_holes.get_count(); h++)
                    active.add_item(ta.trail_holes[h]);

            std::vector<audio_sample> buf;
            bool ok = false;
            try {
                ok = render_track(m_tracks[i], ta, active, buf, abort);
            }
            catch (...) {
                ok = false;
            }
            if (!ok || buf.empty()) {
                FB2K_console_formatter() << "Gapless Mix: skipping empty/unreadable track " << m_tracks[i]->get_path();
                continue;
            }

            pfc::string8 orig_name = pfc::string_filename_ext(m_tracks[i]->get_path());
            t_size dot = orig_name.find_last('.');
            if (dot != pfc::infinite_size) orig_name.truncate(dot);

            pfc::string8 out_path = out_dir;
            out_path << "\\" << pfc::format_uint(track_num, 2) << " - " << orig_name << ".wav";

            const t_size frames_here = buf.size() / ta.channels;
            audio_chunk_impl tmp;
            tmp.set_data(buf.data(), frames_here, ta.channels, ta.srate, ta.channel_mask);

            wavWriterSetup_t setup;
            setup.initialize2(tmp, ta.bps, ta.bps, ta.is_float, false);

            try {
                CWavWriter writer;
                writer.open(out_path, setup, abort);
                writer.write(tmp, abort);
                writer.finalize(abort);
            }
            catch (const std::exception& e) {
                FB2K_console_formatter() << "Gapless Mix: WAV write error on " << out_path << " - " << e.what();
                m_failed = true;
                continue;
            }
            catch (...) {
                m_failed = true;
                continue;
            }

            pfc::string8 title;
            {
                file_info_impl fi;
                if (m_tracks[i]->get_info(fi)) {
                    const char* t = fi.meta_get("TITLE", 0);
                    if (t) title = t;
                }
                if (title.is_empty()) title = orig_name;
            }
            copy_tags_and_art(m_tracks[i], out_path, title.get_ptr(), abort);
            m_output_paths.add_item(out_path);
            track_num++;
        }
    }
}

// ============================================================
// on_done
// ============================================================
void MakeGaplessProcess::on_done(HWND, bool aborted) {
    if (aborted) return;
    if (m_failed) {
        popup_message::g_show("Gapless Mix completed with errors. See Console for details.",
            "Gapless Mix", popup_message::icon_error);
    }

    if (cfg_add_to_playlist && m_output_paths.get_count() > 0) {
        pfc::list_t<const char*> paths;

        for (t_size i = 0; i < m_output_paths.get_count(); i++) {
            const char* path = m_output_paths[i].get_ptr();
            const char* ext = pfc::string_extension(path);
            if (m_mode == OutputMode::ImageWithCue && stricmp_utf8(ext, "wav") == 0) {
                continue;
            }
            paths.add_item(path);
        }

        if (paths.get_count() > 0) {
            static_api_ptr_t<playlist_manager> pm;
            t_size active_pl = pm->get_active_playlist();

            if (active_pl == pfc_infinite) {
                active_pl = pm->create_playlist("Gapless Mix", pfc_infinite, pfc_infinite);
            }

            if (active_pl != pfc_infinite) {
                metadb_handle_list handles;
                static_api_ptr_t<playlist_incoming_item_filter> pif;

                bool ok = pif->process_locations(
                    paths,
                    handles,
                    false,
                    nullptr,
                    nullptr,
                    core_api::get_main_window()
                );

                if (ok && handles.get_count() > 0) {
                    t_size base = pm->playlist_get_item_count(active_pl);
                    pm->playlist_insert_items(active_pl, base, handles, bit_array_true());
                }
                else {
                    FB2K_console_formatter() << "Gapless Mix: failed to process output for playlist.";
                }
            }
        }
    }
    FB2K_console_formatter() << "Gapless Mix: done, "
        << m_output_paths.get_count() << " file(s) written.";
}