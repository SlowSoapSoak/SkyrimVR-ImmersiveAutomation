#include "EndGameDetect.h"


EndGameDetect::EndGameDetect() : tpLinkConn(false)
{

}


EndGameDetect::~EndGameDetect()
{
	ImmersiveWinds::_currentSwitchState = 0;
	ImmersiveWinds::_keepRunning = false;
	//ImmersiveWinds::t->join();
	int udp = -1;
	std::string ip;
	tpLinkConn.LoadIPConfigFromFile(udp, ip);
	tpLinkConn.SwitchRelayState(0);
}
