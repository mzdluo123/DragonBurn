#include "RCS.h"

void RCS::UpdateAngles(const CEntity& Local, Vec2& Angles)
{
	Angles = Local.Pawn.ShotsFired == 0 ? Vec2{} : Local.Pawn.AimPunchAngle;
}

void RCS::RecoilControl(CEntity LocalPlayer)
{
	if (!LegitBotConfig::RCS)
		return;

	static Vec2 OldPunch = { 0, 0 };

	if (LocalPlayer.Pawn.ShotsFired > RCSBullet)
	{
		Vec2 viewAngles = LocalPlayer.Pawn.ViewAngle;
		Vec2 delta = viewAngles - (viewAngles + (OldPunch - (LocalPlayer.Pawn.AimPunchAngle * 2.f)));
		int MouseX = static_cast<int>(std::round((delta.y * RCSScale.x / LocalPlayer.Client.Sensitivity) / 0.011f));
		int MouseY = static_cast<int>(std::round((delta.x * RCSScale.y / LocalPlayer.Client.Sensitivity) / 0.011f));

		if ((GetAsyncKeyState(VK_LBUTTON) & 0x8000))
		{
			mouse_event(MOUSEEVENTF_MOVE, MouseX, -MouseY, NULL, NULL);
			OldPunch = LocalPlayer.Pawn.AimPunchAngle * 2.0f;
		}
	}
	else
		OldPunch = Vec2{ 0,0 };
}