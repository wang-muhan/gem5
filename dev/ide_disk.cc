/*
 * Copyright (c) 2003 The Regents of The University of Michigan
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met: redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer;
 * redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution;
 * neither the name of the copyright holders nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/** @file
 * Device model implementation for an IDE disk
 */

#include <cerrno>
#include <cstring>
#include <deque>
#include <string>

#include "arch/alpha/pmap.h"
#include "base/cprintf.hh" // csprintf
#include "base/trace.hh"
#include "dev/disk_image.hh"
#include "dev/ide_disk.hh"
#include "dev/ide_ctrl.hh"
#include "dev/tsunami.hh"
#include "dev/tsunami_pchip.hh"
#include "mem/functional_mem/physical_memory.hh"
#include "mem/bus/bus.hh"
#include "mem/bus/dma_interface.hh"
#include "mem/bus/pio_interface.hh"
#include "mem/bus/pio_interface_impl.hh"
#include "sim/builder.hh"
#include "sim/sim_object.hh"
#include "sim/universe.hh"

using namespace std;

IdeDisk::IdeDisk(const string &name, DiskImage *img, PhysicalMemory *phys,
                 int id, int delay)
    : SimObject(name), ctrl(NULL), image(img), physmem(phys),
      dmaTransferEvent(this), dmaReadWaitEvent(this),
      dmaWriteWaitEvent(this), dmaPrdReadEvent(this),
      dmaReadEvent(this), dmaWriteEvent(this)
{
    diskDelay = (delay * ticksPerSecond / 100000);

    // initialize the data buffer and shadow registers
    dataBuffer = new uint8_t[MAX_DMA_SIZE];

    memset(dataBuffer, 0, MAX_DMA_SIZE);
    memset(&cmdReg, 0, sizeof(CommandReg_t));
    memset(&curPrd.entry, 0, sizeof(PrdEntry_t));

    curPrdAddr = 0;
    curSector = 0;
    curCommand = 0;
    cmdBytesLeft = 0;
    drqBytesLeft = 0;
    dmaRead = false;
    intrPending = false;

    // fill out the drive ID structure
    memset(&driveID, 0, sizeof(struct hd_driveid));

    // Calculate LBA and C/H/S values
    uint16_t cylinders;
    uint8_t heads;
    uint8_t sectors;

    uint32_t lba_size = image->size();
    if (lba_size >= 16383*16*63) {
        cylinders = 16383;
        heads = 16;
        sectors = 63;
    } else {
        if (lba_size >= 63)
            sectors = 63;
        else
            sectors = lba_size;

        if ((lba_size / sectors) >= 16)
            heads = 16;
        else
            heads = (lba_size / sectors);

        cylinders = lba_size / (heads * sectors);
    }

    // Setup the model name
    sprintf((char *)driveID.model, "5MI EDD si k");
    // Set the maximum multisector transfer size
    driveID.max_multsect = MAX_MULTSECT;
    // IORDY supported, IORDY disabled, LBA enabled, DMA enabled
    driveID.capability = 0x7;
    // UDMA support, EIDE support
    driveID.field_valid = 0x6;
    // Setup default C/H/S settings
    driveID.cyls = cylinders;
    driveID.sectors = sectors;
    driveID.heads = heads;
    // Setup the current multisector transfer size
    driveID.multsect = MAX_MULTSECT;
    driveID.multsect_valid = 0x1;
    // Number of sectors on disk
    driveID.lba_capacity = lba_size;
    // Multiword DMA mode 2 and below supported
    driveID.dma_mword = 0x400;
    // Set PIO mode 4 and 3 supported
    driveID.eide_pio_modes = 0x3;
    // Set DMA mode 4 and below supported
    driveID.dma_ultra = 0x10;
    // Statically set hardware config word
    driveID.hw_config = 0x4001;

    // set the device state to idle
    dmaState = Dma_Idle;

    if (id == DEV0) {
        devState = Device_Idle_S;
        devID = DEV0;
    } else if (id == DEV1) {
        devState = Device_Idle_NS;
        devID = DEV1;
    } else {
        panic("Invalid device ID: %#x\n", id);
    }

    // set the device ready bit
    cmdReg.status |= STATUS_DRDY_BIT;
}

