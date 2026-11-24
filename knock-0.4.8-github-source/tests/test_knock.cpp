#include "mpc_vst2_abi.h"
#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <dlfcn.h>

constexpr int Block = 127;
constexpr int Rate = 44100;
constexpr int Blocks = 480;
using Audio = std::array<std::array<float, Block>, 2>;

int displayUpdates = 0;
int automationEvents = 0;

VstIntPtr host(AEffect*, VstInt32 opcode, VstInt32, VstIntPtr, void*, float)
{
    if (opcode == audioMasterUpdateDisplay) ++displayUpdates;
    if (opcode == audioMasterAutomate) ++automationEvents;
    return opcode == audioMasterVersion ? 2400 : 0;
}

struct Result
{
    double signature = 0;
    float meter = 0;
    float peak = 0;
};

template <typename Configure>
Result render(AEffect* effect, int program, Configure configure, bool dryOnly = false,
              bool linkedTest = false)
{
    auto dispatch = effect->dispatcher;
    dispatch(effect, effMainsChanged, 0, 0, nullptr, 0);
    dispatch(effect, effSetProgram, 0, program, nullptr, 0);
    assert(dispatch(effect, effGetProgram, 0, 0, nullptr, 0) == program);
    configure(effect);
    dispatch(effect, effMainsChanged, 0, 1, nullptr, 0);

    Audio input{}, output{};
    float* in[] = {input[0].data(), input[1].data()};
    float* out[] = {output[0].data(), output[1].data()};
    Result result;
    for (int block = 0; block < Blocks; ++block)
    {
        for (int frame = 0; frame < Block; ++frame)
        {
            const int sample = block * Block + frame;
            const double phase = static_cast<double>(sample % 11025) / Rate;
            const float sub = 0.70f * std::sin(6.2831853 * (54.0 * phase +
                0.9 * (1.0 - std::exp(-60.0 * phase)))) * std::exp(-12.0 * phase);
            const float click = 0.37f * std::sin(6.2831853 * 2110.0 * phase) * std::exp(-95.0 * phase);
            in[0][frame] = sub + click;
            in[1][frame] = linkedTest ? 0.5f * in[0][frame] : 0.72f * sub - 0.8f * click;
        }
        effect->processReplacing(effect, in, out, Block);
        result.meter = std::max(result.meter, effect->getParameter(effect, 12));
        for (int frame = 0; frame < Block; ++frame)
        {
            for (int channel = 0; channel < 2; ++channel)
            {
                const float sample = out[channel][frame];
                assert(std::isfinite(sample));
                if (dryOnly) assert(sample == in[channel][frame]);
                result.peak = std::max(result.peak, std::abs(sample));
                result.signature += sample * static_cast<double>(sample);
            }
            if (linkedTest) assert(out[1][frame] == 0.5f * out[0][frame]);
        }
    }
    return result;
}

