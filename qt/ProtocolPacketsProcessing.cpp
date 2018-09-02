/* 
* ProtocolPacketsProcessing.cpp
*
* Created: 09.07.2018 15:55:04
* Author: Mateusz Patyk
* Email: matpatyk@gmail.com
* Copyright © 2018 Mateusz Patyk. All rights reserved.
*/

#include "ProtocolPacketsProcessing.h"
#include "crc16.h"
#include <QDebug>

ProtocolPacketsProcessing::ProtocolPacketsProcessing()
{
    protocolBytes = new ProtocolBytesProcessing();
	
	this->circularBufferDataStartCursor = 0;
	this->circularBufferDataEndCursor = 0;
    for(uint8_t i = 0; i < PROTOCOL_PACKETS_MAX_NUMBER_OF_PACKETS; i++)
    {
        this->clearPacket(this->newPacketsTable[i]);
    }
	this->newPacketsNumber = 0;
}

void ProtocolPacketsProcessing::processInputBytes()
{
#if(PROTOCOL_PACKETS_DEBUG == 0x01)
    qDebug() << "Process Input Bytes started!";
#endif
	static sProtocolPacket processedPacket;
	static uint16_t processedPacketCRC;
	
	this->copyRxBufferToCircualBuffer();
	
	while(this->circularBufferDataStartCursor != this->circularBufferDataEndCursor)
	{
		static eProcessBytesStateMachine processByteMachine = PREAMBLE;
		switch(processByteMachine)
		{
			case PREAMBLE:
			{
				static ePreambleStateMachine preambleMachine = FIRST_SIGN;
				switch(preambleMachine)
				{
					case FIRST_SIGN:
					{
						if(this->circularBuffer[this->circularBufferDataStartCursor] == PROTOCOL_PACKETS_PREAMBLE_FIRST_SIGN)
						{
							processedPacketCRC = crc16update(processedPacketCRC, PROTOCOL_PACKETS_PREAMBLE_FIRST_SIGN);
							preambleMachine = SECOND_SIGN;
						}
					}
					break;
					case SECOND_SIGN:
					{
						if(this->circularBuffer[this->circularBufferDataStartCursor] == PROTOCOL_PACKETS_PREAMBLE_SECOND_SIGN)
						{
							processedPacketCRC = crc16update(processedPacketCRC, PROTOCOL_PACKETS_PREAMBLE_SECOND_SIGN);
							preambleMachine = FIRST_SIGN;
							processByteMachine = CODE;
						}
					}
					break;
					default:
					{  
						preambleMachine = FIRST_SIGN;
						processByteMachine = PREAMBLE;
                    #if(PROTOCOL_PACKETS_DEBUG == 0x01)
                        qDebug() << "Something goes very bad. Unknown state in preamble processing!";
                    #endif
					}
				}
			}
			break;
			case CODE:
			{
				processedPacket.commandCode = this->circularBuffer[this->circularBufferDataStartCursor];
				processedPacketCRC = crc16update(processedPacketCRC, processedPacket.commandCode);
				processByteMachine = LENGTH;
			}
			break;
			case LENGTH:
			{
				processedPacket.length = this->circularBuffer[this->circularBufferDataStartCursor];
				processedPacketCRC = crc16update(processedPacketCRC, processedPacket.length);
				
				if(processedPacket.length == PROTOCOL_PACKETS_PACKET_ZERO_LENGTH)
				{
					processByteMachine = CRC;
				}
				else
				{
					processByteMachine = DATA;
				}
			}
			break;
			case DATA:
			{
				static uint8_t lengthCounter = 0;
				lengthCounter++;
				
				if(lengthCounter <= processedPacket.length)
				{
					processedPacket.data[lengthCounter - 1] = this->circularBuffer[this->circularBufferDataStartCursor];
					processedPacketCRC = crc16update(processedPacketCRC, processedPacket.data[lengthCounter - 1]);
				}
				
				if(lengthCounter == processedPacket.length)
				{
					lengthCounter = 0;
					processByteMachine = CRC;
				}
			}
			break;
			case CRC:
			{
				static eCrcStateMachine crcMachine = CRC_LSB;
				
				switch(crcMachine)
				{
					case CRC_LSB:
					{
						processedPacket.crc = (uint16_t)this->circularBuffer[this->circularBufferDataStartCursor];
						crcMachine = CRC_MSB;
					}
					break;
					case CRC_MSB:
					{
						processedPacket.crc |= ((uint16_t)this->circularBuffer[this->circularBufferDataStartCursor] << 8);
						crcMachine = CRC_LSB;
						
						if(processedPacket.crc == processedPacketCRC)
						{
							processByteMachine = PREAMBLE;
							
							if(this->newPacketsNumber < PROTOCOL_PACKETS_MAX_NUMBER_OF_PACKETS)
							{
                                this->newPacketsTable[this->newPacketsNumber++] = processedPacket;
                                #if(PROTOCOL_PACKETS_DEBUG == 0x01)
                                    qDebug() << "Number of new packets in `queue` = " << this->newPacketsNumber;
                                #endif
							}
							else
							{
                                emit(message("Packet was overwritten! Make PROTOCOL_PACKETS_MAX_NUMBER_OF_PACKETS bigger."));
							}
							
							processedPacketCRC = 0;
						}
						else
						{
							processByteMachine = PREAMBLE;
							
							processedPacketCRC = 0;
                            emit(message("Somethink goes very bad. CRC is incorrect! Packet is corrupted!"));
						}
					}
					break;
					default:
					{
                    #if(PROTOCOL_PACKETS_DEBUG == 0x01)
                        qDebug() << "Somethink goes very bad. Unknown process CRC state!";
                    #endif
					}
				}
			}
			break;
			default:
			{
            #if(PROTOCOL_PACKETS_DEBUG == 0x01)
                qDebug() << "Somethink goes very bad. Unknown processBytes() state!";
            #endif
			}
		}
		
		this->circularBufferDataStartCursor++;
		this->checkBufferCursor(this->circularBufferDataStartCursor, PROTOCOL_PACKETS_MAX_CIRCULAR_BUFFER_LENGTH);
	}
}