IdeDisk::~IdeDisk()
{
    // destroy the data buffer
    delete [] dataBuffer;
}

Addr
IdeDisk::pciToDma(Addr &pciAddr)
{
    if (ctrl)
        return ctrl->tsunami->pchip->translatePciToDma(pciAddr);
    else
        panic("Access to unset controller!\n");
}

////
// Device registers read/write
////

void
IdeDisk::read(const Addr &offset, bool byte, bool cmdBlk, uint8_t *data)
{
    DevAction_t action = ACT_NONE;

    if (cmdBlk) {
        if (offset < 0 || offset > sizeof(CommandReg_t))
            panic("Invalid disk command register offset: %#x\n", offset);

        if (!byte && offset != DATA_OFFSET)
            panic("Invalid 16-bit read, only allowed on data reg\n");

        if (!byte)
            *(uint16_t *)data = *(uint16_t *)&cmdReg.data0;
        else
            *data = ((uint8_t *)&cmdReg)[offset];

        // determine if an action needs to be taken on the state machine
        if (offset == STATUS_OFFSET) {
            action = ACT_STAT_READ;
        } else if (offset == DATA_OFFSET) {
            if (byte)
                action = ACT_DATA_READ_BYTE;
            else
                action = ACT_DATA_READ_SHORT;
        }

    } else {
        if (offset != ALTSTAT_OFFSET)
            panic("Invalid disk control register offset: %#x\n", offset);

        if (!byte)
            panic("Invalid 16-bit read from control block\n");

        *data = ((uint8_t *)&cmdReg)[STATUS_OFFSET];
    }

    if (action != ACT_NONE)
        updateState(action);
}

void
IdeDisk::write(const Addr &offset, bool byte, bool cmdBlk, const uint8_t *data)
{
    DevAction_t action = ACT_NONE;

    if (cmdBlk) {
        if (offset < 0 || offset > sizeof(CommandReg_t))
            panic("Invalid disk command register offset: %#x\n", offset);

        if (!byte && offset != DATA_OFFSET)
            panic("Invalid 16-bit write, only allowed on data reg\n");

        if (!byte)
            *((uint16_t *)&cmdReg.data0) = *(uint16_t *)data;
        else
            ((uint8_t *)&cmdReg)[offset] = *data;

        // determine if an action needs to be taken on the state machine
        if (offset == COMMAND_OFFSET) {
            action = ACT_CMD_WRITE;
        } else if (offset == DATA_OFFSET) {
            if (byte)
                action = ACT_DATA_WRITE_BYTE;
            else
                action = ACT_DATA_WRITE_SHORT;
        }

    } else {
        if (offset != CONTROL_OFFSET)
            panic("Invalid disk control register offset: %#x\n", offset);

        if (!byte)
            panic("Invalid 16-bit write to control block\n");

        if (*data & CONTROL_RST_BIT)
            panic("Software reset not supported!\n");

        nIENBit = (*data & CONTROL_IEN_BIT) ? true : false;
    }

    if (action != ACT_NONE)
        updateState(action);
}

////
// Perform DMA transactions
////

void
IdeDisk::doDmaTransfer()
{
    if (dmaState != Dma_Transfer || devState != Transfer_Data_Dma)
        panic("Inconsistent DMA transfer state: dmaState = %d devState = %d\n",
              dmaState, devState);

    // first read the current PRD
    if (dmaInterface) {
        if (dmaInterface->busy()) {
            // reschedule after waiting period
            dmaTransferEvent.schedule(curTick + DMA_BACKOFF_PERIOD);
            return;
        }

        dmaInterface->doDMA(Read, curPrdAddr, sizeof(PrdEntry_t), curTick,
                            &dmaPrdReadEvent);
    } else {
        dmaPrdReadDone();
    }
}

