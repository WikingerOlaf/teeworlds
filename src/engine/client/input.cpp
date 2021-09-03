/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include "SDL.h"

#include <base/system.h>
#include <engine/shared/config.h>
#include <engine/console.h>
#include <engine/graphics.h>
#include <engine/input.h>
#include <engine/keys.h>

#include "input.h"


// this header is protected so you don't include it from anywere
#define KEYS_INCLUDE
#include "keynames.h"
#undef KEYS_INCLUDE

// support older SDL version (pre 2.0.6)
#ifndef SDL_JOYSTICK_AXIS_MIN
	#define SDL_JOYSTICK_AXIS_MIN -32768
#endif
#ifndef SDL_JOYSTICK_AXIS_MAX
	#define SDL_JOYSTICK_AXIS_MAX 32767
#endif

void CInput::AddEvent(char *pText, int Key, int Flags)
{
	if(m_NumEvents != INPUT_BUFFER_SIZE)
	{
		m_aInputEvents[m_NumEvents].m_Key = Key;
		m_aInputEvents[m_NumEvents].m_Flags = Flags;
		if(!pText)
			m_aInputEvents[m_NumEvents].m_aText[0] = 0;
		else
			str_copy(m_aInputEvents[m_NumEvents].m_aText, pText, sizeof(m_aInputEvents[m_NumEvents].m_aText));
		m_aInputEvents[m_NumEvents].m_InputCount = m_InputCounter;
		m_NumEvents++;
	}
}

CInput::CInput()
{
	mem_zero(m_aInputCount, sizeof(m_aInputCount));
	mem_zero(m_aInputState, sizeof(m_aInputState));

	m_pConfig = 0;
	m_pConsole = 0;
	m_pGraphics = 0;

	m_InputCounter = 1;
	m_MouseInputRelative = false;
	m_pClipboardText = 0;

	m_pActiveJoystick = 0x0;

	m_PreviousHat = 0;

	m_MouseDoubleClick = false;

	m_NumEvents = 0;
}

CInput::~CInput()
{
	if(m_pClipboardText)
		SDL_free(m_pClipboardText);
	CloseJoysticks();
}

void CInput::Init()
{
	m_pGraphics = Kernel()->RequestInterface<IEngineGraphics>();
	m_pConfig = Kernel()->RequestInterface<IConfigManager>()->Values();
	m_pConsole = Kernel()->RequestInterface<IConsole>();
	// FIXME: unicode handling: use SDL_StartTextInput/SDL_StopTextInput on inputs

	MouseModeRelative();

	InitJoysticks();
}

void CInput::InitJoysticks()
{
	if(!SDL_WasInit(SDL_INIT_JOYSTICK))
	{
		if(SDL_InitSubSystem(SDL_INIT_JOYSTICK) < 0)
		{
			dbg_msg("joystick", "Unable to init SDL joystick system: %s", SDL_GetError());
			return;
		}
	}

	int NumJoysticks = SDL_NumJoysticks();
	if(NumJoysticks > 0)
	{
		dbg_msg("joystick", "%d joystick(s) found", NumJoysticks);
		int ActualIndex = 0;
		for(int i = 0; i < NumJoysticks; i++)
		{
			SDL_Joystick *pJoystick = SDL_JoystickOpen(i);
			if(!pJoystick)
			{
				dbg_msg("joystick", "Could not open joystick %d: '%s'", i, SDL_GetError());
				continue;
			}
			CJoystick Joystick(this, ActualIndex, pJoystick);
			m_aJoysticks.add(Joystick);
			dbg_msg("joystick", "Opened Joystick %d '%s' (%d axes, %d buttons, %d balls, %d hats)", i, Joystick.GetName(),
					Joystick.GetNumAxes(), Joystick.GetNumButtons(), Joystick.GetNumBalls(), Joystick.GetNumHats());
			ActualIndex++;
		}
		if(ActualIndex > 0)
		{
			UpdateActiveJoystick();
			Console()->Chain("joystick_guid", ConchainJoystickGuidChanged, this);
		}
	}
	else
	{
		dbg_msg("joystick", "No joysticks found");
	}
}

void CInput::UpdateActiveJoystick()
{
	if(m_aJoysticks.size() == 0)
		return;
	m_pActiveJoystick = 0x0;
	for(array<CJoystick>::range r = m_aJoysticks.all(); !r.empty(); r.pop_front())
	{
		CJoystick &Joystick = r.front();
		if(str_comp(Joystick.GetGUID(), Config()->m_JoystickGUID) == 0)
		{
			m_pActiveJoystick = &Joystick;
			return;
		}
	}
	// Fall back to first joystick if no match was found
	if(!m_pActiveJoystick)
		m_pActiveJoystick = &m_aJoysticks[0];
}

