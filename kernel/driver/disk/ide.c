
#include <stdio.h>
#include <picirq.h>
#include <pci.h>
#include <trap.h>
#include <ide.h>
#include <clock.h>
#include <physical_page.h>


#define DESC_END 0x8000
#define DESC_NEXT 0

#define IDE_CMD_WRITE 0x80
#define IDE_CMD_READ 0x00
#define IDE_CMD_START 0x01
#define IDE_CMD_STOP 0x00

#define IDE_STATUS_INTR 2
#define IDE_STATUS_ERROR 1
#define CLEAR_BIT(x,offset) (x &= ~(1<<offset))
#define CHECK_BIT(x,offset) (x & (1<<offset))

static void ide_initialize(uint16_t bar0, uint16_t bar1,
	uint16_t bar2, uint16_t bar3,uint16_t bar4);
void parse_progif(uint8_t pg);

Device* ide_dev;

struct IDEChannelRegisters {
   unsigned short base;  // I/O Base.
   unsigned short ctrl;  // Control Base
   unsigned short bmide; // Bus Master IDE
   unsigned char  nIEN;  // nIEN (No Interrupt);
} channels[2];
unsigned char package[3];
unsigned char ide_buf[2048] = {0};
unsigned static char ide_irq_invoked = 0;
unsigned static char atapi_packet[12] = {0xA8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};

struct ide_device {
   unsigned char  reserved;    // 0 (Empty) or 1 (This Drive really exists).
   unsigned char  channel;     // 0 (Primary Channel) or 1 (Secondary Channel).
   unsigned char  drive;       // 0 (Master Drive) or 1 (Slave Drive).
   unsigned short type;        // 0: ATA, 1:ATAPI.
   unsigned short signature;   // Drive Signature
   unsigned short capabilities;// Features.
   unsigned int   commandSets; // Command Sets Supported.
   unsigned int   size;        // Size in Sectors.
   unsigned char  model[41];   // Model in string.
   uint8_t dma;
} ide_devices[4];

void ide_init(){
	debug("ide init...\n");
	Device* dev = 0;
	for(int i=0;i<device_num;i++){
		if(devices[i].class_code == 1 && devices[i].subclass == 1){
			debug("find IDE device at: %02x:%02x:%1x\n",devices[i].bus_id,devices[i].device_id,
				devices[i].func);
			dev = &devices[i];
			break;
		}
	}
	if(!dev)
		return;
	PCI_loadbars(dev);
	parse_progif(dev->prog_if);
	// we except is 0x80:ISA Compatibility mode-only controller, supports bus mastering
	// primary channel is in compatibility mode (ports 0x1F0-0x1F7, 0x3F6, IRQ14).
	// secondary channel is in compatibility mode (ports 0x170-0x177, 0x376, IRQ15).

	// bar0 0x1f0 bar1 0x3f6
	// bar2 0x170 bar3 0x376
	// bar4  dev->iobase
	ide_initialize(dev->reg_base[0],dev->reg_base[1],dev->reg_base[2],
		dev->reg_base[3],dev->reg_base[4]);
	register_intr_handler(IRQ_IDE_PRI + IRQ_OFFSET,ide_irq);
	register_intr_handler(IRQ_IDE_SEC + IRQ_OFFSET,ide_irq);
	pic_enable(IRQ_IDE_PRI);
	pic_enable(IRQ_IDE_SEC);
	ide_enable_dma(0);
	ide_enable_dma(1);
}

static region_desc* prd_table;

void ide_enable_dma(int idx){
	ide_devices[idx].dma = 1;
	// prepare prd table
	uint8_t channel = ide_devices[idx].channel;
	debug("enable dma at %s:%s\n",
		(const char *[]){"ATA_PRIMARY", "ATA_SECONDARY"}[channel],
		(const char *[]){"Master", "Slave"}[ide_devices[idx].drive]
	);
	uint16_t port = channels[channel].bmide;
	prd_table = (region_desc*)physical_alloc(0x8000,0x10000);
	outl(port + IDE_DMA_PRD,(uint32_t)prd_table);
}
void ide_disable_dma(int idx){
	ide_devices[idx].dma = 0;
	// // prepare prd table
	// uint8_t channel = ide_devices[idx].channel;
	// debug("enable dma at %s:%s\n",
	// 	(const char *[]){"ATA_PRIMARY", "ATA_SECONDARY"}[channel],
	// 	(const char *[]){"Master", "Slave"}[ide_devices[idx].drive]
	// );
	// uint16_t port = channels[channel].bmide;
	// prd_table = (region_desc*)physical_alloc(0x8000,0x10000);
	// outl(port + IDE_DMA_PRD,(uint32_t)prd_table);
}


