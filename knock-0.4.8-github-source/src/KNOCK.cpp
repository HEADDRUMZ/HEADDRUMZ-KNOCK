// Stereo-linked compression -> transient gain -> saturation -> soft clip.
// Blend that wet path with the original input, then apply output gain.
#include "mpc_plugin.h"
#include <array>
#include <complex>
#include "dsp/math.h"
#include "dsp/biquad.h"
#include "dsp/onepole.h"
#include "dsp/smoother.h"

using namespace mpc::dsp;

class Knock final : public mpc::Plugin
{
    enum Parameter
    {
        Output, Input, Threshold, Attack, Release, Transient, Drive,
        Clip, Mix, Source, Style, ClipOn, GainReduction, InputPeak, Weight, Snap, Dirt, EqLow, EqMid, EqHz, EqQ, EqHigh, EqOn, Compare, CurveFirst
    };

    static constexpr int curvePoints = 29;
    static constexpr int driveLevel = CurveFirst + curvePoints;

    struct Voicing
    {
        float sidechainHz;
        float slowMs;
        float transientDepth;
        float driveScale;
    };

    static constexpr Voicing voicings_[] = {{43, 35, 0.88f, 1.0f},
                                             {105, 85, 0.36f, 0.78f},
                                             {70, 52, 0.64f, 1.0f}};

    struct Character
    {
        float attackScale;
        float releaseScale;
        float kneeWidth;
    };

public:
    static mpc::PluginInfo info()
    {
        mpc::PluginInfo result;
        result.name = "KNOCK";
        result.vendor = "HEADDRUMZ";
        result.uniqueId = MPC_FOURCC('H', 't', 'N', 'w');
        result.version = 480;
        result.isInstrument = false;
        result.numInputs = result.numOutputs = 2;
        return result;
    }

    Knock() : mpc::Plugin(info())
    {
        using namespace mpc;
        addParam(floatParam("output", "Output", -18, 18, 0, "dB", 1));
        addParam(floatParam("input", "Input", -18, 18, 0, "dB", 1));
        addParam(floatParam("threshold", "Threshold", -48, 0, -16, "dB", 1));
        addParam(logParam("attack", "Attack", 0.5f, 60, 12, "ms", 1));
        addParam(logParam("release", "Release", 20, 600, 140, "ms", 0));
        addParam(floatParam("transient", "Transient", -100, 100, 15, "%", 0));
        addParam(floatParam("drive", "Drive", 0, 18, 3, "dB", 1));
        addParam(floatParam("ceiling", "Clip Ceiling", -18, 0, -0.5f, "dB", 1));
        addParam(floatParam("mix", "Mix", 0, 100, 70, "%", 0));
        addParam(choiceParam("source", "Source", {"Kick", "808", "Drum Bus"}, 2));
        addParam(choiceParam("style", "Style", {"Glue", "Punch", "Crush"}, 0));
        addParam(boolParam("clip_on", "Clip", true));
        auto readout = floatParam("gr_readout", "Gain Reduct", 0, 24, 0, "dB", 1);
        readout.automatable = false;
        addParam(readout);
        auto inputPeak = floatParam("input_peak", "Input Peak", -60, 0, -60, "dBFS", 1);
        inputPeak.automatable = false;
        addParam(inputPeak);
        addParam(floatParam("weight", "Weight", -12, 12, 0, "dB", 1));
        addParam(floatParam("snap", "Snap", -12, 12, 0, "dB", 1));
        addParam(floatParam("dirt", "Dirt", 0, 100, 0, "%", 0));
        addParam(floatParam("eq_low", "Low Shelf", -12, 12, 0, "dB", 1));
        addParam(floatParam("eq_mid", "Mid Gain", -12, 12, 0, "dB", 1));
        addParam(logParam("eq_hz", "Mid Freq", 120, 8000, 450, "Hz", 0));
        addParam(logParam("eq_q", "Mid Q", 0.4f, 5, 1, "", 2));
        addParam(floatParam("eq_high", "High Shelf", -12, 12, 0, "dB", 1));
        addParam(boolParam("eq_on", "EQ", true));
        addParam(choiceParam("compare", "Compare", {"Normal", "A Dry", "B Matched"}, 0));
        for (int i = 0; i < curvePoints; ++i)
        {
            char id[24], name[16];
            std::snprintf(id, sizeof id, "eq_curve_%d", i);
            std::snprintf(name, sizeof name, "Curve %02d", i+1);
            auto point = floatParam(id, name, -24, 24, 0, "dB", 1);
            point.automatable = false;
            addParam(point);
        }

        auto driveMeter = floatParam("drive_level", "Drive Level", -24, 6, -24, "dB", 1);
        driveMeter.automatable = false;
        addParam(driveMeter);

        // Tone ends with Weight, Snap and Dirt. EQ follows with low gain,
        // mid gain, mid frequency, Q, high gain and enable.
        addProgram("West Coast",   { -1, 0, -19, 25, 125,  48,  2, -1.0f,  85, 2, 0, 1, 0, -60,   4,   5,   5, 0, 0, 450, 1, 0, 1, 0 });
        addProgram("Boom Bap", { -2, 0, -24, 35,  65, 100,  0, -1.0f, 100, 0, 1, 1, 0, -60,  -2,  10,   8, 0, 0, 450, 1, 0, 1, 0 });
        addProgram("Trap 808",   {  0, 0, -24,  9, 280, -28,  0, -1.0f, 100, 1, 0, 1, 0, -60,   8,  -9,   0, 0, 0, 450, 1, 0, 1, 0 });
        addProgram("Drum & Bass",  {  3, 6, -26,  1, 160, -70,  0, -5.0f, 100, 2, 2, 1, 0, -60,  -5,   6, 100, 0, 0, 450, 1, 0, 1, 0 });
        addProgram("Lo-Fi Hip Hop", {  0, 0, -20,  2, 110, -95,  2, -2.0f, 100, 0, 0, 1, 0, -60,   6, -12,   0, 0, 0, 450, 1, 0, 1, 0 });
        addProgram("Drill Grit",   {  1, 2, -14, 18,  90,  -8,  0, -4.0f, 100, 1, 1, 1, 0, -60,  -3,   5,  78, 0, 0, 450, 1, 0, 1, 0 });
    }

