#include "EndGameDetect.h"
#include "ImmersiveWinds.h"


EndGameDetect::EndGameDetect()
{
}


EndGameDetect::~EndGameDetect()
{
	ImmersiveWinds::SwitchOffOnExit();
}