void ProtocolPacketsProcessing::checkBufferCursor(uint16_t &position, const uint16_t bufferLength)
{
	if(position == bufferLength)
	{
		position = 0;
	}
}

void ProtocolPacketsProcessing::printPacket(sProtocolPacket packet)
{
    QString message = "Packet:";
    message += "\n\tCommand code = " + QString::number(packet.commandCode);
    message += "\n\tLength = " + QString::number(packet.length);
    message += "\n\tData = ";
    for(uint8_t i = 0; i < packet.length; i++)
	{
        message +=  QString::number(packet.data[i]);
        if(i < packet.length - 1){ message += " | "; }
	}
    if(packet.length == 0){ message += "-"; }
    message += "\n\tCRC = " + QString::number(packet.crc);

    qDebug().noquote() << message;
}

sProtocolPacket* ProtocolPacketsProcessing::getNewPacketsTable()
{
	return this->newPacketsTable;
}

uint8_t ProtocolPacketsProcessing::getNewPacketsNumber()
{
	return this->newPacketsNumber;
}

void ProtocolPacketsProcessing::resetNewPacketsNumber()
{
	this->newPacketsNumber = 0;
}

void ProtocolPacketsProcessing::clearPacket(sProtocolPacket &packet)
{
	packet.commandCode = 0;
	for(uint8_t i = 0; i < PROTOCOL_PACKETS_MAX_DATA_LENGTH; i++)
	{
		packet.data[i] = 0;
	}
	packet.length = 0;
	packet.crc = 0;
}

void ProtocolPacketsProcessing::copyRxBufferToCircualBuffer()
{
	uint8_t tempCounter = this->protocolBytes->getRxBufferCursor();
	
	if(tempCounter)
	{
		for(uint8_t i = 0; i < tempCounter; i++)
		{
			this->circularBuffer[this->circularBufferDataEndCursor] = this->protocolBytes->getBuffer(RX_BUFFER)[i];
			this->circularBufferDataEndCursor++;
			this->checkBufferCursor(this->circularBufferDataEndCursor, PROTOCOL_PACKETS_MAX_CIRCULAR_BUFFER_LENGTH);
			
			if(this->circularBufferDataEndCursor == this->circularBufferDataStartCursor)
			{
            #if(PROTOCOL_PACKETS_DEBUG == 0x01)
                qDebug() << "Not good, data was overwritten! Make protocol processing frequency bigger or make PROTOCOL_PACKETS_MAX_CIRCULAR_BUFFER_LENGTH bigger.";
            #endif
			}
		}
	}
}

void ProtocolPacketsProcessing::rewritePacketToBytes(sProtocolPacket &outcomingPacket)
{
	uint8_t tempTxBuffer = 0;
	uint8_t *txBuffer = this->protocolBytes->getBuffer(TX_BUFFER);
	
	// PREAMBLE:
	txBuffer[tempTxBuffer++] = PROTOCOL_PACKETS_PREAMBLE_FIRST_SIGN;
	txBuffer[tempTxBuffer++] = PROTOCOL_PACKETS_PREAMBLE_SECOND_SIGN;
	
	// COMMAND CODE:
	txBuffer[tempTxBuffer++] = (uint8_t)outcomingPacket.commandCode;
	
	// COMMAND LENGTH:
	txBuffer[tempTxBuffer++] = outcomingPacket.length;
	
	// DATA:
	for(uint8_t i = 0; i < outcomingPacket.length; i++)
	{
		txBuffer[tempTxBuffer++] = outcomingPacket.data[i];
	}
	
	// CALCULATE CRC:
	uint16_t outcomingPacketCRC = 0;
	for(uint8_t i = 0; i < tempTxBuffer; i++)
	{
		outcomingPacketCRC = crc16update(outcomingPacketCRC, txBuffer[i]);
	}
	
	// ADD CRC:
	txBuffer[tempTxBuffer++] = (uint8_t)(outcomingPacketCRC & 0x00FF);
	txBuffer[tempTxBuffer++] = (uint8_t)(outcomingPacketCRC >> 8);
	
	this->protocolBytes->setTxBufferCursor(tempTxBuffer);
}
