#include "EndGameDetect.h"


EndGameDetect::EndGameDetect() : tpLinkConn(false)
{

}


EndGameDetect::~EndGameDetect()
{
	ImmersiveWinds::_currentSwitchState = 0;
	ImmersiveWinds::_keepRunning = false;
	//ImmersiveWinds::t->join();
	tpLinkConn.LoadIPConfigFromFile();
	tpLinkConn.SwitchRelayState(0);
}
