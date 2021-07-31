#include <io.h>
#include <pci.h>
#include <mm.h>
#include <string.h>
#include <sleep.h>
#include <irq.h>
#include <apic.h>
#include <e1000.h>

#include "ethernet.h"

#include <stdio.h>
#include <stdlib.h>

unsigned long E1000_MEM;

NIC_T _nic[1];

unsigned int irq_count, rx;

void e1000()
{
	irq_count = 0, rx = 0;
	NIC_T *nic = _nic;

	printf("PCI Network Controller initialize\n");

	unsigned int data = pci_scan_class(0x2);

	if(data == (-1) ) {
		printf("PCI PANIC: Network Controller not found\n");
		return;
	}
	
	if( e1000_pci_configuration_space(data  >>  24 &0xff,data  >> 16  &0xff,data &0xffff) ) {
	
		printf("PCI PANIC: Network Controller\n");
		return;
	}else if(nic->vid != 0x8086 || nic->did != 0x100E) {
	
		printf("E1000 Fail: Network Controller, Vendor ID:%x Device ID: %x \n", nic->vid, nic->did);
		return;
	}

	printf("Vendor ID:%x Device ID: %x \n", nic->vid, nic->did);
	
	printf("Memory Register Base Address 0x%x\n",nic->phy_address);

	printf("Memory Flash Base Address 0x%x\n",nic->flash_address);
	
	printf("IO Register Base Address 0x%x\n",nic->io_address);

	printf("E1000 irq_lin: %d, irq_pin %d\n",nic->irq_lin, nic->irq_pin );
		
	mm_mp( nic->phy_address , &E1000_MEM, 0x100000 /*1MIB*/,0);
    nic->virt_address = (unsigned long) E1000_MEM;
    
    // Let's try to discover reading the status field!
	nic->eeprom = 0;
	for (int i=0; i < 1000 && !nic->eeprom; i++ ) {
            unsigned int val = e1000_inl ( REG_E1000_EEPROM );
            if ( (val & 0x10) == 0x10) 
            {
                nic->eeprom = 1;
                
                printf("EEPROM\n");
            }
	}
	
	if (nic->eeprom == 1) {
		unsigned int tmp = e1000_read_eeprom ( 0 );
		nic->mac[0] = (unsigned char)(tmp & 0xFF);
		nic->mac[1] = (unsigned char)(tmp >> 8);

		tmp = e1000_read_eeprom ( 1 );
		nic->mac[2] = (unsigned char)(tmp & 0xFF);
		nic->mac[3] = (unsigned char)(tmp >> 8);
		
		tmp = e1000_read_eeprom ( 2 );
		nic->mac[4] = (unsigned char)(tmp & 0xFF);
		nic->mac[5] = (unsigned char)(tmp >> 8);
		    
	} else {
	
		nic->mac[0] = e1000_inb(REG_E1000_RAL + 0);
		nic->mac[1] = e1000_inb(REG_E1000_RAL + 1);
		nic->mac[2] = e1000_inb(REG_E1000_RAL + 2);
		nic->mac[3] = e1000_inb(REG_E1000_RAL + 3);
		nic->mac[4] = e1000_inb(REG_E1000_RAL + 4);
		nic->mac[5] = e1000_inb(REG_E1000_RAL + 5);
	
	}
	printf("MAC Address: %x%x%x%x%x%x\n",nic->mac[0], nic->mac[1], nic->mac[2],
	nic->mac[3],nic->mac[4],nic->mac[5]);
	
	
	if(nic->irq_lin == 9) nic->irq_lin = 19; // TODO improviso para o IRQ19 no Virtual Box
	
	// IRQ set handler
	fnvetors_handler[nic->irq_lin] = &e1000_handler;
	// Enable IRQ Interrupt
	ioapic_umasked(nic->irq_lin);
	
	//
	sti();
	e1000_reset_controller ();
	
	printf("Done\n");
	
	// test 
	// 192.168.122.1
	unsigned char source_ip_address[4];
	source_ip_address[0] = 192;
	source_ip_address[1] = 168;
	source_ip_address[2] = 1;   
	source_ip_address[3] = 112; 

	unsigned char target_ip_address[4];
	target_ip_address[0] = 192;
	target_ip_address[1] = 168;
	target_ip_address[2] = 1; 
	target_ip_address[3] = 88; 

	unsigned char source_mac_address[6];
	source_mac_address[0] = 0;
	source_mac_address[1] = 0;
	source_mac_address[2] = 0;
	source_mac_address[3] = 0;
	source_mac_address[4] = 0;
	source_mac_address[5] = 0;
	
	unsigned char target_mac_address[6];
	target_mac_address[0] = 0xFF;
	target_mac_address[1] = 0xFF;
	target_mac_address[2] = 0xFF;
	target_mac_address[3] = 0xFF;
	target_mac_address[4] = 0xFF;
	target_mac_address[5] = 0xFF;
	
	// while(!irq_count);
	e1000_send_arp ( source_ip_address, target_ip_address, source_mac_address, target_mac_address );
}

