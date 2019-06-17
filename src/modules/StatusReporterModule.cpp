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


#define STATUS_REPORTER_MODULE_CONFIG_VERSION 1

#include <StatusReporterModule.h>

#ifdef ACTIVATE_STATUS_REPORTER_MODULE

#include <Logger.h>
#include <Utility.h>
#include <Node.h>
#include <Config.h>
extern "C"{
#include <app_error.h>
#include <stdlib.h>
#if defined(NRF51)
#include <nrf_drv_config.h>
#endif
#ifndef SIM_ENABLED
#include <nrf_gpio.h>
#endif

}


StatusReporterModule::StatusReporterModule()
	: Module(moduleID::STATUS_REPORTER_MODULE_ID, "status")
{
	moduleVersion = STATUS_REPORTER_MODULE_CONFIG_VERSION;
	isADCInitialized = false;
	this->batteryVoltageDv = 0;
	number_of_adc_channels = 0;
	//Register callbacks n' stuff
	//Save configuration to base class variables
	//sizeof configuration must be a multiple of 4 bytes
	configurationPointer = &configuration;
	configurationLength = sizeof(StatusReporterModuleConfiguration);

	//Set defaults
	ResetToDefaultConfiguration();
}

void StatusReporterModule::ResetToDefaultConfiguration()
{
	//Set default configuration values
	configuration.moduleId = moduleId;
	configuration.moduleActive = true;
	configuration.moduleVersion = STATUS_REPORTER_MODULE_CONFIG_VERSION;
	configuration.batteryMeasurementIntervalDs = SEC_TO_DS(60);
	configuration.statusReportingIntervalDs = 0;
	configuration.connectionReportingIntervalDs = 0;
	configuration.connectionRSSISamplingMode = RSSISamplingModes::HIGH;
	configuration.advertisingRSSISamplingMode = RSSISamplingModes::HIGH;
	configuration.nearbyReportingIntervalDs = 0;
	configuration.deviceInfoReportingIntervalDs = 0;
	configuration.liveReportingState = LiveReportTypes::LEVEL_DEBUG;

	memset(nodeMeasurements, 0x00, sizeof(nodeMeasurements));

	SET_FEATURESET_CONFIGURATION(&configuration);
}

void StatusReporterModule::ConfigurationLoadedHandler(ModuleConfiguration* migratableConfig, u16 migratableConfigLength)
{
	//Start the Module...

}

void StatusReporterModule::TimerEventHandler(u16 passedTimeDs)
{
	//Device Info
	if(SHOULD_IV_TRIGGER(GS->appTimerDs+GS->appTimerRandomOffsetDs, passedTimeDs, configuration.deviceInfoReportingIntervalDs)){
		SendDeviceInfo(NODE_ID_BROADCAST, 0, MESSAGE_TYPE_MODULE_ACTION_RESPONSE);
	}
	//Status
	if(SHOULD_IV_TRIGGER(GS->appTimerDs+GS->appTimerRandomOffsetDs, passedTimeDs, configuration.statusReportingIntervalDs)){
		SendStatus(NODE_ID_BROADCAST, MESSAGE_TYPE_MODULE_ACTION_RESPONSE);
	}
	//Connections
	if(SHOULD_IV_TRIGGER(GS->appTimerDs+GS->appTimerRandomOffsetDs, passedTimeDs, configuration.connectionReportingIntervalDs)){
		SendAllConnections(NODE_ID_BROADCAST, MESSAGE_TYPE_MODULE_GENERAL);
	}
	//Nearby Nodes
	if(SHOULD_IV_TRIGGER(GS->appTimerDs+GS->appTimerRandomOffsetDs, passedTimeDs, configuration.nearbyReportingIntervalDs)){
		SendNearbyNodes(NODE_ID_BROADCAST, MESSAGE_TYPE_MODULE_ACTION_RESPONSE);
	}
	//BatteryMeasurement (measure short after reset and then priodically)
	if( (GS->appTimerDs > SEC_TO_DS(100) && !isADCInitialized)
		|| SHOULD_IV_TRIGGER(GS->appTimerDs, passedTimeDs, configuration.batteryMeasurementIntervalDs)){
		BatteryVoltageADC();
	}
//	//ErrorLog
//	if(SHOULD_IV_TRIGGER(node->appTimerDs+node->appTimerRandomOffsetDs, passedTimeDs, SEC_TO_DS(4))){
//		SendErrors(0);
//	}
}

//This method sends the node's status over the network
void StatusReporterModule::SendStatus(NodeId toNode, u8 messageType) const
{
	MeshConnections conn = GS->cm->GetMeshConnections(ConnectionDirection::DIRECTION_IN);

	StatusReporterModuleStatusMessage data;

	data.batteryInfo = GetBatteryVoltage();
	data.clusterSize = GS->node->clusterSize;
	data.connectionLossCounter = (u8) GS->node->connectionLossCounter; //TODO: connectionlosscounter is random at the moment, and the u8 will wrap
	data.freeIn = GS->cm->freeMeshInConnections;
	data.freeOut = GS->cm->freeMeshOutConnections;
	data.inConnectionPartner = conn.connections[0] == nullptr ? 0 : conn.connections[0]->partnerId;
	data.inConnectionRSSI = conn.connections[0] == nullptr ? 0 : conn.connections[0]->GetAverageRSSI();
	data.initializedByGateway = GS->node->initializedByGateway;

	SendModuleActionMessage(
		messageType,
		toNode,
		(u8)StatusModuleActionResponseMessages::STATUS,
		0,
		(u8*)&data,
		SIZEOF_STATUS_REPORTER_MODULE_STATUS_MESSAGE,
		false
	);
}

