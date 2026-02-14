/*
 * MIT License
 * 
 * Copyright (c) 2021-2022 John "Nielk1" Klein
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 * 
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include "DualSenseTriggerEffectGenerator.h"
#include <cmath>

namespace ExtendInput {
namespace DataTools {
namespace DualSense {

// Official Effects

bool DualSenseTriggerEffectGenerator::Off(uint8_t* destinationArray, int destinationIndex) {
    destinationArray[destinationIndex +  0] = static_cast<uint8_t>(TriggerEffectType::Off);
    destinationArray[destinationIndex +  1] = 0x00;
    destinationArray[destinationIndex +  2] = 0x00;
    destinationArray[destinationIndex +  3] = 0x00;
    destinationArray[destinationIndex +  4] = 0x00;
    destinationArray[destinationIndex +  5] = 0x00;
    destinationArray[destinationIndex +  6] = 0x00;
    destinationArray[destinationIndex +  7] = 0x00;
    destinationArray[destinationIndex +  8] = 0x00;
    destinationArray[destinationIndex +  9] = 0x00;
    destinationArray[destinationIndex + 10] = 0x00;
    return true;
}

bool DualSenseTriggerEffectGenerator::Feedback(uint8_t* destinationArray, int destinationIndex, uint8_t position, uint8_t strength) {
    if (position > 9)
        return false;
    if (strength > 8)
        return false;
    if (strength > 0) {
        uint8_t forceValue = (strength - 1) & 0x07;
        uint32_t forceZones  = 0;
        uint16_t activeZones = 0;
        for (int i = position; i < 10; i++) {
            forceZones  |= static_cast<uint32_t>(forceValue << (3 * i));
            activeZones |= static_cast<uint16_t>(1 << i);
        }

        destinationArray[destinationIndex +  0] = static_cast<uint8_t>(TriggerEffectType::Feedback);
        destinationArray[destinationIndex +  1] = static_cast<uint8_t>((activeZones >> 0) & 0xff);
        destinationArray[destinationIndex +  2] = static_cast<uint8_t>((activeZones >> 8) & 0xff);
        destinationArray[destinationIndex +  3] = static_cast<uint8_t>((forceZones >>  0) & 0xff);
        destinationArray[destinationIndex +  4] = static_cast<uint8_t>((forceZones >>  8) & 0xff);
        destinationArray[destinationIndex +  5] = static_cast<uint8_t>((forceZones >> 16) & 0xff);
        destinationArray[destinationIndex +  6] = static_cast<uint8_t>((forceZones >> 24) & 0xff);
        destinationArray[destinationIndex +  7] = 0x00;
        destinationArray[destinationIndex +  8] = 0x00;
        destinationArray[destinationIndex +  9] = 0x00;
        destinationArray[destinationIndex + 10] = 0x00;
        return true;
    }
    return Off(destinationArray, destinationIndex);
}

bool DualSenseTriggerEffectGenerator::Weapon(uint8_t* destinationArray, int destinationIndex, uint8_t startPosition, uint8_t endPosition, uint8_t strength) {
    if (startPosition > 7 || startPosition < 2)
        return false;
    if (endPosition > 8)
        return false;
    if (endPosition <= startPosition)
        return false;
    if (strength > 8)
        return false;
    if (strength > 0) {
        uint16_t startAndStopZones = static_cast<uint16_t>((1 << startPosition) | (1 << endPosition));

        destinationArray[destinationIndex +  0] = static_cast<uint8_t>(TriggerEffectType::Weapon);
        destinationArray[destinationIndex +  1] = static_cast<uint8_t>((startAndStopZones >> 0) & 0xff);
        destinationArray[destinationIndex +  2] = static_cast<uint8_t>((startAndStopZones >> 8) & 0xff);
        destinationArray[destinationIndex +  3] = strength - 1;
        destinationArray[destinationIndex +  4] = 0x00;
        destinationArray[destinationIndex +  5] = 0x00;
        destinationArray[destinationIndex +  6] = 0x00;
        destinationArray[destinationIndex +  7] = 0x00;
        destinationArray[destinationIndex +  8] = 0x00;
        destinationArray[destinationIndex +  9] = 0x00;
        destinationArray[destinationIndex + 10] = 0x00;
        return true;
    }
    return Off(destinationArray, destinationIndex);
}

bool DualSenseTriggerEffectGenerator::Vibration(uint8_t* destinationArray, int destinationIndex, uint8_t position, uint8_t amplitude, uint8_t frequency) {
    if (position > 9)
        return false;
    if (amplitude > 8)
        return false;
    if (amplitude > 0 && frequency > 0) {
        uint8_t strengthValue = (amplitude - 1) & 0x07;
        uint32_t amplitudeZones = 0;
        uint16_t activeZones    = 0;
        for (int i = position; i < 10; i++) {
            amplitudeZones |= static_cast<uint32_t>(strengthValue << (3 * i));
            activeZones    |= static_cast<uint16_t>(1 << i);
        }

        destinationArray[destinationIndex +  0] = static_cast<uint8_t>(TriggerEffectType::Vibration);
        destinationArray[destinationIndex +  1] = static_cast<uint8_t>((activeZones    >>  0) & 0xff);
        destinationArray[destinationIndex +  2] = static_cast<uint8_t>((activeZones    >>  8) & 0xff);
        destinationArray[destinationIndex +  3] = static_cast<uint8_t>((amplitudeZones >>  0) & 0xff);
        destinationArray[destinationIndex +  4] = static_cast<uint8_t>((amplitudeZones >>  8) & 0xff);
        destinationArray[destinationIndex +  5] = static_cast<uint8_t>((amplitudeZones >> 16) & 0xff);
        destinationArray[destinationIndex +  6] = static_cast<uint8_t>((amplitudeZones >> 24) & 0xff);
        destinationArray[destinationIndex +  7] = 0x00;
        destinationArray[destinationIndex +  8] = 0x00;
        destinationArray[destinationIndex +  9] = frequency;
        destinationArray[destinationIndex + 10] = 0x00;
        return true;
    }
    return Off(destinationArray, destinationIndex);
}

bool DualSenseTriggerEffectGenerator::MultiplePositionFeedback(uint8_t* destinationArray, int destinationIndex, const uint8_t* strength) {
    bool hasNonZero = false;
    for (int i = 0; i < 10; i++) {
        if (strength[i] > 0) {
            hasNonZero = true;
            break;
        }
    }

    if (hasNonZero) {
        uint32_t forceZones  = 0;
        uint16_t activeZones = 0;
        for (int i = 0; i < 10; i++) {
            if (strength[i] > 0) {
                uint8_t forceValue = (strength[i] - 1) & 0x07;
                forceZones  |= static_cast<uint32_t>(forceValue << (3 * i));
                activeZones |= static_cast<uint16_t>(1 << i);
            }
        }

        destinationArray[destinationIndex + 0] = static_cast<uint8_t>(TriggerEffectType::Feedback);
        destinationArray[destinationIndex + 1] = static_cast<uint8_t>((activeZones >> 0) & 0xff);
        destinationArray[destinationIndex + 2] = static_cast<uint8_t>((activeZones >> 8) & 0xff);
        destinationArray[destinationIndex + 3] = static_cast<uint8_t>((forceZones >>  0) & 0xff);
        destinationArray[destinationIndex + 4] = static_cast<uint8_t>((forceZones >>  8) & 0xff);
        destinationArray[destinationIndex + 5] = static_cast<uint8_t>((forceZones >> 16) & 0xff);
        destinationArray[destinationIndex + 6] = static_cast<uint8_t>((forceZones >> 24) & 0xff);
        destinationArray[destinationIndex + 7] = 0x00;
        destinationArray[destinationIndex + 8] = 0x00;
        destinationArray[destinationIndex + 9] = 0x00;
        destinationArray[destinationIndex + 10] = 0x00;
        return true;
    }
    return Off(destinationArray, destinationIndex);
}

bool DualSenseTriggerEffectGenerator::SlopeFeedback(uint8_t* destinationArray, int destinationIndex, uint8_t startPosition, uint8_t endPosition, uint8_t startStrength, uint8_t endStrength) {
    if (startPosition > 8 || startPosition < 0)
        return false;
    if (endPosition > 9)
        return false;
    if (endPosition <= startPosition)
        return false;
    if (startStrength > 8)
        return false;
    if (startStrength < 1)
        return false;
    if (endStrength > 8)
        return false;
    if (endStrength < 1)
        return false;

    uint8_t strength[10] = {0};
    float slope = 1.0f * (endStrength - startStrength) / (endPosition - startPosition);
    for (int i = startPosition; i < 10; i++) {
        if (i <= endPosition)
            strength[i] = static_cast<uint8_t>(std::round(startStrength + slope * (i - startPosition)));
        else
            strength[i] = endStrength;
    }

    return MultiplePositionFeedback(destinationArray, destinationIndex, strength);
}

bool DualSenseTriggerEffectGenerator::MultiplePositionVibration(uint8_t* destinationArray, int destinationIndex, uint8_t frequency, const uint8_t* amplitude) {
    bool hasNonZero = false;
    for (int i = 0; i < 10; i++) {
        if (amplitude[i] > 0) {
            hasNonZero = true;
            break;
        }
    }

    if (frequency > 0 && hasNonZero) {
        uint32_t strengthZones = 0;
        uint16_t activeZones   = 0;
        for (int i = 0; i < 10; i++) {
            if (amplitude[i] > 0) {
                uint8_t strengthValue = (amplitude[i] - 1) & 0x07;
                strengthZones |= static_cast<uint32_t>(strengthValue << (3 * i));
                activeZones   |= static_cast<uint16_t>(1 << i);
            }
        }

        destinationArray[destinationIndex +  0] = static_cast<uint8_t>(TriggerEffectType::Vibration);
        destinationArray[destinationIndex +  1] = static_cast<uint8_t>((activeZones >> 0) & 0xff);
        destinationArray[destinationIndex +  2] = static_cast<uint8_t>((activeZones >> 8) & 0xff);
        destinationArray[destinationIndex +  3] = static_cast<uint8_t>((strengthZones >>  0) & 0xff);
        destinationArray[destinationIndex +  4] = static_cast<uint8_t>((strengthZones >>  8) & 0xff);
        destinationArray[destinationIndex +  5] = static_cast<uint8_t>((strengthZones >> 16) & 0xff);
        destinationArray[destinationIndex +  6] = static_cast<uint8_t>((strengthZones >> 24) & 0xff);
        destinationArray[destinationIndex +  7] = 0x00;
        destinationArray[destinationIndex +  8] = 0x00;
        destinationArray[destinationIndex +  9] = frequency;
        destinationArray[destinationIndex + 10] = 0x00;
        return true;
    }
    return Off(destinationArray, destinationIndex);
}

// Unofficial but Unique Effects

bool DualSenseTriggerEffectGenerator::Bow(uint8_t* destinationArray, int destinationIndex, uint8_t startPosition, uint8_t endPosition, uint8_t strength, uint8_t snapForce) {
    if (startPosition > 8)
        return false;
    if (endPosition > 8)
        return false;
    if (startPosition >= endPosition)
        return false;
    if (strength > 8)
        return false;
    if (snapForce > 8)
        return false;
    if (endPosition > 0 && strength > 0 && snapForce > 0) {
        uint16_t startAndStopZones = static_cast<uint16_t>((1 << startPosition) | (1 << endPosition));
        uint32_t forcePair = static_cast<uint32_t>((((strength  - 1) & 0x07) << (3 * 0))
                                                  | (((snapForce - 1) & 0x07) << (3 * 1)));

        destinationArray[destinationIndex +  0] = static_cast<uint8_t>(TriggerEffectType::Bow);
        destinationArray[destinationIndex +  1] = static_cast<uint8_t>((startAndStopZones >> 0) & 0xff);
        destinationArray[destinationIndex +  2] = static_cast<uint8_t>((startAndStopZones >> 8) & 0xff);
        destinationArray[destinationIndex +  3] = static_cast<uint8_t>((forcePair >> 0) & 0xff);
        destinationArray[destinationIndex +  4] = static_cast<uint8_t>((forcePair >> 8) & 0xff);
        destinationArray[destinationIndex +  5] = 0x00;
        destinationArray[destinationIndex +  6] = 0x00;
        destinationArray[destinationIndex +  7] = 0x00;
        destinationArray[destinationIndex +  8] = 0x00;
        destinationArray[destinationIndex +  9] = 0x00;
        destinationArray[destinationIndex + 10] = 0x00;
        return true;
    }
    return Off(destinationArray, destinationIndex);
}

bool DualSenseTriggerEffectGenerator::Galloping(uint8_t* destinationArray, int destinationIndex, uint8_t startPosition, uint8_t endPosition, uint8_t firstFoot, uint8_t secondFoot, uint8_t frequency) {
    if (startPosition > 8)
        return false;
    if (endPosition > 9)
        return false;
    if (startPosition >= endPosition)
        return false;
    if (secondFoot > 7)
        return false;
    if (firstFoot > 6)
        return false;
    if (firstFoot >= secondFoot)
        return false;
    if (frequency > 0) {
        uint16_t startAndStopZones = static_cast<uint16_t>((1 << startPosition) | (1 << endPosition));
        uint32_t timeAndRatio = static_cast<uint32_t>(((secondFoot & 0x07) << (3 * 0))
                                                     | ((firstFoot  & 0x07) << (3 * 1)));

        destinationArray[destinationIndex +  0] = static_cast<uint8_t>(TriggerEffectType::Galloping);
        destinationArray[destinationIndex +  1] = static_cast<uint8_t>((startAndStopZones >> 0) & 0xff);
        destinationArray[destinationIndex +  2] = static_cast<uint8_t>((startAndStopZones >> 8) & 0xff);
        destinationArray[destinationIndex +  3] = static_cast<uint8_t>((timeAndRatio >> 0) & 0xff);
        destinationArray[destinationIndex +  4] = frequency;
        destinationArray[destinationIndex +  5] = 0x00;
        destinationArray[destinationIndex +  6] = 0x00;
        destinationArray[destinationIndex +  7] = 0x00;
        destinationArray[destinationIndex +  8] = 0x00;
        destinationArray[destinationIndex +  9] = 0x00;
        destinationArray[destinationIndex + 10] = 0x00;
        return true;
    }
    return Off(destinationArray, destinationIndex);
}

bool DualSenseTriggerEffectGenerator::Machine(uint8_t* destinationArray, int destinationIndex, uint8_t startPosition, uint8_t endPosition, uint8_t amplitudeA, uint8_t amplitudeB, uint8_t frequency, uint8_t period) {
    if (startPosition > 8)
        return false;
    if (endPosition > 9)
        return false;
    if (endPosition <= startPosition)
        return false;
    if (amplitudeA > 7)
        return false;
    if (amplitudeB > 7)
        return false;
    if (frequency > 0) {
        uint16_t startAndStopZones = static_cast<uint16_t>((1 << startPosition) | (1 << endPosition));
        uint32_t strengthPair = static_cast<uint32_t>(((amplitudeA & 0x07) << (3 * 0))
                                                     | ((amplitudeB & 0x07) << (3 * 1)));

        destinationArray[destinationIndex +  0] = static_cast<uint8_t>(TriggerEffectType::Machine);
        destinationArray[destinationIndex +  1] = static_cast<uint8_t>((startAndStopZones >> 0) & 0xff);
        destinationArray[destinationIndex +  2] = static_cast<uint8_t>((startAndStopZones >> 8) & 0xff);
        destinationArray[destinationIndex +  3] = static_cast<uint8_t>((strengthPair >> 0) & 0xff);
        destinationArray[destinationIndex +  4] = frequency;
        destinationArray[destinationIndex +  5] = period;
        destinationArray[destinationIndex +  6] = 0x00;
        destinationArray[destinationIndex +  7] = 0x00;
        destinationArray[destinationIndex +  8] = 0x00;
        destinationArray[destinationIndex +  9] = 0x00;
        destinationArray[destinationIndex + 10] = 0x00;
        return true;
    }
    return Off(destinationArray, destinationIndex);
}

// Simple Effects

bool DualSenseTriggerEffectGenerator::Simple_Feedback(uint8_t* destinationArray, int destinationIndex, uint8_t position, uint8_t strength) {
    destinationArray[destinationIndex +  0] = static_cast<uint8_t>(TriggerEffectType::Simple_Feedback);
    destinationArray[destinationIndex +  1] = position;
    destinationArray[destinationIndex +  2] = strength;
    destinationArray[destinationIndex +  3] = 0x00;
    destinationArray[destinationIndex +  4] = 0x00;
    destinationArray[destinationIndex +  5] = 0x00;
    destinationArray[destinationIndex +  6] = 0x00;
    destinationArray[destinationIndex +  7] = 0x00;
    destinationArray[destinationIndex +  8] = 0x00;
    destinationArray[destinationIndex +  9] = 0x00;
    destinationArray[destinationIndex + 10] = 0x00;
    return true;
}

bool DualSenseTriggerEffectGenerator::Simple_Weapon(uint8_t* destinationArray, int destinationIndex, uint8_t startPosition, uint8_t endPosition, uint8_t strength) {
    destinationArray[destinationIndex +  0] = static_cast<uint8_t>(TriggerEffectType::Simple_Weapon);
    destinationArray[destinationIndex +  1] = startPosition;
    destinationArray[destinationIndex +  2] = endPosition;
    destinationArray[destinationIndex +  3] = strength;
    destinationArray[destinationIndex +  4] = 0x00;
    destinationArray[destinationIndex +  5] = 0x00;
    destinationArray[destinationIndex +  6] = 0x00;
    destinationArray[destinationIndex +  7] = 0x00;
    destinationArray[destinationIndex +  8] = 0x00;
    destinationArray[destinationIndex +  9] = 0x00;
    destinationArray[destinationIndex + 10] = 0x00;
    return true;
}

bool DualSenseTriggerEffectGenerator::Simple_Vibration(uint8_t* destinationArray, int destinationIndex, uint8_t position, uint8_t amplitude, uint8_t frequency) {
    if (frequency > 0 && amplitude > 0) {
        destinationArray[destinationIndex +  0] = static_cast<uint8_t>(TriggerEffectType::Simple_Vibration);
        destinationArray[destinationIndex +  1] = frequency;
        destinationArray[destinationIndex +  2] = amplitude;
        destinationArray[destinationIndex +  3] = position;
        destinationArray[destinationIndex +  4] = 0x00;
        destinationArray[destinationIndex +  5] = 0x00;
        destinationArray[destinationIndex +  6] = 0x00;
        destinationArray[destinationIndex +  7] = 0x00;
        destinationArray[destinationIndex +  8] = 0x00;
        destinationArray[destinationIndex +  9] = 0x00;
        destinationArray[destinationIndex + 10] = 0x00;
        return true;
    }
    return Off(destinationArray, destinationIndex);
}

// Limited Effects

bool DualSenseTriggerEffectGenerator::Limited_Feedback(uint8_t* destinationArray, int destinationIndex, uint8_t position, uint8_t strength) {
    if (strength > 10)
        return false;
    if (strength > 0) {
        destinationArray[destinationIndex +  0] = static_cast<uint8_t>(TriggerEffectType::Limited_Feedback);
        destinationArray[destinationIndex +  1] = position;
        destinationArray[destinationIndex +  2] = strength;
        destinationArray[destinationIndex +  3] = 0x00;
        destinationArray[destinationIndex +  4] = 0x00;
        destinationArray[destinationIndex +  5] = 0x00;
        destinationArray[destinationIndex +  6] = 0x00;
        destinationArray[destinationIndex +  7] = 0x00;
        destinationArray[destinationIndex +  8] = 0x00;
        destinationArray[destinationIndex +  9] = 0x00;
        destinationArray[destinationIndex + 10] = 0x00;
        return true;
    }
    return Off(destinationArray, destinationIndex);
}

bool DualSenseTriggerEffectGenerator::Limited_Weapon(uint8_t* destinationArray, int destinationIndex, uint8_t startPosition, uint8_t endPosition, uint8_t strength) {
    if (startPosition < 0x10)
        return false;
    if (endPosition < startPosition || (startPosition + 100) < endPosition)
        return false;
    if (strength > 10)
        return false;
    if (strength > 0) {
        destinationArray[destinationIndex +  0] = static_cast<uint8_t>(TriggerEffectType::Limited_Weapon);
        destinationArray[destinationIndex +  1] = startPosition;
        destinationArray[destinationIndex +  2] = endPosition;
        destinationArray[destinationIndex +  3] = strength;
        destinationArray[destinationIndex +  4] = 0x00;
        destinationArray[destinationIndex +  5] = 0x00;
        destinationArray[destinationIndex +  6] = 0x00;
        destinationArray[destinationIndex +  7] = 0x00;
        destinationArray[destinationIndex +  8] = 0x00;
        destinationArray[destinationIndex +  9] = 0x00;
        destinationArray[destinationIndex + 10] = 0x00;
        return true;
    }
    return Off(destinationArray, destinationIndex);
}

// Apple Interface Adapters

bool DualSenseTriggerEffectGenerator::Apple::SetModeOff(uint8_t* destinationArray, int destinationIndex) {
    return DualSenseTriggerEffectGenerator::Off(destinationArray, destinationIndex);
}

bool DualSenseTriggerEffectGenerator::Apple::SetModeFeedbackWithStartPosition(uint8_t* destinationArray, int destinationIndex, float startPosition, float resistiveStrength) {
    startPosition = std::round(startPosition * 9.0f);
    resistiveStrength = std::round(resistiveStrength * 8.0f);
    return DualSenseTriggerEffectGenerator::Feedback(destinationArray, destinationIndex, 
                                           static_cast<uint8_t>(startPosition), 
                                           static_cast<uint8_t>(resistiveStrength));
}

bool DualSenseTriggerEffectGenerator::Apple::SetModeWeaponWithStartPosition(uint8_t* destinationArray, int destinationIndex, float startPosition, float endPosition, float resistiveStrength) {
    startPosition = std::round(startPosition * 9.0f);
    endPosition = std::round(endPosition * 9.0f);
    resistiveStrength = std::round(resistiveStrength * 8.0f);
    return DualSenseTriggerEffectGenerator::Weapon(destinationArray, destinationIndex, 
                                         static_cast<uint8_t>(startPosition), 
                                         static_cast<uint8_t>(endPosition), 
                                         static_cast<uint8_t>(resistiveStrength));
}

bool DualSenseTriggerEffectGenerator::Apple::SetModeVibrationWithStartPosition(uint8_t* destinationArray, int destinationIndex, float startPosition, float amplitude, float frequency) {
    startPosition = std::round(startPosition * 9.0f);
    amplitude = std::round(amplitude * 8.0f);
    frequency = std::round(frequency * 255.0f);
    return DualSenseTriggerEffectGenerator::Vibration(destinationArray, destinationIndex, 
                                            static_cast<uint8_t>(startPosition), 
                                            static_cast<uint8_t>(amplitude), 
                                            static_cast<uint8_t>(frequency));
}

bool DualSenseTriggerEffectGenerator::Apple::SetModeFeedback(uint8_t* destinationArray, int destinationIndex, const float* positionalResistiveStrengths) {
    uint8_t force[10];
    for (int i = 0; i < 10; i++)
        force[i] = static_cast<uint8_t>(std::round(positionalResistiveStrengths[i] * 8.0f));

    return DualSenseTriggerEffectGenerator::MultiplePositionFeedback(destinationArray, destinationIndex, force);
}

bool DualSenseTriggerEffectGenerator::Apple::setModeSlopeFeedback(uint8_t* destinationArray, int destinationIndex, float startPosition, float endPosition, float startStrength, float endStrength) {
    startPosition = std::round(startPosition * 9.0f);
    endPosition = std::round(endPosition * 9.0f);
    startStrength = std::round(startStrength * 8.0f);
    endStrength = std::round(endStrength * 8.0f);

    return DualSenseTriggerEffectGenerator::SlopeFeedback(destinationArray, destinationIndex, 
                                                static_cast<uint8_t>(startPosition), 
                                                static_cast<uint8_t>(endPosition), 
                                                static_cast<uint8_t>(startStrength), 
                                                static_cast<uint8_t>(endStrength));
}

bool DualSenseTriggerEffectGenerator::Apple::setModeVibration(uint8_t* destinationArray, int destinationIndex, const float* positionalAmplitudes, float frequency) {
    frequency = std::round(frequency * 255.0f);

    uint8_t strength[10];
    for (int i = 0; i < 10; i++)
        strength[i] = static_cast<uint8_t>(std::round(positionalAmplitudes[i] * 8.0f));

    return DualSenseTriggerEffectGenerator::MultiplePositionVibration(destinationArray, destinationIndex, 
                                                            static_cast<uint8_t>(frequency), 
                                                            strength);
}

// ReWASD Interface Adapters

bool DualSenseTriggerEffectGenerator::ReWASD::FullPress(uint8_t* destinationArray, int destinationIndex) {
    return DualSenseTriggerEffectGenerator::Simple_Weapon(destinationArray, destinationIndex, 0x90, 0xa0, 0xff);
}

bool DualSenseTriggerEffectGenerator::ReWASD::SoftPress(uint8_t* destinationArray, int destinationIndex) {
    return DualSenseTriggerEffectGenerator::Simple_Weapon(destinationArray, destinationIndex, 0x70, 0xa0, 0xff);
}

bool DualSenseTriggerEffectGenerator::ReWASD::MediumPress(uint8_t* destinationArray, int destinationIndex) {
    return DualSenseTriggerEffectGenerator::Simple_Weapon(destinationArray, destinationIndex, 0x45, 0xa0, 0xff);
}

bool DualSenseTriggerEffectGenerator::ReWASD::HardPress(uint8_t* destinationArray, int destinationIndex) {
    return DualSenseTriggerEffectGenerator::Simple_Weapon(destinationArray, destinationIndex, 0x20, 0xa0, 0xff);
}

bool DualSenseTriggerEffectGenerator::ReWASD::Pulse(uint8_t* destinationArray, int destinationIndex) {
    return DualSenseTriggerEffectGenerator::Simple_Weapon(destinationArray, destinationIndex, 0x00, 0x00, 0x00);
}

bool DualSenseTriggerEffectGenerator::ReWASD::Choppy(uint8_t* destinationArray, int destinationIndex) {
    destinationArray[destinationIndex + 0] = static_cast<uint8_t>(TriggerEffectType::Feedback);
    destinationArray[destinationIndex + 1] = 0x02;
    destinationArray[destinationIndex + 2] = 0x27;
    destinationArray[destinationIndex + 3] = 0x18;
    destinationArray[destinationIndex + 4] = 0x00;
    destinationArray[destinationIndex + 5] = 0x00;
    destinationArray[destinationIndex + 6] = 0x26;
    destinationArray[destinationIndex + 7] = 0x00;
    destinationArray[destinationIndex + 8] = 0x00;
    destinationArray[destinationIndex + 9] = 0x00;
    destinationArray[destinationIndex + 10] = 0x00;
    return true;
}

bool DualSenseTriggerEffectGenerator::ReWASD::SoftRigidity(uint8_t* destinationArray, int destinationIndex) {
    return DualSenseTriggerEffectGenerator::Simple_Feedback(destinationArray, destinationIndex, 0x00, 0x00);
}

bool DualSenseTriggerEffectGenerator::ReWASD::MediumRigidity(uint8_t* destinationArray, int destinationIndex) {
    return DualSenseTriggerEffectGenerator::Simple_Feedback(destinationArray, destinationIndex, 0x00, 0x64);
}

bool DualSenseTriggerEffectGenerator::ReWASD::MaxRigidity(uint8_t* destinationArray, int destinationIndex) {
    return DualSenseTriggerEffectGenerator::Simple_Feedback(destinationArray, destinationIndex, 0x00, 0xdc);
}

bool DualSenseTriggerEffectGenerator::ReWASD::HalfPress(uint8_t* destinationArray, int destinationIndex) {
    return DualSenseTriggerEffectGenerator::Simple_Feedback(destinationArray, destinationIndex, 0x55, 0x64);
}

bool DualSenseTriggerEffectGenerator::ReWASD::Rifle(uint8_t* destinationArray, int destinationIndex, uint8_t frequency) {
    if (frequency < 2)
        return false;
    if (frequency > 20)
        return false;

    destinationArray[destinationIndex + 0] = static_cast<uint8_t>(TriggerEffectType::Vibration);
    destinationArray[destinationIndex + 1] = 0x00;
    destinationArray[destinationIndex + 2] = 0x03;
    destinationArray[destinationIndex + 3] = 0x00;
    destinationArray[destinationIndex + 4] = 0x00;
    destinationArray[destinationIndex + 5] = 0x00;
    destinationArray[destinationIndex + 6] = 0x3F;
    destinationArray[destinationIndex + 7] = 0x00;
    destinationArray[destinationIndex + 8] = 0x00;
    destinationArray[destinationIndex + 9] = frequency;
    destinationArray[destinationIndex + 10] = 0x00;
    return true;
}

bool DualSenseTriggerEffectGenerator::ReWASD::Vibration(uint8_t* destinationArray, int destinationIndex, uint8_t strength, uint8_t frequency) {
    if (strength < 1)
        return false;
    if (frequency < 1)
        return false;

    destinationArray[destinationIndex + 0] = static_cast<uint8_t>(TriggerEffectType::Vibration);
    destinationArray[destinationIndex + 1] = 0x00;
    destinationArray[destinationIndex + 2] = 0x03;
    destinationArray[destinationIndex + 3] = 0x00;
    destinationArray[destinationIndex + 4] = 0x00;
    destinationArray[destinationIndex + 5] = 0x00;
    destinationArray[destinationIndex + 6] = strength;
    destinationArray[destinationIndex + 7] = 0x00;
    destinationArray[destinationIndex + 8] = 0x00;
    destinationArray[destinationIndex + 9] = frequency;
    destinationArray[destinationIndex + 10] = 0x00;
    return true;
}

} // namespace DualSense
} // namespace DataTools
} // namespace ExtendInput