void ide_write(unsigned char channel, unsigned char reg, unsigned char data) {
   if (reg > 0x07 && reg < 0x0C)
	//Set this to read back the High Order Byte of the last LBA48 value sent to an IO port.
      ide_write(channel, ATA_REG_CONTROL, 0x80 | channels[channel].nIEN); // 
   if (reg < 0x08)
      outb(channels[channel].base  + reg - 0x00, data);
   else if (reg < 0x0C)
      outb(channels[channel].base  + reg - 0x06, data);
   else if (reg < 0x0E)
      outb(channels[channel].ctrl  + reg - 0x0A, data);
   else if (reg < 0x16)
      outb(channels[channel].bmide + reg - 0x0E, data);
   if (reg > 0x07 && reg < 0x0C)
      ide_write(channel, ATA_REG_CONTROL, channels[channel].nIEN);
}

unsigned char ide_read(unsigned char channel, unsigned char reg) {
   unsigned char result;
   if (reg > 0x07 && reg < 0x0C)
      ide_write(channel, ATA_REG_CONTROL, 0x80 | channels[channel].nIEN);
   if (reg < 0x08)
      result = inb(channels[channel].base + reg - 0x00);
   else if (reg < 0x0C)
      result = inb(channels[channel].base  + reg - 0x06);
   else if (reg < 0x0E)
      result = inb(channels[channel].ctrl  + reg - 0x0A);
   else if (reg < 0x16)
      result = inb(channels[channel].bmide + reg - 0x0E);
   if (reg > 0x07 && reg < 0x0C)
      ide_write(channel, ATA_REG_CONTROL, channels[channel].nIEN);
   return result;
}

void ide_read_buffer(unsigned char channel, unsigned char reg, void* buffer,
                     unsigned int quads) {
   /* WARNING: This code contains a serious bug. The inline assembly trashes ES and
    *           ESP for all of the code the compiler generates between the inline
    *           assembly blocks.
    */
   if (reg > 0x07 && reg < 0x0C)
      ide_write(channel, ATA_REG_CONTROL, 0x80 | channels[channel].nIEN);
   // asm("pushw %es; movw %ds, %ax; movw %ax, %es");
   if (reg < 0x08)
      insl(channels[channel].base  + reg - 0x00, buffer, quads);
   else if (reg < 0x0C)
      insl(channels[channel].base  + reg - 0x06, buffer, quads);
   else if (reg < 0x0E)
      insl(channels[channel].ctrl  + reg - 0x0A, buffer, quads);
   else if (reg < 0x16)
      insl(channels[channel].bmide + reg - 0x0E, buffer, quads);
   // asm("popw %es;");
   if (reg > 0x07 && reg < 0x0C)
      ide_write(channel, ATA_REG_CONTROL, channels[channel].nIEN);
}

