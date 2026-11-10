#pragma once

// ===========================================================================
//  E-STOP OVERLAY
//
//  Full-screen panel shown on ANY screen while the rig reports its e-stop
//  switch open (telemetry es=1). It lives on lv_layer_top(), above every
//  screen and above w_modal() dialogs, and cannot be dismissed while the
//  switch is still pressed.
//
//  Once the switch is released it offers:
//    Reset           -> 'Z'. The rig retracts 5 mm first if Settings >
//                       "Retract after E-stop reset" is on (sent as 'E1').
//    Not now         -> closes the panel; the rig stays faulted, so Start
//                       buttons keep refusing until it is reset.
//
//  The panel closes by itself when the rig leaves the fault state (reset
//  from here, from another dialog, or from the rig's USB console), or when
//  the link drops.
// ===========================================================================

// Call from the global UI tick (ui.cpp), every tick.
void estop_overlay_update();

// True while the panel is on screen.
bool estop_overlay_is_open();