    void prepare(double sampleRate, int /*maxBlockSize*/) override
    {
        for (auto& control : controls_) control.setTime(10, sampleRate);
        matchGain_.setTime(130, sampleRate);
        for (auto& filter : body_) filter.setCutoff(105, sampleRate);
        for (auto& filter : edge_) filter.setCutoff(1700, sampleRate);
        for (auto& filter : dirtHighPass_) filter.setCutoff(120, sampleRate);
        for (auto& filter : eqLow_) filter.setCutoff(90, sampleRate);
        // Bilinear one-pole high-pass for the parallel shelf. Its gain
        // reaches unity at Nyquist, including at the MPC's 44.1 kHz rate.
        const float cutoff = std::min(7500.0f, 0.45f * static_cast<float>(sampleRate));
        const float tangent = std::tan(kPi * cutoff / static_cast<float>(sampleRate));
        const float scale = 1.0f / (1.0f + tangent);
        eqHighCoeffs_ = BiquadCoeffs{scale, -scale, 0, (tangent - 1.0f) * scale, 0};
        for (auto& filter : eqHigh_) filter.setCoeffs(eqHighCoeffs_);
    }

    void reset() override
    {
        for (auto& filter : detector_) filter.reset();
        for (auto& filter : body_) filter.reset();
        for (auto& filter : edge_) filter.reset();
        for (auto& filter : dirtHighPass_) filter.reset();
        for (auto& filter : eqLow_) filter.reset();
        for (auto& filter : eqMid_) filter.reset();
        for (auto& filter : eqHigh_) filter.reset();
        eqFrequency_ = get(EqHz);
        eqQ_ = get(EqQ);
        fast_ = slow_ = meter_ = 0;
        inputMeter_ = -60;
        displayFrames_ = 0;
        dryEnergy_ = processedEnergy_ = driveEnergy_ = 0;
        set(driveLevel, -24);
        matchGain_.snap(1);
        curveDirty_ = true;
        compressorGain_ = 1;
        set(InputPeak, -60);
        set(GainReduction, 0);
        updateControls(true);
    }

