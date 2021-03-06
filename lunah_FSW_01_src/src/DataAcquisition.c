/*
 * DataAcquisition.c
 *
 *  Created on: Dec 4, 2018
 *      Author: IRDLab
 */

#include "DataAcquisition.h"

//FILE SCOPE VARIABLES
static char current_run_folder[100] = "";
static char current_filename_EVT[100] = "";
static char current_filename_CPS[100] = "";
static char current_filename_2DH_1[100] = "";
static char current_filename_2DH_2[100] = "";
static char current_filename_2DH_3[100] = "";
static char current_filename_2DH_4[100] = "";
static char current_filename_WAV[100] = "";
static unsigned int daq_run_id_number = 0;
static unsigned int daq_run_run_number = 0;
static unsigned int daq_run_set_number = 0;

static FIL m_EVT_file;
static FIL m_CPS_file;
static FIL m_2DH_file;

//Data buffer which can hold 4096*4 integers, each buffer holds 512 8-integer events, x4 for four buffers
static unsigned int data_array[DATA_BUFFER_SIZE * 4];

static DATA_FILE_HEADER_TYPE file_header_to_write;	//not declaring this above so we can make it static
static DATA_FILE_FOOTER_TYPE file_footer_to_write;
static DATA_FILE_SECONDARY_HEADER_TYPE file_secondary_header_to_write;

/*
 * Getter function to get the folder name for the DAQ run which has been started.
 * We need to let the user know what the internally tracked value of the RUN number is
 *  so that they can request a specific run's files. The way we can do that is we
 *  return the ID number and run number used at the beginning of a run in a SUCCESS packet.
 *
 *  @param	None
 *
 *  @return	(char *)Pointer to the name of the folder that is being used for the run.
 *  				Note that the name is appended with the ROOT directory (0:)
 */
char *GetFolderName( void )
{
	return current_run_folder;
}

/*
 * Getter function for the size of the filenames which are assembled by the system
 * This function doesn't need a file type because the filenames were designed to
 * have the same length.
 *
 * @param	None
 *
 * @return	The number of bytes in the length of the filename string
 */
int GetFileNameSize( void )
{
	return (int)strlen(current_filename_EVT);
}

/* Getter function for the current data acquisition filename string
 *
 * Each function and file name will have to be assembled from this string
 * which will be composed of the following parts:
 *
 * IDNum_RunNum_TYPE.bin
 *
 * ID Number 	= user input value which is the first unique value
 * Run Number 	= Mini-NS tracked value which counts how many runs have been made since the last POR
 * TYPE			= EVTS	-> event-by-event data product
 * 				= CPS 	-> counts-per-second data product
 * 				= WAV	-> waveform data product
 * 				= 2DH	-> two-dimensional histogram data product
 *
 * @param	None
 *
 * @return	Pointer to the buffer holding the filename.
 *
 */
char *GetFileName( int file_type )
{
	char * current_filename;

	switch(file_type)
	{
	case DATA_TYPE_EVT:
		current_filename = current_filename_EVT;
		break;
	case DATA_TYPE_WAV:
		current_filename = current_filename_WAV;
		break;
	case DATA_TYPE_CPS:
		current_filename = current_filename_CPS;
		break;
	case DATA_TYPE_2DH_1:
		current_filename = current_filename_2DH_1;
		break;
	case DATA_TYPE_2DH_2:
		current_filename = current_filename_2DH_2;
		break;
	case DATA_TYPE_2DH_3:
		current_filename = current_filename_2DH_3;
		break;
	case DATA_TYPE_2DH_4:
		current_filename = current_filename_2DH_4;
		break;
	default:
		current_filename = NULL;
		break;
	}

	return current_filename;
}

/*
 * Getter function for the current DAQ run ID number.
 * This value is provided by the user and stored.
 *
 * @param	None
 *
 * @return	The ID number provided to the MNS_DAQ command from the most recent/current run
 */
unsigned int GetDAQRunIDNumber( void )
{
	return daq_run_id_number;
}

/*
 * Getter function for the current DAQ run RUN number.
 * This value is calculated by the system and stored for internal use and in creating
 *  unique filenames for the data products being stored to the SD card.
 * This value is zeroed out each time the Mini-NS power cycles.
 * This value is incremented each time the Mini-NS begins a new DAQ run.
 *
 * @param	None
 *
 * @return	The number of DAQ runs since the system power cycled last, the RUN number
 */
