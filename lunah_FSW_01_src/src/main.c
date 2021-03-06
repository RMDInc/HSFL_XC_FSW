/******************************************************************************
*
* Copyright (C) 2014 - 2016 Xilinx, Inc. All rights reserved.
*
* Permission is hereby granted, free of charge, to any person obtaining a copy
* of this software and associated documentation files (the "Software"), to deal
* in the Software without restriction, including without limitation the rights
* to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
* copies of the Software, and to permit persons to whom the Software is
* furnished to do so, subject to the following conditions:
*
* The above copyright notice and this permission notice shall be included in
* all copies or substantial portions of the Software.
*
* Use of the Software is limited solely to applications:
* (a) running on a Xilinx device, or
* (b) that interact with a Xilinx device through a bus or interconnect.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
* IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
* FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
* XILINX  BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
* WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF
* OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
* SOFTWARE.
*
* Except as contained in this notice, the name of the Xilinx shall not be used
* in advertising or otherwise to promote the sale, use or other dealings in
* this Software without prior written authorization from Xilinx.
*
******************************************************************************/

/*
 * Mini-NS Flight Software, Version 5.4
 * Graham Stoddard, 3/28/2019
 *
 * 02-25-2019
 * Added a compiler option "m" to allow us to include math.h to be linked in so we
 *  may use the floor() function. If this can be worked around, I think we should. - GJS
 *
 */

#include "main.h"

