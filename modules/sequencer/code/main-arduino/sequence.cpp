#include "sequence.h"

// Note: this entire class is used in a static way, it is not to be instantiated.

volatile byte Sequence::_currentStep = 0;
volatile byte Sequence::_currentStepRepetition = 0;
volatile byte Sequence::_sequenceMode = SEQUENCE_MODE_FORWARD;
volatile byte Sequence::_gateMode[NUM_STEPS];
volatile byte Sequence::_stepRepeat[NUM_STEPS];
volatile byte Sequence::_timeDivider = DEFAULT_TIME_DIVIDER;
volatile byte Sequence::_indexInSequence = 0;
volatile unsigned long Sequence::_pulsesPerSubstep = (PPQ / 2) / (DEFAULT_TIME_DIVIDER / 4);
volatile bool Sequence::_firstHalfOfStep = true;
volatile unsigned long Sequence::_internalTicks = 0;
volatile bool Sequence::_running = false;
volatile bool Sequence::_gate = false;
volatile bool Sequence::_trigger = false;
BoolChangedHandler Sequence::_onRunningChangedHandler;
BoolChangedHandler Sequence::_onGateChangedHandler;
BoolChangedHandler Sequence::_onTriggerChangedHandler;
ByteChangedHandler Sequence::_onSelectedStepChangedHandler;
ByteChangedHandler Sequence::_onSequenceModeChangedHandler;

// Initialize hardcoded sequences
const byte Sequence::_sequences[][MAX_SEQUENCE_LENGTH + 1] = {
  // 1: Simple forward, length 8
  { 0, 1, 2, 3, 4, 5, 6, 7, SEQUENCE_TERMINATOR },
  // 2: Reverse, length 8
  { 7, 6, 5, 4, 3, 2, 1, 0, SEQUENCE_TERMINATOR },
  // 3: Forward, then revers, length 16
  { 0, 1, 2, 3, 4, 5, 6, 7, 7, 6, 5, 4, 3, 2, 1, 0, SEQUENCE_TERMINATOR },
  // 4: Even steps, then odd steps, length 8
  { 0, 2, 4, 6, 1, 3, 5, 7, SEQUENCE_TERMINATOR },
  // 5: The above, reversed, length 8
  { 7, 5, 3, 1, 6, 4, 2, 0, SEQUENCE_TERMINATOR },
  // 6: The above two in order kinda, length 8
  { 0, 2, 4, 6, 1, 3, 5, 7, 6, 4, 2, 0, 7, 5, 3, 1, SEQUENCE_TERMINATOR },
  // 7: Even step forwared, odd steps backwards, length 8
  { 0, 2, 4, 6, 7, 5, 3, 1, SEQUENCE_TERMINATOR },
  // 8: Random steps, generated when random mode is selected, length 16
  { SEQ_RS, SEQ_RS, SEQ_RS, SEQ_RS, SEQ_RS, SEQ_RS, SEQ_RS, SEQ_RS,
    SEQ_RS, SEQ_RS, SEQ_RS, SEQ_RS, SEQ_RS, SEQ_RS, SEQ_RS, SEQ_RS,
    SEQUENCE_TERMINATOR }
};

volatile byte Sequence::_selectedSequence[MAX_SEQUENCE_LENGTH + 1]; 

void Sequence::init() {
  for (byte i = 0; i < NUM_STEPS; i++) {
    _gateMode[i] = GATE_MODE_HALF_STEP;
    _stepRepeat[i] = 1;
  }

  _initSequence(SEQUENCE_MODE_FORWARD);
}

void Sequence::tick() {
  if (!_running) return;

  unsigned long ticks = _internalTicks;
  ticks++;

  if (ticks >= _pulsesPerSubstep) {
    ticks = 0;
    _advanceSubStep();
  }

  _internalTicks = ticks;
}

void Sequence::start() {
  if (_running) return;

  _running = true;
  (*_onRunningChangedHandler)(true);
}

void Sequence::stop() {
  if (!_running) return;

  _running = false;
  (*_onRunningChangedHandler)(false);

  setGate(false);
  _setTrigger(false);
}

void Sequence::toggleRunning() {
  if (_running) {
    stop();
  } else {
    start();
  }
}

bool Sequence::isRunning() {
  return _running;
}

