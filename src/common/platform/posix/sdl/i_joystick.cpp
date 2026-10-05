/*
** i_joystick.cpp
**
**---------------------------------------------------------------------------
** Copyright 2005-2016 Randy Heit
** All rights reserved.
**
** Redistribution and use in source and binary forms, with or without
** modification, are permitted provided that the following conditions
** are met:
**
** 1. Redistributions of source code must retain the above copyright
**    notice, this list of conditions and the following disclaimer.
** 2. Redistributions in binary form must reproduce the above copyright
**    notice, this list of conditions and the following disclaimer in the
**    documentation and/or other materials provided with the distribution.
** 3. The name of the author may not be used to endorse or promote products
**    derived from this software without specific prior written permission.
**
** THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
** IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
** OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
** IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
** INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
** NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
** DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
** THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
** (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
** THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
**---------------------------------------------------------------------------
**
*/

#include <SDL2/SDL.h>
#include <cstdint>
#include <cstdlib>

#include "basics.h"
#include "cmdlib.h"

#include "m_joy.h"
#include "keydef.h"

#define DEFAULT_DEADZONE 0.25f;

// Very small deadzone so that floating point magic doesn't happen
#define MIN_DEADZONE 0.000001f

#ifdef __EMSCRIPTEN__
// Browsers report most gamepads in the Gamepad API's "standard" layout:
// axes 0-3 are the two sticks, buttons 0-15 are A B X Y, bumpers, triggers,
// back, start, stick clicks, then the d-pad. SDL doesn't pass the layout
// name on, so recognise it by shape. Such pads get the pad_* keys (and
// their default bindings) instead of the generic joyN ones, which assume a
// desktop driver's layout and would put pause and the main menu on the
// triggers.
bool I_IsStandardGamepad(SDL_Joystick *joy)
{
	return joy && SDL_JoystickNumAxes(joy) == 4 && SDL_JoystickNumButtons(joy) >= 16;
}

static const int StandardGamepadKeys[16] = {
	KEY_PAD_A, KEY_PAD_B, KEY_PAD_X, KEY_PAD_Y,
	KEY_PAD_LSHOULDER, KEY_PAD_RSHOULDER, KEY_PAD_LTRIGGER, KEY_PAD_RTRIGGER,
	KEY_PAD_BACK, KEY_PAD_START, KEY_PAD_LTHUMB, KEY_PAD_RTHUMB,
	KEY_PAD_DPAD_UP, KEY_PAD_DPAD_DOWN, KEY_PAD_DPAD_LEFT, KEY_PAD_DPAD_RIGHT,
};

int I_GamepadButtonKey(SDL_JoystickID which, int button)
{
	if (button < 16 && I_IsStandardGamepad(SDL_JoystickFromInstanceID(which)))
		return StandardGamepadKeys[button];
	return KEY_FIRSTJOYBUTTON + button;
}
#endif

class SDLInputJoystick: public IJoystickConfig
{
public:
	SDLInputJoystick(int DeviceIndex) : DeviceIndex(DeviceIndex), Multiplier(1.0f) , Enabled(true)
	{
		Device = SDL_JoystickOpen(DeviceIndex);
		if(Device != NULL)
		{
			NumAxes = SDL_JoystickNumAxes(Device);
			NumHats = SDL_JoystickNumHats(Device);
#ifdef __EMSCRIPTEN__
			Standard = I_IsStandardGamepad(Device);
#endif

			SetDefaultConfig();
		}
	}
	~SDLInputJoystick()
	{
		if(Device != NULL)
			M_SaveJoystickConfig(this);
		SDL_JoystickClose(Device);
	}

	bool IsValid() const
	{
		return Device != NULL;
	}

	FString GetName()
	{
		return SDL_JoystickName(Device);
	}
	float GetSensitivity()
	{
		return Multiplier;
	}
	void SetSensitivity(float scale)
	{
		Multiplier = scale;
	}

	int GetNumAxes()
	{
		return NumAxes + NumHats*2;
	}
	float GetAxisDeadZone(int axis)
	{
		return Axes[axis].DeadZone;
	}
	EJoyAxis GetAxisMap(int axis)
	{
		return Axes[axis].GameAxis;
	}
	const char *GetAxisName(int axis)
	{
		return Axes[axis].Name.GetChars();
	}
	float GetAxisScale(int axis)
	{
		return Axes[axis].Multiplier;
	}

	void SetAxisDeadZone(int axis, float zone)
	{
		Axes[axis].DeadZone = clamp(zone, MIN_DEADZONE, 1.f);
	}
	void SetAxisMap(int axis, EJoyAxis gameaxis)
	{
		Axes[axis].GameAxis = gameaxis;
	}
	void SetAxisScale(int axis, float scale)
	{
		Axes[axis].Multiplier = scale;
	}

