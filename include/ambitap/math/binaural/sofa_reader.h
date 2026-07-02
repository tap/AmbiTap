/// AmbiTap: target-independent ambisonics library
/// SOFA file reader for loading custom HRTF datasets at runtime.
/// Requires libmysofa; gated by the AMBITAP_HAS_SOFA macro, set by the
/// AMBITAP_ENABLE_SOFA build option.
/// Timothy Place
/// Copyright 2025-2026 Timothy Place.

#ifndef AMBITAP_MATH_SOFA_READER_H
#define AMBITAP_MATH_SOFA_READER_H

#ifdef AMBITAP_HAS_SOFA

#include "../core/indexing.h"
#include "../core/normalization.h"
#include "../core/spherical_harmonics.h"

#include <mysofa.h>

#include <Eigen/Dense>
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace ambitap {

    /// HRTF data loaded from a SOFA file.
    ///
    /// Holds raw HRIR measurements (per-measurement, per-ear, per-sample) and can
    /// project them onto the SH basis at any order via decompose_sh().
    struct hrtf_data {
        size_t                                       num_measurements {0};
        size_t                                       hrir_length {0};
        float                                        sample_rate {0.f};
        std::vector<float>                           azimuth;   // radians
        std::vector<float>                           elevation; // radians
        std::vector<std::vector<std::vector<float>>> hrir; // [measurement][ear 0=L/1=R][sample]

        /// Project the measured HRIRs onto the SH basis at the given order.
        ///
        /// out_left and out_right are filled as [acn_channel][sample] of size
        /// (order+1)^2 x min(hrir_length, max_taps).
        void decompose_sh(int order, size_t max_taps, std::vector<std::vector<float>>& out_left,
                          std::vector<std::vector<float>>& out_right) const {
            const size_t num_ch = channel_count(order);
            const size_t taps   = std::min(hrir_length, max_taps);
            const auto   M      = static_cast<Eigen::Index>(num_measurements);
            const auto   C      = static_cast<Eigen::Index>(num_ch);

            // Re-encoding matrix Y[i][j] = SH_j evaluated at measurement direction i.
            Eigen::MatrixXf Y(M, C);
            float           sh_buf[max_channel_count];
            for (Eigen::Index i = 0; i < M; ++i) {
                evaluate_sh(order, azimuth[static_cast<size_t>(i)],
                            elevation[static_cast<size_t>(i)], sh_buf);
                for (Eigen::Index j = 0; j < C; ++j) {
                    Y(i, j) = sh_buf[j];
                }
            }

            // Rank-revealing pseudoinverse. Normal equations + LDLT would return
            // silent NaN/garbage for measurement grids that cannot support the
            // requested order (e.g. a horizontal-ring SOFA file); detect and
            // report that instead.
            Eigen::CompleteOrthogonalDecomposition<Eigen::MatrixXf> cod(Y);
            cod.setThreshold(1e-4f);
            if (cod.rank() < C) {
                throw std::runtime_error(
                    "ambitap::hrtf_data::decompose_sh: measurement grid cannot support order "
                    + std::to_string(order) + " (re-encoding matrix rank "
                    + std::to_string(cod.rank()) + " < " + std::to_string(C) + " channels)");
            }
            Eigen::MatrixXf Y_pinv = cod.pseudoInverse();

            out_left.assign(num_ch, std::vector<float>(taps, 0.f));
            out_right.assign(num_ch, std::vector<float>(taps, 0.f));

            for (size_t t = 0; t < taps; ++t) {
                Eigen::VectorXf meas_L(M);
                Eigen::VectorXf meas_R(M);
                for (Eigen::Index i = 0; i < M; ++i) {
                    const auto idx = static_cast<size_t>(i);
                    meas_L(i)      = hrir[idx][0][t];
                    meas_R(i)      = hrir[idx][1][t];
                }
                const Eigen::VectorXf sh_L = Y_pinv * meas_L;
                const Eigen::VectorXf sh_R = Y_pinv * meas_R;
                for (size_t ch = 0; ch < num_ch; ++ch) {
                    out_left[ch][t]  = sh_L(static_cast<Eigen::Index>(ch));
                    out_right[ch][t] = sh_R(static_cast<Eigen::Index>(ch));
                }
            }
        }
    };

    /// Load HRTF measurements from a SOFA file via libmysofa.
    ///
    /// Requirements checked at load time (violations throw): exactly two
    /// receivers (left/right ears), and no per-measurement Data.Delay — files
    /// that store time-of-arrival separately would decode with broken ITD if
    /// the delays were silently dropped.
    ///
    /// @throws std::runtime_error if the file cannot be opened or parsed, or
    ///         does not meet the requirements above.
    inline hrtf_data load_sofa(const std::string& path) {
        int err = MYSOFA_OK;

        const std::unique_ptr<MYSOFA_HRTF, decltype(&mysofa_free)> hrtf(
            mysofa_load(path.c_str(), &err), &mysofa_free);
        if (hrtf == nullptr || err != MYSOFA_OK) {
            throw std::runtime_error("ambitap::load_sofa: failed to load \"" + path + "\"");
        }

        if (hrtf->R != 2) {
            throw std::runtime_error("ambitap::load_sofa: \"" + path + "\" has "
                                     + std::to_string(hrtf->R)
                                     + " receivers; only 2 (left/right) is supported");
        }
        if (hrtf->DataDelay.values != nullptr) {
            for (unsigned i = 0; i < hrtf->DataDelay.elements; ++i) {
                if (hrtf->DataDelay.values[i] != 0.0f) {
                    throw std::runtime_error(
                        "ambitap::load_sofa: \"" + path
                        + "\" carries non-zero Data.Delay, which is not supported; "
                          "bake the delays into the IRs before loading");
                }
            }
        }

        // libmysofa stores source positions in spherical-degrees by default; convert
        // to Cartesian to get a uniform handle, then derive radians.
        mysofa_tocartesian(hrtf.get());

        hrtf_data data;
        data.num_measurements = static_cast<size_t>(hrtf->M);
        data.hrir_length      = static_cast<size_t>(hrtf->N);
        data.sample_rate      = static_cast<float>(hrtf->DataSamplingRate.values[0]);

        data.azimuth.resize(data.num_measurements);
        data.elevation.resize(data.num_measurements);
        data.hrir.resize(data.num_measurements);

        for (size_t i = 0; i < data.num_measurements; ++i) {
            const float x = hrtf->SourcePosition.values[i * 3 + 0];
            const float y = hrtf->SourcePosition.values[i * 3 + 1];
            const float z = hrtf->SourcePosition.values[i * 3 + 2];

            data.azimuth[i]   = std::atan2(y, x);
            data.elevation[i] = std::atan2(z, std::sqrt(x * x + y * y));

            data.hrir[i].resize(2);
            data.hrir[i][0].resize(data.hrir_length);
            data.hrir[i][1].resize(data.hrir_length);

            // DataIR layout: [measurement * R * N + receiver * N + sample]
            const size_t base = i * 2 * data.hrir_length;
            for (size_t t = 0; t < data.hrir_length; ++t) {
                data.hrir[i][0][t] = hrtf->DataIR.values[base + t];
                data.hrir[i][1][t] = hrtf->DataIR.values[base + data.hrir_length + t];
            }
        }

        return data;
    }

} // namespace ambitap

#endif // AMBITAP_HAS_SOFA

#endif // AMBITAP_MATH_SOFA_READER_H
