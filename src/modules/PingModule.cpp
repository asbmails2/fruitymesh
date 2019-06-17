////////////////////////////////////////////////////////////////////////////////
// /****************************************************************************
// **
// ** Copyright (C) 2015-2019 M-Way Solutions GmbH
// ** Contact: https://www.blureange.io/licensing
// **
// ** This file is part of the Bluerange/FruityMesh implementation
// **
// ** $BR_BEGIN_LICENSE:GPL-EXCEPT$
// ** Commercial License Usage
// ** Licensees holding valid commercial Bluerange licenses may use this file in
// ** accordance with the commercial license agreement provided with the
// ** Software or, alternatively, in accordance with the terms contained in
// ** a written agreement between them and M-Way Solutions GmbH. 
// ** For licensing terms and conditions see https://www.bluerange.io/terms-conditions. For further
// ** information use the contact form at https://www.bluerange.io/contact.
// **
// ** GNU General Public License Usage
// ** Alternatively, this file may be used under the terms of the GNU
// ** General Public License version 3 as published by the Free Software
// ** Foundation with exceptions as appearing in the file LICENSE.GPL3-EXCEPT
// ** included in the packaging of this file. Please review the following
// ** information to ensure the GNU General Public License requirements will
// ** be met: https://www.gnu.org/licenses/gpl-3.0.html.
// **
// ** $BR_END_LICENSE$
// **
// ****************************************************************************/
////////////////////////////////////////////////////////////////////////////////

#define PING_MODULE_CONFIG_VERSION 1

#include <PingModule.h>
#include <Logger.h>
#include <Utility.h>
#include <Node.h>
#include <stdlib.h>
#include <time.h> 
extern "C"{


}



PingModule::PingModule()
	: Module(moduleID::PING_MODULE_ID, "ping")
{
	moduleVersion = PING_MODULE_CONFIG_VERSION;

	//Register callbacks n' stuff

	//Save configuration to base class variables
	//sizeof configuration must be a multiple of 4 bytes
	configurationPointer = &configuration;
	configurationLength = sizeof(PingModuleConfiguration);

	//Set defaults
	ResetToDefaultConfiguration();
}

void PingModule::ResetToDefaultConfiguration()
{
	//Set default configuration values
	configuration.moduleId = moduleId;
	configuration.moduleActive = true;
	configuration.moduleVersion = PING_MODULE_CONFIG_VERSION;

	//Set additional config values...

}
void PingModule::ConfigurationLoadedHandler(ModuleConfiguration* migratableConfig, u16 migratableConfigLength)
{
	//Version migration can be added here, e.g. if module has version 2 and config is version 1
	if(migratableConfig->moduleVersion == 1){/* ... */};

	//Do additional initialization upon loading the config


	//Start the Module...

}

void PingModule::TimerEventHandler(u16 passedTimeDs)
{
	//Do stuff on timer...

}

#ifdef TERMINAL_ENABLED
bool PingModule::TerminalCommandHandler(char* commandArgs[], u8 commandArgsSize)
{
	//React on commands, return true if handled, false otherwise
	if(TERMARGS(0, "pm")){ 
		//Get the id of the target node
		NodeId targetNodeId = atoi(commandArgs[1]);
		int ping = 0;
		u32 delayMs = 0;
		if(atoi(commandArgs[1])==0)
		{		
		ping = 1000;
		delayMs = 50*100;}
		else
		{
		ping = atoi(commandArgs[2]);
		delayMs = atoi(commandArgs[3])*100;
		}

		//Some data
		u8 data[1];
		data[0] = 111;
		
		for(int i=0; i<ping;i++){
		u32 atraso = delayMs;
		logt("PINGMOD", "Trying to ping node %u - ping n = %d", targetNodeId,(i+1));
		data[0] = 0;
		StatusReporterModule* statusMod =(StatusReporterModule*)GS->node->GetModuleById(moduleID::STATUS_REPORTER_MODULE_ID);
		if(statusMod != nullptr){
		data[0] = statusMod->GetBatteryVoltage();
	}
		//Send ping packet to that node
		//to send to broadcast use id == 0
		SendModuleActionMessage(
				MESSAGE_TYPE_MODULE_TRIGGER_ACTION,
				targetNodeId,
				PingModuleTriggerActionMessages::MESSAGE_0,
				0,
				data,
				1,
				false,false
		  );
		  while(atraso != 0)
    {
    	atraso--;
        nrf_delay_us(999);
    } 	
		}
		return true;
	}

	//Must be called to allow the module to get and set the config
	return Module::TerminalCommandHandler(commandArgs, commandArgsSize);
}
#endif


void PingModule::MeshMessageReceivedHandler(BaseConnection* connection, BaseConnectionSendData* sendData, connPacketHeader* packetHeader)
{
	//Must call superclass for handling
	Module::MeshMessageReceivedHandler(connection, sendData, packetHeader);

	if(packetHeader->messageType == MESSAGE_TYPE_MODULE_TRIGGER_ACTION){
		connPacketModule* packet = (connPacketModule*)packetHeader;

		//Check if our module is meant and we should trigger an action
		if(packet->moduleId == moduleId){
			if(packet->actionType == PingModuleTriggerActionMessages::MESSAGE_0){
					//Inform the user
                   logt("PINGMOD", "Ping request received with data: %d", packet->data[0]);
					
                //TODO: Send ping response
			}
		}
	}

}