void e1000_reset_controller ()
{

	NIC_T *nic = _nic;

	// alocar memoria
	
	unsigned long virtual = 0;
	alloc_pages(0, 2, (unsigned long *)&virtual); //8 KiB
	unsigned long physical = get_phy_addr( virtual );
	memset((void*)virtual, 0, 0x2000 );
	
	nic->receive = (E1000_RECEIVE_T*) virtual;
	virtual += 0x1000; // + 4096 bytes
	nic->transmit = (E1000_TRANSMIT_T*) virtual;
	
	// Receive Descriptor
	nic->desc[0].base_l = physical;
	nic->desc[0].base_h = physical >> 32;
	nic->desc[0].length = 512; // 16bytes * 32 = 512 bytes  
	nic->desc[0].head	= 0;
	nic->desc[0].tail	= 31;
	
	nic->desc[0].current = 0;
	alloc_pages(0, 32, (unsigned long *)&virtual); //128 KiB
	nic->desc[0].buffer_base_addr = virtual;
	memset((void*)virtual, 0, 32*0x1000);
	
	unsigned long long phy = get_phy_addr( virtual );
	for(int i=0; i < 32; i ++) {
	
		nic->receive[i].buffer_l =  phy;
		nic->receive[i].buffer_h =  phy >> 32;
		
		
		nic->receive[i].length = 0x1000;
		nic->receive[i].status = 0;
		
		phy += 0x1000; 	
	}
	
	// Transmit Descriptor
	physical = physical + 0x1000; // + 4096 bytes
	nic->desc[1].base_l = physical;
	nic->desc[1].base_h = physical >> 32;
	nic->desc[1].length = 128; // 16bytes * 8 = 128 bytes
	nic->desc[1].head	= 0;
	nic->desc[1].tail	= 0;
	
	nic->desc[1].current = 0;
	alloc_pages(0, 8, (unsigned long *)&virtual); //32 KiB
	nic->desc[1].buffer_base_addr = virtual;
	memset((void*)virtual, 0, 8*0x1000);
	
	phy = get_phy_addr( virtual );
	for(int i=0; i < 8; i ++) {
	
		nic->transmit[i].buffer_l =  phy;
		nic->transmit[i].buffer_h = phy >> 32;
		
		nic->transmit[i].command = 0;
		nic->transmit[i].status = 1;
		
		phy += 0x1000; 	
	}
	
	
	// MTA(n) = 0
	for (int i=0; i < 128; i++) 
		e1000_outl( REG_E1000_MTA + (i * 4), 0 );
		
		
	e1000_outl( REG_E1000_IMS, 0x1F6DE );
	e1000_inl( REG_E1000_ICR);
	
	
	
	// Receive
	e1000_outl( REG_E1000_RDBAL0, nic->desc[0].base_l);
	e1000_outl( REG_E1000_RDBAH0, nic->desc[0].base_h);
	e1000_outl( REG_E1000_RDLEN0, nic->desc[0].length);
	e1000_outl( REG_E1000_RDH0, nic->desc[0].head);
	e1000_outl( REG_E1000_RDT0, nic->desc[0].tail);
	e1000_outl( REG_E1000_RCTL, 0x602801E);
	
	
	
	
	// Transmit
	e1000_outl( REG_E1000_TDBAL0, nic->desc[1].base_l);
	e1000_outl( REG_E1000_TDBAH0, nic->desc[1].base_h);
	e1000_outl( REG_E1000_TDLEN0, nic->desc[1].length);
	e1000_outl( REG_E1000_TDH0, nic->desc[1].head);
	e1000_outl( REG_E1000_TDT0, nic->desc[1].tail);
	
	e1000_outl( REG_E1000_TXDCTL, (0x01000000 | 0x003F0000) );
	e1000_outl( REG_E1000_TCTL, (0x00000ff0 | 0x003ff000 | 0x00000008 | 0x00000002) );
	e1000_outl( REG_E1000_TIPG, (0x0000000A | 0x00000008 | 0x00000002) );


	// SLU (bit 6) - Set Link Up
	unsigned int cmd = e1000_inl(REG_E1000_CTRL);
	cmd |= 0x40;
	e1000_outl( REG_E1000_CTRL , cmd );
	
}