void CInput::ConchainJoystickGuidChanged(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData)
{
	pfnCallback(pResult, pCallbackUserData);
	static_cast<CInput *>(pUserData)->UpdateActiveJoystick();
}

CInput::CJoystick::CJoystick(CInput *pInput, int Index, SDL_Joystick *pDelegate)
{
	m_pInput = pInput;
	m_Index = Index;
	m_pDelegate = pDelegate;
	m_NumAxes = SDL_JoystickNumAxes(pDelegate);
	m_NumButtons = SDL_JoystickNumButtons(pDelegate);
	m_NumBalls = SDL_JoystickNumBalls(pDelegate);
	m_NumHats = SDL_JoystickNumHats(pDelegate);
	str_copy(m_aName, SDL_JoystickName(pDelegate), sizeof(m_aName));
	SDL_JoystickGetGUIDString(SDL_JoystickGetGUID(pDelegate), m_aGUID, sizeof(m_aGUID));
}

void CInput::CloseJoysticks()
{
	for(array<CJoystick>::range r = m_aJoysticks.all(); !r.empty(); r.pop_front())
		r.front().Close();
	m_aJoysticks.clear();
	m_pActiveJoystick = 0x0;
}

void CInput::CJoystick::Close()
{
	if(SDL_JoystickGetAttached(m_pDelegate))
		SDL_JoystickClose(m_pDelegate);
}

void CInput::SelectNextJoystick()
{
	const int Num = m_aJoysticks.size();
	if(Num > 1)
	{
		m_pActiveJoystick = &m_aJoysticks[(m_pActiveJoystick->GetIndex() + 1) % Num];
		str_copy(Config()->m_JoystickGUID, m_pActiveJoystick->GetGUID(), sizeof(Config()->m_JoystickGUID));
	}
}

float CInput::CJoystick::GetAxisValue(int Axis)
{
	return (SDL_JoystickGetAxis(m_pDelegate, Axis) - SDL_JOYSTICK_AXIS_MIN) / float(SDL_JOYSTICK_AXIS_MAX - SDL_JOYSTICK_AXIS_MIN) * 2.0f - 1.0f;
}

bool CInput::CJoystick::Relative(float *pX, float *pY)
{
	if(!Input()->Config()->m_JoystickEnable)
		return false;

	const vec2 RawJoystickPos = vec2(GetAxisValue(Input()->Config()->m_JoystickX), GetAxisValue(Input()->Config()->m_JoystickY));
	const float Len = length(RawJoystickPos);
	const float DeadZone = Input()->Config()->m_JoystickTolerance/50.0f;
	if(Len > DeadZone)
	{
		const float Factor = 0.1f * maximum((Len - DeadZone) / (1 - DeadZone), 0.001f) / Len;
		*pX = RawJoystickPos.x * Factor;
		*pY = RawJoystickPos.y * Factor;
		return true;
	}
	return false;
}

bool CInput::CJoystick::Absolute(float *pX, float *pY)
{
	if(!Input()->m_MouseInputRelative || !Input()->Config()->m_JoystickEnable)
		return false;

	const vec2 RawJoystickPos = vec2(GetAxisValue(Input()->Config()->m_JoystickX), GetAxisValue(Input()->Config()->m_JoystickY));
	const float DeadZone = Input()->Config()->m_JoystickTolerance/50.0f;
	if(dot(RawJoystickPos, RawJoystickPos) > DeadZone*DeadZone)
	{
		*pX = RawJoystickPos.x;
		*pY = RawJoystickPos.y;
		return true;
	}
	return false;
}

bool CInput::MouseRelative(float *pX, float *pY)
{
	if(!m_MouseInputRelative)
		return false;

	int MouseX = 0, MouseY = 0;
	SDL_GetRelativeMouseState(&MouseX, &MouseY);
	if(MouseX || MouseY)
	{
		*pX = MouseX;
		*pY = MouseY;
		return true;
	}
	return false;
}

void CInput::MouseModeAbsolute()
{
	if(m_MouseInputRelative)
	{
		m_MouseInputRelative = false;
		SDL_ShowCursor(SDL_ENABLE);
		SDL_SetRelativeMouseMode(SDL_FALSE);
	}
}

