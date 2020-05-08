#include "EndGameDetect.h"
#include "ImmersiveSun.h"


EndGameDetect::EndGameDetect()
{

}


EndGameDetect::~EndGameDetect()
{
	ImmersiveSun::currentSwitchState1 = 0;
	ImmersiveSun::currentSwitchState2 = 0;
	ImmersiveSun::keepRunning = false;
	//ImmersiveWinds::t->join();
	tpLinkConn.LoadIPConfigFromFile("sTpLinkPlugIp1");
	tpLinkConn.SwitchRelayState(0);

	tpLinkConn.LoadIPConfigFromFile("sTpLinkPlugIp2");
	tpLinkConn.SwitchRelayState(0);
}