//Message type can be either MESSAGE_TYPE_MODULE_ACTION_RESPONSE or MESSAGE_TYPE_MODULE_GENERAL
void StatusReporterModule::SendDeviceInfo(NodeId toNode, u8 requestHandle, u8 messageType) const
{
	StatusReporterModuleDeviceInfoMessage data;

	data.manufacturerId = RamConfig->manufacturerId;
	data.deviceType = GS->node->configuration.deviceType;
	memcpy(data.chipId, (u8*)NRF_FICR->DEVICEADDR, 8);
	memcpy(data.serialNumber, RamConfig->serialNumber, NODE_SERIAL_NUMBER_LENGTH);
	FruityHal::BleGapAddressGet(&data.accessAddress);
	data.nodeVersion = fruityMeshVersion;
	data.networkId = GS->node->configuration.networkId;
	data.dBmRX = Boardconfig->dBmRX;
	data.dBmTX = GS->node->configuration.dBmTX;
	data.calibratedTX = Boardconfig->calibratedTX;
	data.chipGroupId = GS->config->fwGroupIds[0];
	data.featuresetGroupId = GS->config->fwGroupIds[1];
	data.bootloaderVersion = (u16)FruityHal::GetBootloaderVersion();

	SendModuleActionMessage(
		messageType,
		toNode,
		(u8)StatusModuleActionResponseMessages::DEVICE_INFO,
		requestHandle,
		(u8*)&data,
		SIZEOF_STATUS_REPORTER_MODULE_DEVICE_INFO_MESSAGE,
		false
	);
}

//Message type can be either MESSAGE_TYPE_MODULE_ACTION_RESPONSE or MESSAGE_TYPE_MODULE_GENERAL
void StatusReporterModule::SendDeviceInfoV2(NodeId toNode, u8 requestHandle, u8 messageType) const
{
	StatusReporterModuleDeviceInfoV2Message data;

	data.manufacturerId = RamConfig->manufacturerId;
	data.deviceType = GS->node->configuration.deviceType;
	memcpy(data.chipId, (u8*)NRF_FICR->DEVICEADDR, 8);
	data.serialNumberIndex = RamConfig->serialNumberIndex;
	FruityHal::BleGapAddressGet(&data.accessAddress);
	data.nodeVersion = fruityMeshVersion;
	data.networkId = GS->node->configuration.networkId;
	data.dBmRX = Boardconfig->dBmRX;
	data.dBmTX = GS->node->configuration.dBmTX;
	data.calibratedTX = Boardconfig->calibratedTX;
	data.chipGroupId = GS->config->fwGroupIds[0];
	data.featuresetGroupId = GS->config->fwGroupIds[1];
	data.bootloaderVersion = (u16)FruityHal::GetBootloaderVersion();

	SendModuleActionMessage(
		messageType,
		toNode,
		(u8)StatusModuleActionResponseMessages::DEVICE_INFO_V2,
		requestHandle,
		(u8*)&data,
		SIZEOF_STATUS_REPORTER_MODULE_DEVICE_INFO_V2_MESSAGE,
		false
	);
}

void StatusReporterModule::SendNearbyNodes(NodeId toNode, u8 messageType)
{
	u16 numMeasurements = 0;
	for(int i=0; i<NUM_NODE_MEASUREMENTS; i++){
		if(nodeMeasurements[i].nodeId != 0) numMeasurements++;
	}

	u8 packetSize = (u8)(numMeasurements * 3);
	DYNAMIC_ARRAY(buffer, packetSize);

	u16 j = 0;
	for(int i=0; i<NUM_NODE_MEASUREMENTS; i++)
	{
		if(nodeMeasurements[i].nodeId != 0){
			NodeId sender = nodeMeasurements[i].nodeId;
			i8 rssi = (i8)(nodeMeasurements[i].rssiSum / nodeMeasurements[i].packetCount);

			memcpy(buffer + j*3 + 0, &sender, 2);
			memcpy(buffer + j*3 + 2, &rssi, 1);

			j++;
		}
	}

	//Clear node measurements
	memset(nodeMeasurements, 0x00, sizeof(nodeMeasurements));

	SendModuleActionMessage(
		messageType,
		toNode,
		(u8)StatusModuleActionResponseMessages::NEARBY_NODES,
		0,
		buffer,
		packetSize,
		false
	);
}