int main()
{
	int status = 0;			//local status variable for reporting SUCCESS/FAILURE

	int valid_data = 0;		//local test variable for WF
	int array_index = 0;
	int dram_addr = 0;
	int dram_base = 0xA000000;
	int dram_ceiling = 0xA004000;
	int numWFs = 0;
	unsigned int numBytesWritten = 0;
	unsigned int wf_data[DATA_BUFFER_SIZE] = {};
	FIL WFData;
	FRESULT f_res = FR_OK;

	init_platform();		//Maybe we dropped out important init functions?
	ps7_post_config();
	Xil_DCacheDisable();	// Disable the L1/L2 data caches
	InitializeAXIDma();		// Initialize the AXI DMA Transfer Interface

	status = InitializeInterruptSystem(XPAR_PS7_SCUGIC_0_DEVICE_ID);
	if(status != XST_SUCCESS)
	{
		xil_printf("Interrupt system initialization error\n");

	}
	// *********** Setup the Hardware Reset GPIO ****************//
	// GPIO/TEC Test Variables
	XGpioPs Gpio;
	int gpio_status = 0;
	XGpioPs_Config *GPIOConfigPtr;

	GPIOConfigPtr = XGpioPs_LookupConfig(XPAR_PS7_GPIO_0_DEVICE_ID);
	gpio_status = XGpioPs_CfgInitialize(&Gpio, GPIOConfigPtr, GPIOConfigPtr->BaseAddr);
	if(gpio_status != XST_SUCCESS)
		xil_printf("GPIO PS init failed\r\n");

	XGpioPs_SetDirectionPin(&Gpio, TEC_PIN, 1);
	XGpioPs_SetOutputEnablePin(&Gpio, TEC_PIN, 1);
	XGpioPs_WritePin(&Gpio, TEC_PIN, 0);	//disable TEC startup

	XGpioPs_SetDirectionPin(&Gpio, SW_BREAK_GPIO, 1);
	//******************Setup and Initialize IIC*********************//
	int iic_status = 0;
	//Make the IIC Instance come from here and we pass it in to the functions
	XIicPs Iic;

	iic_status = IicPsInit(&Iic, IIC_DEVICE_ID_0);
	if(iic_status != XST_SUCCESS)
	{
		//handle the issue
		xil_printf("fix the Iic device 0\r\n");
	}
	iic_status = IicPsInit(&Iic, IIC_DEVICE_ID_1);
	if(iic_status != XST_SUCCESS)
	{
		//handle the issue
		xil_printf("fix the Iic device 1\r\n");
	}

	//*******************Receive and Process Packets **********************//
	Xil_Out32 (XPAR_AXI_GPIO_0_BASEADDR, 0);	//baseline integration time	//subtract 38 from each int
	Xil_Out32 (XPAR_AXI_GPIO_1_BASEADDR, 35);	//short
	Xil_Out32 (XPAR_AXI_GPIO_2_BASEADDR, 131);	//long
	Xil_Out32 (XPAR_AXI_GPIO_3_BASEADDR, 1513);	//full
	Xil_Out32 (XPAR_AXI_GPIO_4_BASEADDR, 0);	//TEC stuff, 0 turns things off
	Xil_Out32 (XPAR_AXI_GPIO_5_BASEADDR, 0);	//TEC stuff
	Xil_Out32 (XPAR_AXI_GPIO_6_BASEADDR, 0);	//enable the system, allows data
	Xil_Out32 (XPAR_AXI_GPIO_7_BASEADDR, 0);	//enable 5V to sensor head
	Xil_Out32 (XPAR_AXI_GPIO_10_BASEADDR, 8500);	//threshold, max of 2^14 (16384)
	Xil_Out32 (XPAR_AXI_GPIO_16_BASEADDR, 16384);	//master-slave frame size
	Xil_Out32 (XPAR_AXI_GPIO_17_BASEADDR, 1);	//master-slave enable
	Xil_Out32(XPAR_AXI_GPIO_18_BASEADDR, 0);	//capture module enable

	//*******************Setup the UART **********************//
	status = 0;
	int LoopCount = 0;
	XUartPs Uart_PS;	// instance of UART

	XUartPs_Config *Config = XUartPs_LookupConfig(UART_DEVICEID);
	if (Config == NULL) { return 1;}
	status = XUartPs_CfgInitialize(&Uart_PS, Config, Config->BaseAddress);
	if (status != 0){ xil_printf("XUartPS did not CfgInit properly.\n");	}

	/* Conduct a Selftest for the UART */
	status = XUartPs_SelfTest(&Uart_PS);
	if (status != 0) { xil_printf("XUartPS failed self test.\n"); }			//handle error checks here better

	/* Set to normal mode. */
	XUartPs_SetOperMode(&Uart_PS, XUARTPS_OPER_MODE_NORMAL);
	while (XUartPs_IsSending(&Uart_PS)) {
		LoopCount++;
	}
	// *********** Mount SD Card ****************//
	/* FAT File System Variables */
	FATFS fatfs[2];
	int sd_status = 0;

	sd_status = MountSDCards( fatfs );
	if(sd_status == CMD_SUCCESS)	//correct mounting
	{
		sd_status = InitLogFile0();	//create log file on SD0
		if(sd_status == CMD_FAILURE)
		{
			//handle a bad log file?
			xil_printf("SD0 failed to init\r\n");
		}
		sd_status = InitLogFile1();	//create log file on SD1
		if(sd_status == CMD_FAILURE)
		{
			//handle a bad log file?
			xil_printf("SD1 failed to init\r\n");
		}
	}
	else
	{
		xil_printf("SD0/1 failed to mount\r\n");
		//need to handle the SD card not reading
		//do we try each one separately then set a flag?
		sd_status = MountSD0(fatfs);
		if(sd_status == CMD_SUCCESS)
		{
			//SD0 is not the problem
		}
		else
		{
			//SD0 is the problem
			//set a flag to indicate to only use SD1?
			xil_printf("SD0 failed to mount\r\n");
		}
		sd_status = MountSD1(fatfs);
		if(sd_status == CMD_SUCCESS)
		{
			//SD1 is not the problem
		}
		else
		{
			//SD1 is the problem
			//set a flag to indicate to only use SD0?
			xil_printf("SD1 failed to mount\r\n");
		}
	}
	// *********** Initialize Mini-NS System Parameters ****************//
	InitConfig();

	// *********** Initialize Local Variables ****************//

	//start timing
	InitStartTime();

	// Initialize buffers
	char RecvBuffer[100] = "";	//user input buffer
	int done = 0;				//local status variable for keeping track of progress within loops
	int DAQ_run_number = 0;		//run number value for file names, tracks the number of runs per POR
	int	menusel = 99999;		//case select variable for polling
	FIL *cpsDataFile;
	FIL *evtDataFile;
	// ******************* APPLICATION LOOP *******************//

	//This loop will continue forever and the program won't leave it
	//This loop checks for input from the user, then checks to see if it's time to report SOH
	//If input is received, then it reads the input for correctness
	// if input is a valid MNS command, the system processes the command and reacts
	// if not, then the system rejects the command and issues a failure packet
	//After checking for input, the clock is checked to see if it is time to report SOH
	//When it is time, reports a full CCSDS SOH packet

	while(1){	//OUTER LEVEL 3 TESTING LOOP
		while(1){
			//resetting this value every time is (potentially) critical
			//resetting this ensures we don't re-use a command a second time (erroneously)
			menusel = 99999;
			menusel = ReadCommandType(RecvBuffer, &Uart_PS);	//Check for user input

			if ( menusel >= -1 && menusel <= 15 )	//let all input in, including errors, so we can report them
			{
				//we found a valid LUNAH command or input was bad (-1)
				//log the command issued, unless it is an error
				if(menusel != -1)
					LogFileWrite( GetLastCommand(), GetLastCommandSize() );
				break;	//leave the inner loop and execute the commanded function
			}
			//check to see if it is time to report SOH information, 1 Hz
			CheckForSOH(&Iic, Uart_PS);
		}//END TEMP ASU TESTING LOOP

		//MAIN MENU OF FUNCTIONS
		switch (menusel) { // Switch-Case Menu Select
		case -1:
			//we found an invalid command
			//Report CMD_FAILURE
			reportFailure(Uart_PS);
			break;
		case DAQ_CMD:
			//set processed data mode
			Xil_Out32(XPAR_AXI_GPIO_14_BASEADDR, 4);
			//turn on the system (not the ADC)
			Xil_Out32 (XPAR_AXI_GPIO_7_BASEADDR, 1);	//enable 5V to analog board
			//set all the configuration parameters
			status = ApplyDAQConfig(&Iic);
			if(status != CMD_SUCCESS)
			{
				//TODO: more error checking
			}
			//prepare the status variables
			done = 0;	//not done yet
			CPSInit();	//reset neutron counts for the run
			status = CMD_SUCCESS;	//reset the variable so that we jump into the loop
			/* Create the file names we will use for this run:
			 * Check if the filename given is unique
			 * if the filename is unique, then we will go through these functions once
			 * if not, then we will loop until a unique name is found */
			while(status == CMD_SUCCESS)
			{
				//only report a packet when the file has been successfully changed and did not exist already?
				++DAQ_run_number;	//initialized to 0, the first run will increment to 1
				SetFileName(GetIntParam(1), DAQ_run_number, 0);	//creates a file name of IDNum_runNum_type.bin
				//check that the file name(s) do not already exist on the SD card...we do not want to append existing files
				status = DoesFileExist();
				//returns FALSE if file does NOT exist
				//returns TRUE if file does exist
				//we need the file to be unique, so FALSE is a positive result,
				// if we get TRUE(CMD_SUCCESS), we need to keep looping
				//when status is FALSE(CMD_FAILURE), we send a packet to report the file name
				if(status == CMD_FAILURE)
				{
					reportSuccess(Uart_PS, 1);
					//create the files before polling for user input, leave them open
					//This also fills in the data header, as much as we know
					status = CreateDAQFiles();
					if(status == CMD_SUCCESS)
						break;
				}
				CheckForSOH(&Iic, Uart_PS);
			}
			while(done != 1)
			{
				status = ReadCommandType(RecvBuffer, &Uart_PS);	//Check for user input
				//see if we got anything meaningful //we'll accept any valid command
				if ( status >= -1 && status <= 23 )
				{
					if(status != -1)
						LogFileWrite( GetLastCommand(), GetLastCommandSize() );
					//if no good input is found, silently ignore the input
					switch(status)
					{
					case -1:
						done = 0;
						reportFailure(Uart_PS);
						break;
					case READ_TMP_CMD:
						done = 0;
						status = report_SOH(&Iic, GetLocalTime(), GetNeutronTotal(), Uart_PS, READ_TMP_CMD);
						if(status == CMD_FAILURE)
							reportFailure(Uart_PS);
						break;
					case BREAK_CMD:
						done = 1;
						reportSuccess(Uart_PS, 0);
						break;
					case START_CMD:
						//TODO: pass in the FIL pointers
						status = DataAcquisition(&Iic, Uart_PS, RecvBuffer, GetIntParam(1));
						//we will return in three ways:
						// time out (1) = success
						// END (2)		= success
						// BREAK (0)	= failure
						switch(status)
						{
						case 0:
							LogFileWrite( GetLastCommand(), GetLastCommandSize() );
							reportFailure(Uart_PS);
							break;
						case 1:
							reportSuccess(Uart_PS, 0);
							break;
						case 2:
							LogFileWrite( GetLastCommand(), GetLastCommandSize() );
							reportSuccess(Uart_PS, 0);
							break;
						default:
							reportFailure(Uart_PS);
							break;
						}
						done = 1;
						break;
					default:
						//got something outside of these commands
						done = 0;	//continue looping //not done
						//I want to report failure and also include something like daq loop in the string
						// that way a person controlling the MNS would see the reason all their commands
						// are failing. If the loop is here, the command entered was recognized and relevant
						// but we can't do it because this loop doesn't have access.
						reportFailure(Uart_PS);
						break;
					}
				}
				//check to see if it is time to report SOH information, 1 Hz
				CheckForSOH(&Iic, Uart_PS);
			}//END OF WHILE DONE != 0

			cpsDataFile = GetCPSFilePointer();	//check the FIL pointers created by DAQ are closed safely
			if (cpsDataFile->fs != NULL)
				f_close(cpsDataFile);
			evtDataFile = GetEVTFilePointer();
			if (evtDataFile->fs != NULL)
				f_close(evtDataFile);

			//change directories back to the root directory
			f_res = f_chdir("0:/");
			if(f_res != FR_OK)
			{
				//TODO: handle change directory fail
			}
			//data acquisition has been completed, wrap up anything not handled by the DAQ function
			//turn off the active components //TODO: add this to the Flow Diagram so people know that it's off
			Xil_Out32(XPAR_AXI_GPIO_18_BASEADDR, 0);	//disable capture module
			Xil_Out32(XPAR_AXI_GPIO_6_BASEADDR, 0);		//disable ADC
			Xil_Out32 (XPAR_AXI_GPIO_7_BASEADDR, 0);	//disable 5V to analog board

			break;
		case WF_CMD:
			Xil_Out32(XPAR_AXI_GPIO_18_BASEADDR, 1);	//enable capture module
			//set processed data mode
			if(GetIntParam(1) == 0)
				Xil_Out32(XPAR_AXI_GPIO_14_BASEADDR, GetIntParam(1));	//get AA wfs = 0 //TRG wfs = 3
			else
			{
				reportFailure(Uart_PS);	//report failure
				break;
			}
//			Xil_Out32(XPAR_AXI_GPIO_14_BASEADDR, 3);	//get TRG waveforms
			Xil_Out32(XPAR_AXI_GPIO_6_BASEADDR, 1);		//enable ADC
			Xil_Out32 (XPAR_AXI_GPIO_7_BASEADDR, 1);	//enable 5V to analog board

			status = ApplyDAQConfig(&Iic);
			f_res = f_open(&WFData, "wfAA01.bin", FA_WRITE|FA_OPEN_ALWAYS);
			if(f_res != FR_OK)
				xil_printf("1 open file fail WF\n");
			f_res = f_lseek(&WFData, file_size(&WFData));
			if(f_res != FR_OK)
				xil_printf("2 lseek fail WF\n");


			memset(wf_data, '\0', sizeof(unsigned int)*DATA_BUFFER_SIZE);
			ClearBRAMBuffers();
			while(done != 1)
			{
				valid_data = Xil_In32 (XPAR_AXI_GPIO_11_BASEADDR);
				if(valid_data == 1)
				{
					//init/start MUX to transfer data between integrator modules and the DMA
					Xil_Out32 (XPAR_AXI_GPIO_15_BASEADDR, 1);
					Xil_Out32 (XPAR_AXI_DMA_0_BASEADDR + 0x48, 0xa000000);
					Xil_Out32 (XPAR_AXI_DMA_0_BASEADDR + 0x58 , 65536);
					usleep(54);
					//TODO: need to check a shared variable within the interrupt handler and this function
					// to see if the transfer is completed
					//This check would replace the sleep statement.

					Xil_Out32 (XPAR_AXI_GPIO_15_BASEADDR, 0);

					ClearBRAMBuffers();

					Xil_DCacheInvalidateRange(0xa0000000, 65536);

					array_index = 0;
					dram_addr = dram_base;
					while(dram_addr < dram_ceiling)
					{
						wf_data[array_index] = Xil_In32(dram_addr);
						dram_addr += 4;
						array_index++;
					}

					numWFs++;
					//have the WF, save to file //WFData
					f_res = f_write(&WFData, wf_data, DATA_BUFFER_SIZE, &numBytesWritten);
					if(f_res != FR_OK)
						xil_printf("3 write fail WF\n");
				}

				//check for input
				CheckForSOH(&Iic, Uart_PS);
				if(numWFs > GetIntParam(2))
					done = 1;
			}
			f_close(&WFData);
			Xil_Out32(XPAR_AXI_GPIO_6_BASEADDR, 0);		//enable ADC
			Xil_Out32 (XPAR_AXI_GPIO_7_BASEADDR, 0);	//enable 5V to analog board
			reportSuccess(Uart_PS, 0);
			break;
		case READ_TMP_CMD:
			//tell the report_SOH function that we want a temp packet
			status = report_SOH(&Iic, GetLocalTime(), GetNeutronTotal(), Uart_PS, READ_TMP_CMD);
			if(status == CMD_FAILURE)
				reportFailure(Uart_PS);
			break;
		case GETSTAT_CMD: //Push an SOH packet to the bus
			//instead of checking for SOH, just push one SOH packet out because it was requested
			status = report_SOH(&Iic, GetLocalTime(), GetNeutronTotal(), Uart_PS, GETSTAT_CMD);
			if(status == CMD_FAILURE)
				reportFailure(Uart_PS);
			break;
		case DISABLE_ACT_CMD:
			//disable the components
			Xil_Out32(XPAR_AXI_GPIO_6_BASEADDR, 0);		//disable 3.3V
			Xil_Out32(XPAR_AXI_GPIO_7_BASEADDR, 0);		//disable 5v to Analog board
			//No SW check on success/failure
			reportSuccess(Uart_PS, 0);
			break;
		case ENABLE_ACT_CMD:
			//enable the active components
			Xil_Out32(XPAR_AXI_GPIO_6_BASEADDR, 1);		//enable ADC
			Xil_Out32(XPAR_AXI_GPIO_7_BASEADDR, 1);		//enable 5V to analog board
			//No SW check on success/failure
			reportSuccess(Uart_PS, 0);
			break;
		case TX_CMD:
			//transfer any file on the SD card
//			status = TransferSDFile( Uart_PS, TX_CMD );
			if(status == 0)
				reportSuccess(Uart_PS, 0);
			else
				reportFailure(Uart_PS);
			break;
		case DEL_CMD:
			//delete a file from the SD card

			//xil_printf("received DEL command\r\n");
			break;
		case LS_CMD:
			//transfer the names and sizes of the files on the SD card
			//xil_printf("received LS_FILES command\r\n");
			break;
		case TXLOG_CMD:
			//transfer the system log file
			//Transfer options:
			// 0 = data product file
			// 1 = Log File
			// 2 = Config file
//			status = TransferSDFile( Uart_PS, TXLOG_CMD );
			if(status == 0)
				reportSuccess(Uart_PS, 0);
			else
				reportFailure(Uart_PS);
			break;
		case CONF_CMD:
			//transfer the configuration file
			//Transfer options:
			// 0 = data product file
			// 1 = Log File
			// 2 = Config file
//			status = TransferSDFile( Uart_PS, CONF_CMD );
			if(status == 0)
				reportSuccess(Uart_PS, 0);
			else
				reportFailure(Uart_PS);
			break;
		case TRG_CMD:
			//set the trigger threshold
			status = SetTriggerThreshold( GetIntParam(1) );
			//Determine SUCCESS or FAILURE
			if(status)
				reportSuccess(Uart_PS, 0);
			else
				reportFailure(Uart_PS);
			break;
		case ECAL_CMD:
			//set the energy calibration parameters
			status = SetEnergyCalParam( GetFloatParam(1), GetFloatParam(2) );
			//Determine SUCCESS or FAILURE
			if(status)
				reportSuccess(Uart_PS, 0);
			else
				reportFailure(Uart_PS);
			break;
		case NGATES_CMD:
			//set the neutron cuts
			status = SetNeutronCutGates(GetIntParam(1), GetIntParam(2), GetFloatParam(1), GetFloatParam(2), GetFloatParam(3), GetFloatParam(4) );
			//Determine SUCCESS or FAILURE
			if(status)
				reportSuccess(Uart_PS, 0);
			else
				reportFailure(Uart_PS);
			break;
		case HV_CMD:
			//set the PMT bias voltage for one or more PMTs
			//intParam1 = PMT ID
			//intParam2 = Bias Voltage (taps)
			status = SetHighVoltage(&Iic, GetIntParam(1), GetIntParam(2));
			//Determine SUCCESS or FAILURE
			if(status)
				reportSuccess(Uart_PS, 0);
			else
				reportFailure(Uart_PS);
			break;
		case INT_CMD:
			//set the integration times
			//intParam1 = Baseline integration time
			//intParam2 = Short integration time
			//intParam3 = Long integration time
			//intParam4 = Full integration time
			status = SetIntegrationTime(GetIntParam(1), GetIntParam(2), GetIntParam(3), GetIntParam(4));
			//Determine SUCCESS or FAILURE
			if(status)
				reportSuccess(Uart_PS, 0);
			else
				reportFailure(Uart_PS);
			break;
		case INPUT_OVERFLOW:
			//too much input
			//TODO: Handle this problem here and in ReadCommandType
			reportFailure(Uart_PS);
			break;
		default:
			//got a value for menusel we did not expect
			//valid things can come here: Break, End, Start
			//Report CMD_FAILURE
			break;
		}//END OF SWITCH/CASE (MAIN MENU OF FUNCTIONS)

		//check to see if it is time to report SOH information, 1 Hz
		//this may help with functions which take too long during their own loops
		CheckForSOH(&Iic, Uart_PS);
	}//END OF OUTER LEVEL 2 TESTING LOOP

    return 0;
}

