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

#ifndef TRIGGER_EFFECT_GENERATOR_H
#define TRIGGER_EFFECT_GENERATOR_H

#include <cstdint>

namespace ExtendInput {
namespace DataTools {
namespace DualSense {

/// <summary>
/// Actual effect byte values sent to the controller. More complex effects may be build through the combination of these
/// values and specific paramaters.
/// </summary>
enum class TriggerEffectType : uint8_t {
    // Offically recognized modes
    // These are 100% safe and are the only effects that modify the trigger status nybble
    Off       = 0x05, // 00 00 0 101
    Feedback  = 0x21, // 00 10 0 001
    Weapon    = 0x25, // 00 10 0 101
    Vibration = 0x26, // 00 10 0 110

    // Unofficial but unique effects left in the firmware
    // These might be removed in the future
    Bow       = 0x22, // 00 10 0 010
    Galloping = 0x23, // 00 10 0 011
    Machine   = 0x27, // 00 10 0 111

    // Leftover versions of offical modes with simpler logic and no paramater protections
    // These should not be used
    Simple_Feedback  = 0x01, // 00 00 0 001
    Simple_Weapon    = 0x02, // 00 00 0 010
    Simple_Vibration = 0x06, // 00 00 0 110

    // Leftover versions of offical modes with limited paramater ranges
    // These should not be used
    Limited_Feedback = 0x11, // 00 01 0 001
    Limited_Weapon   = 0x12, // 00 01 0 010

    // Debug or Calibration functions
    // Don't use these as they will courrupt the trigger state until the reset button is pressed
    DebugFC = 0xFC, // 11 11 1 100
    DebugFD = 0xFD, // 11 11 1 101
    DebugFE = 0xFE, // 11 11 1 110
};

/**
 * Changelog
 * Revision 1: Initial
 * Revision 2: Added Apple approximated adapter factories. (This may not be correct, please test if you have access to Apple APIs.)
 *             Added Sony factories that use Sony's names.
 *             Added Raw factories for Resistance and AutomaticGun that give direct access to bit-packed region data.
 *             Added ReWASD factories that replicate reWASD effects, warts and all.
 *             Trigger enumerations now public and wrapper classes static.
 *             Minor documentation fixes.
 * Revision 3: Corrected Apple factories based on new capture log tests that show only simple rounding was needed.
 * Revision 4: Added 3 new Apple factories based on documentation and capture logs.
 *             These effects are not fully confirmed and are poorly documented even in Apple's docs.
 *             Two of these new effects are similar to our existing raw effect functions.
 * Revision 5: Reorganized and renamed functions and paramaters to be more inline with Sony's API.
 *             Information on the API was exposed by Apple and now further Steamworks version 1.55.
 *             Information is offically source from Apple documentation and Steamworks via logging
 *             HID writes to device based in inputs to new Steamworks functions. Interestingly, my
 *             Raw factories now have equivilents in Sony's offical API and will also be renamed.
 * Revision 6: Fixed MultiplePositionVibration not using frequency paramater.
 */

/// <summary>
/// DualSense controller trigger effect generators.
/// Revision: 6
/// 
/// If you are converting from offical Sony code you will need to convert your chosen effect enum to its chosen factory
/// function and your paramater struct to paramaters for that function. Please also note that you will need to track the
/// controller's currently set effect yourself. Note that all effect factories will return false and not modify the
/// destinationArray if invalid paramaters are used. If paramaters that would result in zero effect are used, the
/// Off effect is applied instead in line with Sony's offical behavior.
/// All Unofficial, simple, and limited effects are defined as close to the offical effect implementations as possible.
/// </summary>
class DualSenseTriggerEffectGenerator {
public:
    // Official Effects
    static bool Off(uint8_t* destinationArray, int destinationIndex);
    static bool Feedback(uint8_t* destinationArray, int destinationIndex, uint8_t position, uint8_t strength);
    static bool Weapon(uint8_t* destinationArray, int destinationIndex, uint8_t startPosition, uint8_t endPosition, uint8_t strength);
    static bool Vibration(uint8_t* destinationArray, int destinationIndex, uint8_t position, uint8_t amplitude, uint8_t frequency);
    static bool MultiplePositionFeedback(uint8_t* destinationArray, int destinationIndex, const uint8_t* strength);
    static bool SlopeFeedback(uint8_t* destinationArray, int destinationIndex, uint8_t startPosition, uint8_t endPosition, uint8_t startStrength, uint8_t endStrength);
    static bool MultiplePositionVibration(uint8_t* destinationArray, int destinationIndex, uint8_t frequency, const uint8_t* amplitude);