//This method sends information about the current connections over the network
void StatusReporterModule::SendAllConnections(NodeId toNode, u8 messageType) const
{
	StatusReporterModuleConnectionsMessage message;
	memset(&message, 0x00, sizeof(StatusReporterModuleConnectionsMessage));

	MeshConnections connIn = GS->cm->GetMeshConnections(ConnectionDirection::DIRECTION_IN);
	MeshConnections connOut = GS->cm->GetMeshConnections(ConnectionDirection::DIRECTION_OUT);

	u8* buffer = (u8*)&message;

	if(connIn.count > 0){
		memcpy(buffer, &connIn.connections[0]->partnerId, 2);
		i8 avgRssi = connIn.connections[0]->GetAverageRSSI();
		memcpy(buffer + 2, &avgRssi, 1);
	}

	for(u32 i=0; i<connOut.count; i++){
		memcpy(buffer + (i+1)*3, &connOut.connections[i]->partnerId, 2);
		i8 avgRssi = connOut.connections[i]->GetAverageRSSI();
		memcpy(buffer + (i+1)*3 + 2, &avgRssi, 1);
	}

	SendModuleActionMessage(
		MESSAGE_TYPE_MODULE_ACTION_RESPONSE,
		NODE_ID_BROADCAST,
		(u8)StatusModuleActionResponseMessages::ALL_CONNECTIONS,
		0,
		(u8*)&message,
		SIZEOF_STATUS_REPORTER_MODULE_CONNECTIONS_MESSAGE,
		false
	);
}

void StatusReporterModule::SendRebootReason(NodeId toNode) const
{
	SendModuleActionMessage(
		MESSAGE_TYPE_MODULE_ACTION_RESPONSE,
		toNode,
		(u8)StatusModuleActionResponseMessages::REBOOT_REASON,
		0,
		(u8*)GS->ramRetainStructPtr,
		sizeof(RamRetainStruct) - sizeof(u32), //crc32 not needed
		false
	);
}
void StatusReporterModule::SendErrors(NodeId toNode) const{

	//Log another error so that we know the uptime of the node when the errors were requested
	GS->logger->logError(ErrorTypes::CUSTOM, (u32)CustomErrorTypes::COUNT_ERRORS_REQUESTED, 0);

	StatusReporterModuleErrorLogEntryMessage data;
	for(int i=0; i< GS->logger->errorLogPosition; i++){
		data.errorType = GS->logger->errorLog[i].errorType;
		data.extraInfo = GS->logger->errorLog[i].extraInfo;
		data.errorCode = GS->logger->errorLog[i].errorCode;
		data.timestamp = GS->logger->errorLog[i].timestamp;

		SendModuleActionMessage(
			MESSAGE_TYPE_MODULE_ACTION_RESPONSE,
			toNode,
			(u8)StatusModuleActionResponseMessages::ERROR_LOG_ENTRY,
			0,
			(u8*)&data,
			SIZEOF_STATUS_REPORTER_MODULE_ERROR_LOG_ENTRY_MESSAGE,
			false
		);
	}

	//Reset the error log
	GS->logger->errorLogPosition = 0;

	//Clears the reboot reason because we have sent it already
	//TODO: Remove this after the gateway sets this beacon to initialized after requesting errors
	GS->ramRetainStructPtr->rebootReason = 0;
	FruityHal::ClearRebootReason();
}


void StatusReporterModule::SendLiveReport(LiveReportTypes type, u32 extra, u32 extra2) const
{
	//Live reporting states are off=0, error=50, warn=100, info=150, debug=200
	if (type > configuration.liveReportingState) return;

	StatusReporterModuleLiveReportMessage data;

	data.reportType = (u8)type;
	data.extra = extra;
	data.extra2 = extra2;

	SendModuleActionMessage(
		MESSAGE_TYPE_MODULE_GENERAL,
		NODE_ID_BROADCAST, //TODO: Could use gateway
		(u8)StatusModuleGeneralMessages::LIVE_REPORT,
		0,
		(u8*)&data,
		SIZEOF_STATUS_REPORTER_MODULE_LIVE_REPORT_MESSAGE,
		false
	);
}

void StatusReporterModule::StartConnectionRSSIMeasurement(MeshConnection& connection) const{
	u32 err = 0;

	if (connection.isConnected())
	{
		//Reset old values
		connection.lastReportedRssi = 0;
		connection.rssiAverageTimes1000 = 0;

		err = sd_ble_gap_rssi_start(connection.connectionHandle, 2, 7);
		if(err == NRF_ERROR_INVALID_STATE || err == BLE_ERROR_INVALID_CONN_HANDLE){
			//Both errors are due to a disconnect and we can simply ignore them
		} else {
			APP_ERROR_CHECK(err); //OK
		}

		logt("STATUSMOD", "RSSI measurement started for connection %u with code %u", connection.connectionId, err);
	}
}

void StatusReporterModule::StopConnectionRSSIMeasurement(const MeshConnection& connection) const{
	u32 err = 0;

	if (connection.isConnected())
	{
		err = sd_ble_gap_rssi_stop(connection.connectionHandle);
		if(err == NRF_ERROR_INVALID_STATE || err == BLE_ERROR_INVALID_CONN_HANDLE){
			//Both errors are due to a disconnect and we can simply ignore them
		} else {
			APP_ERROR_CHECK(err); //OK
		}

		logt("STATUSMOD", "RSSI measurement stopped for connection %u with code %u", connection.connectionId, err);
	}
}