//////////////////////////// InitializeAXIDma////////////////////////////////
// Sets up the AXI DMA
int InitializeAXIDma(void) {
	u32 tmpVal_0 = 0;

	tmpVal_0 = Xil_In32(XPAR_AXI_DMA_0_BASEADDR + 0x30);

	tmpVal_0 = tmpVal_0 | 0x1001; //<allow DMA to produce interrupts> 0 0 <run/stop>

	Xil_Out32 (XPAR_AXI_DMA_0_BASEADDR + 0x30, tmpVal_0);
	Xil_In32(XPAR_AXI_DMA_0_BASEADDR + 0x30);

	tmpVal_0 = Xil_In32(XPAR_AXI_DMA_0_BASEADDR + 0x30);

	return 0;
}
//////////////////////////// InitializeAXIDma////////////////////////////////

//////////////////////////// InitializeInterruptSystem////////////////////////////////
int InitializeInterruptSystem(u16 deviceID) {
	int Status;

	GicConfig = XScuGic_LookupConfig (deviceID);
	if(NULL == GicConfig) {

		return XST_FAILURE;
	}

	Status = XScuGic_CfgInitialize(&InterruptController, GicConfig, GicConfig->CpuBaseAddress);
	if(Status != XST_SUCCESS) {
		return XST_FAILURE;

	}

	Status = SetUpInterruptSystem(&InterruptController);
	if(Status != XST_SUCCESS) {
		return XST_FAILURE;

	}

	Status = XScuGic_Connect (&InterruptController,
			XPAR_FABRIC_AXI_DMA_0_S2MM_INTROUT_INTR,
			(Xil_ExceptionHandler) InterruptHandler, NULL);
	if(Status != XST_SUCCESS) {
		return XST_FAILURE;

	}

	XScuGic_Enable(&InterruptController, XPAR_FABRIC_AXI_DMA_0_S2MM_INTROUT_INTR );

	return XST_SUCCESS;

}
//////////////////////////// InitializeInterruptSystem////////////////////////////////


