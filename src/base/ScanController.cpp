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

#include <Node.h>
#include <ScanController.h>
#include <ConnectionManager.h>
#include <Logger.h>
#include <Config.h>

extern "C"
{
#include <app_error.h>
}

/**
 * IMPORTANT: The ScanController must be informed if the scan state changes without its
 * knowledge, e.g. when callind sd_ble_gap_connect. Otherwise scanning will stop and it will
 * not know it has to restart scanning.
 */

ScanController::ScanController()
{
	//Define scanning Parameters
	currentScanParams.interval = 0;				// Scan interval.
	currentScanParams.window = 0;	// Scan window.
	currentScanParams.timeout = 0;					// Never stop scanning unless explicit asked to.

	scanningState = SCAN_STATE_OFF;
	scanStateOk = true;
}

void ScanController::TimerEventHandler(u16 passedTimeDs)
{
	//To be absolutely sure that scanning is in the correct state, we call this function
	//within the timerHandler
	TryConfiguringScanState();
}


//Start scanning with the specified scanning parameters
void ScanController::SetScanState(scanState newState)
{
	logt("SC", "SetScanState %u", newState);

	u32 err = 0;
	//Nothing to do if requested state is same as current state
	if (newState == scanningState) return;

	scanningState = newState;
	scanStateOk = false;

	if (newState == SCAN_STATE_HIGH)
	{
		currentScanParams.interval = Config->meshScanIntervalHigh;
		currentScanParams.window = Config->meshScanWindowHigh;
	}
	else if (newState == SCAN_STATE_LOW)
	{
		currentScanParams.interval = Config->meshScanIntervalLow;
		currentScanParams.window = Config->meshScanWindowLow;
	}

	TryConfiguringScanState();
}

void ScanController::SetScanDutyCycle(u16 interval, u16 window)
{
	logt("SC", "SetScanDutyCycle %u %u", interval, window);

	scanningState = SCAN_STATE_CUSTOM;
	scanStateOk = false;

	currentScanParams.interval = interval;
	currentScanParams.window = window;

	TryConfiguringScanState();
}

//This will call the HAL to enable the current scan state
void ScanController::TryConfiguringScanState()
{
	u32 err;
	if(!scanStateOk){
		//First, try stopping
		err = FruityHal::BleGapScanStop();
		if(scanningState == SCAN_STATE_OFF){
			if(err == NRF_SUCCESS) scanStateOk = true;
		}

		//Next, try starting
		if(scanningState != SCAN_STATE_OFF){
			err = FruityHal::BleGapScanStart(&currentScanParams);
			if(err == NRF_SUCCESS) scanStateOk = true;
		}
	}
}

void ScanController::ScanningHasStopped()
{
	scanStateOk = false;
}

//If a BLE event occurs, this handler will be called to do the work
bool ScanController::ScanEventHandler(ble_evt_t &bleEvent) const
{
	//u32 err = 0;

	//Depending on the type of the BLE event, we have to do different stuff
	switch (bleEvent.header.evt_id)
	{
		//########## Advertisement data coming in
		case BLE_GAP_EVT_ADV_REPORT:
		{
			//Check if packet is a valid mesh advertising packet
			advPacketHeader* packetHeader = (advPacketHeader*) bleEvent.evt.gap_evt.params.adv_report.data;

			if (
					bleEvent.evt.gap_evt.params.adv_report.dlen >= SIZEOF_ADV_PACKET_HEADER
					&& packetHeader->manufacturer.companyIdentifier == COMPANY_IDENTIFIER
					&& packetHeader->meshIdentifier == MESH_IDENTIFIER
					&& packetHeader->networkId == GS->node->configuration.networkId
				)
			{
				//Packet is valid and belongs to our network, forward to Node for further processing
				GS->node->AdvertisementMessageHandler(bleEvent);

			}

			return true;
		}
	}
	return false;
}

//add a function to modified the interval and window scan for %fo the battery

void ScanController::SetScanDutyCycleBattery()
{
	u8 bat = 0;
	StatusReporterModule* statusMod =(StatusReporterModule*)GS->node->GetModuleById(moduleID::STATUS_REPORTER_MODULE_ID);
	if(statusMod != nullptr){
		bat = statusMod->GetBatteryVoltage();
	}
	//bat medida em dc
	logt("SC", "Battery %u DV", bat);
	
	//bat dada em DV logo 3v == 30DV
	//tensão de funcionamento 3v a 1.8V -> 1,2V  que pode ser dividos em 5 estagios
	//100% (B>=27), 75%(B<27), 50%(<24), 25%(<21)
	
    scanningState = SCAN_STATE_CUSTOM;
	scanStateOk = false;
	if(bat>27)//100%
	{
	
	currentScanParams.interval = (u16) MSEC_TO_UNITS(20, UNIT_0_625_MS);
	currentScanParams.window = (u16) MSEC_TO_UNITS(5, UNIT_0_625_MS);
	currentScanParams.timeout = (u16) 1;
	}
	else if(bat>24)//75%
	{
	currentScanParams.interval = (u16) MSEC_TO_UNITS(30, UNIT_0_625_MS);
	currentScanParams.window = (u16) MSEC_TO_UNITS(4, UNIT_0_625_MS);
	currentScanParams.timeout = (u16) 2;
	}
	else if(bat>21)//50%
	{
	currentScanParams.interval = (u16) MSEC_TO_UNITS(40, UNIT_0_625_MS);
	currentScanParams.window = (u16) MSEC_TO_UNITS(3, UNIT_0_625_MS);
	currentScanParams.timeout = (u16) 3;
	}
	else //25%
	{
	currentScanParams.interval = (u16) MSEC_TO_UNITS(50, UNIT_0_625_MS);
	currentScanParams.window = (u16) MSEC_TO_UNITS(1, UNIT_0_625_MS);
	currentScanParams.timeout = (u16) 4;
	}
	TryConfiguringScanState();
	logt("SC", "SetScanDutyCycleBattery %u %u", currentScanParams.interval, currentScanParams.window);
}

//EOF
