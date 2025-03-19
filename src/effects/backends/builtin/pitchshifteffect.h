#pragma once

#include <rubberband/RubberBandStretcher.h>

#include <QMap>

#include "effects/backends/effectprocessor.h"
#include "util/class.h"
#include "util/samplebuffer.h"
#include "util/types.h"

#ifdef RUBBERBAND_API_MAJOR_VERSION
#define RUBBERBANDV4 (RUBBERBAND_API_MAJOR_VERSION >= 3)
#else
#define RUBBERBANDV4 0
#endif

#if RUBBERBANDV4
#include <rubberband/RubberBandLiveShifter.h>
#endif

namespace RubberBand {
class RubberBandStretcher;
} // namespace RubberBand

#if RUBBERBANDV4
using RubberBandType = RubberBand::RubberBandLiveShifter;
// using RubberBandType = RubberBand::RubberBandStretcher;
#else
using RubberBandType = RubberBand::RubberBandStretcher;
#endif

class PitchShiftGroupState : public EffectState {
  public:
    PitchShiftGroupState(const mixxx::EngineParameters& engineParameters);
    ~PitchShiftGroupState() override;

    void initializeBuffer(const mixxx::EngineParameters& engineParameters);
    void audioParametersChanged(const mixxx::EngineParameters& engineParameters);

    std::unique_ptr<RubberBandType> m_pRubberBand;
    mixxx::SampleBuffer m_retrieveBuffer[2];
};

class PitchShiftEffect final : public EffectProcessorImpl<PitchShiftGroupState> {
  public:
    PitchShiftEffect();
    ~PitchShiftEffect() override = default;

    static QString getId();
    static EffectManifestPointer getManifest();

    void loadEngineEffectParameters(
            const QMap<QString, EngineEffectParameterPointer>& parameters) override;

    void processChannel(
            PitchShiftGroupState* pState,
            const CSAMPLE* pInput,
            CSAMPLE* pOutput,
            const mixxx::EngineParameters& engineParameters,
            const EffectEnableState enableState,
            const GroupFeatureState& groupFeatures) override;

  private:
    QString debugString() const {
        return getId();
    }

    bool m_currentFormant;
    EngineEffectParameterPointer m_pPitchParameter;
    EngineEffectParameterPointer m_pRangeParameter;
    EngineEffectParameterPointer m_pSemitonesModeParameter;
    EngineEffectParameterPointer m_pFormantPreservingParameter;

    DISALLOW_COPY_AND_ASSIGN(PitchShiftEffect);
};
