#pragma once

// Switches the plug off when the game exits (destructor of a static object).
class EndGameDetect
{
public:
	EndGameDetect();
	virtual ~EndGameDetect();
};