void Sequence::setSequenceMode(byte newSequenceMode) {
  if (_sequenceMode == newSequenceMode) {
    // Trigger change handler anyway to update display
    (*_onSequenceModeChangedHandler)(newSequenceMode);

    if (_sequenceMode == SEQUENCE_MODE_RANDOM) {
      _initSequence(_sequenceMode);
    }
    return;
  }

  _sequenceMode = newSequenceMode;
  _initSequence(_sequenceMode);
  (*_onSequenceModeChangedHandler)(newSequenceMode);
}

void Sequence::selectStep(byte newSelectedStep) {
  _selectStep(constrain(newSelectedStep, 0, NUM_STEPS - 1));
}

byte Sequence::getSelectedStep() {
  return _currentStep;
}

void Sequence::setGate(bool on) {
  if (_gate == on) return;

  _gate = on;
  (*_onGateChangedHandler)(on);

  if (on) _setTrigger(true);
}

void Sequence::setTimeDivider(byte newDivider) {
  _timeDivider = constrain(newDivider, 1, 16);
  _pulsesPerSubstep = (short int)((double)(PPQ / 2) / ((double)newDivider / (double)4));
}

byte Sequence::cycleTimeDivider(bool higher) {
  byte newTimeDivider;

  if (higher) {
    newTimeDivider = _timeDivider << 1;

    if (newTimeDivider > MAX_TIME_DIVIDER) {
      newTimeDivider = MAX_TIME_DIVIDER;
    }
  } else {
    newTimeDivider = _timeDivider >> 1;

    if (newTimeDivider < MIN_TIME_DIVIDER) {
      newTimeDivider = MIN_TIME_DIVIDER;
    }
  }

  setTimeDivider(newTimeDivider);
  return _timeDivider;
}

byte Sequence::setGateModeForStep(byte step, byte gateMode) {
  step = constrain(step, 0, NUM_STEPS - 1);
  gateMode = constrain(gateMode, 0, MAX_GATE_MODE_VALUE);

  _gateMode[step] = gateMode;
  return gateMode;
}

byte Sequence::cycleGateModeForStep(byte step) {
  step = constrain(step, 0, NUM_STEPS - 1);
  byte gateMode = _gateMode[step];

  gateMode++;
  if (gateMode > MAX_GATE_MODE_VALUE) gateMode = 0;
  _gateMode[step] = gateMode;
  return gateMode;
}

byte Sequence::setStepRepeatForStep(byte step, byte repetitions) {
  step = constrain(step, 0, NUM_STEPS - 1);
  repetitions = constrain(repetitions, MIN_STEP_REPEAT, MAX_STEP_REPEAT);

  _stepRepeat[step] = repetitions;
  return repetitions;
}

byte Sequence::cycleStepRepeatForStep(byte step) {
  step = constrain(step, 0, NUM_STEPS - 1);
  byte stepRepetitions = _stepRepeat[step];

  stepRepetitions++;
  if (stepRepetitions > MAX_STEP_REPEAT) stepRepetitions = MIN_STEP_REPEAT;
  _stepRepeat[step] = stepRepetitions;
  return stepRepetitions;
}

void Sequence::collectSettings(Settings *settingsToSave) {
  settingsToSave->timeDivider = _timeDivider;
  settingsToSave->sequenceMode = _sequenceMode;

  for (uint8_t i = 0; i < NUM_STEPS; i++) {
    settingsToSave->gateModes[i] = _gateMode[i];
    settingsToSave->stepRepeat[i] = _stepRepeat[i];
  }
}

void Sequence::loadFromSettings(Settings *settings) {
  setTimeDivider(constrain(settings->timeDivider, MIN_TIME_DIVIDER, MAX_TIME_DIVIDER));
  _sequenceMode = constrain(settings->sequenceMode, SEQUENCE_MODE_FORWARD, SEQUENCE_MODE_RANDOM);

  // TODO further clean up _timeDivider, essentially ensuring it only has one 1 in the byte

  for (uint8_t i = 0; i < NUM_STEPS; i++) {
    _gateMode[i] = constrain(settings->gateModes[i], GATE_MODE_HALF_STEP, MAX_GATE_MODE_VALUE);
    _stepRepeat[i] = constrain(settings->stepRepeat[i], MIN_STEP_REPEAT, MAX_STEP_REPEAT);
  }
}