	// Used by the saver to not save properties that are at their defaults.
	bool IsSensitivityDefault()
	{
		return Multiplier == 1.0f;
	}
	bool IsAxisDeadZoneDefault(int axis)
	{
		return Axes[axis].DeadZone <= MIN_DEADZONE;
	}
	bool IsAxisMapDefault(int axis)
	{
		if(axis >= 5)
			return Axes[axis].GameAxis == JOYAXIS_None;
		return Axes[axis].GameAxis == DefaultAxis(axis);
	}
	bool IsAxisScaleDefault(int axis)
	{
		return Axes[axis].Multiplier == 1.0f;
	}

	void SetDefaultConfig()
	{
		for(int i = 0;i < GetNumAxes();i++)
		{
			AxisInfo info;
			if(i < NumAxes)
				info.Name.Format("Axis %d", i+1);
			else
				info.Name.Format("Hat %d (%c)", (i-NumAxes)/2 + 1, (i-NumAxes)%2 == 0 ? 'x' : 'y');
			info.DeadZone = DEFAULT_DEADZONE;
			info.Multiplier = 1.0f;
			info.Value = 0.0;
			info.ButtonValue = 0;
			info.GameAxis = DefaultAxis(i);
			Axes.Push(info);
		}
	}

	bool GetEnabled()
	{
		return Enabled;
	}
	
	void SetEnabled(bool enabled)
	{
		Enabled = enabled;
	}

	bool AllowsEnabledInBackground() { return false; }
	bool GetEnabledInBackground() { return false; }
	void SetEnabledInBackground(bool enabled) {}

	FString GetIdentifier()
	{
		char id[16];
		snprintf(id, countof(id), "JS:%d", DeviceIndex);
		return id;
	}

	void AddAxes(float axes[NUM_JOYAXIS])
	{
		// Add to game axes.
		for (int i = 0; i < GetNumAxes(); ++i)
		{
			if(Axes[i].GameAxis != JOYAXIS_None)
				axes[Axes[i].GameAxis] -= float(Axes[i].Value * Multiplier * Axes[i].Multiplier);
		}
	}

	void ProcessInput()
	{
		uint8_t buttonstate;

		for (int i = 0; i < NumAxes; ++i)
		{
			buttonstate = 0;

			Axes[i].Value = SDL_JoystickGetAxis(Device, i)/32767.0;
			Axes[i].Value = Joy_RemoveDeadZone(Axes[i].Value, Axes[i].DeadZone, &buttonstate);

			// Map button to axis
			// X and Y are handled differently so if we have 2 or more axes then we'll use that code instead.
			if (!Standard && (NumAxes == 1 || (i >= 2 && i < NUM_JOYAXISBUTTONS)))
			{
				Joy_GenerateButtonEvents(Axes[i].ButtonValue, buttonstate, 2, KEY_JOYAXIS1PLUS + i*2);
				Axes[i].ButtonValue = buttonstate;
			}
		}

		if (Standard)
		{
			// Stick directions as the pad's thumbstick keys.
			buttonstate = Joy_XYAxesToButtons(Axes[0].Value, Axes[1].Value);
			Joy_GenerateButtonEvents(Axes[0].ButtonValue, buttonstate, 4, KEY_PAD_LTHUMB_RIGHT);
			Axes[0].ButtonValue = buttonstate;
			buttonstate = Joy_XYAxesToButtons(Axes[2].Value, Axes[3].Value);
			Joy_GenerateButtonEvents(Axes[2].ButtonValue, buttonstate, 4, KEY_PAD_RTHUMB_RIGHT);
			Axes[2].ButtonValue = buttonstate;
		}
		else if(NumAxes > 1)
		{
			buttonstate = Joy_XYAxesToButtons(Axes[0].Value, Axes[1].Value);
			Joy_GenerateButtonEvents(Axes[0].ButtonValue, buttonstate, 4, KEY_JOYAXIS1PLUS);
			Axes[0].ButtonValue = buttonstate;
		}

		// Map POV hats to buttons and axes.  Why axes?  Well apparently I have
		// a gamepad where the left control stick is a POV hat (instead of the
		// d-pad like you would expect, no that's pressure sensitive).  Also
		// KDE's joystick dialog maps them to axes as well.
		for (int i = 0; i < NumHats; ++i)
		{
			AxisInfo &x = Axes[NumAxes + i*2];
			AxisInfo &y = Axes[NumAxes + i*2 + 1];

			buttonstate = SDL_JoystickGetHat(Device, i);

			// If we're going to assume that we can pass SDL's value into
			// Joy_GenerateButtonEvents then we might as well assume the format here.
			if(buttonstate & 0x1) // Up
				y.Value = -1.0;
			else if(buttonstate & 0x4) // Down
				y.Value = 1.0;
			else
				y.Value = 0.0;
			if(buttonstate & 0x2) // Left
				x.Value = 1.0;
			else if(buttonstate & 0x8) // Right
				x.Value = -1.0;
			else
				x.Value = 0.0;

			if(i < 4)
			{
				Joy_GenerateButtonEvents(x.ButtonValue, buttonstate, 4, KEY_JOYPOV1_UP + i*4);
				x.ButtonValue = buttonstate;
			}
		}
	}

protected:
	struct AxisInfo
	{
		FString Name;
		float DeadZone;
		float Multiplier;
		EJoyAxis GameAxis;
		double Value;
		uint8_t ButtonValue;
	};
	static const EJoyAxis DefaultAxes[5];
	static const EJoyAxis StandardAxes[4];