static void ide_initialize(uint16_t bar0, uint16_t bar1,
	uint16_t bar2, uint16_t bar3,uint16_t bar4){
	uint8_t count = 0;
	// 1- Detect I/O Ports which interface IDE Controller:
	channels[ATA_PRIMARY  ].base  = (bar0 & 0xFFFFFFFC) + 0x1F0 * (!bar0);
	channels[ATA_PRIMARY  ].ctrl  = (bar1 & 0xFFFFFFFC) + 0x3F6 * (!bar1);
	channels[ATA_SECONDARY].base  = (bar2 & 0xFFFFFFFC) + 0x170 * (!bar2);
	channels[ATA_SECONDARY].ctrl  = (bar3 & 0xFFFFFFFC) + 0x376 * (!bar3);
	channels[ATA_PRIMARY  ].bmide = (bar4 & 0xFFFFFFFC) + 0; // Bus Master IDE
	channels[ATA_SECONDARY].bmide = (bar4 & 0xFFFFFFFC) + 8; // Bus Master IDE
	// 2- Disable IRQs:
	ide_write(ATA_PRIMARY  , ATA_REG_CONTROL, 2);
	ide_write(ATA_SECONDARY, ATA_REG_CONTROL, 2);
	// 3- Detect ATA-ATAPI Devices:
	for(int i = 0;i<2;i++){
		for(int j=0;j<2;j++){
			uint8_t err = 0;
			uint8_t type = IDE_ATA;
			ide_devices[count].reserved = 0;
			// (I) Select Drive:
			ide_write(i, ATA_REG_HDDEVSEL, 0xA0 | (j << 4)); // Select Drive.
			babysleep(10);
			// (II) Send ATA Identify Command:
			ide_write(i, ATA_REG_COMMAND, ATA_CMD_IDENTIFY);
			babysleep(10); // This function should be implemented in your OS. which waits for 1 ms.
			       // it is based on System Timer Device Driver.
			if (ide_read(i, ATA_REG_STATUS) == 0) continue;
			while(1) {
				uint8_t status = ide_read(i, ATA_REG_STATUS);
				if ((status & ATA_SR_ERR)) {err = 1; break;} // If Err, Device is not ATA.
				if (!(status & ATA_SR_BSY) && (status & ATA_SR_DRQ)) break; // Everything is right.
			}
			if(err){
				unsigned char cl = ide_read(i, ATA_REG_LBA1);
				unsigned char ch = ide_read(i, ATA_REG_LBA2);
				if (cl == 0x14 && ch ==0xEB)
					type = IDE_ATAPI;
				else if (cl == 0x69 && ch == 0x96)
					type = IDE_ATAPI;
				else
					continue; // Unknown Type (may not be a device).
				ide_write(i, ATA_REG_COMMAND, ATA_CMD_IDENTIFY_PACKET);
				babysleep(50);
			}
			memset(ide_buf,0,sizeof(ide_buf));

	        // (V) Read Identification Space of the Device:
	        ide_read_buffer(i, ATA_REG_DATA, ide_buf, 128);
 
			// (VI) Read Device Parameters:
			ide_devices[count].reserved     = 1;
			ide_devices[count].type         = type;
			ide_devices[count].channel      = i;
			ide_devices[count].drive        = j;
			ide_devices[count].signature    = *((unsigned short *)(ide_buf + ATA_IDENT_DEVICETYPE));
			ide_devices[count].capabilities = *((unsigned short *)(ide_buf + ATA_IDENT_CAPABILITIES));
			ide_devices[count].commandSets  = *((unsigned int *)(ide_buf + ATA_IDENT_COMMANDSETS));

			// (VII) Get Size:
			if (ide_devices[count].commandSets & (1 << 26)){
				// Device uses 48-Bit Addressing:
				ide_devices[count].size   = *((unsigned int *)(ide_buf + ATA_IDENT_MAX_LBA_EXT));
			}

			else{
				// Device uses CHS or 28-bit Addressing:
				ide_devices[count].size   = *((unsigned int *)(ide_buf + ATA_IDENT_MAX_LBA));

			}

			// (VIII) String indicates model of device (like Western Digital HDD and SONY DVD-RW...):
			for(int k = 0; k < 40; k += 2) {
				ide_devices[count].model[k] = ide_buf[ATA_IDENT_MODEL + k + 1];
				ide_devices[count].model[k + 1] = ide_buf[ATA_IDENT_MODEL + k];
			}
			ide_devices[count].model[40] = 0; // Terminate String.

			count++;
		}
	}
   // 4- Print Summary:
   for (int i = 0; i < 4; i++)
      if (ide_devices[i].reserved == 1) {
         debug("[%d] Found %s Drive %dMB - %s\n",
         	i,
            (const char *[]){"ATA", "ATAPI"}[ide_devices[i].type],         /* Type */
            ide_devices[i].size / 1024 / 2,               /* Size */
            ide_devices[i].model);
      }
}


