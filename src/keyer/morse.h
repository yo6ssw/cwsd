// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025 Adrian Scripcă (YO6SSW), Matei Conovici (YO3GEK)

#pragma once

#include <cmath>
#include <cstdint>

// float ratio to WinKeyer ratio
int ratio_to_int(float ratio);

// WinKeyer ratio to float ratio
float ratio_from_int(int value);

struct code_timings {
    float dit_ms;
    float dah_ms;
    float space_between_chars_ms;
    float space_between_words_ms;
    float weighted_dit_ms;
    float weighted_dah_ms;
    float weighted_inter_element_space_ms;
    float paddle_switchpoint_ms;
};

struct code_style {
    uint8_t impulses_between_words = 6;
    uint8_t impulses_between_characters = 3;
    bool autospace_enabled = true;
    float dah_dit_ratio = 3.0f;
    float weighting = 50.0f; // [10..90], percentage. 50 is normal
    float extra_letterspace_percent = 0.0f;
    float keying_compensation_ms = 0.0f; // ms to add to all dits and dahs. the extra length is removed from the spacing
    float character_wpm = 24.0f;
    float farnsworth_wpm = 24.0f;
    float paddle_switchpoint = 50.0f; // [10..90]. percentage of a dot. 50 is normal
    float speed_low = 20.0f;
    float speed_high = 40.0f;
    float dah_dit_ratio_low = 3.0f;
    float dah_dit_ratio_high = 3.0f;
    float weighting_low = 10.0f;
    float weighting_high = 90.0f;
    float extra_letterspace_percent_low = 0.0f;
    float extra_letterspace_percent_high = 0.0f;

    code_timings calculate_timings();

private:
    float space_impulses_in_paris();
    float non_space_impulses_in_paris();
};