    void process(mpc::ProcessContext& ctx) override
    {
        constexpr Character characters[] = {{1.0f, 1.0f, 0.65f},
                                              {0.65f, 0.76f, 0.24f},
                                              {0.26f, 0.58f, 0.001f}};
        const int style = getInt(Style);
        const auto& character = characters[style];
        const auto& voicing = voicings_[getInt(Source)];
        for (auto& filter : detector_) filter.setCutoff(voicing.sidechainHz, ctx.sampleRate);
        const float attack = onePoleCoeff(get(Attack) * character.attackScale, ctx.sampleRate);
        const float release = onePoleCoeff(get(Release) * character.releaseScale, ctx.sampleRate);
        const float fastAttack = onePoleCoeff(0.65f, ctx.sampleRate);
        const float fastRelease = onePoleCoeff(8, ctx.sampleRate);
        const float slowResponse = onePoleCoeff(voicing.slowMs, ctx.sampleRate);
        updateControls(false);
        // Smooth frequency and width at block rate. Band gains ramp per sample.
        const float eqStep = onePoleCoeff(20, ctx.sampleRate) * ctx.numFrames;
        const float blend = clamp(eqStep, 0.0f, 1.0f);
        eqFrequency_ = lerp(eqFrequency_, get(EqHz), blend);
        eqQ_ = lerp(eqQ_, get(EqQ), blend);
        const auto mid = BiquadCoeffs::bandpass(ctx.sampleRate, eqFrequency_, eqQ_);
        for (auto& filter : eqMid_) filter.setCoeffs(mid);

        float minimumGain = 1;
        float inputPeak = 0;
        double drySquares = 0, processedSquares = 0, drivenSquares = 0;
        for (int frame = 0; frame < ctx.numFrames; ++frame)
        {
            const float left = sanitize(ctx.inputs[0][frame]);
            const float right = sanitize(ctx.inputs[1][frame]);
            const float inputGain = controls_[InputGain].process();
            const float wetLeft = left * inputGain;
            const float wetRight = right * inputGain;
            // A high-pass on the linked detector leaves the actual audio full range.
            const float peak = std::max(std::abs(detector_[0].process(wetLeft)),
                                        std::abs(detector_[1].process(wetRight)));
            const float above = std::max(1.0f, peak / controls_[ThresholdGain].process());
            // Square roots implement the static slopes for 2:1, 4:1 and 8:1.
            const float inverse = 1.0f / above;
            const float half = std::sqrt(inverse);
            const float quarter = std::sqrt(half);
            const float fullReduction = style == 0 ? half :
                half * quarter * (style == 1 ? 1.0f : std::sqrt(quarter));
            const float knee = clamp((above - 1.0f) / character.kneeWidth, 0.0f, 1.0f);
            const float target = lerp(1.0f, fullReduction, knee);
            compressorGain_ += (target < compressorGain_ ? attack : release) * (target - compressorGain_);
            minimumGain = std::min(minimumGain, compressorGain_);

            // Fast/slow peak contrast adds or subtracts attack on both channels.
            const float amplitude = std::max(std::abs(wetLeft), std::abs(wetRight));
            inputPeak = std::max(inputPeak, amplitude);
            fast_ += (amplitude > fast_ ? fastAttack : fastRelease) * (amplitude - fast_);
            slow_ += slowResponse * (amplitude - slow_);
            const float onset = clamp((fast_ - slow_) / (fast_ + slow_ + 0.005f), 0.0f, 1.0f);
            const float gain = compressorGain_ * (1.0f + controls_[ShapeDepth].process() * onset);
            const float drive = controls_[DriveGain].process();
            const float reciprocalDrive = 1.0f / drive;
            const float saturation = controls_[SaturationBlend].process();
            const float ceiling = controls_[Ceiling].process();
            const float reciprocalCeiling = 1.0f / ceiling;
            const float clipping = controls_[ClipBlend].process();
            const float mix = controls_[Blend].process();
            const float output = controls_[OutputGain].process();
            const float weight = controls_[BodyGain].process();
            const float snap = controls_[EdgeGain].process();
            const float dirt = controls_[DirtBlend].process();
            const float eqLow = controls_[EqLowGain].process();
            const float eqMid = controls_[EqMidGain].process();
            const float eqHigh = controls_[EqHighGain].process();
            const float dryCompare = controls_[CompareDry].process();
            const float matchAmount = controls_[CompareMatch].process();
            const float compensation = lerp(1.0f, matchGain_.process(), matchAmount);

            for (int channel = 0; channel < 2; ++channel)
            {
                const float dry = channel == 0 ? left : right;
                const float shaped = (channel == 0 ? wetLeft : wetRight) * gain;
                const float driven = shaped * drive;
                drivenSquares += driven * static_cast<double>(driven);
                const float saturated = fastTanh(driven) * reciprocalDrive;
                float wet = lerp(shaped, saturated, saturation);
                // Shape the wet tone, then blend a high-passed distortion return.
                // Driving the return from the low frequencies generates audible
                // harmonics on an 808 while leaving the clean fundamental in place.
                wet += weight * body_[channel].process(wet) + snap * edge_[channel].process(wet);
                const float dirty = dirtHighPass_[channel].process(fastTanh(wet * (1.0f + 13.0f * dirt)));
                wet += dirt * dirty * 0.85f;
                // Series shelves and a parallel constant-peak band-pass bell.
                // EQ precedes clipping so boosted peaks still hit the ceiling.
                wet += eqLow * eqLow_[channel].process(wet);
                wet += eqMid * eqMid_[channel].process(wet);
                wet += eqHigh * eqHigh_[channel].process(wet);
                // Linear up to 75% of the ceiling, then a smooth knee to a flat limit.
                const float normalised = std::abs(wet) * reciprocalCeiling;
                if (normalised > 0.75f)
                {
                    const float t = std::min(1.0f, (normalised - 0.75f) * 2.0f);
                    const float clipped = 0.75f + 0.25f * t * (2.0f - t);
                    wet = lerp(wet, std::copysign(clipped * ceiling, wet), clipping);
                }
                const float processed = sanitize(lerp(dry, wet, mix) * output);
                drySquares += dry * static_cast<double>(dry);
                processedSquares += processed * static_cast<double>(processed);
                ctx.outputs[channel][frame] = sanitize(lerp(processed * compensation, dry, dryCompare));
            }
        }

        const float reduction = clamp(-gainToDb(minimumGain), 0.0f, 24.0f);
        const float seconds = ctx.numFrames / static_cast<float>(ctx.sampleRate);
        // Measure the dry and pre-compensation processed signals continuously.
        // Preserve the last gain through silence and cap correction to +/-12 dB.
        const float averaging = onePoleCoeff(400, ctx.sampleRate / std::max(1, ctx.numFrames));
        const double samples = 2.0 * std::max(1, ctx.numFrames);
        const float driveResponse = onePoleCoeff(125, ctx.sampleRate / std::max(1, ctx.numFrames));
        driveEnergy_ += driveResponse * (drivenSquares / samples - driveEnergy_);
        dryEnergy_ += averaging * (drySquares / samples - dryEnergy_);
        processedEnergy_ += averaging * (processedSquares / samples - processedEnergy_);
        if (drySquares > 1.0e-7 * samples && dryEnergy_ > 1.0e-7 && processedEnergy_ > 1.0e-7)
            matchGain_.setTarget(clamp(static_cast<float>(std::sqrt(dryEnergy_ / processedEnergy_)), 0.25119f, 3.98107f));

        meter_ = std::max(reduction, meter_ - 30.0f * seconds);
        inputMeter_ = std::max(clamp(gainToDb(inputPeak), -60.0f, 0.0f),
                               inputMeter_ - 48.0f * seconds);

        // Refresh the host display at most 25 Hz and only when a visible step
        // changes. Meter telemetry never sends an automation event.
        const int interval = std::max(1, static_cast<int>(ctx.sampleRate / 25.0));
        displayFrames_ = std::min(interval, displayFrames_ + ctx.numFrames);
        if (displayFrames_ == interval)
        {
            const float previousInput = lastDisplayInput_;
            const float previousReduction = lastDisplayReduction_;
            set(InputPeak, inputMeter_);
            set(GainReduction, meter_);
            // Needle scale is relative to a -18 dBFS RMS reference.
            set(driveLevel, clamp(gainToDb(static_cast<float>(std::sqrt(driveEnergy_))) + 18, -24.0f, 6.0f));
            const int needleFrame = static_cast<int>(std::lround(getNormalised(driveLevel) * 60));
            const bool needleMoved = needleFrame != lastDriveFrame_;
            lastDriveFrame_ = needleFrame;
            constexpr float step = 1.0f / 24.0f;
            const bool curveChanged = updateEqCurve(ctx.sampleRate);
            if (curveChanged || needleMoved || std::abs(getNormalised(InputPeak) - previousInput) >= step ||
                std::abs(getNormalised(GainReduction) - previousReduction) >= step ||
                (getNormalised(InputPeak) == 0 && previousInput != 0) ||
                (getNormalised(GainReduction) == 0 && previousReduction != 0))
            {
                lastDisplayInput_ = getNormalised(InputPeak);
                lastDisplayReduction_ = getNormalised(GainReduction);
                requestDisplayUpdate();
            }
            displayFrames_ = 0;
        }
    }

private:
    enum Control
    {
        OutputGain, InputGain, ThresholdGain, ShapeDepth, DriveGain,
        SaturationBlend, Ceiling, Blend, ClipBlend, BodyGain, EdgeGain, DirtBlend, EqLowGain, EqMidGain, EqHighGain, CompareDry, CompareMatch, ControlCount
    };