unsigned char ide_polling(unsigned char channel, unsigned int advanced_check) {
   // (I) Delay 400 nanosecond for BSY to be set:
   // -------------------------------------------------
   for(int i = 0; i < 4; i++)
      ide_read(channel, ATA_REG_ALTSTATUS); // Reading the Alternate Status port wastes 100ns; loop four times.
   // (II) Wait for BSY to be cleared:
   // -------------------------------------------------
   while (ide_read(channel, ATA_REG_STATUS) & ATA_SR_BSY)
      ; // Wait for BSY to be zero.
 
   if (advanced_check) {
      unsigned char state = ide_read(channel, ATA_REG_STATUS); // Read Status Register.
 
      // (III) Check For Errors:
      // -------------------------------------------------
      if (state & ATA_SR_ERR)
         return 2; // Error.
 
      // (IV) Check If Device fault:
      // -------------------------------------------------
      if (state & ATA_SR_DF)
         return 1; // Device Fault.
 
      // (V) Check DRQ:
      // -------------------------------------------------
      // BSY = 0; DF = 0; ERR = 0 so we should check for DRQ now.
      if ((state & ATA_SR_DRQ) == 0)
         return 3; // DRQ should be set
 
   }
 
   return 0; // No Error.
 
}



unsigned char ide_ata_access(unsigned char direction, unsigned char drive, unsigned int lba, 
	unsigned char numsects, void* addr) {
/*
drive is the drive number which can be from 0 to 3.
lba is the LBA address which allows us to access disks up to 2TB.
numsects is the number of sectors to be read, it is a char, as reading more than 256 sector immediately may performance issues. If numsects is 0, the ATA controller will know that we want 256 sectors.
selector is the segment selector to read from, or write to.
edi is the offset in that segment. (the memory address for the data buffer)
*/
	uint8_t lba_mode; /* 0: CHS, 1:LBA28, 2: LBA48 */
	uint8_t dma; /* 0: No DMA, 1: DMA */
	uint8_t cmd;
	uint8_t head,sect,err;
	uint8_t lba_io[6];
	uint16_t words = 256;
	uint16_t cyl;

	unsigned int  slavebit      = ide_devices[drive].drive;
	unsigned int  channel      = ide_devices[drive].channel; // Read the Channel.
	uint16_t bus = channels[channel].base;
	
	if(lba > 0x10000000){// Sure Drive should support LBA in this case, or you are giving a wrong LBA.
		lba_mode  = 2;
		lba_io[0] = (lba & 0x000000FF)>> 0;
		lba_io[1] = (lba & 0x0000FF00)>> 8;
		lba_io[2] = (lba & 0x00FF0000)>>16;
		lba_io[3] = (lba & 0xFF000000)>>24;
		lba_io[4] = 0; // We said that we lba is integer, so 32-bit are enough to access 2TB.
		lba_io[5] = 0; // We said that we lba is integer, so 32-bit are enough to access 2TB.
		head      = 0; // Lower 4-bits of HDDEVSEL are not used here.
	}else if (ide_devices[drive].capabilities & 0x200)  { 
		lba_mode = 1;
		lba_io[0] = (lba & 0x00000FF)>> 0;
		lba_io[1] = (lba & 0x000FF00)>> 8;
		lba_io[2] = (lba & 0x0FF0000)>>16;
		lba_io[3] = 0; // These Registers are not used here.
		lba_io[4] = 0; // These Registers are not used here.
		lba_io[5] = 0; // These Registers are not used here.
		head      = (lba & 0xF000000)>>24;
	}else{
		lba_mode  = 0;
		sect      = (lba % 63) + 1;
		cyl       = (lba + 1  - sect)/(16*63);
		lba_io[0] = sect;
		lba_io[1] = (cyl>>0) & 0xFF;
		lba_io[2] = (cyl>>8) & 0xFF;
		lba_io[3] = 0;
		lba_io[4] = 0;
		lba_io[5] = 0;
		head      = (lba + 1  - sect)%(16*63)/(63); // Head number is written to HDDEVSEL lower 4-bits.
	}
	// (II) See if Drive Supports DMA or not;
	dma = ide_devices[drive].dma;
	ide_irq_invoked = 0x0;
	if(dma){
		// enable irq
		channels[channel].nIEN = 0;
		ide_write(channel, ATA_REG_CONTROL, 0x00);
	}else{
		//disable irq
		channels[channel].nIEN = 0x02;
		ide_write(channel, ATA_REG_CONTROL, 0x02);
	}
	// (III) Wait if the drive is busy;
	while (ide_read(channel, ATA_REG_STATUS) & ATA_SR_BSY); // Wait if Busy.
	// (IV) Select Drive from the controller;
	if (lba_mode == 0) 
		ide_write(channel, ATA_REG_HDDEVSEL, 0xA0 | (slavebit<<4) | head);   // Select Drive CHS.
	else
		ide_write(channel, ATA_REG_HDDEVSEL, 0xE0 | (slavebit<<4) | head);   // Select Drive LBA.
	// (V) Write Parameters;
	if (lba_mode == 2) {
		ide_write(channel, ATA_REG_SECCOUNT1,   0);
		ide_write(channel, ATA_REG_LBA3,   lba_io[3]);
		ide_write(channel, ATA_REG_LBA4,   lba_io[4]);
		ide_write(channel, ATA_REG_LBA5,   lba_io[5]);
	}
	ide_write(channel, ATA_REG_SECCOUNT0,   numsects);
	ide_write(channel, ATA_REG_LBA0,   lba_io[0]);
	ide_write(channel, ATA_REG_LBA1,   lba_io[1]);
	ide_write(channel, ATA_REG_LBA2,   lba_io[2]);
	if (lba_mode == 0 && dma == 0 && direction == 0) cmd = ATA_CMD_READ_PIO;
	if (lba_mode == 1 && dma == 0 && direction == 0) cmd = ATA_CMD_READ_PIO;   
	if (lba_mode == 2 && dma == 0 && direction == 0) cmd = ATA_CMD_READ_PIO_EXT;   
	if (lba_mode == 0 && dma == 1 && direction == 0) cmd = ATA_CMD_READ_DMA;
	if (lba_mode == 1 && dma == 1 && direction == 0) cmd = ATA_CMD_READ_DMA;
	if (lba_mode == 2 && dma == 1 && direction == 0) cmd = ATA_CMD_READ_DMA_EXT;
	if (lba_mode == 0 && dma == 0 && direction == 1) cmd = ATA_CMD_WRITE_PIO;
	if (lba_mode == 1 && dma == 0 && direction == 1) cmd = ATA_CMD_WRITE_PIO;
	if (lba_mode == 2 && dma == 0 && direction == 1) cmd = ATA_CMD_WRITE_PIO_EXT;
	if (lba_mode == 0 && dma == 1 && direction == 1) cmd = ATA_CMD_WRITE_DMA;
	if (lba_mode == 1 && dma == 1 && direction == 1) cmd = ATA_CMD_WRITE_DMA;
	if (lba_mode == 2 && dma == 1 && direction == 1) cmd = ATA_CMD_WRITE_DMA_EXT;

   
   if (dma){
   		uint16_t port = channels[channel].bmide;
   		for(int i=0;i<numsects;i++){
   			prd_table[i].address = (uint32_t)addr;
   			prd_table[i].count = words*2;
   			prd_table[i].end = 0;
   		}
   		prd_table[numsects-1].end = 0x8000;
   		if(direction == ATA_READ)
	   		outb(port + IDE_DMA_CMD,inb(port + IDE_DMA_CMD)|(1<<3));
	   	else
	   		outb(port + IDE_DMA_CMD,inb(port + IDE_DMA_CMD)&~(1<<3));
   		outb(port + IDE_DMA_STATUS,inb(port + IDE_DMA_STATUS)| 4 | 2); //clear err intr
		ide_write(channel, ATA_REG_COMMAND, cmd);
   		outb(port + IDE_DMA_CMD,inb(port + IDE_DMA_CMD)| 1);
   		debug("wait irq for dma\n");
   		ide_wait_irq();
   		debug("wait irq for dma done\n");
   		outb(port + IDE_DMA_CMD, inb(port + IDE_DMA_CMD) & (~1));
   		if(direction == 1){
   			ide_polling(channel, 0); // Polling.
   		}
   }else{
	   	ide_write(channel, ATA_REG_COMMAND, cmd);
		if (direction == 0){
			// PIO read
			for (int i = 0; i < numsects; i++) {
			 	err = ide_polling(channel, 1);
				if (err){
					return err;
				}
				insw(bus,addr,words);
		        addr += words*2;
			}
		}else{
			// PIO write
	     for (int i = 0; i < numsects; i++) {
	        ide_polling(channel, 0); // Polling.
	        outsw(bus,addr,words);
	        addr += (words*2);
	     }
	     ide_write(channel, ATA_REG_COMMAND, (char []) {
	     				ATA_CMD_CACHE_FLUSH,
	                    ATA_CMD_CACHE_FLUSH,
	                    ATA_CMD_CACHE_FLUSH_EXT}[lba_mode]);
	     debug("ide status: %d\n",ide_polling(channel, 1)); // Polling.
		}
   }
   return 0;
}