void e1000_handler()
{
	NIC_T *nic = _nic;
	
	irq_count++;
	
	e1000_outl( REG_E1000_IMS, 1);		
	unsigned int status = e1000_inl ( REG_E1000_ICR );

	printf("E1000 status: %x\n",status);
	
	if(status & 0x02){
		printf("Transmit Queue Empty: %d %x\n", nic->transmit[0].length,nic->transmit[0].status );
		return;
	}else if ( status & 0x04 ) {	
		// Set Link Up
		unsigned int cmd = e1000_inl(REG_E1000_CTRL);
		cmd |= 0x40;
		e1000_outl( REG_E1000_CTRL , cmd );
		return;
	}else if( status & 0x80 ) {
		printf("%d  %d  ", irq_count, nic->receive[rx++].length);
		//for(;;);
		
		// debug
		for(int i=0; i < 8; i++) {
			/*printf("Length: %x, checksum: %x status: %x, errors: %x, special: %x\n",
			nic->receive[i].length, nic->receive[i].checksum, nic->receive[i].status,
			nic->receive[i].errors, nic->receive[i].special);
		
			unsigned int *a = (unsigned int *) (nic->desc[0].buffer_base_addr + (i*0x1000) );
			printf("%x ",*a++);printf("%x ",*a++);printf("%x ",*a++);printf("%x\n",*a++);*/
		}
	}
	
	
}

int e1000_send_arp ( unsigned char src_ip[4], unsigned char dst_ip[4], unsigned char src_mac[6], unsigned char dst_mac[6] ) {
	
	ether_header_t *eh;
	ether_arp_t *arp;
	
	NIC_T *nic = _nic;


	//configurando a estrutura do dispositivo,
/*
	nic->ip[0] = source_ip[0];  //192;
	nic->ip[1] = source_ip[1];  //168;
	nic->ip[2] = source_ip[2];  //1;    	
	nic->ip[3] = source_ip[3];  //112;*/
	
	// alocar
	eh = (ether_header_t *) malloc(sizeof(ether_header_t));
	arp = (ether_arp_t *) malloc(sizeof(ether_arp_t));
	
	memset(eh, 0, sizeof(ether_header_t));
	memset(arp, 0, sizeof(ether_arp_t));
	
	for(int i=0; i < 6; i ++)
	{
		eh->src[i] = src_mac[i]; 		//source ok
		eh->dst[i] = dst_mac[i];     //dest. (broadcast)	
	}	
	
	eh->type = (unsigned short) ToNetByteOrder16(ETH_TYPE_ARP);
	
	//Hardware type (HTYPE)
	arp->type = 1;//0x0100; // (00 01)
	
	//Protocol type (PTYPE)
	arp->proto = 0x800;//0x0008;  //(08 00)    
	
	//Hardware address length (MAC)
	arp->hlen = 6;
	
	////Protocol address length (IP)
	arp->plen = 4;
	
	arp->op = 0x0100;
    
    //mac
	for(int i=0; i<6; i++)
	{
		arp->arp_sha[i] = src_mac[i];  //sender mac
		arp->arp_tha[i] = dst_mac[i];     //target mac
	}	
	
	//ip
	for (int i=0; i<4; i++)
	{
		arp->arp_spa[i] = src_ip[i];    //sender ip
		arp->arp_tpa[i] = dst_ip[i];    //target ip
	}
	
	
	// Copiando o pacote no buffer
	
	
	unsigned char *buffer = (unsigned char *)nic->desc[1].buffer_base_addr;
	unsigned char *src_ethernet = (unsigned char *) (unsigned long)eh; 
	unsigned char *src_arp      = (unsigned char *) (unsigned long)arp;
	
	//copiando o header ethernet
	//copiando o arp logo após do header ethernet


	for(int i=0; i < 14; i ++){
	
		buffer[i] = src_ethernet[i];
	}

	for(int i=0; i < 28; i ++){
	
		buffer[i + 14] = src_arp[i];
	}
	
	
	
	// Ethernet frame length = ethernet header (MAC + MAC + ethernet type) + ethernet data (ARP header)
	//O comprimento deve ser o tamanho do header etherne + o tamanho do arp.
	
	//len;
	nic->transmit[0].length = (ETHERNET_HEADER_LENGHT + ARP_HEADER_LENGHT);
	nic->transmit[0].command = 0x1B;
	nic->transmit[0].status = 0;
	
	e1000_outl( REG_E1000_TDT0, 7);
	e1000_outl( REG_E1000_TDH0, 0);
	

	
	int spin = 10000000;
	
	while (!((long)nic->transmit[0].status) && spin)
	{
		spin--;

	}
	
	if(!spin) printf("send arp error, status: %x\n",nic->transmit[0].status);
	
	
	
	free(eh);
	free(arp);
	
	return 0;
}