void
IdeDisk::dmaPrdReadDone()
{
    // actually copy the PRD from physical memory
    memcpy((void *)&curPrd.entry,
           physmem->dma_addr(curPrdAddr, sizeof(PrdEntry_t)),
           sizeof(PrdEntry_t));

    curPrdAddr += sizeof(PrdEntry_t);

    if (dmaRead)
        doDmaRead();
    else
        doDmaWrite();
}

void
IdeDisk::doDmaRead()
{
    Tick totalDiskDelay = diskDelay * (curPrd.getByteCount() / SectorSize);

    if (dmaInterface) {
        if (dmaInterface->busy()) {
            // reschedule after waiting period
            dmaReadWaitEvent.schedule(curTick + DMA_BACKOFF_PERIOD);
            return;
        }

        Addr dmaAddr =
            ctrl->tsunami->pchip->translatePciToDma(curPrd.getBaseAddr());
        dmaInterface->doDMA(Read, dmaAddr, curPrd.getByteCount(),
                            curTick + totalDiskDelay, &dmaReadEvent);
    } else {
        // schedule dmaReadEvent with sectorDelay (dmaReadDone)
        dmaReadEvent.schedule(curTick + totalDiskDelay);
    }
}

void
IdeDisk::dmaReadDone()
{

    Addr curAddr = 0, dmaAddr = 0;
    uint32_t bytesWritten = 0, bytesInPage = 0, bytesLeft = 0;

    // set initial address
    curAddr = curPrd.getBaseAddr();

    // clear out the data buffer
    memset(dataBuffer, 0, MAX_DMA_SIZE);

    // read the data from memory via DMA into a data buffer
    while (bytesWritten < curPrd.getByteCount()) {
        if (cmdBytesLeft <= 0)
            panic("DMA data is larger than # of sectors specified\n");

        dmaAddr = pciToDma(curAddr);

        // calculate how many bytes are in the current page
        bytesLeft = curPrd.getByteCount() - bytesWritten;
        bytesInPage = (bytesLeft > ALPHA_PGBYTES) ? ALPHA_PGBYTES : bytesLeft;
        // check to make sure we don't cross a page boundary
        if ((curAddr + bytesInPage) >
            (alpha_trunc_page(curAddr) + ALPHA_PGBYTES))

            bytesInPage = alpha_round_page(curAddr) - curAddr;

        // copy the data from memory into the data buffer
        /** @todo Use real DMA with interfaces here */
        memcpy((void *)(dataBuffer + bytesWritten),
               physmem->dma_addr(dmaAddr, bytesInPage),
               bytesInPage);

        curAddr += bytesInPage;
        bytesWritten += bytesInPage;
        cmdBytesLeft -= bytesInPage;
    }

    // write the data to the disk image
    for (bytesWritten = 0;
         bytesWritten < curPrd.getByteCount();
         bytesWritten += SectorSize)

        writeDisk(curSector++, (uint8_t *)(dataBuffer + bytesWritten));

#if 0
    // actually copy the data from memory to data buffer
    Addr dmaAddr =
        ctrl->tsunami->pchip->translatePciToDma(curPrd.getBaseAddr());
    memcpy((void *)dataBuffer,
           physmem->dma_addr(dmaAddr, curPrd.getByteCount()),
           curPrd.getByteCount());

    uint32_t bytesWritten = 0;

    while (bytesWritten < curPrd.getByteCount()) {
        if (cmdBytesLeft <= 0)
            panic("DMA data is larger than # sectors specified\n");

        writeDisk(curSector++, (uint8_t *)(dataBuffer + bytesWritten));

        bytesWritten += SectorSize;
        cmdBytesLeft -= SectorSize;
    }
#endif

    // check for the EOT
    if (curPrd.getEOT()){
        assert(cmdBytesLeft == 0);
        dmaState = Dma_Idle;
        updateState(ACT_DMA_DONE);
    } else {
        doDmaTransfer();
    }
}