void ide_wait_irq() {
   while (!ide_irq_invoked){
	   	asm("hlt");
   }
   ide_irq_invoked = 0;
}

void ide_irq() {
   ide_irq_invoked = 1;
   uint8_t status;
   status = inb(channels[0].bmide + IDE_DMA_STATUS);
   debug("current status: %x\n",status);
   status |= 1<<2;
   outb(channels[0].bmide + IDE_DMA_STATUS,status);
   status = inb(channels[1].bmide + IDE_DMA_STATUS);
   status |= 1<<2;
   outb(channels[1].bmide + IDE_DMA_STATUS,status);
}

unsigned char ide_atapi_access(uint8_t direction,unsigned char drive, unsigned int lba, unsigned char numsects,
          void* addr) {
   unsigned int   channel      = ide_devices[drive].channel;
   unsigned int   slavebit      = ide_devices[drive].drive;
   unsigned int   bus      = channels[channel].base;
   unsigned int   words      = 2048 / 2; // Sector Size in Words, Almost All ATAPI Drives has a sector size of 2048 bytes.
   unsigned char  err;
   uint8_t dma = ide_devices[drive].dma;
   // Enable IRQs:
   ide_write(channel, ATA_REG_CONTROL, channels[channel].nIEN = ide_irq_invoked = 0x0);
   // (I): Setup SCSI Packet:
   // ------------------------------------------------------------------
   atapi_packet[ 0] = direction == ATAPI_READ?ATAPI_CMD_READ:ATAPI_CMD_WRITE;
   atapi_packet[ 1] = 0x0;
   atapi_packet[ 2] = (lba>>24) & 0xFF;
   atapi_packet[ 3] = (lba>>16) & 0xFF;
   atapi_packet[ 4] = (lba>> 8) & 0xFF;
   atapi_packet[ 5] = (lba>> 0) & 0xFF;
   atapi_packet[ 6] = 0x0;
   atapi_packet[ 7] = 0x0;
   atapi_packet[ 8] = 0x0;
   atapi_packet[ 9] = numsects;
   atapi_packet[10] = 0x0;
   atapi_packet[11] = 0x0;
   // (II): Select the Drive:
   // ------------------------------------------------------------------
   ide_write(channel, ATA_REG_HDDEVSEL, slavebit<<4);
   // (III): Delay 400 nanosecond for select to complete:
   // ------------------------------------------------------------------
   ide_read(channel, ATA_REG_ALTSTATUS); // Reading Alternate Status Port wastes 100ns.
   ide_read(channel, ATA_REG_ALTSTATUS); // Reading Alternate Status Port wastes 100ns.
   ide_read(channel, ATA_REG_ALTSTATUS); // Reading Alternate Status Port wastes 100ns.
   ide_read(channel, ATA_REG_ALTSTATUS); // Reading Alternate Status Port wastes 100ns.
   // (IV): Inform the Controller that we use PIO mode:
   // ------------------------------------------------------------------

   ide_write(channel, ATA_REG_FEATURES, dma);         // PIO mode.
   // (V): Tell the Controller the size of buffer:
   // ------------------------------------------------------------------
   ide_write(channel, ATA_REG_LBA1, (words * 2) & 0xFF);   // Lower Byte of Sector Size.
   ide_write(channel, ATA_REG_LBA2, (words * 2)>>8);   // Upper Byte of Sector Size.
   // (VI): Send the Packet Command:
   // ------------------------------------------------------------------
   ide_write(channel, ATA_REG_COMMAND, ATA_CMD_PACKET);      // Send the Command.
   // (VII): Waiting for the driver to finish or invoke an error:
   // ------------------------------------------------------------------
   err = ide_polling(channel, 1);
   if (err) return err;         // Polling and return if error.
  // (VIII): Sending the packet data:
   // ------------------------------------------------------------------
   outsw(bus,atapi_packet,6);
   // asm("rep   outsw"::"c"(6), "d"(bus), "S"(atapi_packet));   // Send Packet Data
   // (IX): Recieving Data:
   // ------------------------------------------------------------------
   	if(dma){
		uint16_t port = channels[channel].bmide;
		for(int i=0;i<numsects;i++){
			prd_table[i].address = (uint32_t)addr;
			prd_table[i].count = words*2;
			prd_table[i].end = 0;
		}
		prd_table[numsects-1].end = 0x8000;
		outb(port + IDE_DMA_CMD,0);
		outb(port + IDE_DMA_CMD,direction<<3);
		uint8_t status = inb(channels[channel].bmide + IDE_DMA_STATUS);
		status |= 1<<2;
		status &= ~(1<<1);
		outb(channels[channel].bmide + IDE_DMA_STATUS,status);
		outb(port + IDE_DMA_CMD,1|(direction<<3));
		debug("wait irq for dma\n");
		ide_wait_irq();
		debug("wait irq for dma done\n");
		// reset the Start/Stop bit
		outb(port + IDE_DMA_CMD,0);
	}else{
	   for (int i = 0; i < numsects; i++) {
	      ide_wait_irq();                  // Wait for an IRQ.
	      err = ide_polling(channel, 1);
	      if (err) return err;      // Polling and return if error.
	      if(direction == ATAPI_READ)
		      insw(bus,addr,words);
		  else
			  outsw(bus,addr,words);
	      addr += (words*2);
	   }
	   // (X): Waiting for an IRQ:
	   // ------------------------------------------------------------------
	   ide_wait_irq();
	}
   // (XI): Waiting for BSY & DRQ to clear:
   // ------------------------------------------------------------------
   while (ide_read(channel, ATA_REG_STATUS) & (ATA_SR_BSY | ATA_SR_DRQ));

   return 0; // Easy, ... Isn't it?
}




