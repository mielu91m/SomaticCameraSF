Somatic Camera SF"  — Beta

**A first-person camera overhaul for Starfield that lets you see your full character body in first-person view.**


New Beta 7:


Added support for Osf UI (by ozooma10)

You can now change pseudo-camera settings in-game, such as:
- iToggleKey (currently has an issue with Numpad)
- fFOV
- bAutoEquipHideHeadSS
- bAutoEquipHideHead
- fSideOffset
- fForwardOffset
- fUpOffset
Fixed various bugs

(Beta 6 V4)
Updated the code so that setting bAutoEquipHideHeadSS=0 prevents the pseudo-camera from unequipping your currently fitted helmet.
Added auto-re-equipping of your previously equipped helmet upon disabling the pseudo-camera.
When activating the pseudo-camera, an invisible headgear and invisible helmet are now added to your inventory.
Added settings in the SomaticCameraSF.ini file to auto-equip the invisible head items (enabled by default).
There is now just one main hotkey to toggle the pseudo-camera on and off— Num / by default (you can change this to whatever you want in the ImprovedCameraSF.ini file).
During animations like SnuSnuField, the pseudo-camera now tracks the head position but isn't locked, meaning you can still move the camera around.
Fixed several minor bugs and issues.

-Three functions for controlling the pseudo-camera position have been added to the 
SomaticCameraSF.ini file:
fUpOffset – camera height/vertical position
fForwardOffset – moves the camera forward/backward
fSideOffset – centers the camera if it drifts away from the middle
And For SAF:
fSAFOffsetForward – centers the camera if it drifts away from the middle
fSAFOffsetSide – moves the camera forward/backward
fSAFOffsetUp – camera height/vertical position


The provided values are my personal preferences and may or may not suit your taste, but you can always adjust them to your liking.
I haven't found an effective way to hide the head yet.

- Added invisible headgear thanks to HooliG4N83 


To Do:
- Work on the seat-getting-up animation for the pseudo-camera 
(keeping actual ship flight control native for now, as the pseudo-camera acts up during flight).
- Addressing other known bugs.



Note: This is still a beta release with a few things left to iron out, but we're getting closer to a truly unique version!

## Overview

Somatic Camera SF brings back the classic third-person-to-first-person hybrid camera — also known as **pseudo-FPP** or **first-person with visible body**. Instead of the game's standard first-person mode (which hides your character), this mod places the camera at your character's head while keeping the full body rendered, animated, and visible. The result is an immersive first-person view where you can see your own body, gear, and shadows.

Toggle between normal third-person and the enhanced first-person view with a hotkey.

## Features

- **Full body in first person** — See your character's body, equipment, and armor while in first-person view
- **Head tracking** — Camera follows the head bone during standard animations (walking, running, sprinting, jumping, aiming)
- **Player rotation on look** — Your character model rotates naturally when you look left or right in pseudo-FPP mode
- **Toggle on/off** — Default: Numpad /   (can be changed in the .ini file.) 
- **Compatible with third-person animations** — Body animations play normally while in pseudo-FPP mode
- **INI files** — for manual tuning of the pseudo-camera.

## Requirements

- **Starfield** version 1.16.244 (or compatible)
- **SFSE** (Starfield Script Extender)
- **Address Library** (for SFSE)

## Installation
Mod Manager or manually:
1. Install SFSE and Address Library if you haven't already
2. Extract the archive
3. Copy 'SomaticCameraSF.dll`and `SomaticCameraSF.ini` to `YourGameFolder\Data\SFSE\Plugins\`
4. Launch the game through SFSE

## Usage

The toggle hotkey is configurable. By default:
- **Press the toggle key** while in third-person to activate pseudo-FPP mode
- **Press again** to return to normal third-person


 ⚠ This is a **beta release**. The mod is functional but not yet polished. Expect imperfections.



## Technical Notes

Built with CommonLibSF and MinHook. The mod hooks into `ThirdPersonState::Update` and `TESCamera::Update` to override the camera position without modifying game files.

## Credits
- HooliG4N83 - for invisible headwear
- Libxe dla CommonLibSF, without which creating this type of plugin for Starfield would have been much more difficult. This file uses CommonLibSF, which is licensed under the GNU GPLv3 .
- Tsuda Kageyu for MinHook
- meh321﻿ for Address Library for SFSE Plugins.
- Ian Patterson﻿ for Starfield Script Extender.
- Inspired by Improved Camera for Skyrim - ArranzCNL and TwistedModding