    void updateControls(bool snap)
    {
        const auto& voicing = voicings_[getInt(Source)];
        const float driveDb = get(Drive) * voicing.driveScale;
        const float eqEnabled = getBool(EqOn) ? 1.0f : 0.0f;
        const float targets[] = {
            dbToGain(get(Output)), dbToGain(get(Input)), dbToGain(get(Threshold)),
            get(Transient) * 0.01f * voicing.transientDepth, dbToGain(driveDb),
            clamp(driveDb / 8.0f, 0.0f, 1.0f), dbToGain(get(Clip)),
            get(Mix) * 0.01f, getBool(ClipOn) ? 1.0f : 0.0f,
            dbToGain(get(Weight)) - 1.0f, dbToGain(get(Snap)) - 1.0f, get(Dirt) * 0.01f,
            eqEnabled * (dbToGain(get(EqLow)) - 1.0f), eqEnabled * (dbToGain(get(EqMid)) - 1.0f),
            eqEnabled * (dbToGain(get(EqHigh)) - 1.0f),
            getInt(Compare) == 1 ? 1.0f : 0.0f, getInt(Compare) == 2 ? 1.0f : 0.0f
        };
        for (int i = 0; i < ControlCount; ++i)
        {
            if (snap) controls_[i].snap(targets[i]);
            else controls_[i].setTarget(targets[i]);
        }
    }