void ide_read_sectors(unsigned char drive, unsigned char numsects, unsigned int lba, void* addr) {
   // 1: Check if the drive presents:
   // ==================================
   if (drive > 3 || ide_devices[drive].reserved == 0) package[0] = 0x1;      // Drive Not Found!

   // 2: Check if inputs are valid:
   // ==================================
   else if (((lba + numsects) > ide_devices[drive].size) && (ide_devices[drive].type == IDE_ATA))
      package[0] = 0x2;                     // Seeking to invalid position.

   // 3: Read in PIO Mode through Polling & IRQs:
   // ============================================
   else {
      unsigned char err;
      if (ide_devices[drive].type == IDE_ATA)
         err = ide_ata_access(ATA_READ, drive, lba, numsects, addr);
      else if (ide_devices[drive].type == IDE_ATAPI)
         for (int i = 0; i < numsects; i++)
            err = ide_atapi_access(ATAPI_READ,drive, lba + i, 1, addr + i*2048);
      package[0] = ide_print_error(drive, err);
   }
}

void ide_write_sectors(unsigned char drive, unsigned char numsects, unsigned int lba, void* addr) {

   // 1: Check if the drive presents:
   // ==================================
   if (drive > 3 || ide_devices[drive].reserved == 0) package[0] = 0x1;      // Drive Not Found!
   // 2: Check if inputs are valid:
   // ==================================
   else if (((lba + numsects) > ide_devices[drive].size) && (ide_devices[drive].type == IDE_ATA))
      package[0] = 0x2;                     // Seeking to invalid position.
   // 3: Read in PIO Mode through Polling & IRQs:
   // ============================================
   else {
      unsigned char err;
      if (ide_devices[drive].type == IDE_ATA)
         err = ide_ata_access(ATA_WRITE, drive, lba, numsects, addr);
      else if (ide_devices[drive].type == IDE_ATAPI)
      	for (int i = 0; i < numsects; i++)
      		err = ide_atapi_access(ATAPI_WRITE,drive, lba + i, 1, addr + i*2048);
      package[0] = ide_print_error(drive, err);
   }
}