//////////////////////////// Interrupt Handler////////////////////////////////
/*
 * This function is called when the system issues an interrupt. Currently, an interrupt is
 *  issued when the DMA transfer is completed. When that happens, we need to clear a bit
 *  which is set by the completion of that transfer so that we may acknowledge and clear
 *  the interrupt. If that does not happen, the system will hang.
 *
 *  We have the DMA in Direct Register Mode with Interrupt on Complete enabled.
 *  This means that an interrupt is generated on the completion of a transfer.
 *  The interrupt handler writes to the DMA status register to clear the
 */
void InterruptHandler (void ) {
	u32 tmpValue = 0;
	tmpValue = Xil_In32(XPAR_AXI_DMA_0_BASEADDR + 0x34);	//Read the DMA status register
	tmpValue |= 0x1000;							//bit 12 is write-to-clear, this acknowledges the interrupt generated by IOC
	Xil_Out32 (XPAR_AXI_DMA_0_BASEADDR + 0x34, tmpValue);	//write to the DMA status register


}
//////////////////////////// Interrupt Handler////////////////////////////////


//////////////////////////// SetUp Interrupt System////////////////////////////////
int SetUpInterruptSystem(XScuGic *XScuGicInstancePtr) {
	Xil_ExceptionRegisterHandler(XIL_EXCEPTION_ID_INT, (Xil_ExceptionHandler)XScuGic_InterruptHandler, XScuGicInstancePtr);
	Xil_ExceptionEnable();
	return XST_SUCCESS;

}
//////////////////////////// SetUp Interrupt System////////////////////////////////