	EJoyAxis DefaultAxis(int i) const
	{
		if (Standard)
			return i < 4 ? StandardAxes[i] : JOYAXIS_None;
		return i < 5 ? DefaultAxes[i] : JOYAXIS_None;
	}

	bool				Standard = false;	// Gamepad API "standard" layout

	int					DeviceIndex;
	SDL_Joystick		*Device;

	float				Multiplier;
	bool				Enabled;
	TArray<AxisInfo>	Axes;
	int					NumAxes;
	int					NumHats;

	friend class SDLInputJoystickManager;
};

// [Nash 4 Feb 2024] seems like on Linux, the third axis is actually the Left Trigger, resulting in the player uncontrollably looking upwards.
const EJoyAxis SDLInputJoystick::DefaultAxes[5] = {JOYAXIS_Side, JOYAXIS_Forward, JOYAXIS_None, JOYAXIS_Yaw, JOYAXIS_Pitch};
const EJoyAxis SDLInputJoystick::StandardAxes[4] = {JOYAXIS_Side, JOYAXIS_Forward, JOYAXIS_Yaw, JOYAXIS_Pitch};

class SDLInputJoystickManager
{
public:
	SDLInputJoystickManager()
	{
		for(int i = 0;i < SDL_NumJoysticks();i++)
		{
			SDLInputJoystick *device = new SDLInputJoystick(i);
			if(device->IsValid())
				Joysticks.Push(device);
			else
				delete device;
		}
	}
	~SDLInputJoystickManager()
	{
		for(unsigned int i = 0;i < Joysticks.Size();i++)
			delete Joysticks[i];
	}

	void AddAxes(float axes[NUM_JOYAXIS])
	{
		for(unsigned int i = 0;i < Joysticks.Size();i++)
			Joysticks[i]->AddAxes(axes);
	}
	void GetDevices(TArray<IJoystickConfig *> &sticks)
	{
		for(unsigned int i = 0;i < Joysticks.Size();i++)
		{
			M_LoadJoystickConfig(Joysticks[i]);
			sticks.Push(Joysticks[i]);
		}
	}

	void ProcessInput() const
	{
		for(unsigned int i = 0;i < Joysticks.Size();++i)
			if(Joysticks[i]->Enabled) Joysticks[i]->ProcessInput();
	}
protected:
	TArray<SDLInputJoystick *> Joysticks;
};
static SDLInputJoystickManager *JoystickManager;

void I_StartupJoysticks()
{
#ifndef NO_SDL_JOYSTICK
	if(SDL_InitSubSystem(SDL_INIT_JOYSTICK) >= 0)
		JoystickManager = new SDLInputJoystickManager();
#endif
}

#ifdef __EMSCRIPTEN__
// Browsers reveal a gamepad only once a button is pressed on the focused
// page, long after startup, so rebuild the list when SDL reports a change.
void I_RescanJoysticks()
{
	if (!JoystickManager)
		return;
	delete JoystickManager;
	JoystickManager = new SDLInputJoystickManager();
}
#endif
void I_ShutdownInput()
{
	if(JoystickManager)
	{
		delete JoystickManager;
		SDL_QuitSubSystem(SDL_INIT_JOYSTICK);
	}
}

void I_GetJoysticks(TArray<IJoystickConfig *> &sticks)
{
	sticks.Clear();

	if (JoystickManager)
		JoystickManager->GetDevices(sticks);
}

void I_GetAxes(float axes[NUM_JOYAXIS])
{
	for (int i = 0; i < NUM_JOYAXIS; ++i)
	{
		axes[i] = 0;
	}
	if (use_joystick && JoystickManager)
	{
		JoystickManager->AddAxes(axes);
	}
}

void I_ProcessJoysticks()
{
	if (use_joystick && JoystickManager)
		JoystickManager->ProcessInput();
}

IJoystickConfig *I_UpdateDeviceList()
{
	return NULL;
}