void parse_progif(uint8_t pg){
/*
https://wiki.osdev.org/IDE
Bit 0: When set, the primary channel is in PCI native mode. When clear, the primary channel is in compatibility mode (ports 0x1F0-0x1F7, 0x3F6, IRQ14).
Bit 1: When set, you can modify bit 0 to switch between PCI native and compatibility mode. When clear, you cannot modify bit 0.
Bit 2: When set, the secondary channel is in PCI native mode. When clear, the secondary channel is in compatibility mode (ports 0x170-0x177, 0x376, IRQ15).
Bit 3: When set, you can modify bit 2 to switch between PCI native and compatibility mode. When clear, you cannot modify bit 2.
Bit 7: When set, this is a bus master IDE controller. When clear, this controller doesn't support DMA.
*/
	debug("IDE mode(0x%02x)",pg);
	switch(pg){
		case 0:
		debug("ISA Compatibility mode-only controller\n");
		break;
		case 5:
		debug("PCI native mode-only controller\n");
		break;
		case 0xa:
		debug("ISA Compatibility mode controller, supports both channels switched to PCI native mode\n");
		break;
		case 0xf:
		debug("PCI native mode controller, supports both channels switched to ISA compatibility mode\n");
		break;
		case 0x80:
		debug("ISA Compatibility mode-only controller, supports bus mastering\n");
		break;
		case 0x85:
		debug("PCI native mode-only controller, supports bus mastering\n");
		break;
		case 0x8a:
		debug("ISA Compatibility mode controller, supports both channels switched to PCI native mode, supports bus mastering\n");
		break;
		case 0x8f:
		debug("PCI native mode controller, supports both channels switched to ISA compatibility mode, supports bus mastering\n");
		break;
	}
}