void
IdeDisk::doDmaWrite()
{
    Tick totalDiskDelay = diskDelay * (curPrd.getByteCount() / SectorSize);

    if (dmaInterface) {
        if (dmaInterface->busy()) {
            // reschedule after waiting period
            dmaWriteWaitEvent.schedule(curTick + DMA_BACKOFF_PERIOD);
            return;
        }

        Addr dmaAddr =
            ctrl->tsunami->pchip->translatePciToDma(curPrd.getBaseAddr());
        dmaInterface->doDMA(WriteInvalidate, dmaAddr,
                            curPrd.getByteCount(), curTick + totalDiskDelay,
                            &dmaWriteEvent);
    } else {
        // schedule event with disk delay (dmaWriteDone)
        dmaWriteEvent.schedule(curTick + totalDiskDelay);
    }
}

void
IdeDisk::dmaWriteDone()
{
    Addr curAddr = 0, pageAddr = 0, dmaAddr = 0;
    uint32_t bytesRead = 0, bytesInPage = 0;

    // setup the initial page and DMA address
    curAddr = curPrd.getBaseAddr();
    pageAddr = alpha_trunc_page(curAddr);
    dmaAddr = pciToDma(curAddr);

    // clear out the data buffer
    memset(dataBuffer, 0, MAX_DMA_SIZE);

    while (bytesRead < curPrd.getByteCount()) {
        // see if we have crossed into a new page
        if (pageAddr != alpha_trunc_page(curAddr)) {
            // write the data to memory
            /** @todo Do real DMA using interfaces here */
            memcpy(physmem->dma_addr(dmaAddr, bytesInPage),
                   (void *)(dataBuffer + (bytesRead - bytesInPage)),
                   bytesInPage);

            // update the DMA address and page address
            pageAddr = alpha_trunc_page(curAddr);
            dmaAddr = pciToDma(curAddr);

            bytesInPage = 0;
        }

        if (cmdBytesLeft <= 0)
            panic("DMA requested data is larger than # sectors specified\n");

        readDisk(curSector++, (uint8_t *)(dataBuffer + bytesRead));

        curAddr += SectorSize;
        bytesRead += SectorSize;
        bytesInPage += SectorSize;
        cmdBytesLeft -= SectorSize;
    }

    // write the last page worth read to memory
    /** @todo Do real DMA using interfaces here */
    if (bytesInPage != 0) {
        memcpy(physmem->dma_addr(dmaAddr, bytesInPage),
               (void *)(dataBuffer + (bytesRead - bytesInPage)),
               bytesInPage);
    }

#if 0
    Addr dmaAddr = ctrl->tsunami->pchip->
        translatePciToDma(curPrd.getBaseAddr());

    memcpy(physmem->dma_addr(dmaAddr, curPrd.getByteCount()),
           (void *)dataBuffer, curPrd.getByteCount());
#endif

    // check for the EOT
    if (curPrd.getEOT()) {
        assert(cmdBytesLeft == 0);
        dmaState = Dma_Idle;
        updateState(ACT_DMA_DONE);
    } else {
        doDmaTransfer();
    }
}

////
// Disk utility routines
///

void
IdeDisk::readDisk(uint32_t sector, uint8_t *data)
{
    uint32_t bytesRead = image->read(data, sector);

    if (bytesRead != SectorSize)
        panic("Can't read from %s. Only %d of %d read. errno=%d\n",
              name(), bytesRead, SectorSize, errno);
}

void
IdeDisk::writeDisk(uint32_t sector, uint8_t *data)
{
    uint32_t bytesWritten = image->write(data, sector);

    if (bytesWritten != SectorSize)
        panic("Can't write to %s. Only %d of %d written. errno=%d\n",
              name(), bytesWritten, SectorSize, errno);
}

////
// Setup and handle commands
////

void
IdeDisk::startDma(const uint32_t &prdTableBase)
{
    if (dmaState != Dma_Start)
        panic("Inconsistent DMA state, should be in Dma_Start!\n");

    if (devState != Transfer_Data_Dma)
        panic("Inconsistent device state for DMA start!\n");

    curPrdAddr = ctrl->tsunami->pchip->translatePciToDma(prdTableBase);

    dmaState = Dma_Transfer;

    // schedule dma transfer (doDmaTransfer)
    dmaTransferEvent.schedule(curTick + 1);
}

