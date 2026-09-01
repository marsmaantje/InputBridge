#pragma once

// Modal "Pair Wiimote" dialog - lets the user scan for and pair a Wiimote
// without leaving InputBridge. See src/Bluetooth/WiimoteBluetoothPairing.h
// for what's actually happening underneath; this class is just the ImGui
// front end for it.
class WiimotePairingWindow {
public:
    // Call once per frame unconditionally - a no-op until Open() has been
    // called. Must keep being called every frame while the dialog is open
    // (not just while some other condition holds) so the underlying
    // WiimotePairing::Pump() keeps draining - see that method's comment.
    static void Draw();

    // Opens the dialog and starts a scan immediately.
    static void Open();
};