//This handler receives all ble events and can act on them
void StatusReporterModule::BleEventHandler(const ble_evt_t& bleEvent){

	if(bleEvent.header.evt_id == BLE_GAP_EVT_ADV_REPORT){

		const u8* data = bleEvent.evt.gap_evt.params.adv_report.data;
		u16 dataLength = bleEvent.evt.gap_evt.params.adv_report.dlen;

		const advPacketHeader* packetHeader = (const advPacketHeader*) data;

		switch (packetHeader->messageType)
		{
			case MESSAGE_TYPE_JOIN_ME_V0:
				if (dataLength == SIZEOF_ADV_PACKET_JOIN_ME)
				{
					const advPacketJoinMeV0* packet = (const advPacketJoinMeV0*) data;

					bool found = false;

					for(int i=0; i<NUM_NODE_MEASUREMENTS; i++){
						if(nodeMeasurements[i].nodeId == packet->payload.sender){
							if(nodeMeasurements[i].packetCount == UINT16_MAX){
								nodeMeasurements[i].packetCount = 0;
								nodeMeasurements[i].rssiSum = 0;
							}
							nodeMeasurements[i].packetCount++;
							nodeMeasurements[i].rssiSum += bleEvent.evt.gap_evt.params.adv_report.rssi;
							found = true;
							break;
						}
					}
					if(!found){
						for(int i=0; i<NUM_NODE_MEASUREMENTS; i++){
							if(nodeMeasurements[i].nodeId == 0){
								nodeMeasurements[i].nodeId = packet->payload.sender;
								nodeMeasurements[i].packetCount = 1;
								nodeMeasurements[i].rssiSum = bleEvent.evt.gap_evt.params.adv_report.rssi;

								break;
							}
						}
					}

				}
		}

	} else if(bleEvent.header.evt_id == BLE_GATTC_EVT_TIMEOUT){
		SendModuleActionMessage(
				MESSAGE_TYPE_MODULE_ACTION_RESPONSE,
				0,
				(u8)StatusModuleActionResponseMessages::DISCONNECT_REASON,
				0,
				nullptr,
				0,
				false
			);


	} else if(bleEvent.header.evt_id == BLE_GAP_EVT_DISCONNECTED){

	}
}

#ifdef TERMINAL_ENABLED
bool StatusReporterModule::TerminalCommandHandler(char* commandArgs[], u8 commandArgsSize)
{
	//React on commands, return true if handled, false otherwise
	if(commandArgsSize >= 3 && TERMARGS(2, moduleName))
	{
		if(TERMARGS(0, "action"))
		{
			//Rewrite "this" to our own node id, this will actually build the packet
			//But reroute it to our own node
			NodeId destinationNode = (TERMARGS(1, "this")) ? GS->node->configuration.nodeId : atoi(commandArgs[1]);

			if(commandArgsSize >= 4 && TERMARGS(3, "get_status"))
			{
				SendModuleActionMessage(
					MESSAGE_TYPE_MODULE_TRIGGER_ACTION,
					destinationNode,
					(u8)StatusModuleTriggerActionMessages::GET_STATUS,
					0,
					nullptr,
					0,
					false
				);

				return true;
			}
			else if(commandArgsSize >= 4 && TERMARGS(3,"get_device_info"))
			{
				SendModuleActionMessage(
					MESSAGE_TYPE_MODULE_TRIGGER_ACTION,
					destinationNode,
					(u8)StatusModuleTriggerActionMessages::GET_DEVICE_INFO,
					0,
					nullptr,
					0,
					false
				);

				return true;
			}
			else if(commandArgsSize >= 4 && TERMARGS(3, "get_device_info2"))	//jstodo possibly unused (or version 1)
			{
				SendModuleActionMessage(
					MESSAGE_TYPE_MODULE_TRIGGER_ACTION,
					destinationNode,
					(u8)StatusModuleTriggerActionMessages::GET_DEVICE_INFO_V2,
					0,
					nullptr,
					0,
					false
				);

				return true;
			}
			else if(commandArgsSize >= 4 && TERMARGS(3,"get_connections"))
			{
				SendModuleActionMessage(
					MESSAGE_TYPE_MODULE_TRIGGER_ACTION,
					destinationNode,
					(u8)StatusModuleTriggerActionMessages::GET_ALL_CONNECTIONS,
					0,
					nullptr,
					0,
					false
				);

				return true;
			}
			else if(commandArgsSize >= 4 && TERMARGS(3, "get_nearby"))
			{
				SendModuleActionMessage(
					MESSAGE_TYPE_MODULE_TRIGGER_ACTION,
					destinationNode,
					(u8)StatusModuleTriggerActionMessages::GET_NEARBY_NODES,
					0,
					nullptr,
					0,
					false
				);

				return true;
			}
			else if(commandArgsSize >= 4 && TERMARGS(3,"set_init"))
			{
				SendModuleActionMessage(
					MESSAGE_TYPE_MODULE_TRIGGER_ACTION,
					destinationNode,
					(u8)StatusModuleTriggerActionMessages::SET_INITIALIZED,
					0,
					nullptr,
					0,
					false
				);

				return true;
			}
			else if(commandArgsSize >= 4 && TERMARGS(3, "keep_alive"))
			{
				SendModuleActionMessage(
					MESSAGE_TYPE_MODULE_TRIGGER_ACTION,
					destinationNode,
					(u8)StatusModuleTriggerActionMessages::SET_KEEP_ALIVE,
					0,
					nullptr,
					0,
					false
				);

				return true;
			}
			else if(commandArgsSize >= 4 && TERMARGS(3, "get_errors"))
			{
				SendModuleActionMessage(
					MESSAGE_TYPE_MODULE_TRIGGER_ACTION,
					destinationNode,
					(u8)StatusModuleTriggerActionMessages::GET_ERRORS,
					0,
					nullptr,
					0,
					false
				);

				return true;
			}
			else if(commandArgsSize >= 5 && TERMARGS(3 ,"livereports")){
					//Enables or disables live reporting of connection establishments
					u8 liveReportingState = atoi(commandArgs[4]);

					SendModuleActionMessage(
						MESSAGE_TYPE_MODULE_TRIGGER_ACTION,
						destinationNode,
						(u8)StatusModuleTriggerActionMessages::SET_LIVEREPORTING,
						0,
						&liveReportingState,
						1,
						false
					);

					return true;
				}
			else if(commandArgsSize >= 4 && TERMARGS(3, "get_rebootreason"))
			{
				SendModuleActionMessage(
					MESSAGE_TYPE_MODULE_TRIGGER_ACTION,
					destinationNode,
					(u8)StatusModuleTriggerActionMessages::GET_REBOOT_REASON,
					0,
					nullptr,
					0,
					false
				);

				return true;
			}
		}
	}

	//Must be called to allow the module to get and set the config
	return Module::TerminalCommandHandler(commandArgs, commandArgsSize);
}
#endif