int e1000_pci_configuration_space(int bus,int dev,int fun)
{

	NIC_T *nic = _nic;

	nic->fun = fun;
	nic->dev = dev;
	nic->bus = bus;
	
	unsigned long data;

	// VENDOR ID and DEVICE ID
	data = read_pci_config_addr( bus, dev, fun, 0x00);
	
	nic->vid = data &0xffff;
	nic->did = data >> 16 &0xffff;

	// STATUS CMD Enable Memory mapped and  Bus Mastering Enable
	data = read_pci_config_addr( bus, dev, fun, 0x04) | 0x6;
	write_pci_config_addr( bus , dev, fun, 0x04, data );
	
 	
 	// BAR0-2
	data = read_pci_config_addr( bus, dev, fun, 0x10);
	
	if( (data&0x6) == 0x4 ) {	
		//64-bit
		nic->phy_address = data & 0xFFFFFFF0;
		
		
		data = read_pci_config_addr( bus, dev, fun, 0x14);
		nic->phy_address |= data << 32;
		
		data = read_pci_config_addr( bus, dev, fun, 0x18);
		nic->flash_address = data  & 0xFFFFFFF0;
		data = read_pci_config_addr( bus, dev, fun, 0x1C);
		nic->flash_address |= data << 32;
		
		data = read_pci_config_addr( bus, dev, fun, 0x20);
		nic->io_address = data & 0xFFFFFFF0;
		
	} else {
	
		// 32-bit
	
		nic->phy_address = data & 0xFFFFFFF0;
		data = read_pci_config_addr( bus, dev, fun, 0x14);
		nic->flash_address = data  & 0xFFFFFFF0;
	
		data = read_pci_config_addr( bus, dev, fun, 0x18);
		nic->io_address = data & 0xFFFFFFF0;
	}
	
	data  = read_pci_config_addr( bus, dev, fun, 0x3C);
	nic->irq_lin = data & 0xff;
	nic->irq_pin  = data >> 8 &0xff;
	

	return 0;
}


void e1000_outb(unsigned int offset, unsigned char val) {
	volatile unsigned char *mem = (volatile unsigned char *) (E1000_MEM + offset);
	mem[0]=val;
}

void e1000_outw(unsigned int offset, unsigned short val) {
	volatile unsigned short *mem = (volatile unsigned short *) (E1000_MEM + offset);
	mem[0]=val;
}

void e1000_outl(unsigned int offset, unsigned int val) {
	volatile unsigned int *mem = (volatile unsigned int *) (E1000_MEM + offset);
	mem[0]=val;
}

unsigned char e1000_inb(unsigned int offset) {
	volatile unsigned char *mem = (volatile unsigned char *) ( E1000_MEM + offset);
	return (mem[0]);
}

unsigned short e1000_inw(unsigned int offset) {
	volatile unsigned short *mem = (volatile unsigned short *) ( E1000_MEM + offset);
	return (mem[0]);
}

unsigned int e1000_inl(unsigned int offset) {
	volatile unsigned int *mem = (volatile unsigned int *) ( E1000_MEM + offset);
	return (mem[0]);
}

unsigned int e1000_read_eeprom (  unsigned char addr ) {
	NIC_T *nic = _nic;
	unsigned int data;
 	if (nic->eeprom == 1) 
	{
 		e1000_outl ( REG_E1000_EEPROM, 1 | (addr << 8) );
 				
		while (( (data = e1000_inl (  REG_E1000_EEPROM )) & 0x10 ) != 0x10 );
		
 	}else {
 	
 		e1000_outl ( REG_E1000_EEPROM, 1 | (addr << 2) );
		while (( (data = e1000_inl( REG_E1000_EEPROM )) & 0x01 ) != 0x01 );
 	
 	}
 	
 	return (data >> 16) & 0xFFFF;
 }