unsigned int GetDAQRunRUNNumber( void )
{
	return daq_run_run_number;
}

/*
 * Getter function for the current DAQ run SET number.
 * As the Mini-NS is collecting data, the EVTS data product files will become quite large. To
 *  mitigate the problem of having to downlink very large files with limited bandwidth, the
 *  system will close a file which exceeds ~1MB in size. It will then open a file with the same
 *  filename, except the SET number will be incremented by 1. It will continue recording data in
 *  that file for the run.
 * This value is zeroed out for each DAQ run.
 * This value is incremented each time the Mini-NS closes a data product file to start a new one.
 *
 * @param	None
 *
 * @return	The number of DAQ runs since the system power cycled last, the RUN number
 */
unsigned int GetDAQRunSETNumber( void )
{
	return daq_run_set_number;
}

/*
 * Create the file names for the various data products which will be created when we start a DAQ run.
 * This will create the file name strings for CPS, EVT, and 2DH data products. There is one file for
 *  both CPS and EVT data, but 2DH has 4 four files which are created.
 *
 *  @param	(integer) ID number for the run, this comes from the spacecraft
 *  @param	(integer) Run number, is internally generated and tracked from run-to-run
 *  @param	(integer) Set number, this will get incremented each time that the MNS changes file mid-DAQ
 *
 *  @return	(integer) CMD_SUCCESS/CMD_FAILURE
 */
int SetFileName( int ID_number, int run_number, int set_number )
{
	int status = CMD_SUCCESS;
	int bytes_written = 0;

	//save the values so we can access them later, we can put them in the file headers
	daq_run_id_number = ID_number;
	daq_run_run_number = run_number;
	daq_run_set_number = set_number;

	//create a folder for the run //0:/I0000_R0001/
	bytes_written = snprintf(current_run_folder, 100, "0:/I%04d_R%04d", ID_number, run_number);
	if(bytes_written == 0 || bytes_written != ROOT_DIR_NAME_SIZE + FOLDER_NAME_SIZE)
		status = CMD_FAILURE;

	bytes_written = snprintf(current_filename_EVT, 100, "evt_S%04d.bin", set_number);
	if(bytes_written == 0)
		status = CMD_FAILURE;
	bytes_written = snprintf(current_filename_CPS, 100, "cps_S%04d.bin", set_number);
	if(bytes_written == 0)
		status = CMD_FAILURE;
	bytes_written = snprintf(current_filename_2DH_1, 100, "2d1_S%04d.bin", set_number);
	if(bytes_written == 0)
		status = CMD_FAILURE;
	bytes_written = snprintf(current_filename_2DH_2, 100, "2d2_S%04d.bin", set_number);
	if(bytes_written == 0)
		status = CMD_FAILURE;
	bytes_written = snprintf(current_filename_2DH_3, 100, "2d3_S%04d.bin", set_number);
	if(bytes_written == 0)
		status = CMD_FAILURE;
	bytes_written = snprintf(current_filename_2DH_4, 100, "2d4_S%04d.bin", set_number);
	if(bytes_written == 0)
		status = CMD_FAILURE;

	return status;
}

/*
 * Check if the folder for the run exists already or not
 * 		returns FALSE if folder does NOT exist
 *		returns TRUE if folder does exist
 *	This function tells our loop in main whether or not to generate another folder
 *	 to put the current run into. If the folder already exists, then we'll try the
 *	 next run number after the current one. We'll loop until we get to a combination
 *	 of the ID number requested by the user and the run number which is unique.
 *
 *	 @param		none
 *
 *	 @return	CMD_FAILURE if the folder does not exist
 *	 			CMD_SUCCESS if the folder does exist
 */
int DoesFileExist( void )
{
	int status = CMD_SUCCESS;
	FILINFO fno;		//file info structure
	FRESULT ffs_res;	//FAT file system return type

	//check the SD card for the folder, then the files:
	ffs_res = f_stat(current_run_folder, &fno);
	if(ffs_res == FR_NO_FILE)
		status = CMD_FAILURE;
	else if(ffs_res == FR_NO_PATH)
		status = CMD_FAILURE;
	else if(ffs_res == FR_DENIED)
		status = CMD_FAILURE;

	return status;
}