void Sequence::onRunningChange(BoolChangedHandler handler) {
  _onRunningChangedHandler = handler;
}

void Sequence::onGateChange(BoolChangedHandler handler) {
  _onGateChangedHandler = handler;
}

void Sequence::onTriggerChange(BoolChangedHandler handler) {
  _onTriggerChangedHandler = handler;
}

void Sequence::onSelectedStepChange(ByteChangedHandler handler) {
  _onSelectedStepChangedHandler = handler;
}

void Sequence::onSequenceModeChange(ByteChangedHandler handler) {
  _onSequenceModeChangedHandler = handler;
}

void Sequence::_selectStep(byte newSelectedStep) {
  if (_currentStep == newSelectedStep) return;
  _currentStep = newSelectedStep;
  (*_onSelectedStepChangedHandler)(newSelectedStep);
}

void Sequence::_setTrigger(bool on) {
  if (_trigger == on) return;

  _trigger = on;
  (*_onTriggerChangedHandler)(on);
}

void Sequence::_advanceSubStep() {
  _firstHalfOfStep = !_firstHalfOfStep;
  bool firstHalf = _firstHalfOfStep;
  bool gateWasOn = _gate;
  byte repeatsForThisStep = _stepRepeat[Sequence::_currentStep];

  if (firstHalf) {
    if (_currentStepRepetition >= repeatsForThisStep) {
      _advanceSequence();
      _currentStepRepetition = 0;
    }

    while (_stepRepeat[_currentStep] == 0) {
      _advanceSequence();
      _currentStepRepetition = 0;
    }

    _currentStepRepetition++;

    (*_onSelectedStepChangedHandler)(_currentStep);
  }

  switch (_gateMode[_currentStep]) {
    case GATE_MODE_HALF_STEP:
      setGate(
        _firstHalfOfStep && _currentStepRepetition == 1);
      break;

    case GATE_MODE_FULL_STEP:
      setGate(_currentStepRepetition == 1);
      break;

    case GATE_MODE_REPEAT_HALF:
      setGate(_firstHalfOfStep);
      break;

    case GATE_MODE_REPEAT_FULL:
      setGate(true);
      break;

    case GATE_MODE_SILENT:
      setGate(false);
      break;
  }

  if ((_gate && firstHalf && _currentStepRepetition == 1) || (_gate && !gateWasOn && firstHalf)) {
    _setTrigger(true);
  } else if (!firstHalf) {
    _setTrigger(false);
  }
}

void Sequence::_advanceSequence() {
  if (_selectedSequence[++_indexInSequence] == SEQUENCE_TERMINATOR) {
    _indexInSequence = 0;
  }

  _selectStep(_selectedSequence[_indexInSequence]);
}

void Sequence::_initSequence(byte sequenceMode) {
  sequenceMode = constrain(sequenceMode, MIN_SEQUENCE_MODE, MAX_SEQUENCE_MODE);

  if (sequenceMode == SEQUENCE_MODE_RANDOM) {
    ::randomSeed(::millis());
  }

  byte *sequence = _sequences[sequenceMode];

  byte previousStep = SEQUENCE_TERMINATOR;
  byte i;
  bool reachedEnd = false;

  // Copies over the values of the selected sequence,
  // and fills remaining space with sequence terminator
  for (i = 0; i <= MAX_SEQUENCE_LENGTH; i++) {
    if (reachedEnd) {
      _selectedSequence[i] = SEQUENCE_TERMINATOR;
      continue;
    }

    if (sequence[i] == SEQUENCE_RANDOM_STEP) {
      _selectedSequence[i] = _generateRandomStep(previousStep);
      previousStep = _selectedSequence[i];
    } else {
      _selectedSequence[i] = sequence[i];
    }

    if (_selectedSequence[i] == SEQUENCE_TERMINATOR) reachedEnd = true;
  }

  _selectedSequence[i] = SEQUENCE_TERMINATOR;
}

byte Sequence::_generateRandomStep(byte previousStep) {
  byte nextStep;

  do {
    nextStep = random(NUM_STEPS);
  } while (nextStep == previousStep);

  return nextStep;
}