int main(int argc, char** argv)
{
    assert(argc == 2);
    void* library = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
    assert(library);
    auto entry = reinterpret_cast<AEffect* (*)(audioMasterCallback)>(dlsym(library, "VSTPluginMain"));
    assert(entry);
    AEffect* effect = entry(&host);
    assert(effect && effect->numPrograms == 6 && effect->numParams == 54);
    assert(effect->numInputs == 2 && effect->numOutputs == 2 && effect->initialDelay == 0);

    auto dispatch = effect->dispatcher;
    dispatch(effect, effOpen, 0, 0, nullptr, 0);
    dispatch(effect, effSetSampleRate, 0, 0, nullptr, Rate);
    dispatch(effect, effSetBlockSize, 0, Block, nullptr, 0);

    auto checkCompression = [&](float threshold, float attack) {
        return render(effect, 1, [=](AEffect* e) {
            e->setParameter(e, 1, 0.5f); // unity input
            e->setParameter(e, 2, threshold);
            e->setParameter(e, 3, attack);
            e->setParameter(e, 5, 0.5f); // neutral transient
            e->setParameter(e, 6, 0);    // no drive
            e->setParameter(e, 8, 1);    // fully wet
            e->setParameter(e, 11, 0);   // clip off
            e->setParameter(e, 14, 0.5f);
            e->setParameter(e, 15, 0.5f);
            e->setParameter(e, 16, 0.0f);
        });
    };
    const Result openThreshold = checkCompression(1, 0);
    const Result fastAttack = checkCompression(0.30f, 0);
    const Result slowAttack = checkCompression(0.30f, 1);
    assert(openThreshold.meter == 0 && fastAttack.meter > 0.15f);
    assert(fastAttack.peak < slowAttack.peak * 0.85f);
    std::printf("PASS threshold triggers %.1f dB reduction; fast/slow attack peak %.3f / %.3f\n",
                fastAttack.meter * 24, fastAttack.peak, slowAttack.peak);

    const Result punch = render(effect, 1, [](AEffect*) {});
    const Result round = render(effect, 4, [](AEffect*) {});
    const double crestDifference = 20 * std::log10((punch.peak / std::sqrt(punch.signature)) /
                                                    (round.peak / std::sqrt(round.signature)));
    assert(crestDifference > 1);
    std::printf("PASS Boom Bap has %.1f dB more peak-to-RMS contrast than Lo-Fi Hip Hop\n", crestDifference);

    auto measureEq = [&](float hz, int band, float gain, bool enabled, bool fullyWet = true, int compare = 0, float outputDb = 0, float driveDb = 0) {
        dispatch(effect, effMainsChanged, 0, 0, nullptr, 0);
        dispatch(effect, effSetProgram, 0, 0, nullptr, 0);
        const auto set = [&](int id, float value) { effect->setParameter(effect, id, value); };
        set(0, (outputDb + 18) / 36);
        set(1, 0.5f);
        set(2, 1);
        set(5, 0.5f);
        set(6, driveDb / 18);
        set(8, fullyWet ? 1 : 0);
        set(11, 0);
        set(14, 0.5f);
        set(15, 0.5f);
        set(16, 0);
        set(19, std::log(1000.0f / 120) / std::log(8000.0f / 120));
        set(22, enabled ? 1 : 0);
        set(23, compare * 0.5f);
        set(band, (gain + 12) / 24);
        dispatch(effect, effMainsChanged, 0, 1, nullptr, 0);
        Audio input{}, output{};
        float* in[] = { input[0].data(), input[1].data() };
        float* out[] = { output[0].data(), output[1].data() };
        double energy = 0, reference = 0;
        for (int block = 0; block < 650; ++block)
        {
            for (int i = 0; i < Block; ++i)
                input[0][i] = input[1][i] = 0.07f * std::sin(6.283185307 * hz * (block * Block + i) / Rate);
            effect->processReplacing(effect, in, out, Block);
            if (block < 500) continue;
            for (int i = 0; i < Block; ++i)
            {
                assert(output[0][i] == output[1][i]);
                energy += output[0][i] * output[0][i];
                reference += input[0][i] * input[0][i];
            }
        }
        if (!enabled) assert(effect->getParameter(effect, 39) == 0.5f);
        if (enabled && band == 18 && gain == -9)
            assert(effect->getParameter(effect, 39) < 0.34f); // near 1 kHz
        return 10 * std::log10(energy / reference);
    };
    const double low = measureEq(30, 17, 9, true);
    const double mid = measureEq(1000, 18, -9, true);
    const double high = measureEq(14000, 21, 9, true);
    std::printf("EQ response: low %+0.1f dB, mid %+0.1f dB, high %+0.1f dB\n", low, mid, high);
    assert(low > 7 && mid < -8.9 && high > 6);
    assert(std::abs(measureEq(1000, 18, -9, false)) < 0.01);
    assert(std::abs(measureEq(1000, 18, -9, true, false)) < 0.01);
    assert(std::abs(measureEq(1000, 18, 0, true)) < 0.01);
    std::printf("PASS stereo EQ: low %+0.1f dB, mid %+0.1f dB, high %+0.1f dB, neutral, bypass and dry unity\n",
                low, mid, high);

    const double normal = measureEq(1000, 18, 0, true, true, 0, -9);
    const double dryA = measureEq(1000, 18, 0, true, true, 1, -9);
    const double matchedB = measureEq(1000, 18, 0, true, true, 2, -9);
    assert(std::abs(normal + 9) < 0.01 && std::abs(dryA) < 0.01 && std::abs(matchedB) < 0.1);
    std::printf("PASS A/B at -9 dB test offset: normal %+.1f dB, A %+.1f dB, B %+.1f dB\n",
                normal, dryA, matchedB);

    measureEq(1000, 18, 0, true);
    const float drive0 = effect->getParameter(effect, 53);
    measureEq(1000, 18, 0, true, false, 1, -9, 9);
    const float drive9 = effect->getParameter(effect, 53);
    assert(std::abs((drive9 - drive0) * 30 - 9) < 0.1f);
    std::puts("PASS Drive needle rises 9 dB with Drive, independent of Mix, Output and A Dry");

    const Result dry = render(effect, 0, [](AEffect* e) { e->setParameter(e, 0, 0.5f); e->setParameter(e, 8, 0); }, true);
    for (int program = 0; program < effect->numPrograms; ++program)
    {
        const Result preset = render(effect, program, [](AEffect*) {});
        assert(preset.peak > 0.02f && preset.peak <= 1.0f && preset.meter > 0.01f);
        assert(std::abs(preset.signature - dry.signature) > 1);
        std::printf("PASS preset %d, peak %.3f, maximum gain reduction %.1f dB\n",
                    program + 1, preset.peak, preset.meter * 24);
    }

    double combinations[9]{};
    for (int source = 0; source < 3; ++source)
    {
        for (int style = 0; style < 3; ++style)
        {
            const Result mode = render(effect, 0, [=](AEffect* e) {
                e->setParameter(e, 9, source * 0.5f);
                e->setParameter(e, 10, style * 0.5f);
            });
            const int index = source * 3 + style;
            for (int previous = 0; previous < index; ++previous)
                assert(std::abs(mode.signature - combinations[previous]) > 0.05);
            combinations[index] = mode.signature;
        }
    }
    std::puts("PASS all nine Source/Style combinations produce distinguishable energy");

    const Result clip = render(effect, 0, [](AEffect* e) {
        e->setParameter(e, 0, 0.5f); // unity output
        e->setParameter(e, 1, 1);   // input +18 dB
        e->setParameter(e, 2, 1);   // threshold 0 dB
        e->setParameter(e, 6, 0);   // drive 0
        e->setParameter(e, 7, 0);   // wet ceiling -18 dB
        e->setParameter(e, 8, 1);   // 100% wet
    });
    assert(std::abs(clip.peak - std::pow(10.0f, -18.0f / 20)) < 1e-6);
    render(effect, 0, [](AEffect* e) {
        e->setParameter(e, 6, 0);
        e->setParameter(e, 11, 0);
        e->setParameter(e, 14, 0.5f);
        e->setParameter(e, 15, 0.5f);
        e->setParameter(e, 16, 0.0f);
    }, false, true);
    std::puts("PASS dry unity, stereo-linked dynamics, and wet clip ceiling");

    dispatch(effect, effMainsChanged, 0, 0, nullptr, 0);
    effect->setParameter(effect, 0, 0.5f); // Output at unity
    effect->setParameter(effect, 1, 0.5f); // Input at unity
    effect->setParameter(effect, 2, 1.0f); // Threshold at 0 dB
    effect->setParameter(effect, 8, 0);    // Dry signal
    dispatch(effect, effMainsChanged, 0, 1, nullptr, 0);
    const int updatesBefore = displayUpdates;
    Audio input{}, output{};
    float* in[] = {input[0].data(), input[1].data()};
    float* out[] = {output[0].data(), output[1].data()};
    for (int i = 0; i < 35; ++i)
    {
        // A one-sample right-channel pulse before the display polling interval.
        input[1][0] = i == 0 ? 0.5f : 0.0f;
        effect->processReplacing(effect, in, out, Block);
    }
    assert(effect->getParameter(effect, 13) > 0.8f);
    assert(effect->getParameter(effect, 12) == 0);
    assert(displayUpdates > updatesBefore);
    for (int i = 0; i < 500; ++i) effect->processReplacing(effect, in, out, Block);
    assert(effect->getParameter(effect, 13) == 0);
    assert(displayUpdates - updatesBefore <= 39);
    const int settledUpdates = displayUpdates;
    for (int i = 0; i < 80; ++i) effect->processReplacing(effect, in, out, Block);
    assert(displayUpdates == settledUpdates && automationEvents == 0);
    std::puts("PASS input meter sees a brief right-channel pulse at 0% Mix without compression; decays to zero without automation");
    dispatch(effect, effMainsChanged, 0, 0, nullptr, 0);
    dispatch(effect, effClose, 0, 0, nullptr, 0);
    dlclose(library);
}