void
IdeDisk::abortDma()
{
    if (dmaState == Dma_Idle)
        panic("Inconsistent DMA state, should be in Dma_Start or Dma_Transfer!\n");

    if (devState != Transfer_Data_Dma && devState != Prepare_Data_Dma)
        panic("Inconsistent device state, should be in Transfer or Prepare!\n");

    updateState(ACT_CMD_ERROR);
}

void
IdeDisk::startCommand()
{
    DevAction_t action = ACT_NONE;
    uint32_t size = 0;
    dmaRead = false;

    // copy the command to the shadow
    curCommand = cmdReg.command;

    // Decode commands
    switch (cmdReg.command) {
        // Supported non-data commands
      case WIN_READ_NATIVE_MAX:
        size = image->size() - 1;
        cmdReg.sec_num = (size & 0xff);
        cmdReg.cyl_low = ((size & 0xff00) >> 8);
        cmdReg.cyl_high = ((size & 0xff0000) >> 16);
        cmdReg.head = ((size & 0xf000000) >> 24);

        devState = Command_Execution;
        action = ACT_CMD_COMPLETE;
        break;

      case WIN_RECAL:
      case WIN_SPECIFY:
      case WIN_FLUSH_CACHE:
      case WIN_VERIFY:
      case WIN_SEEK:
      case WIN_SETFEATURES:
      case WIN_SETMULT:
        devState = Command_Execution;
        action = ACT_CMD_COMPLETE;
        break;

        // Supported PIO data-in commands
      case WIN_IDENTIFY:
        cmdBytesLeft = drqBytesLeft = sizeof(struct hd_driveid);
        devState = Prepare_Data_In;
        action = ACT_DATA_READY;
        break;

      case WIN_MULTREAD:
      case WIN_READ:
        if (!(cmdReg.drive & DRIVE_LBA_BIT))
            panic("Attempt to perform CHS access, only supports LBA\n");

        if (cmdReg.sec_count == 0)
            cmdBytesLeft = (256 * SectorSize);
        else
            cmdBytesLeft = (cmdReg.sec_count * SectorSize);

        drqBytesLeft = SectorSize;
        curSector = getLBABase();

        devState = Prepare_Data_In;
        action = ACT_DATA_READY;
        break;

        // Supported PIO data-out commands
      case WIN_MULTWRITE:
      case WIN_WRITE:
        if (!(cmdReg.drive & DRIVE_LBA_BIT))
            panic("Attempt to perform CHS access, only supports LBA\n");

        if (cmdReg.sec_count == 0)
            cmdBytesLeft = (256 * SectorSize);
        else
            cmdBytesLeft = (cmdReg.sec_count * SectorSize);

        drqBytesLeft = SectorSize;
        curSector = getLBABase();

        devState = Prepare_Data_Out;
        action = ACT_DATA_READY;
        break;

        // Supported DMA commands
      case WIN_WRITEDMA:
        dmaRead = true;  // a write to the disk is a DMA read from memory
      case WIN_READDMA:
        if (!(cmdReg.drive & DRIVE_LBA_BIT))
            panic("Attempt to perform CHS access, only supports LBA\n");

        if (cmdReg.sec_count == 0)
            cmdBytesLeft = (256 * SectorSize);
        else
            cmdBytesLeft = (cmdReg.sec_count * SectorSize);

        drqBytesLeft = SectorSize;
        curSector = getLBABase();

        devState = Prepare_Data_Dma;
        action = ACT_DMA_READY;
        break;

      default:
        panic("Unsupported ATA command: %#x\n", cmdReg.command);
    }

    if (action != ACT_NONE) {
        // set the BSY bit
        cmdReg.status |= STATUS_BSY_BIT;
        // clear the DRQ bit
        cmdReg.status &= ~STATUS_DRQ_BIT;

        updateState(action);
    }
}

////
// Handle setting and clearing interrupts
////

void
IdeDisk::intrPost()
{
    if (intrPending)
        panic("Attempt to post an interrupt with one pending\n");

    intrPending = true;

    // talk to controller to set interrupt
    if (ctrl)
        ctrl->intrPost();
}

