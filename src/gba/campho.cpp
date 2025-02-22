// GB Enhanced+ Copyright Daniel Baxter 2022
// Licensed under the GPLv2
// See LICENSE.txt for full license text

// File : campho.cpp
// Date : December 28, 2022
// Description : Campho Advance
//
// Handles I/O for the Campho Advance (CAM-001)

#include "mmu.h"
#include "common/util.h"

/****** Resets Campho data structure ******/
void AGB_MMU::campho_reset()
{
	campho.data.clear();
	campho.g_stream.clear();

	campho.bank_index_lo = 0;
	campho.bank_index_hi = 0;
	campho.bank_id = 0;
	campho.rom_stat = 0xA00A;
	campho.rom_cnt = 0;
	campho.block_len = 0;
	campho.block_stat = 0;
	campho.bank_state = 0;
}

/****** Writes data to Campho I/O ******/
void AGB_MMU::write_campho(u32 address, u8 value)
{
	switch(address)
	{
		//Campho ROM Status
		case CAM_ROM_STAT:
			campho.rom_stat &= 0xFF00;
			campho.rom_stat |= value;
			break;

		case CAM_ROM_STAT+1:
			campho.rom_stat &= 0xFF;
			campho.rom_stat |= (value << 8);
			break;

		//Campho ROM Control
		case CAM_ROM_CNT:
			campho.rom_cnt &= 0xFF00;
			campho.rom_cnt |= value;
			break;

		case CAM_ROM_CNT+1:
			campho.rom_cnt &= 0xFF;
			campho.rom_cnt |= (value << 8);

			//Detect access to main Program ROM
			if(campho.rom_cnt == 0xA00A)
			{
				//Detect reading of first Program ROM bank
				if(!campho.block_stat)
				{
					campho.block_stat = 0xCC00;
					campho.bank_state = 1;
					campho.bank_index_lo = 0;
					campho_set_rom_bank(0xCC00, 0x00, 0);
				}

				//Increment Program ROM bank
				else
				{
					campho.block_stat++;
					campho.bank_state = 1;

					//Signal end of Program ROM banks
					if(campho.block_stat == 0xCC10)
					{
						campho.block_stat = 0xCD00;
						campho.bank_state = 1;
					}
				}
			}

			break;
	}
}

/****** Reads data from Campho I/O ******/
u8 AGB_MMU::read_campho(u32 address)
{
	u8 result = 0;

	switch(address)
	{
		//ROM Data Stream
		case CAM_ROM_DATA_LO:
			//Read Program ROM
			if(campho.bank_state)
			{
				//Return STAT LOW on first read
				if(campho.bank_state == 1)
				{
					result = (campho.block_stat & 0xFF);
				}

				//Return LEN LOW on second read
				//These 16-bit values should be fixed (0xFFA)
				else if(campho.bank_state == 2)
				{
					result = 0xFA;
				}
			}

			//Sequential ROM read
			else { result = read_campho_seq(address); }

			break;

		case CAM_ROM_DATA_LO+1:
			//Read Program ROM
			if(campho.bank_state)
			{
				//Return STAT HIGH on first read
				if(campho.bank_state == 1)
				{
					result = ((campho.block_stat >> 8) & 0xFF);
					campho.bank_state++;
				}

				//Return LEN HIGH on second read
				//These 16-bit values should be fixed (0xFFA)
				else if(campho.bank_state == 2)
				{
					result = 0x0F;
					campho.bank_state = 0;
				}
			}

			//Sequential ROM read
			else { result = read_campho_seq(address); }

			break;
		
		//Campho ROM Status
		case CAM_ROM_STAT:
		case CAM_ROM_STAT_B:
			result = (campho.rom_stat & 0xFF);
			break;

		case CAM_ROM_STAT+1:
		case CAM_ROM_STAT_B+1:
			result = ((campho.rom_stat >> 8) & 0xFF);
			break;

		//Campho ROM Control
		case CAM_ROM_CNT:
		case CAM_ROM_CNT_B:
			result = (campho.rom_cnt & 0xFF);
			break;

		case CAM_ROM_CNT+1:
		case CAM_ROM_CNT_B+1:
			result = ((campho.rom_cnt >> 8) & 0xFF);
			break;

		//Sequential ROM read
		default:
			result = read_campho_seq(address);
	}

	return result;
}