void StatusReporterModule::MeshMessageReceivedHandler(BaseConnection* connection, BaseConnectionSendData* sendData, connPacketHeader* packetHeader)
{
	//Must call superclass for handling
	Module::MeshMessageReceivedHandler(connection, sendData, packetHeader);

	if(packetHeader->messageType == MESSAGE_TYPE_MODULE_TRIGGER_ACTION){
		connPacketModule* packet = (connPacketModule*)packetHeader;

		//Check if our module is meant and we should trigger an action
		if(packet->moduleId == moduleId){

			//We were queried for our status
			StatusModuleTriggerActionMessages actionType = (StatusModuleTriggerActionMessages)packet->actionType;
			if(actionType == StatusModuleTriggerActionMessages::GET_STATUS)
			{
				SendStatus(packet->header.sender, MESSAGE_TYPE_MODULE_ACTION_RESPONSE);

			}
			//We were queried for our device info
			else if(actionType == StatusModuleTriggerActionMessages::GET_DEVICE_INFO)
			{
				SendDeviceInfo(packet->header.sender, packet->requestHandle, MESSAGE_TYPE_MODULE_ACTION_RESPONSE);

			}
			//We were queried for our device info v2
			else if(actionType == StatusModuleTriggerActionMessages::GET_DEVICE_INFO_V2)
			{
				SendDeviceInfoV2(packet->header.sender, packet->requestHandle, MESSAGE_TYPE_MODULE_ACTION_RESPONSE);

			}
			//We were queried for our connections
			else if(actionType == StatusModuleTriggerActionMessages::GET_ALL_CONNECTIONS)
			{
				StatusReporterModule::SendAllConnections(packetHeader->sender, MESSAGE_TYPE_MODULE_ACTION_RESPONSE);
			}
			//We were queried for nearby nodes (nodes in the join_me buffer)
			else if(actionType == StatusModuleTriggerActionMessages::GET_NEARBY_NODES)
			{
				StatusReporterModule::SendNearbyNodes(packetHeader->sender, MESSAGE_TYPE_MODULE_ACTION_RESPONSE);
			}
			//We should set ourselves initialized
			else if(actionType == StatusModuleTriggerActionMessages::SET_INITIALIZED)
			{
				GS->node->initializedByGateway = true;
				GS->ramRetainStructPtr->rebootReason = 0;
				FruityHal::ClearRebootReason();

				SendModuleActionMessage(
					MESSAGE_TYPE_MODULE_ACTION_RESPONSE,
					packet->header.sender,
					(u8)StatusModuleActionResponseMessages::SET_INITIALIZED_RESULT,
					0,
					nullptr,
					0,
					false
				);
			}
			else if(actionType == StatusModuleTriggerActionMessages::SET_KEEP_ALIVE)
			{
				FruityHal::FeedWatchdog();
			}
			//Send back the errors
			else if(actionType == StatusModuleTriggerActionMessages::GET_ERRORS)
			{
				SendErrors(packet->header.sender);
			}
			//Configures livereporting
			else if(actionType == StatusModuleTriggerActionMessages::SET_LIVEREPORTING)
			{
				configuration.liveReportingState = (LiveReportTypes)packet->data[0];
				logt("DEBUGMOD", "LiveReporting is now %u", configuration.liveReportingState);
			}
			//Send back the reboot reason
			else if(actionType == StatusModuleTriggerActionMessages::GET_REBOOT_REASON)
			{
				SendRebootReason(packet->header.sender);
			}
		}
	}

	//Parse Module responses
	if(packetHeader->messageType == MESSAGE_TYPE_MODULE_ACTION_RESPONSE){

		connPacketModule* packet = (connPacketModule*)packetHeader;

		//Check if our module is meant and we should trigger an action
		if(packet->moduleId == moduleId)
		{
			StatusModuleActionResponseMessages actionType = (StatusModuleActionResponseMessages)packet->actionType;
			//Somebody reported its connections back
			if(actionType == StatusModuleActionResponseMessages::ALL_CONNECTIONS)
			{
				StatusReporterModuleConnectionsMessage* packetData = (StatusReporterModuleConnectionsMessage*) (packet->data);
				logjson("STATUSMOD", "{\"type\":\"connections\",\"nodeId\":%d,\"module\":%d,\"partners\":[%d,%d,%d,%d],\"rssiValues\":[%d,%d,%d,%d]}" SEP, packet->header.sender, moduleId, packetData->partner1, packetData->partner2, packetData->partner3, packetData->partner4, packetData->rssi1, packetData->rssi2, packetData->rssi3, packetData->rssi4);
			}
			else if(actionType == StatusModuleActionResponseMessages::DEVICE_INFO)
			{
				//Print packet to console
				StatusReporterModuleDeviceInfoMessage* data = (StatusReporterModuleDeviceInfoMessage*) (packet->data);

				u8* addr = data->accessAddress.addr;

				char serialNumber[NODE_SERIAL_NUMBER_LENGTH+1];
				memcpy(serialNumber, data->serialNumber, NODE_SERIAL_NUMBER_LENGTH);
				serialNumber[NODE_SERIAL_NUMBER_LENGTH] = '\0';

				logjson("STATUSMOD", "{\"nodeId\":%u,\"type\":\"device_info\",\"module\":%d,", packet->header.sender, moduleId);
				logjson("STATUSMOD", "\"dBmRX\":%d,\"dBmTX\":%d,\"calibratedTX\":%d,", data->dBmRX, data->dBmTX, data->calibratedTX);
				logjson("STATUSMOD", "\"deviceType\":%u,\"manufacturerId\":%u,", data->deviceType, data->manufacturerId);
				logjson("STATUSMOD", "\"networkId\":%u,\"nodeVersion\":%u,", data->networkId, data->nodeVersion);
				logjson("STATUSMOD", "\"chipId\":\"%02X:%02X:%02X:%02X:%02X:%02X:%02X:%02X\",", data->chipId[0], data->chipId[1], data->chipId[2], data->chipId[3], data->chipId[4], data->chipId[5], data->chipId[6], data->chipId[7]);
				logjson("STATUSMOD", "\"serialNumber\":\"%s\",\"accessAddress\":\"%02X:%02X:%02X:%02X:%02X:%02X\",", serialNumber, addr[5], addr[4], addr[3], addr[2], addr[1], addr[0]);
				logjson("STATUSMOD", "\"groupIds\":[%u,%u],\"blVersion\":%u", data->chipGroupId, data->featuresetGroupId, data->bootloaderVersion);
				logjson("STATUSMOD", "}" SEP);

			}
			else if(actionType == StatusModuleActionResponseMessages::DEVICE_INFO_V2)
			{
				//Print packet to console
				StatusReporterModuleDeviceInfoV2Message* data = (StatusReporterModuleDeviceInfoV2Message*) (packet->data);

				u8* addr = data->accessAddress.addr;

				logjson("STATUSMOD", "{\"nodeId\":%u,\"type\":\"device_info\",\"module\":%d,", packet->header.sender, moduleId);
				logjson("STATUSMOD", "\"dBmRX\":%d,\"dBmTX\":%d,\"calibratedTX\":%d,", data->dBmRX, data->dBmTX, data->calibratedTX);
				logjson("STATUSMOD", "\"deviceType\":%u,\"manufacturerId\":%u,", data->deviceType, data->manufacturerId);
				logjson("STATUSMOD", "\"networkId\":%u,\"nodeVersion\":%u,", data->networkId, data->nodeVersion);
				logjson("STATUSMOD", "\"chipId\":\"%02X:%02X:%02X:%02X:%02X:%02X:%02X:%02X\",", data->chipId[0], data->chipId[1], data->chipId[2], data->chipId[3], data->chipId[4], data->chipId[5], data->chipId[6], data->chipId[7]);
				logjson("STATUSMOD", "\"serialNumberIndex\":%u,\"accessAddress\":\"%02X:%02X:%02X:%02X:%02X:%02X\",", data->serialNumberIndex, addr[5], addr[4], addr[3], addr[2], addr[1], addr[0]);
				logjson("STATUSMOD", "\"groupIds\":[%u,%u],\"blVersion\":%u", data->chipGroupId, data->featuresetGroupId, data->bootloaderVersion);
				logjson("STATUSMOD", "}" SEP);

			}
			else if(actionType == StatusModuleActionResponseMessages::STATUS)
			{
				//Print packet to console
				StatusReporterModuleStatusMessage* data = (StatusReporterModuleStatusMessage*) (packet->data);

				logjson("STATUSMOD", "{\"nodeId\":%u,\"type\":\"status\",\"module\":%d,", packet->header.sender, moduleId);
				logjson("STATUSMOD", "\"batteryInfo\":%u,\"clusterSize\":%u,", data->batteryInfo, data->clusterSize);
				logjson("STATUSMOD", "\"connectionLossCounter\":%u,\"freeIn\":%u,", data->connectionLossCounter, data->freeIn);
				logjson("STATUSMOD", "\"freeOut\":%u,\"inConnectionPartner\":%u,", data->freeOut, data->inConnectionPartner);
				logjson("STATUSMOD", "\"inConnectionRSSI\":%d, \"initialized\":%u", data->inConnectionRSSI, data->initializedByGateway);
				logjson("STATUSMOD", "}" SEP);
			}
			else if(actionType == StatusModuleActionResponseMessages::NEARBY_NODES)
			{
				//Print packet to console
				logjson("STATUSMOD", "{\"nodeId\":%u,\"type\":\"nearby_nodes\",\"module\":%u,\"nodes\":[", packet->header.sender, moduleId);

				u16 nodeCount = (sendData->dataLength - SIZEOF_CONN_PACKET_MODULE) / 3;
				bool first = true;
				for(int i=0; i<nodeCount; i++){
					u16 nodeId;
					i8 rssi;
					//TODO: Find a nicer way to access unaligned data in packets
					memcpy(&nodeId, packet->data + i*3+0, 2);
					memcpy(&rssi, packet->data + i*3+2, 1);
					if(!first){
						logjson("STATUSMOD", ",");
					}
					logjson("STATUSMOD", "{\"nodeId\":%u,\"rssi\":%d}", nodeId, rssi);
					first = false;
				}

				logjson("STATUSMOD", "]}" SEP);
			}
			else if(actionType == StatusModuleActionResponseMessages::SET_INITIALIZED_RESULT)
			{
				logjson("STATUSMOD", "{\"type\":\"set_init_result\",\"nodeId\":%u,\"module\":%u}" SEP, packet->header.sender, moduleId);
			}
			else if(actionType == StatusModuleActionResponseMessages::ERROR_LOG_ENTRY)
			{
				StatusReporterModuleErrorLogEntryMessage* data = (StatusReporterModuleErrorLogEntryMessage*) (packet->data);

				logjson("STATUSMOD", "{\"type\":\"error_log_entry\",\"nodeId\":%u,\"module\":%u,", packet->header.sender, moduleId);
				logjson("STATUSMOD", "\"errType\":%u,\"code\":%u,\"extra\":%u,\"time\":%u", data->errorType, data->errorCode, data->extraInfo, data->timestamp);
				logjson("STATUSMOD", "}" SEP);
			}
			else if(actionType == StatusModuleActionResponseMessages::REBOOT_REASON)
			{
				RamRetainStruct* data = (RamRetainStruct*) (packet->data);

				logjson("STATUSMOD", "{\"type\":\"reboot_reason\",\"nodeId\":%u,\"module\":%u,", packet->header.sender, moduleId);
				logjson("STATUSMOD", "\"reason\":%u,\"code1\":%u,\"code2\":%u,\"code3\":%u,\"stack\":[", data->rebootReason, data->code1, data->code2, data->code3);
				for(u8 i=0; i<data->stacktraceSize; i++){
					logjson("STATUSMOD", (i < data->stacktraceSize-1) ? "%x," : "%x", data->stacktrace[i]);
				}
				logjson("STATUSMOD", "]}" SEP);
			}
		}
	}

	//Parse Module general messages
	if(packetHeader->messageType == MESSAGE_TYPE_MODULE_GENERAL){

		connPacketModule* packet = (connPacketModule*)packetHeader;

		//Check if our module is meant and we should trigger an action
		if(packet->moduleId == moduleId)
		{
			StatusModuleGeneralMessages actionType = (StatusModuleGeneralMessages)packet->actionType;
			//Somebody reported its connections back
			if(actionType == StatusModuleGeneralMessages::LIVE_REPORT)
			{
				StatusReporterModuleLiveReportMessage* packetData = (StatusReporterModuleLiveReportMessage*) (packet->data);
				logjson("STATUSMOD", "{\"type\":\"live_report\",\"nodeId\":%d,\"module\":%d,\"code\":%u,\"extra\":%u,\"extra2\":%u}" SEP, packet->header.sender, moduleId, packetData->reportType, packetData->extra, packetData->extra2);
			}
		}
	}
}

