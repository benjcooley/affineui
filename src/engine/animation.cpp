#include "engine/animation.h"

#include <algorithm>

namespace affineui {

KeyframeCurveId AnimationRuntime::add_curve(KeyframeCurve curve) {
    const auto id = static_cast<KeyframeCurveId>(curves_.size());
    curves_.push_back(std::move(curve));
    return id;
}

std::uint32_t AnimationRuntime::play(ActiveAnimation anim) {
    const auto idx = static_cast<std::uint32_t>(animations_.size());
    animations_.push_back(anim);
    return idx;
}

void AnimationRuntime::stop(std::uint32_t index, bool /*snap_to_end*/) {
    if (index >= animations_.size()) return;
    animations_[index].flags |= 0x1u;  // mark paused; gc_finished() removes
}

void AnimationRuntime::pause(std::uint32_t index) {
    if (index < animations_.size()) animations_[index].flags |= 0x1u;
}

void AnimationRuntime::resume(std::uint32_t index, double resume_time_s) {
    if (index >= animations_.size()) return;
    auto& a = animations_[index];
    a.flags &= static_cast<std::uint8_t>(~0x1u);
    a.start_time_s = resume_time_s;
}

std::uint32_t AnimationRuntime::sample(LayerTree& layers, double time_s) {
    // Phase-2 stub: walks the active list and marks each target layer
    // composite-dirty. Real keyframe interpolation lands when
    // Compositor wiring is complete.
    std::uint32_t sampled = 0;
    for (auto& a : animations_) {
        if (a.flags & 0x1u) continue;  // paused
        if (time_s < a.start_time_s + static_cast<double>(a.delay_s)) continue;
        if (a.layer >= layers.size()) continue;
        layers.at(a.layer).mark_composite_dirty();
        ++sampled;
    }
    return sampled;
}

void AnimationRuntime::gc_finished() {
    animations_.erase(
        std::remove_if(animations_.begin(), animations_.end(),
            [](const ActiveAnimation& a) { return (a.flags & 0x1u) != 0; }),
        animations_.end());
}

}  // namespace affineui