    bool updateEqCurve(double rate)
    {
        const std::array<float, 6> settings{get(EqLow), get(EqMid), get(EqHz),
                                             get(EqQ), get(EqHigh), get(EqOn)};
        if (!curveDirty_ && settings == curveSettings_) return false;
        curveDirty_ = false;
        curveSettings_ = settings;

        using Complex = std::complex<double>;
        const auto response = [](const BiquadCoeffs& f, Complex z) {
            return (static_cast<double>(f.b0) + z * (static_cast<double>(f.b1) + z * static_cast<double>(f.b2))) /
                   (1.0 + z * (static_cast<double>(f.a1) + z * static_cast<double>(f.a2)));
        };
        const double lowPole = std::exp(-2.0 * kPi * 90.0 / rate);
        const auto mid = BiquadCoeffs::bandpass(rate, settings[2], settings[3]);
        const double low = dbToGain(settings[0]) - 1.0;
        const double bell = dbToGain(settings[1]) - 1.0;
        const double high = dbToGain(settings[4]) - 1.0;
        for (int i = 0; i < curvePoints; ++i)
        {
            const double frequency = std::min(0.49 * rate, 25.0 * std::pow(800.0, i / double(curvePoints - 1)));
            const Complex z = std::polar(1.0, -2.0 * kPi * frequency / rate);
            const auto lowResponse = (1.0 - lowPole) / (1.0 - lowPole * z);
            const auto total = (1.0 + low * lowResponse) *
                               (1.0 + bell * response(mid, z)) *
                               (1.0 + high * response(eqHighCoeffs_, z));
            set(CurveFirst + i, settings[5] > 0.5f ? clamp(gainToDb(static_cast<float>(std::abs(total))), -24.0f, 24.0f) : 0.0f);
        }
        return true;
    }

    bool curveDirty_ = true;
    std::array<float, 6> curveSettings_{};
    BiquadCoeffs eqHighCoeffs_;
    Smoother matchGain_;
    double dryEnergy_ = 0, processedEnergy_ = 0, driveEnergy_ = 0;
    int lastDriveFrame_ = -1;

    float inputMeter_ = -60;
    float lastDisplayInput_ = 0, lastDisplayReduction_ = 0;
    int displayFrames_ = 0;
    OnePoleHP detector_[2], edge_[2], dirtHighPass_[2];
    OnePoleLP body_[2], eqLow_[2];
    Biquad eqMid_[2], eqHigh_[2];
    float eqFrequency_ = 450, eqQ_ = 1;
    Smoother controls_[ControlCount];
    float fast_ = 0, slow_ = 0, compressorGain_ = 1, meter_ = 0;
};

MPC_PLUGIN_ENTRY(Knock)
