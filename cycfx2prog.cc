/*
 * cycfx2prog.cc - Cypress FX2(LP) programmer. 
 * 
 * Copyright (c) 2006--2009 by Wolfgang Wieser ] wwieser (a) gmx <*> de [ 
 * 
 * This file may be distributed and/or modified under the terms of the 
 * GNU General Public License version 2 as published by the Free Software 
 * Foundation. (See COPYING.GPL for details.)
 * 
 * This file is provided AS IS with NO WARRANTY OF ANY KIND, INCLUDING THE
 * WARRANTY OF DESIGN, MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 * 
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/time.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <assert.h>

#include "cycfx2dev.h"

// The version is set in Makefile. 
#ifndef CYCFX2PROG_VERSION
# error CYCFX2PROG_VERSION undefined
#endif

// Global FX2 device we're connected to. 
CypressFX2Device cycfx2;


static void *CheckMalloc(void *ptr)
{
	if(!ptr)
	{
		fprintf(stderr,"malloc failed\n");
		exit(1);
	}
	return(ptr);
}


// FIXME: What to do with these: 
// HMMM.... There is usb_device::num_children and **usb_device::children. 

void USBDumpBusses(FILE *out)
{
	for(usb_bus *b=usb_busses; b; b=b->next)
	{
		for(struct usb_device *d=b->devices; d; d=d->next)
		{
			bool is_fx2_dev = (d->descriptor.idVendor==0x4b4 && 
				d->descriptor.idProduct==0x8613);
			fprintf(out,"Bus %s Device %s: ID %04x:%04x%s\n",
				b->dirname,d->filename,
				d->descriptor.idVendor,d->descriptor.idProduct,
				is_fx2_dev ? " (unconfigured FX2)" : "");
		}
	}
}


static inline void _HexdumpPutChar(FILE *out,unsigned char x)
{
	if(isprint(x))
	{  fprintf(out,"%c",x);  }
	else
	{  fprintf(out,".");  }
}


static void HexDumpBuffer(FILE *out,const unsigned char *data,size_t size,
	int with_ascii)
{
	//ssize_t skip_start=-1;
	for(size_t i=0; i<size; )
	{
		size_t j=0;
#if 0
		// Do not dump lines which are only ffffff....
		int do_skip=1;
		for(size_t ii=i; j<16 && ii<size; j++,ii++)
		{
			if(data[ii]!=0xffU)
			{  do_skip=0;  break;  }
		}
		if(do_skip)
		{
			if(skip_start<0)  skip_start=i;
			i+=16;
			continue;
		}
		else if(skip_start>=0)
		{
			printf("         [skipping 0x%04x..0x%04x: "
				"%u words 0xffff]\n",
				size_t(skip_start),i-1,i-size_t(skip_start));
			skip_start=-1;
		}
#endif
		
		printf("  0x%04zx ",i);
		size_t oldi=i;
		for(j=0; j<32 && i<size; j++,i++)
		{
			if(j && !(j%8))  printf(" ");
			printf("%02x",(unsigned int)data[i]);
		}
		// This adds a plaintext column (printable chars only): 
		if(with_ascii)
		{
			for(; j<32; j++)
			{  printf((j && !(j%8)) ? "   " : "  ");  }
			
			printf("    ");
			i=oldi;
			for(j=0; j<32 && i<size; j++,i++)
			{  _HexdumpPutChar(out,data[i]);  }
		}
		printf("\n");
	}
#if 0
	if(skip_start>=0 && size>0)
	{
		printf("         [skipping 0x%04x..0x%04x: "
			"%u words 0xffff]\n",
			size_t(skip_start),size-1,size-size_t(skip_start));
		skip_start=-1;
	}
#endif
	fflush(out);
}


static void PrintHelp()
{
	fprintf(stderr,
		"Usage: cycfx2prog [-d=BUS.DEV] [id=VV.PP[.N]] [commands...]\n"
		"Options:\n"
		" --help       print this and then exit\n"
		" --version    print version information and then exit\n"
		" --list       list devices and busses and then exit\n"
		" -d=BBB.DDD   set device to use e.g. 006.003; if not specified, first\n"
		"              unconfigured Cypress FX2 is used. Use --list to get BBB\n"
		"              and DDD (bus and device number, not ID).\n"
		" -id=VV.PP[.N]  set vendor and product ID in hex; default 04b4.8613 for\n"
		"              unconfigured FX2. N is the n-th device to use, default 0.\n"
		"Commands: Must be specified after all options.\n"
		"  reset          reset 8051 by putting reset low\n"
		"  run            start the 8051 by putting reset high\n"
		"  prg:FILE       program 8051; FILE is an Intel hex file (.ihx); will\n"
		"                 reset the 8051 before download; use \"run\" afterwards\n"
		"  delay:NN       make a delay for NN msec\n"
		"  set:ADR,VAL    set byte at address ADR to value VAL\n"
		"  dram:ADR,LEN   dump RAM content: LEN bytes starting at ADR\n"
		"  dbulk:EP,L[,N] bulk read N (default: 1) buffers of size L from endpoint\n"
		"                 EP (1,2,4,6,8) and dump them; L<0 to allow short reads\n"
		"  sbulk:EP,STR   send string STR as bulk message to endpoint EP (1,2,4,6,8)\n"
		"  fbulk:EP,FILE[,CS] send FILE as bulk message to endpoint EP (1,2,4,6,8)\n"
		"                 stdin if no file specified; chunk size CS with default 64\n"
		"  bench_bulk:EP,L[,CS]  bench reading L bytes from endpoint EP (chunk size CS)\n"
		"                 NOTE: This uses libusb and is slow on the host side!\n"
		"  altif:[IF]     set alt interface for next bulk IO; none for FX2 default\n"
		"  ctrl:TYPE,REQUEST[,VALUE[,INDEX]] send a zero-length control message\n"
		"Cypress FX2(LP) programmer tool v%s copyright (c) 2006--2009 by Wolfgang Wieser\n"
		,CYCFX2PROG_VERSION);
}


int main(int argc,char **arg)
{
	int errors=0;
	const char *arg_bus_dev=NULL;
	bool do_list=0;
	int arg_vend=0x4b4,arg_prod=0x8613,arg_nth=0,arg_id_specified=0;
	for(int i=1; i<argc; i++)
	{
		if(!strcmp(arg[i],"--help"))
		{  PrintHelp();  return(0);  }
		if(!strcmp(arg[i],"--version"))
		{
			printf("cycfx2prog version %s\n",CYCFX2PROG_VERSION);
			fflush(stdout);
			return(0);
		}
		else if(!strcmp(arg[i],"--list"))
		{  do_list=1;  }
		else if(!strncmp(arg[i],"-d=",3))
		{  arg_bus_dev=arg[i]+3;  }
		else if(!strncmp(arg[i],"-id=",4))
		{
			int rv=sscanf(arg[i]+4,"%x%*c%x%*c%d",&arg_vend,&arg_prod,&arg_nth);
			if(rv<2)
			{  fprintf(stderr,"Illegal argument for option -id=\n");
				++errors;  }
			else if(rv==2)
			{  arg_nth=0;  }
			else if(arg_nth<0)
			{  arg_nth=0;  }
			arg_id_specified=1;
		}
		else if(*arg[i]=='-')
		{  fprintf(stderr,"Illegal option \"%s\".\n",arg[i]);  ++errors;  }
	}
	if(errors)
	{  return(1);  }
	
	// Initialize the USB library...
	usb_init();
	
	int rv=usb_find_busses();
	if(rv<0)
	{  fprintf(stderr,"usb_find_busses failed (rv=%d)\n",rv);  return(1);  }
	rv=usb_find_devices();
	if(rv<0)
	{  fprintf(stderr,"usb_find_devices failed (rv=%d)\n",rv);  return(1);  }
	
	if(do_list)
	{  USBDumpBusses(stdout);  return(0);  }
	
	// Look for device. 
	struct usb_device *usbdev=NULL;
	if(arg_bus_dev)
	{
		char bus[16];
		char dev[16];
		do {
			if(strlen(arg_bus_dev)>=16)  break;  // Prevents buffer overflows. 
			const char *p=strchr(arg_bus_dev,'.');
			if(!p)  break;
			strncpy(bus,arg_bus_dev,p-arg_bus_dev);
			bus[p-arg_bus_dev]='\0';
			strcpy(dev,p+1);
			usbdev=USBFindDevice(bus,dev);
		} while(0);
		if(!usbdev)
		{  fprintf(stderr,"Illegal/nonexistant device %s.\n",arg_bus_dev);
			return(1);  }
	}
	else
	{
		usbdev=USBFindDevice(arg_vend,arg_prod,arg_nth);
		if(!usbdev)
		{
			if(arg_id_specified)
			{  fprintf(stderr,"Device with vendorID=0x%04x, productID="
				"0x%04x, nth=%d not attached.\n",arg_vend,arg_prod,arg_nth);  }
			else
			{  fprintf(stderr,"No unconfigured Cypress FX2 attached.\n");  }
			return(1);
		}
	}
	
	fprintf(stderr,"Using ID %04x:%04x on %s.%s.\n",
		usbdev->descriptor.idVendor,usbdev->descriptor.idProduct,
		usbdev->bus->dirname,usbdev->filename);
	
	if(cycfx2.open(usbdev))
	{  return(1);  }
	
	// Execute all the user commands. 
	for(int argc_i=1; argc_i<argc; argc_i++)
	{
		if(*arg[argc_i]=='-') continue;
		
		// Copy command and args so that we can manipulate it. 
		char *cmd=(char*)CheckMalloc(malloc(strlen(arg[argc_i])+1));
		strcpy(cmd,arg[argc_i]);
		
		// Cut into command and arguments: 
		const int MAXARGS=16;
		char *a[MAXARGS];
		for(int j=0; j<MAXARGS; j++)  a[j]=NULL;
		char *first_arg=strchr(cmd,':');
		int nargs=0;
		if(first_arg)
		{
			do {
				*(first_arg++)='\0';
				if(nargs>=MAXARGS)
				{
					fprintf(stderr,"Too many arguments for command \"%s\" "
						"(further args ignored)\n",cmd);
					++errors;
					break;
				}
				a[nargs++]=first_arg;
				first_arg=strchr(first_arg,',');
			} while(first_arg);
		}
		
#if 0
		// Debug: 
		printf("Command: <%s>",cmd);
		for(int j=0; j<nargs; j++)
		{  printf(" <%s>",a[j]);  }
		printf("\n");
#endif
		
		if(!strcmp(cmd,"reset"))
		{
			fprintf(stderr,"Putting 8051 into reset.\n");
			errors+=cycfx2.FX2Reset(/*running=*/0);
		}
		else if(!strcmp(cmd,"run"))
		{
			fprintf(stderr,"Putting 8051 out of reset.\n");
			errors+=cycfx2.FX2Reset(/*running=*/1);
		}
		else if(!strcmp(cmd,"prg"))
		{
			// NOTE: We put the 8051 into reset prior to downloading but 
			//       we won't put it out of reset afterwards. 
			fprintf(stderr,"Putting 8051 into reset.\n");
			errors+=cycfx2.FX2Reset(/*running=*/0);
			
			const char *file=a[0];
			if(!file)
			{  fprintf(stderr,"Command \"dl\" requires file to download.\n");
				++errors;  }
			else
			{
				fprintf(stderr,"Programming 8051 using \"%s\".\n",file);
				errors+=cycfx2.ProgramIHexFile(file);
			}
		}
		else if(!strcmp(cmd,"delay"))
		{
			long delay=-1;
			if(a[0] && *a[0])
			{  delay=strtol(a[0],NULL,0);  }
			if(delay<0)  delay=250;
			fprintf(stderr,"Delay: %ld msec\n",delay);
			usleep(delay*1000);
		}
		else if(!strcmp(cmd,"dram"))
		{
			int adr=0;
			int len=1;
			if(a[0] && *a[0])
			{  adr=strtol(a[0],NULL,0);  }
			if(adr<0)  adr=0;
			if(a[1] && *a[1])
			{  len=strtol(a[1],NULL,0);  }
			if(len<1)  len=1;
			if(len>1024*1024)  len=1024*1024;
			
			fprintf(stderr,"Dumping %u bytes of RAM at 0x%x:\n",len,adr);
			unsigned char *buf=(unsigned char*)CheckMalloc(malloc(len));
			memset(buf,0,len);
			errors+=cycfx2.ReadRAM(adr,buf,len);
			HexDumpBuffer(stdout,buf,len,/*with_ascii=*/1);
			if(buf)  free(buf);
		}
		else if(!strcmp(cmd,"set"))
		{
			int adr=-1;
			int val=-1;
			if(a[0] && *a[0])  {  adr=strtol(a[0],NULL,0);  }
			if(a[1] && *a[1])  {  val=strtol(a[1],NULL,0);  }
			if(adr<0 || val<0 || val>=256)
			{  fprintf(stderr,"Command set: Illegal/missing address "
				"and/or value.\n");  ++errors;  }
			else
			{
				fprintf(stderr,"Setting value at 0x%x to 0x%x\n",adr,val);
				unsigned char cval=val;
				errors+=cycfx2.WriteRAM(adr,&cval,1);
			}
		}
		else if(!strcmp(cmd,"dbulk"))
		{
			int ep=-1;
			int len=512;
			int num=1;
			char type='b';
			
			if(a[0] && *a[0])  {  ep=strtol(a[0],NULL,0);  }
			if(a[1] && *a[1])  {  len=strtol(a[1],NULL,0);  }
			if(a[2] && *a[2])  {  num=strtol(a[2],NULL,0);  }
			// If len<0, allow short reads. 
			if(len<0)
			{  type='B';  len=-len;  }
			if(ep<0 || ep>=127 || len<=0 || len>32*1024*1024 || num<1)
			{  fprintf(stderr,"Command dbulk: Illegal/missing "
				"endpoint/length/number.\n");  ++errors;  }
			else
			{
				// IN endpoints have the bit 7 set. 
				ep|=0x80;
				
				unsigned char *buf=(unsigned char*)CheckMalloc(malloc(len));
				memset(buf,0,len);
				
				for(int i=0; i<num; i++)
				{
					fprintf(stderr,"Reading %s%d bytes from EP adr 0x%02x\n",
						type=='B' ? "<=" : "",len,ep);
					int rv=cycfx2.BlockRead(ep,buf,len,type);
					errors += rv<0 ? 1 : 0;
					if(rv>0)
					{  HexDumpBuffer(stdout,buf,rv,/*with_ascii=*/1);  }
				}
				
				if(buf)  free(buf);
			}
		}
		else if(!strcmp(cmd,"sbulk"))
		{
			int ep=-1;
			char type='b';
			const char *str=a[1];
			int len=str ? strlen(str) : 0;
			
			if(a[0] && *a[0])  {  ep=strtol(a[0],NULL,0);  }
			if(ep<0 || ep>=127)
			{  fprintf(stderr,"Command sbulk: Illegal/missing "
				"endpoint.\n");  ++errors;  }
			else
			{
				fprintf(stderr,"Sending %d bytes to EP adr 0x%02x\n",len,ep);
				int rv=cycfx2.BlockWrite(ep,(const unsigned char*)str,len,type);
				errors += rv<0 ? 1 : 0;
			}
		}
		else if(!strcmp(cmd,"fbulk"))
		{
			char type='b';
			const char *filename = a[1];
			int ep=-1;
			int chunk_size=64;
			
			do {
				if(a[0] && *a[0])  {  ep=strtol(a[0],NULL,0);  }
				if(ep<0 || ep>=127)
				{  fprintf(stderr,"Command fbulk: Illegal/missing "
					"endpoint.\n");  ++errors;  break;  }
				
				if(a[2] && *a[2])  {  chunk_size=strtol(a[2],NULL,0);  }
				if(chunk_size<1 || chunk_size>2048)
				{  fprintf(stderr,"Command fbulk: Illegal chunk size %d.\n",
					chunk_size);  ++errors;  break;  }
				
				FILE *stream = filename ? fopen(filename,"r") : stdin;
				if(!stream)
				{
					fprintf(stderr,"Failed to open \"%s\" for reading: %s\n",
						filename ? filename : "[stdin]",strerror(errno));
					++errors;  break;
				}
				
				int len;
				int tot_bytes=0;
				unsigned char buf[chunk_size];
				
				fprintf(stderr,"Sending \"%s\" in chunks of %d bytes to EP adr "
					"0x%02x\n",
					filename ? filename : "[stdin]",chunk_size,ep);
				while((len=fread(buf,1,chunk_size,stream))>0)
				{
			    	int rv=cycfx2.BlockWrite(ep,buf,len,type);
					if(rv<0)
					{  ++errors; break;  }
					tot_bytes += len;
				}
				if(filename) fclose(stream);
				fprintf(stderr,"Sent %d bytes to EP adr 0x%02x\n",tot_bytes,ep);
			} while(0);
		}
		else if(!strcmp(cmd,"bench_bulk"))
		{
			int ep=-1;
			int len=1024*1024;
			int cs=65536;
			
			if(a[0] && *a[0])  {  ep=strtol(a[0],NULL,0);  }
			if(a[1] && *a[1])  {  len=strtol(a[1],NULL,0);  }
			if(a[2] && *a[2])  {  cs=strtol(a[2],NULL,0);  }
			// If len<0, allow short reads. 
			if(ep<0 || ep>=127 || len<=0 || len>32*1024*1024 || 
				cs<1 || cs>32*1024*1024)
			{  fprintf(stderr,"Command bench_bulk: Illegal/missing "
				"endpoint/length/number.\n");  ++errors;  }
			else
			{
				// IN endpoints have the bit 7 set. 
				ep|=0x80;
				
				int rv=cycfx2.BenchBlockRead(ep,len,cs,'b');
				errors += rv ? 1 : 0;
			}
		}
		else if(!strcmp(cmd,"altif"))
		{
			int af=-1;
			if(a[0] && *a[0])  {  af=strtol(a[0],NULL,0);  }
			
			cycfx2.ForceAltInterface(af);
		}
		else if(!strcmp(cmd,"ctrl"))
		{
			int requesttype=0,request=0;
			int value=0,index=0;
			if(a[0] && *a[0]) requesttype=strtol(a[0],NULL,0);
			if(a[1] && *a[1]) request=strtol(a[1],NULL,0);
			if(a[2] && *a[2]) value=strtol(a[2],NULL,0);
			if(a[3] && *a[3]) index=strtol(a[3],NULL,0);
			fprintf(stderr,"Sending control message type 0x%02x, request "
				"0x%02x (value=%d,index=%d)\n",
				requesttype,request,value,index);
			errors+=cycfx2.CtrlMsg(requesttype,request,value,index);
		}
		else
		{
			fprintf(stderr,"Ignoring unknown command \"%s\".\n",cmd);
			++errors;
		}
		
		if(cmd)
		{  free(cmd);  cmd=NULL;  }
	}
	
	return(errors ? 1 : 0);
}