void
IdeDisk::intrClear()
{
    if (!intrPending)
        panic("Attempt to clear a non-pending interrupt\n");

    intrPending = false;

    // talk to controller to clear interrupt
    if (ctrl)
        ctrl->intrClear();
}

////
// Manage the device internal state machine
////

void
IdeDisk::updateState(DevAction_t action)
{
    switch (devState) {
      case Device_Idle_S:
        if (!isDEVSelect())
            devState = Device_Idle_NS;
        else if (action == ACT_CMD_WRITE)
            startCommand();

        break;

      case Device_Idle_SI:
        if (!isDEVSelect()) {
            devState = Device_Idle_NS;
            intrClear();
        } else if (action == ACT_STAT_READ || isIENSet()) {
            devState = Device_Idle_S;
            intrClear();
        } else if (action == ACT_CMD_WRITE) {
            intrClear();
            startCommand();
        }

        break;

      case Device_Idle_NS:
        if (isDEVSelect()) {
            if (!isIENSet() && intrPending) {
                devState = Device_Idle_SI;
                intrPost();
            }
            if (isIENSet() || !intrPending) {
                devState = Device_Idle_S;
            }
        }
        break;

      case Command_Execution:
        if (action == ACT_CMD_COMPLETE) {
            // clear the BSY bit
            setComplete();

            if (!isIENSet()) {
                devState = Device_Idle_SI;
                intrPost();
            } else {
                devState = Device_Idle_S;
            }
        }
        break;

      case Prepare_Data_In:
        if (action == ACT_CMD_ERROR) {
            // clear the BSY bit
            setComplete();

            if (!isIENSet()) {
                devState = Device_Idle_SI;
                intrPost();
            } else {
                devState = Device_Idle_S;
            }
        } else if (action == ACT_DATA_READY) {
            // clear the BSY bit
            cmdReg.status &= ~STATUS_BSY_BIT;
            // set the DRQ bit
            cmdReg.status |= STATUS_DRQ_BIT;

            // put the first two bytes into the data register
            memcpy((void *)&cmdReg.data0, (void *)dataBuffer,
                   sizeof(uint16_t));

            // copy the data into the data buffer
            if (curCommand == WIN_IDENTIFY)
                memcpy((void *)dataBuffer, (void *)&driveID,
                       sizeof(struct hd_driveid));
            else
                readDisk(curSector++, dataBuffer);

            if (!isIENSet()) {
                devState = Data_Ready_INTRQ_In;
                intrPost();
            } else {
                devState = Transfer_Data_In;
            }
        }
        break;

      case Data_Ready_INTRQ_In:
        if (action == ACT_STAT_READ) {
            devState = Transfer_Data_In;
            intrClear();
        }
        break;

      case Transfer_Data_In:
        if (action == ACT_DATA_READ_BYTE || action == ACT_DATA_READ_SHORT) {
            if (action == ACT_DATA_READ_BYTE) {
                panic("DEBUG: READING DATA ONE BYTE AT A TIME!\n");
            } else {
                drqBytesLeft -= 2;
                cmdBytesLeft -= 2;

                // copy next short into data registers
                memcpy((void *)&cmdReg.data0,
                       (void *)&dataBuffer[SectorSize - drqBytesLeft],
                       sizeof(uint16_t));
            }

            if (drqBytesLeft == 0) {
                if (cmdBytesLeft == 0) {
                    // Clear the BSY bit
                    setComplete();
                    devState = Device_Idle_S;
                } else {
                    devState = Prepare_Data_In;
                    cmdReg.status |= STATUS_BSY_BIT;
                }
            }
        }
        break;

      case Prepare_Data_Out:
        if (action == ACT_CMD_ERROR || cmdBytesLeft == 0) {
            // clear the BSY bit
            setComplete();

            if (!isIENSet()) {
                devState = Device_Idle_SI;
                intrPost();
            } else {
                devState = Device_Idle_S;
            }
        } else if (cmdBytesLeft != 0) {
            // clear the BSY bit
            cmdReg.status &= ~STATUS_BSY_BIT;
            // set the DRQ bit
            cmdReg.status |= STATUS_DRQ_BIT;

            // clear the data buffer to get it ready for writes
            memset(dataBuffer, 0, MAX_DMA_SIZE);

            if (!isIENSet()) {
                devState = Data_Ready_INTRQ_Out;
                intrPost();
            } else {
                devState = Transfer_Data_Out;
            }
        }
        break;

      case Data_Ready_INTRQ_Out:
        if (action == ACT_STAT_READ) {
            devState = Transfer_Data_Out;
            intrClear();
        }
        break;

      case Transfer_Data_Out:
        if (action == ACT_DATA_WRITE_BYTE ||
            action == ACT_DATA_WRITE_SHORT) {

            if (action == ACT_DATA_READ_BYTE) {
                panic("DEBUG: WRITING DATA ONE BYTE AT A TIME!\n");
            } else {
                // copy the latest short into the data buffer
                memcpy((void *)&dataBuffer[SectorSize - drqBytesLeft],
                       (void *)&cmdReg.data0,
                       sizeof(uint16_t));

                drqBytesLeft -= 2;
                cmdBytesLeft -= 2;
            }

            if (drqBytesLeft == 0) {
                // copy the block to the disk
                writeDisk(curSector++, dataBuffer);

                // set the BSY bit
                cmdReg.status |= STATUS_BSY_BIT;
                // clear the DRQ bit
                cmdReg.status &= ~STATUS_DRQ_BIT;

                devState = Prepare_Data_Out;
            }
        }
        break;

      case Prepare_Data_Dma:
        if (action == ACT_CMD_ERROR) {
            // clear the BSY bit
            setComplete();

            if (!isIENSet()) {
                devState = Device_Idle_SI;
                intrPost();
            } else {
                devState = Device_Idle_S;
            }
        } else if (action == ACT_DMA_READY) {
            // clear the BSY bit
            cmdReg.status &= ~STATUS_BSY_BIT;
            // set the DRQ bit
            cmdReg.status |= STATUS_DRQ_BIT;

            devState = Transfer_Data_Dma;

            if (dmaState != Dma_Idle)
                panic("Inconsistent DMA state, should be Dma_Idle\n");

            dmaState = Dma_Start;
            // wait for the write to the DMA start bit
        }
        break;

      case Transfer_Data_Dma:
        if (action == ACT_CMD_ERROR || action == ACT_DMA_DONE) {
            // clear the BSY bit
            setComplete();
            // set the seek bit
            cmdReg.status |= 0x10;
            // clear the controller state for DMA transfer
            ctrl->setDmaComplete(this);

            if (!isIENSet()) {
                devState = Device_Idle_SI;
                intrPost();
            } else {
                devState = Device_Idle_S;
            }
        }
        break;

      default:
        panic("Unknown IDE device state: %#x\n", devState);
    }
}