void CInput::MouseModeRelative()
{
	if(!m_MouseInputRelative)
	{
		m_MouseInputRelative = true;
		SDL_ShowCursor(SDL_DISABLE);
		if(SDL_SetHintWithPriority(SDL_HINT_MOUSE_RELATIVE_MODE_WARP, Config()->m_InpGrab ? "0" : "1", SDL_HINT_OVERRIDE) == SDL_FALSE)
		{
			Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "input", "unable to switch relative mouse mode");
		}
		SDL_SetRelativeMouseMode(SDL_TRUE);
		SDL_GetRelativeMouseState(NULL, NULL);
	}
}

bool CInput::MouseDoubleClick()
{
	if(m_MouseDoubleClick)
	{
		m_MouseDoubleClick = false;
		return true;
	}
	return false;
}

const char *CInput::GetClipboardText()
{
	if(m_pClipboardText)
		SDL_free(m_pClipboardText);
	m_pClipboardText = SDL_GetClipboardText();
	if(m_pClipboardText)
		str_sanitize_cc(m_pClipboardText);
	return m_pClipboardText;
}

void CInput::SetClipboardText(const char *pText)
{
	SDL_SetClipboardText(pText);
}

void CInput::Clear()
{
	mem_zero(m_aInputState, sizeof(m_aInputState));
	mem_zero(m_aInputCount, sizeof(m_aInputCount));
	m_NumEvents = 0;
}

bool CInput::KeyState(int Key) const
{
	return Key >= KEY_FIRST
		&& Key < KEY_LAST
		&& m_aInputState[Key>=KEY_MOUSE_1 ? Key : SDL_GetScancodeFromKey(KeyToKeycode(Key))];
}