/* Creates the data acquisition files for the run requested by the DAQ command.
 * Uses the filenames which are created from the ID number sent with the DAQ
 *  command to open and write the header into the files.
 * The FIL pointers are left open by this function intentionally so that DAQ doesn't
 *  have to spend the time opening them.
 *NOTE: Upon successful completion, this function leaves the SD card in the directory
 *		 which was created. The user needs to change directories before doing other
 *		 file operations.
 *
 * @param	None
 *
 * @return	Success/failure based on how we finished the run:
 * 			BREAK (0)	 = failure
 * 			Time Out (1) = success
 * 			END (2)		 = success
 */
int CreateDAQFiles( void )
{
	char * file_to_open = NULL;
	int iter = 0;
	int status = CMD_SUCCESS;
	uint NumBytesWr;
	FIL *DAQ_file = NULL;
	FRESULT ffs_res;

	//a blank struct to write into the CPS file //reserves space for later
	DATA_FILE_SECONDARY_HEADER_TYPE blank_file_secondary_header_to_write = {};

	//gather the header information
	file_header_to_write.configBuff = *GetConfigBuffer();		//dereference to copy the struct into our local struct
	//	TODO: check the return was not NULL?
	file_header_to_write.IDNum = daq_run_id_number;
	file_header_to_write.RunNum = daq_run_run_number;
	file_header_to_write.SetNum = daq_run_set_number;
	file_header_to_write.TempCorrectionSetNum = 1;		//will have to get this from somewhere
	file_header_to_write.EventIDFF = 0xFF;

	//open the run folder so we can create the files there
	ffs_res = f_mkdir(current_run_folder);
	if(ffs_res != FR_OK)
	{
		//TODO: handle folder creation fail
	}
	else
	{
		ffs_res = f_chdir(current_run_folder);
		if(ffs_res != FR_OK)
		{
			//TODO: handle change directory fail
		}
	}

	for(iter = 0; iter < 6; iter++)
	{
		switch(iter)
		{
		case 0:
			file_to_open = current_filename_EVT;
			file_header_to_write.FileTypeAPID = 0x77;
			DAQ_file = &m_EVT_file;
			break;
		case 1:
			file_to_open = current_filename_CPS;
			file_header_to_write.FileTypeAPID = 0x55;
			DAQ_file = &m_CPS_file;
			break;
		case 2:
			file_to_open = current_filename_2DH_1;
			file_header_to_write.FileTypeAPID = 0x88;
			DAQ_file = &m_2DH_file;
			break;
		case 3:
			file_to_open = current_filename_2DH_2;
			file_header_to_write.FileTypeAPID = 0x88;
			DAQ_file = &m_2DH_file;
			break;
		case 4:
			file_to_open = current_filename_2DH_3;
			file_header_to_write.FileTypeAPID = 0x88;
			DAQ_file = &m_2DH_file;
			break;
		case 5:
			file_to_open = current_filename_2DH_4;
			file_header_to_write.FileTypeAPID = 0x88;
			DAQ_file = &m_2DH_file;
			break;
		default:
			status = CMD_FAILURE;
			break;
		}

		//TODO: do we need to check to see if any of the FILs are NULL? //they should be automatically created when the program starts, but...good practice to check them
		ffs_res = f_open(DAQ_file, file_to_open, FA_OPEN_ALWAYS | FA_WRITE | FA_READ);
		if(ffs_res == FR_OK)
		{
			ffs_res = f_lseek(DAQ_file, 0);
			if(ffs_res == FR_OK)
			{
				ffs_res = f_write(DAQ_file, &file_header_to_write, sizeof(file_header_to_write), &NumBytesWr);
				if(ffs_res == FR_OK && NumBytesWr == sizeof(file_header_to_write))
				{
					if(iter == 1)	//this is for CPS files only
					{
						ffs_res = f_write(DAQ_file, &blank_file_secondary_header_to_write, sizeof(blank_file_secondary_header_to_write), &NumBytesWr);
						if(ffs_res == FR_OK)
							status = CMD_SUCCESS;
						else
							status = CMD_FAILURE;
					}
					if(iter < 2)	//sync the EVT, CPS files
					{
						ffs_res = f_sync(DAQ_file);
						if(ffs_res == FR_OK)
							status = CMD_SUCCESS;
						else
							status = CMD_FAILURE;
					}
					else
					{
						f_close(DAQ_file);
						status = CMD_SUCCESS;
					}
				}
				else
					status = CMD_FAILURE;
			}
			else
				status = CMD_FAILURE;
		}
		else
			status = CMD_FAILURE;
	}

	return status;
}