    // Unofficial but Unique Effects
    static bool Bow(uint8_t* destinationArray, int destinationIndex, uint8_t startPosition, uint8_t endPosition, uint8_t strength, uint8_t snapForce);
    static bool Galloping(uint8_t* destinationArray, int destinationIndex, uint8_t startPosition, uint8_t endPosition, uint8_t firstFoot, uint8_t secondFoot, uint8_t frequency);
    static bool Machine(uint8_t* destinationArray, int destinationIndex, uint8_t startPosition, uint8_t endPosition, uint8_t amplitudeA, uint8_t amplitudeB, uint8_t frequency, uint8_t period);

    // Simple Effects
    static bool Simple_Feedback(uint8_t* destinationArray, int destinationIndex, uint8_t position, uint8_t strength);
    static bool Simple_Weapon(uint8_t* destinationArray, int destinationIndex, uint8_t startPosition, uint8_t endPosition, uint8_t strength);
    static bool Simple_Vibration(uint8_t* destinationArray, int destinationIndex, uint8_t position, uint8_t amplitude, uint8_t frequency);

    // Limited Effects
    static bool Limited_Feedback(uint8_t* destinationArray, int destinationIndex, uint8_t position, uint8_t strength);
    static bool Limited_Weapon(uint8_t* destinationArray, int destinationIndex, uint8_t startPosition, uint8_t endPosition, uint8_t strength);

    // Apple Interface Adapters
    class Apple {
    public:
        static bool SetModeOff(uint8_t* destinationArray, int destinationIndex);
        static bool SetModeFeedbackWithStartPosition(uint8_t* destinationArray, int destinationIndex, float startPosition, float resistiveStrength);
        static bool SetModeWeaponWithStartPosition(uint8_t* destinationArray, int destinationIndex, float startPosition, float endPosition, float resistiveStrength);
        static bool SetModeVibrationWithStartPosition(uint8_t* destinationArray, int destinationIndex, float startPosition, float amplitude, float frequency);
        static bool SetModeFeedback(uint8_t* destinationArray, int destinationIndex, const float* positionalResistiveStrengths);
        static bool setModeSlopeFeedback(uint8_t* destinationArray, int destinationIndex, float startPosition, float endPosition, float startStrength, float endStrength);
        static bool setModeVibration(uint8_t* destinationArray, int destinationIndex, const float* positionalAmplitudes, float frequency);
    };

    // ReWASD Interface Adapters
    class ReWASD {
    public:
        static bool FullPress(uint8_t* destinationArray, int destinationIndex);
        static bool SoftPress(uint8_t* destinationArray, int destinationIndex);
        static bool MediumPress(uint8_t* destinationArray, int destinationIndex);
        static bool HardPress(uint8_t* destinationArray, int destinationIndex);
        static bool Pulse(uint8_t* destinationArray, int destinationIndex);
        static bool Choppy(uint8_t* destinationArray, int destinationIndex);
        static bool SoftRigidity(uint8_t* destinationArray, int destinationIndex);
        static bool MediumRigidity(uint8_t* destinationArray, int destinationIndex);
        static bool MaxRigidity(uint8_t* destinationArray, int destinationIndex);
        static bool HalfPress(uint8_t* destinationArray, int destinationIndex);
        static bool Rifle(uint8_t* destinationArray, int destinationIndex, uint8_t frequency = 10);
        static bool Vibration(uint8_t* destinationArray, int destinationIndex, uint8_t strength = 220, uint8_t frequency = 30);
    };
};

} // namespace DualSense
} // namespace DataTools
} // namespace ExtendInput

#endif // TRIGGER_EFFECT_GENERATOR_H