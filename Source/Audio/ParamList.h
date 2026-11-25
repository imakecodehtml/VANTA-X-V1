#pragma once
#include <string>
#include <cstdio>

// X-macro list of every scalar parameter ID used by the audio engine (ID string == member name).
// Stage 1/2 IDs are untouched; Stage 3 IDs are appended (engine, granular, additive).
#define VANTA_PARAM_LIST(X) \
    X(master_vol) X(master_pan) X(poly) X(voice_mode) X(bend_range) \
    X(uni_voices) X(uni_detune) X(uni_spread) \
    X(va_wave) X(va_oct) X(va_semi) X(va_fine) X(va_pw) X(va_sync) X(va_drift) \
    X(va_mix1) X(va_mix2) X(va_mix3) X(va_tune2) X(va_tune3) \
    X(sub_level) X(sub_oct) X(sub_wave) \
    X(noise_level) X(noise_type) X(noise_color) \
    X(porta_time) X(porta_curve) \
    X(amp_a) X(amp_d) X(amp_s) X(amp_r) \
    X(flt_type) X(flt_cutoff) X(flt_res) X(flt_drive) \
    /* ---- Stage 3: engine select */ \
    X(engine) \
    /* ---- Stage 3: granular */ \
    X(gr_size) X(gr_sizerand) X(gr_density) X(gr_pos) X(gr_spray) X(gr_speed) \
    X(gr_pitch) X(gr_pitchrand) X(gr_spread) X(gr_texture) X(gr_start) X(gr_end) \
    X(gr_dir) X(gr_window) X(gr_width) X(gr_shape) X(gr_root) \
    /* ---- Stage 3: additive (32 amplitude + 32 phase params are generated: add_h01..add_h32, add_p01..add_p32) */ \
    X(add_count) X(add_level) X(add_shift) X(add_morph) X(add_target) X(add_tone) X(add_inharm) \
    X(add_balance) X(add_rolloff) X(add_semi) X(add_fine) \
    X(add_sf_on) X(add_sf_pos) X(add_sf_width) X(add_sf_gain) \
    X(add_henv) X(add_hdecay) X(add_uni) X(add_spread) X(add_detune)

namespace vanta
{
// Harmonic parameter IDs: kind 'h' = amplitude, 'p' = phase; i = 0..31  ->  "add_h01" .. "add_h32"
inline std::string addHarmId (char kind, int i)
{ char b[48]; std::snprintf (b, sizeof b, "add_%c%02d", kind, i + 1); return b; }
}