int CInput::Update()
{
	// keep the counter between 1..0xFFFF, 0 means not pressed
	m_InputCounter = (m_InputCounter%0xFFFF)+1;

	int NumKeyStates;
	const Uint8 *pState = SDL_GetKeyboardState(&NumKeyStates);
	if(NumKeyStates >= KEY_MOUSE_1)
		NumKeyStates = KEY_MOUSE_1;
	mem_copy(m_aInputState, pState, NumKeyStates);
	mem_zero(m_aInputState+NumKeyStates, KEY_LAST-NumKeyStates);

	// these states must always be updated manually because they are not in the SDL_GetKeyboardState from SDL
	int MouseState = SDL_GetMouseState(NULL, NULL);
	if(MouseState&SDL_BUTTON(SDL_BUTTON_LEFT)) m_aInputState[KEY_MOUSE_1] = 1;
	if(MouseState&SDL_BUTTON(SDL_BUTTON_RIGHT)) m_aInputState[KEY_MOUSE_2] = 1;
	if(MouseState&SDL_BUTTON(SDL_BUTTON_MIDDLE)) m_aInputState[KEY_MOUSE_3] = 1;
	if(MouseState&SDL_BUTTON(SDL_BUTTON_X1)) m_aInputState[KEY_MOUSE_4] = 1;
	if(MouseState&SDL_BUTTON(SDL_BUTTON_X2)) m_aInputState[KEY_MOUSE_5] = 1;
	if(MouseState&SDL_BUTTON(6)) m_aInputState[KEY_MOUSE_6] = 1;
	if(MouseState&SDL_BUTTON(7)) m_aInputState[KEY_MOUSE_7] = 1;
	if(MouseState&SDL_BUTTON(8)) m_aInputState[KEY_MOUSE_8] = 1;
	if(MouseState&SDL_BUTTON(9)) m_aInputState[KEY_MOUSE_9] = 1;

	SDL_Event Event;
	while(SDL_PollEvent(&Event))
	{
		int Key = -1;
		int Scancode = -1;
		int Action = IInput::FLAG_PRESS;
		switch(Event.type)
		{
			case SDL_TEXTINPUT:
				AddEvent(Event.text.text, 0, IInput::FLAG_TEXT);
				break;

			// handle keys
			case SDL_KEYUP:
				Action = IInput::FLAG_RELEASE;

				// fall through
			case SDL_KEYDOWN:
				Key = KeycodeToKey(Event.key.keysym.sym);
				Scancode = Event.key.keysym.scancode;
				break;

			// handle the joystick events
			case SDL_JOYBUTTONUP:
				if(Event.jbutton.button >= NUM_JOYSTICK_BUTTONS)
					break;
				Action = IInput::FLAG_RELEASE;

				// fall through
			case SDL_JOYBUTTONDOWN:
				if(Event.jbutton.button >= NUM_JOYSTICK_BUTTONS)
					break;
				Key = Event.jbutton.button + KEY_JOYSTICK_BUTTON_0;
				Scancode = Key;
				break;

			case SDL_JOYHATMOTION:
				switch(Event.jhat.value) {
					case SDL_HAT_LEFTUP:
						Key = KEY_JOY_HAT_LEFTUP;
						Scancode = Key;
						m_PreviousHat = Key;
						break;
					case SDL_HAT_UP:
						Key = KEY_JOY_HAT_UP;
						Scancode = Key;
						m_PreviousHat = Key;
						break;
					case SDL_HAT_RIGHTUP:
						Key = KEY_JOY_HAT_RIGHTUP;
						Scancode = Key;
						m_PreviousHat = Key;
						break;
					case SDL_HAT_LEFT:
						Key = KEY_JOY_HAT_LEFT;
						Scancode = Key;
						m_PreviousHat = Key;
						break;
					case SDL_HAT_CENTERED:
						Action = IInput::FLAG_RELEASE;
						Key = m_PreviousHat;
						Scancode = m_PreviousHat;
						m_PreviousHat = 0;
						break;
					case SDL_HAT_RIGHT:
						Key = KEY_JOY_HAT_RIGHT;
						Scancode = Key;
						m_PreviousHat = Key;
						break;
					case SDL_HAT_LEFTDOWN:
						Key = KEY_JOY_HAT_LEFTDOWN;
						Scancode = Key;
						m_PreviousHat = Key;
						break;
					case SDL_HAT_DOWN:
						Key = KEY_JOY_HAT_DOWN;
						Scancode = Key;
						m_PreviousHat = Key;
						break;
					case SDL_HAT_RIGHTDOWN:
						Key = KEY_JOY_HAT_RIGHTDOWN;
						Scancode = Key;
						m_PreviousHat = Key;
						break;
				}
				break;

			// handle mouse buttons
			case SDL_MOUSEBUTTONUP:
				Action = IInput::FLAG_RELEASE;

				// fall through
			case SDL_MOUSEBUTTONDOWN:
				if(Event.button.button == SDL_BUTTON_LEFT) // ignore_convention
				{
					Key = KEY_MOUSE_1;
					if(Event.button.clicks%2 == 0)
						m_MouseDoubleClick = true;
					else if(Event.button.clicks == 1)
						m_MouseDoubleClick = false;
				}
				else if(Event.button.button == SDL_BUTTON_RIGHT) Key = KEY_MOUSE_2; // ignore_convention
				else if(Event.button.button == SDL_BUTTON_MIDDLE) Key = KEY_MOUSE_3; // ignore_convention
				else if(Event.button.button == SDL_BUTTON_X1) Key = KEY_MOUSE_4; // ignore_convention
				else if(Event.button.button == SDL_BUTTON_X2) Key = KEY_MOUSE_5; // ignore_convention
				else if(Event.button.button == 6) Key = KEY_MOUSE_6; // ignore_convention
				else if(Event.button.button == 7) Key = KEY_MOUSE_7; // ignore_convention
				else if(Event.button.button == 8) Key = KEY_MOUSE_8; // ignore_convention
				else if(Event.button.button == 9) Key = KEY_MOUSE_9; // ignore_convention
				Scancode = Key;
				break;

			case SDL_MOUSEWHEEL:
				if(Event.wheel.y > 0) Key = KEY_MOUSE_WHEEL_UP; // ignore_convention
				else if(Event.wheel.y < 0) Key = KEY_MOUSE_WHEEL_DOWN; // ignore_convention
				else break;
				Action |= IInput::FLAG_RELEASE;
				Scancode = Key;
				break;

#if defined(CONF_PLATFORM_MACOSX)	// Todo SDL: remove this when fixed (mouse state is faulty on start)
			case SDL_WINDOWEVENT:
				if(Event.window.event == SDL_WINDOWEVENT_MAXIMIZED)
				{
					MouseModeAbsolute();
					MouseModeRelative();
				}
				break;
#endif

			// other messages
			case SDL_QUIT:
				return 1;
		}

		if(Key >= 0)
		{
			if(Action&IInput::FLAG_PRESS && Key < g_MaxKeys && Scancode >= 0 && Scancode < g_MaxKeys)
			{
				m_aInputState[Scancode] = 1;
				m_aInputCount[Key] = m_InputCounter;
			}
			AddEvent(0, Key, Action);
		}
	}

	return 0;
}


IEngineInput *CreateEngineInput() { return new CInput; }