FIL *GetEVTFilePointer( void )
{
	return &m_EVT_file;
}

FIL *GetCPSFilePointer( void )
{
	return &m_CPS_file;
}

int WriteRealTime( unsigned long long int real_time )
{
	int status = CMD_SUCCESS;
//	uint NumBytesWr;
//	FRESULT F_RetVal;
//	FILINFO CnfFno;
//	FIL ConfigFile;
//	int RetVal = 0;
//	int ConfigSize = sizeof(ConfigBuff);
//
//	//take the config buffer and put it into each data product file
//	// check that data product file exists
//	if( f_stat( cConfigFile, &CnfFno) )	//f_stat returns non-zero (true) if no file exists, so open/create the file
//	{
//		F_RetVal = f_open(&ConfigFile, cConfigFile, FA_WRITE|FA_OPEN_ALWAYS);
//		if(F_RetVal == FR_OK)
//			F_RetVal = f_write(&ConfigFile, &ConfigBuff, ConfigSize, &NumBytesWr);
//		if(F_RetVal == FR_OK)
//			F_RetVal = f_close(&ConfigFile);
//	}
//	else // If the file exists, write it
//	{
//		F_RetVal = f_open(&ConfigFile, cConfigFile, FA_READ|FA_WRITE);	//open with read/write access
//		if(F_RetVal == FR_OK)
//			F_RetVal = f_lseek(&ConfigFile, 0);							//go to beginning of file
//		if(F_RetVal == FR_OK)
//			F_RetVal = f_write(&ConfigFile, &ConfigBuff, ConfigSize, &NumBytesWr);	//Write the ConfigBuff to config file
//		if(F_RetVal == FR_OK)
//			F_RetVal = f_close(&ConfigFile);							//close the file
//		}
//
//	RetVal = (int)F_RetVal;
//    return RetVal;

	return status;
}

//Clears the BRAM buffers
// I need to refresh myself as to why this is important
// All that I remember is that it's important to do before each DRAM transfer
//Resets which buffer we are reading from
// issuing this "clear" allows us to move to the next buffer to read from it
//Tells the FPGA, we are done with this buffer, read from the next one
void ClearBRAMBuffers( void )
{
	Xil_Out32(XPAR_AXI_GPIO_9_BASEADDR,1);
	usleep(1);
	Xil_Out32(XPAR_AXI_GPIO_9_BASEADDR,0);
}

/* What it's all about.
 * The main event.
 * This is where we interact with the FPGA to receive data,
 *  then process and save it. We are reporting SOH and various SUCCESS/FAILURE packets along
 *  the way.
 *
 * @param	(XIicPs *) Pointer to Iic instance (for read temp while in DAQ)
 *
 * @param	(XUartPs) UART instance for reporting SOH
 *
 * @param	(char *) Pointer to the receive buffer for getting user input
 *
 * @param	(integer) Time out value indicating when to break out of DAQ in minutes
 * 			Ex. 1 = loop for 1 minute
 *
 *
 * @return	Success/failure based on how we finished the run:
 * 			BREAK (0)	 = failure
 * 			Time Out (1) = success
 * 			END (2)		 = success
 */