void
IdeDisk::serialize(ostream &os)
{
}

void
IdeDisk::unserialize(Checkpoint *cp, const string &section)
{
}

#ifndef DOXYGEN_SHOULD_SKIP_THIS

BEGIN_DECLARE_SIM_OBJECT_PARAMS(IdeDisk)

    SimObjectParam<DiskImage *> image;
    SimObjectParam<PhysicalMemory *> physmem;
    Param<int> driveID;
    Param<int> disk_delay;

END_DECLARE_SIM_OBJECT_PARAMS(IdeDisk)

BEGIN_INIT_SIM_OBJECT_PARAMS(IdeDisk)

    INIT_PARAM(image, "Disk image"),
    INIT_PARAM(physmem, "Physical memory"),
    INIT_PARAM(driveID, "Drive ID (0=master 1=slave)"),
    INIT_PARAM_DFLT(disk_delay, "Fixed disk delay in milliseconds", 0)

END_INIT_SIM_OBJECT_PARAMS(IdeDisk)


CREATE_SIM_OBJECT(IdeDisk)
{
    return new IdeDisk(getInstanceName(), image, physmem, driveID,
                       disk_delay);
}

REGISTER_SIM_OBJECT("IdeDisk", IdeDisk)

#endif //DOXYGEN_SHOULD_SKIP_THIS