unsigned char ide_print_error(unsigned int drive, unsigned char err) {
   if (err == 0) return err;
   printf(" IDE:");
   if (err == 1) {printf("- Device Fault\n     "); err = 19;}
   else if (err == 2) {
      unsigned char st = ide_read(ide_devices[drive].channel, ATA_REG_ERROR);
      if (st & ATA_ER_AMNF)   {printf("- No Address Mark Found\n     ");   err = 7;}
      if (st & ATA_ER_TK0NF)   {printf("- No Media or Media Error\n     ");   err = 3;}
      if (st & ATA_ER_ABRT)   {printf("- Command Aborted\n     ");      err = 20;}
      if (st & ATA_ER_MCR)   {printf("- No Media or Media Error\n     ");   err = 3;}
      if (st & ATA_ER_IDNF)   {printf("- ID mark not Found\n     ");      err = 21;}
      if (st & ATA_ER_MC)   {printf("- No Media or Media Error\n     ");   err = 3;}
      if (st & ATA_ER_UNC)   {printf("- Uncorrectable Data Error\n     ");   err = 22;}
      if (st & ATA_ER_BBK)   {printf("- Bad Sectors\n     ");       err = 13;}
   } else  if (err == 3)           {printf("- Reads Nothing\n     "); err = 23;}
     else  if (err == 4)  {printf("- Write Protected\n     "); err = 8;}

   return err;
}


