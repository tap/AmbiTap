/// AmbiTap: target-independent ambisonics library
/// Umbrella header for the AmbiTap math layer.
/// Timothy Place
/// Copyright 2025-2026 Timothy Place.

#ifndef AMBITAP_AMBITAP_H
#define AMBITAP_AMBITAP_H

#include "analysis/energy_vector.h"
#include "analysis/soundfield_grid.h"
#include "dsp/binaural_renderer.h"
#include "dsp/decoder.h"
#include "dsp/directional_loudness.h"
#include "dsp/doppler.h"
#include "dsp/encoder.h"
#include "dsp/format_converter.h"
#include "dsp/mirror.h"
#include "dsp/nfc.h"
#include "dsp/rotator.h"
#include "dsp/spatial_compressor.h"
#include "dsp/util/async_rebuilder.h"
#include "dsp/virtual_mic.h"
#include "math/binaural/convolution.h"
#include "math/binaural/hrtf_data.h"
#include "math/binaural/ooura_fft.h"
#include "math/binaural/sofa_reader.h" // header-guarded by AMBITAP_HAS_SOFA
#include "math/core/coords.h"
#include "math/core/indexing.h"
#include "math/core/normalization.h"
#include "math/core/rotation.h"
#include "math/core/spherical_harmonics.h"
#include "math/decoding/allrad.h"
#include "math/decoding/epad.h"
#include "math/decoding/max_re.h"
#include "math/decoding/mode_matching.h"
#include "math/geometry/convex_hull.h"
#include "math/geometry/layouts.h"
#include "math/geometry/speaker_layout.h"
#include "math/geometry/tdesigns.h"

#endif // AMBITAP_AMBITAP_H
