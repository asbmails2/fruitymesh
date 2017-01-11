/**

Copyright (c) 2014-2015 "M-Way Solutions GmbH"
FruityMesh - Bluetooth Low Energy mesh protocol [http://mwaysolutions.com/]

This file is part of FruityMesh

FruityMesh is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.

*/
#ifndef ARS100748_CABLE_H
#define ARS100748_CABLE_H

// Definitions for ARS100748_CABLE v1.0


#define SET_ARS100748_CABLE_BOARD()			\
do{ 									\
	Config->Led1Pin = 0;				\
	Config->Led2Pin = 1;				\
	Config->Led3Pin = 1;				\
	Config->LedActiveHigh = true;				\
	Config->Button1Pin = 3;			\
	Config->ButtonsActiveHigh = false;			\
	Config->uartRXPin = 11;				\
	Config->uartTXPin = 9;				\
	Config->uartCTSPin = 10;				\
	Config->uartRTSPin = 8;				\
	Config->calibratedTX = -65;				\
										\
} while(0)


//This macro checks whether the boardId is for ARS100748_cable board
#define SET_ARS100748_CABLE_BOARD_IF_FIT(boardid)		\
do{												\
	if(boardid == 0x005){							\
		SET_ARS100748_CABLE_BOARD(); 						\
	}												\
} while(0)



#endif // ARS100748_CABLE_H