void StatusReporterModule::MeshConnectionChangedHandler(MeshConnection& connection)
{
	//New connection has just been made
	if(connection.handshakeDone()){
		//TODO: Implement low and medium rssi sampling with timer handler
		//TODO: disable and enable rssi sampling on existing connections
		if(Config->enableConnectionRSSIMeasurement){
			if(configuration.connectionRSSISamplingMode == RSSISamplingModes::HIGH){
				StartConnectionRSSIMeasurement(connection);
			}
		}
	}
}

void StatusReporterModule::ButtonHandler(u8 buttonId, u32 holdTime)
{

}

#define _____________BATTERY_MEASUREMENT_________________

#if defined(NRF52)
/* FIXME: since std::bind and std::placeholders depend on cpp-11 or higher, I don't know any other way. */
void saadc_callback(nrf_drv_saadc_evt_t const * p_event)
{
	if (p_event->type == NRF_DRV_SAADC_EVT_DONE)
	    {
	        ret_code_t err_code = nrf_drv_saadc_buffer_convert(p_event->data.done.p_buffer, 1);
	        APP_ERROR_CHECK(err_code);
	    }
}
#endif

void StatusReporterModule::initBatteryVoltageADC() {
#ifdef ACTIVATE_BATTERY_MEASUREMENT
	//Do not initialize battery checking if board does not support it
	if(Boardconfig->batteryAdcAin < 0 || Boardconfig->batteryCheckDIO < 0 || isADCInitialized){
		return;
	}

#if defined(NRF51)
	ret_code_t err_code;
	err_code = nrf_drv_adc_init(nullptr,nullptr);
	APP_ERROR_CHECK(err_code);
    nrf_drv_adc_channel_config_t cct;
    cct.resolution = NRF_ADC_CONFIG_RES_8BIT;
    cct.input = NRF_ADC_CONFIG_SCALING_INPUT_FULL_SCALE ;
    cct.reference = NRF_ADC_CONFIG_REF_VBG;
    cct.ain = Boardconfig->batteryAdcAin;
	adc_channel_config.config.config = cct;
	adc_channel_config.p_next = nullptr;
	nrf_drv_adc_config_t adc_config;
	adc_config.interrupt_priority = ADC_CONFIG_IRQ_PRIORITY;             //Get default ADC configuration
	nrf_drv_adc_channel_enable(&adc_channel_config);                          //Configure and enable an ADC channel
#endif
#if defined(NRF52)
	ret_code_t err_code;
	nrf_saadc_channel_config_t channel_config
		= NRF_DRV_SAADC_DEFAULT_CHANNEL_CONFIG_SE(Boardconfig->batteryAdcAin);  //Default Channel Configuration on ADC INPUT 0(P0.02)
	err_code = nrf_drv_saadc_init(nullptr, saadc_callback);
	APP_ERROR_CHECK(err_code);
	err_code = nrf_drv_saadc_channel_init(0, &channel_config);
	APP_ERROR_CHECK(err_code);
	err_code = nrf_drv_saadc_buffer_convert(m_buffer, 1);
	APP_ERROR_CHECK(err_code);
#endif

	isADCInitialized = true;
	logt("STATUSMOD", "Battery Measurement Initialized");
#endif
}
void StatusReporterModule::BatteryVoltageADC(){
#ifdef ACTIVATE_BATTERY_MEASUREMENT
	initBatteryVoltageADC();
	//Check if initialization did work
	if(!isADCInitialized) return;

	logt("STATUSMOD", "Measuring Battery");

#ifndef SIM_ENABLED
	nrf_gpio_cfg_output (Boardconfig->batteryCheckDIO);
	nrf_gpio_pin_set(Boardconfig->batteryCheckDIO);
#if defined(NRF51)
	if(!nrf_drv_adc_is_busy())
	{
		ret_code_t err_code = nrf_drv_adc_sample_convert(&adc_channel_config,m_buffer);
		APP_ERROR_CHECK(err_code);
	}
#endif
#if defined(NRF52)
	ret_code_t err_code = nrf_drv_saadc_sample();// Non-blocking triggering of SAADC Sampling
	APP_ERROR_CHECK(err_code);
#endif
	nrf_gpio_pin_clear(Boardconfig->batteryCheckDIO);
	convertADCtoVoltage(m_buffer, BATTERY_SAMPLES_IN_BUFFER);

#endif
#endif
}

void StatusReporterModule::convertADCtoVoltage(i16 * buffer, u16 size)
{
#ifdef ACTIVATE_BATTERY_MEASUREMENT
	u32 adc_sum_value = 0;
	for (u16 i = 0; i < size; i++)
	{
		//Buffer implemented for future use
		adc_sum_value += buffer[i];                           //Sum all values in ADC buffer
	}
	batteryVoltageDv = RESULT_IN_DECI_VOLTS(adc_sum_value/size);          //Transform the average ADC value into decivolts value
	logt("STATUSMOD", "Measured Battery -> %d DV",batteryVoltageDv);
#endif
}

u8 StatusReporterModule::GetBatteryVoltage() const
{
	return batteryVoltageDv;
}
#endif
