/// @file ooura_fft.h
/// @brief Re-export of the shared tap::dsp real FFT into the tap::ambi namespace.
// SPDX-License-Identifier: MIT
// Copyright 2025-2026 Timothy Place.
//
// The real FFT used to live here as a vendored Ooura wrapper. It now lives in
// DspTap (tap::dsp), consumed via the submodules/dsptap submodule — one
// transform shared with the rest of the family, since tap/DspTap#42 the
// clean-room srdif engine at the same contract (packing, exp(+i), unnormalized
// inverse). This header keeps the historical include path and the unqualified
// names (`real_fft`, `real_fft32`) working inside tap::ambi.

#pragma once

#include "tap/dsp/fft.h"

namespace tap::ambi {

    using tap::dsp::real_fft;
    using tap::dsp::real_fft32;

} // namespace tap::ambi