/****** Reads sequential ROM data from Campho ******/
u8 AGB_MMU::read_campho_seq(u32 address)
{
	u8 result = 0;

	//Read Low ROM Data Stream
	if(address < 0x8008000)
	{
		if((campho.bank_index_lo + 1) < campho.data.size())
		{
			result = campho.data[campho.bank_index_lo++];
		}
	}

	//Read High ROM Data Stream
	else
	{
		if((campho.bank_index_hi + 1) < campho.data.size())
		{
			result = campho.data[campho.bank_index_hi++];
		}
	}

	return result;
}

/****** Sets the absolute position within the Campho ROM for a bank's base address ******/
void AGB_MMU::campho_set_rom_bank(u32 bank, u32 address, bool set_hi_bank)
{
	//Search all known banks for a specific ID
	for(u32 x = 0; x < campho.mapped_bank_id.size(); x++)
	{
		//Match bank ID and base address
		if((campho.mapped_bank_id[x] == bank) && (campho.mapped_bank_addr[x] == address))
		{
			//Set High ROM bank
			if(set_hi_bank) { campho.bank_index_hi = campho.mapped_bank_pos[x]; }

			//Set Low ROM bank
			else { campho.bank_index_lo = campho.mapped_bank_pos[x]; }

			campho.block_len = campho.mapped_bank_len[x];
			return;
		}
	}

	std::cout<<"MMU::Warning - Campho Advance ROM position for Bank 0x" << bank << " @ 0x" << address << " was not found\n";
}

/****** Maps various ROM banks for the Campho ******/
void AGB_MMU::campho_map_rom_banks()
{
	//Currently it is unknown how much ROM data needs to be dumped from the Campho Advance
	//Also, the way the hardware maps data is not entirely understood and it's all over the place
	//Until more research is done, a basic PROVISIONAL mapper is implemented here
	//GBE+ will accept a ROM with a header that with the following data (MSB first!):

	//TOTAL HEADER LENGTH		1st 4 bytes
	//BANK ENTRIES			... rest of the header, see below for Bank Entry format

	//BANK ID			4 bytes
	//BANK BASE ADDRESS		4 bytes
	//BANK LENGTH IN BYTES		4 bytes

	//Grab ROM header length
	if(campho.data.size() < 4) { return; }

	u32 header_len = (campho.data[0] << 24) | (campho.data[1] << 16) | (campho.data[2] << 8) | campho.data[3];
	u32 rom_pos = 0;
	u32 bank_pos = header_len;

	campho.mapped_bank_id.clear();
	campho.mapped_bank_addr.clear();
	campho.mapped_bank_len.clear();
	campho.mapped_bank_pos.clear();

	//Grab bank entries and parse them accordingly
	for(u32 header_index = 0; header_index < header_len;)
	{
		rom_pos = header_index + 4;
		if((rom_pos + 12) >= campho.data.size()) { break; }

		u32 bank_id = (campho.data[rom_pos] << 24) | (campho.data[rom_pos+1] << 16) | (campho.data[rom_pos+2] << 8) | campho.data[rom_pos+3];
		u32 bank_addr = (campho.data[rom_pos+4] << 24) | (campho.data[rom_pos+5] << 16) | (campho.data[rom_pos+6] << 8) | campho.data[rom_pos+7];
		u32 bank_len = (campho.data[rom_pos+8] << 24) | (campho.data[rom_pos+9] << 16) | (campho.data[rom_pos+10] << 8) | campho.data[rom_pos+11];

		campho.mapped_bank_id.push_back(bank_id);
		campho.mapped_bank_addr.push_back(bank_addr);
		campho.mapped_bank_len.push_back(bank_len);
		campho.mapped_bank_pos.push_back(bank_pos);

		bank_pos += bank_len;
	}	 
}