int DataAcquisition( XIicPs * Iic, XUartPs Uart_PS, char * RecvBuffer, int time_out )
{
	//initialize variables
	int done = 0;				//local status variable for keeping track of progress within loops
	int status = CMD_SUCCESS;	//monitors the status of how we break out of DAQ
	int status_SOH = CMD_SUCCESS;	//local status variable
	int poll_val = 0;			//local polling status variable
	int valid_data = 0;			//goes high/low if there is valid data within the FPGA buffers
	int buff_num = 0;			//keep track of which buffer we are writing
	int m_buffers_written = 0;	//keep track of how many buffers are written, but not synced
	int array_index = 0;		//the index of our array which will hold data
	int dram_addr;				//the address in the DRAM we are reading from
	int dram_base = 0xA000000;	//where the buffer starts	//167,772,160
	int dram_ceiling = 0xA004000;	//where it ends			//167,788,544
	int m_run_time = time_out * 60;	//multiply minutes by 60 to get seconds
	int m_write_header = 1;		//write a file header the first time we use a file
	XTime m_run_start;			//timing variable
	XTime m_run_current_time;	//timing variable
	XTime_GetTime(&m_run_start);//record the "start" time to base a time out on
	char m_write_blank_space_buff[16384] = "";
	unsigned int bytes_written = 0;
	FRESULT f_res = FR_OK;
	GENERAL_EVENT_TYPE * evts_array = NULL;

	memset(&m_write_blank_space_buff, 186, 16384);

	ResetEVTsBuffer();
	ResetEVTsIterator();
	ClearBRAMBuffers();
	while(done != 1)
	{
		//check the FPGA to see if there is valid data in the buffers
		//bit set high (1) when there is at least one valid (full) buffer of data in the FPGA
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
			switch(buff_num)
			{
			case 0:
				while(dram_addr < dram_ceiling) //Does this need to be non-inclusive? Can we include the dram_ceiling? //TRYING THIS 2/26/19 GJS
				{
					data_array[array_index + DATA_BUFFER_SIZE * buff_num] = Xil_In32(dram_addr);
					dram_addr += 4;
					array_index++;
				}
				status_SOH = ProcessData( &data_array[DATA_BUFFER_SIZE * buff_num] );
				buff_num++;
				break;
			case 1:
				while(dram_addr < dram_ceiling)
				{
					data_array[array_index + DATA_BUFFER_SIZE * buff_num] = Xil_In32(dram_addr);
					dram_addr += 4;
					array_index++;
				}
				status_SOH = ProcessData( &data_array[DATA_BUFFER_SIZE * buff_num] );
				buff_num++;
				break;
			case 2:
				while(dram_addr < dram_ceiling)
				{
					data_array[array_index + DATA_BUFFER_SIZE * buff_num] = Xil_In32(dram_addr);
					dram_addr += 4;
					array_index++;
				}
				status_SOH = ProcessData( &data_array[DATA_BUFFER_SIZE * buff_num] );
				buff_num++;
				break;
			case 3:
				while(dram_addr < dram_ceiling)
				{
					data_array[array_index + DATA_BUFFER_SIZE * buff_num] = Xil_In32(dram_addr);
					dram_addr += 4;
					array_index++;
				}
				status_SOH = ProcessData( &data_array[DATA_BUFFER_SIZE * buff_num] );
				buff_num = 0;

				//check the file size and see if we need to change files
				if(m_EVT_file.fsize >= SIZE_1_MIB)
				{
					//prepare and write in footer for file here
					file_footer_to_write.digiTemp = GetDigiTemp();
//					m_spacecraft_real_time = GetRealTimeParam();
//					memcpy(&(file_footer_to_write.spacecraftRealTime[0]), &m_spacecraft_real_time, sizeof(m_spacecraft_real_time));
//					m_digi_temp = GetDigiTemp();
//					file_footer_to_write.digiTemp = (unsigned char)m_digi_temp;
					f_res = f_write(&m_EVT_file, &file_footer_to_write, sizeof(file_footer_to_write), &bytes_written);
					if(f_res != FR_OK || bytes_written != sizeof(file_footer_to_write))
						status = CMD_FAILURE;
					//then close the file, as we're done with it
					f_close(&m_EVT_file);
					//create the new file name (increment the set number)
					daq_run_set_number++; file_header_to_write.SetNum = daq_run_set_number;
					file_header_to_write.FileTypeAPID = 0x77;	//change back to EVTS
					bytes_written = snprintf(current_filename_EVT, 100, "evt_S%04d.bin", daq_run_set_number);
					if(bytes_written == 0)
						status = CMD_FAILURE;
					f_res = f_open(&m_EVT_file, current_filename_EVT, FA_OPEN_ALWAYS|FA_READ|FA_WRITE);
					if(f_res == FR_OK)
					{
						f_res = f_lseek(&m_EVT_file, 0);
						if(f_res != FR_OK)
							status = CMD_FAILURE;
						//write file header
						f_res = f_write(&m_EVT_file, &file_header_to_write, sizeof(file_header_to_write), &bytes_written);
						if(f_res != FR_OK || bytes_written != sizeof(file_header_to_write))
							status = CMD_FAILURE;
						//write secondary header
						f_res = f_write(&m_EVT_file, &file_secondary_header_to_write, sizeof(file_secondary_header_to_write), &bytes_written);
						if(f_res != FR_OK || bytes_written != sizeof(file_secondary_header_to_write))
							status = CMD_FAILURE;
						//write blank bytes up to Cluster edge (16384
						f_res = f_write(&m_EVT_file, m_write_blank_space_buff, 16384 - file_size(&m_EVT_file), &bytes_written);
						if(f_res != FR_OK || bytes_written != sizeof(file_secondary_header_to_write))
							status = CMD_FAILURE;
					}
					else
						status = CMD_FAILURE;
					//update any more data structures?
				}

				if(m_write_header == 1)
				{
					//get the first event and the real time
					file_secondary_header_to_write.RealTime = GetRealTimeParam();
					file_secondary_header_to_write.EventID1 = 0xFF;
					file_secondary_header_to_write.EventID2 = 0xEE;
					file_secondary_header_to_write.EventID3 = 0xDD;
					file_secondary_header_to_write.EventID4 = 0xCC;
					file_secondary_header_to_write.FirstEventTime = GetFirstEventTime();
					file_secondary_header_to_write.EventID5 = 0xCC;
					file_secondary_header_to_write.EventID6 = 0xDD;
					file_secondary_header_to_write.EventID7 = 0xEE;
					file_secondary_header_to_write.EventID8 = 0xFF;
					//write the secondary header into the EVT file
					f_res = f_write(&m_EVT_file, &file_secondary_header_to_write, sizeof(file_secondary_header_to_write), &bytes_written);
					if(f_res != FR_OK || bytes_written != sizeof(file_secondary_header_to_write))
					{
						//TODO: handle error checking the write
						xil_printf("10 error writing DAQ\n");
					}
					//write the secondary header into the CPS file
					f_res = f_lseek(&m_CPS_file, sizeof(file_header_to_write));	//want to move to the reserved space we allocated before the run
					//error check if we want
					f_res = f_write(&m_CPS_file, &file_secondary_header_to_write, sizeof(file_secondary_header_to_write), &bytes_written);
					if(f_res != FR_OK || bytes_written != sizeof(file_secondary_header_to_write))
					{
						//TODO: handle error checking the write
						xil_printf("10 error writing DAQ\n");
					}
					//forward the file pointer so we're at the top of the file again
					f_res = f_lseek(&m_CPS_file, file_size(&m_CPS_file));

					//also write the footer information that isn't going to change //this way we only do it once
					file_footer_to_write.eventID1 = 0xFF;
					file_footer_to_write.RealTime = GetRealTimeParam();
					file_footer_to_write.eventID2 = 0xFF;
					file_footer_to_write.eventID3 = 0xFF;
					file_footer_to_write.eventID4 = 0x45;
					file_footer_to_write.eventID5 = 0x4E;
					file_footer_to_write.eventID6 = 0x44;
					m_write_header = 0;	//turn off header writing
				}

				evts_array = GetEVTsBufferAddress();
				//TODO: check that the evts_array address is not NULL
				f_res = f_write(&m_EVT_file, evts_array, EVT_DATA_BUFF_SIZE, &bytes_written); //write the entire events buffer
				if(f_res != FR_OK || bytes_written != EVT_DATA_BUFF_SIZE)
				{
					//TODO: handle error checking the write here
					//now we need to check to make sure that there is a file open, if we get specific return values from f_write, need to check to see if we can open a file
					xil_printf("7 error writing DAQ\n");
				}
				m_buffers_written++;
				if(f_res == FR_OK && m_buffers_written == 4)
				{
					f_res = f_sync(&m_EVT_file);
					if(f_res != FR_OK)
					{
						//TODO: error check
						xil_printf("8 error syncing DAQ\n");
					}
					m_buffers_written = 0;	//reset
				}

				ResetEVTsBuffer();
				ResetEVTsIterator();
				break;
			default:
				//TODO: fill in the default behavior when we don't get the right buff_num
				//maybe try and figure out what buffer this should be?
				// could be worth trying to figure out what went wrong with buff_num and fix that
				//otherwise maybe just throw out everything and start over (zero out most stuff)
				// this could maybe get us back to a "good" state, or at least a known one?
				break;
			}
			valid_data = 0;	//reset

		}//END OF IF VALID DATA

		//check to see if it is time to report SOH information, 1 Hz
		CheckForSOH(Iic, Uart_PS);

		//check timeout condition //calculate run time?
		//TODO: check the time it takes to run this
		XTime_GetTime(&m_run_current_time);
		if(((m_run_current_time - m_run_start)/COUNTS_PER_SECOND) >= m_run_time)
		{
			file_footer_to_write.digiTemp = GetDigiTemp();
			f_res = f_write(&m_EVT_file, &file_footer_to_write, sizeof(file_footer_to_write), &bytes_written);
			if(f_res != FR_OK || bytes_written != sizeof(file_footer_to_write))
				status = CMD_FAILURE;
			f_res = f_write(&m_CPS_file, &file_footer_to_write, sizeof(file_footer_to_write), &bytes_written);
			if(f_res != FR_OK || bytes_written != sizeof(file_footer_to_write))
				status = CMD_FAILURE;
			status = DAQ_TIME_OUT;
			done = 1;
		}

		poll_val = ReadCommandType(RecvBuffer, &Uart_PS);
		switch(poll_val)
		{
		case -1:
			//this is bad input or an error in input
			//no real need for a case if we aren't handling it
			//just leave this to default
			break;
		case READ_TMP_CMD:
			status_SOH = report_SOH(Iic, GetLocalTime(), GetNeutronTotal(), Uart_PS, READ_TMP_CMD);
			if(status_SOH == CMD_FAILURE)
				reportFailure(Uart_PS);
			break;
		case BREAK_CMD:
			file_footer_to_write.digiTemp = GetDigiTemp();
			f_res = f_write(&m_EVT_file, &file_footer_to_write, sizeof(file_footer_to_write), &bytes_written);
			if(f_res != FR_OK || bytes_written != sizeof(file_footer_to_write))
				status = CMD_FAILURE;
			f_res = f_write(&m_CPS_file, &file_footer_to_write, sizeof(file_footer_to_write), &bytes_written);
			if(f_res != FR_OK || bytes_written != sizeof(file_footer_to_write))
				status = CMD_FAILURE;
			status = DAQ_BREAK;
			done = 1;
			break;
		case END_CMD:
			file_footer_to_write.RealTime = GetRealTimeParam();
			file_footer_to_write.digiTemp = GetDigiTemp();
			f_res = f_write(&m_EVT_file, &file_footer_to_write, sizeof(file_footer_to_write), &bytes_written);
			if(f_res != FR_OK || bytes_written != sizeof(file_footer_to_write))
				status = CMD_FAILURE;
			f_res = f_write(&m_CPS_file, &file_footer_to_write, sizeof(file_footer_to_write), &bytes_written);
			if(f_res != FR_OK || bytes_written != sizeof(file_footer_to_write))
				status = CMD_FAILURE;
			status = DAQ_END;
			done = 1;
			break;
		default:
			break;
		}
	}//END OF WHILE DONE != 1

	//here is where we should transfer the CPS, 2DH files?
	status_SOH = Save2DHToSD( 1 );
	if(status_SOH != CMD_SUCCESS)
		xil_printf("9 save sd 1 DAQ\n");
	status_SOH = Save2DHToSD( 2 );
	if(status_SOH != CMD_SUCCESS)
		xil_printf("10 save sd 2 DAQ\n");
	status_SOH = Save2DHToSD( 3 );
	if(status_SOH != CMD_SUCCESS)
		xil_printf("11 save sd 3 DAQ\n");
	status_SOH = Save2DHToSD( 4 );
	if(status_SOH != CMD_SUCCESS)
		xil_printf("12 save sd 4 DAQ\n");

	//cleanup operations
	//2DH files are closed by that module
	f_close(&m_EVT_file);
	f_close(&m_CPS_file);

	return status;
}
